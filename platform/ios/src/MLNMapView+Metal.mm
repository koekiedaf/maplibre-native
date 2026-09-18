#import "MLNDisplayUtils.h"
#import "MLNFoundation_Private.h"
#import "MLNLoggingConfiguration_Private.h"
#import "MLNMapView+Metal.h"
#import "MLNMapView_Private.h"

#import <mln/map/map.hpp>
#import <mln/mtl/renderable_resource.hpp>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#import <Metal/Metal.hpp>

// The Bazel Apple toolchain's Metal module only forward-declares these newer MTLDrawable
// members even when building against an SDK that provides them. Keep the Objective-C surface
// local and typed; every CAMetalDrawable on the deployment target implements this at runtime.
@protocol MLNDrawablePresentation <NSObject>
@property(nonatomic, readonly) CFTimeInterval presentedTime;
- (void)addPresentedHandler:(void (^)(id<MLNDrawablePresentation> drawable))block;
@end

@interface MLNMapViewImplDelegate : NSObject <MTKViewDelegate>
@end

@implementation MLNMapViewImplDelegate {
  MLNMapViewMetalImpl* _impl;
}

- (instancetype)initWithImpl:(MLNMapViewMetalImpl*)impl {
  if (self = [super init]) {
    _impl = impl;
  }
  return self;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawInMTKView:(MTKView*)view {
  _impl->render();
}

@end

class MLNMapViewMetalRenderableResource final : public mln::mtl::RenderableResource {
public:
  MLNMapViewMetalRenderableResource(MLNMapViewMetalImpl& backend_)
      : backend(backend_), delegate([[MLNMapViewImplDelegate alloc] initWithImpl:&backend]) {}

  void bind() override {
    if (!commandQueue) {
      commandQueue = [mtlView.device newCommandQueue];
    }

    if (!commandBuffer) {
      commandBuffer = [commandQueue commandBuffer];
      commandBufferPtr = NS::RetainPtr((__bridge MTL::CommandBuffer*)commandBuffer);
    }
  }

  const mln::mtl::RendererBackend& getBackend() const override { return backend; }

  const mln::mtl::MTLCommandBufferPtr& getCommandBuffer() const override {
    return commandBufferPtr;
  }

  virtual mln::mtl::MTLBlitPassDescriptorPtr getUploadPassDescriptor() const override {
    // Create from render pass descriptor?
    return NS::TransferPtr(MTL::BlitPassDescriptor::alloc()->init());
  }

  const mln::mtl::MTLRenderPassDescriptorPtr& getRenderPassDescriptor() const override {
    if (!cachedRenderPassDescriptor) {
      auto* mtlDesc = mtlView.currentRenderPassDescriptor;
      cachedRenderPassDescriptor = NS::RetainPtr((__bridge MTL::RenderPassDescriptor*)mtlDesc);
    }
    return cachedRenderPassDescriptor;
  }

  void swap() override {
    id<CAMetalDrawable> currentDrawable = [mtlView currentDrawable];
    if (currentDrawable) {
      // Round G: presented-frame timing. The registration precedes presentation, and a
      // dropped or unpresented drawable reports presentedTime 0 and is deliberately skipped. A
      // copied handler is taken before any Metal work is scheduled so replacement or teardown
      // on the main thread cannot race the callback's lifetime.
      MLNPresentedFrameHandler handler = backend.getPresentedFrameHandler();
      if (handler) {
        id<MLNDrawablePresentation> presentedDrawable = (id)currentDrawable;
        if ([presentedDrawable respondsToSelector:@selector(addPresentedHandler:)]) {
          [presentedDrawable addPresentedHandler:^(id<MLNDrawablePresentation> drawable) {
            const CFTimeInterval presented = drawable.presentedTime;
            if (presented > 0) {
              handler(presented);
            }
          }];
        } else {
          // CoreSimulator's CAMetalDrawable lacks the presentation callback. A completed
          // command buffer is the closest simulator-only frame boundary; the device path
          // above is true presentation timing.
          [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            handler(CACurrentMediaTime());
          }];
        }
      }
      // Task "make it measurable": a real, always-on GPU timing sample for this frame. Metal
      // fills in `GPUStartTime`/`GPUEndTime` on the command buffer itself once the GPU has
      // actually finished it - not an estimate from how often a delegate callback fires, which
      // is all the harness had before this (`HarnessRuntime.fpsLast2s`, a CPU-side count of
      // `didFinishRenderingFrame` arrivals). The handler must be added before `commit`, and
      // fires asynchronously, potentially well after this function returns and on a queue of
      // Metal's own choosing - so the map view is captured weakly (matching the `mapView` field
      // itself) and the whole handler is a no-op once it, or the command buffer's timing, is
      // gone. Left unconditional (see this file's header note on cost) rather than gated behind
      // a flag: the two GPU timestamp reads and one dictionary-free struct field write this
      // performs cost nothing next to the drawable present it already always does.
      // Performance round, Phase 0: only while the owner's panel has the recorders on.
      MLNMapView* mapViewForTiming = backend.getMapViewForTiming();
      if (mapViewForTiming && mapViewForTiming.frameTimingEnabled) {
      __weak MLNMapView* weakMapViewForTiming = mapViewForTiming;
      [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
        if (completedBuffer.status != MTLCommandBufferStatusCompleted) {
          return;  // error/cancelled: GPUStartTime/GPUEndTime are meaningless here
        }
        const CFTimeInterval gpuMs = (completedBuffer.GPUEndTime - completedBuffer.GPUStartTime) * 1000.0;
        if (gpuMs < 0) {
          return;  // seen on the simulator, which has no real GPU timeline of its own
        }
        MLNMapView* strongMapView = weakMapViewForTiming;
        if (!strongMapView) {
          return;
        }
        strongMapView.mbglMap.recordFrameGPUMs(gpuMs);
      }];
      }

      if (presentsWithTransaction) {
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        [currentDrawable present];
      } else {
        [commandBuffer presentDrawable:currentDrawable];
        [commandBuffer commit];
      }
    }

    commandBuffer = nil;
    commandBufferPtr.reset();

    cachedRenderPassDescriptor.reset();
  }

  mln::Size framebufferSize() {
    assert(mtlView);
    return {static_cast<uint32_t>(mtlView.drawableSize.width),
            static_cast<uint32_t>(mtlView.drawableSize.height)};
  }

private:
  MLNMapViewMetalImpl& backend;
  mln::mtl::MTLCommandBufferPtr commandBufferPtr;
  mutable mln::mtl::MTLRenderPassDescriptorPtr cachedRenderPassDescriptor;

public:
  MLNMapViewImplDelegate* delegate = nil;
  MTKView* mtlView = nil;
  id<MTLCommandBuffer> commandBuffer;
  id<MTLCommandQueue> commandQueue;
  bool presentsWithTransaction = false;

  // Cached last-applied values comparing against MTKView's reflected state round-trips
  // through UIKit and can defeat the no-op guard under non-integer scale factors.
  CGFloat lastAppliedScaleFactor = 0;
  CGSize lastAppliedDrawableSize = CGSizeZero;

  // We count how often the context was activated/deactivated so that we can truly deactivate it
  // after the activation count drops to 0.
  NSUInteger activationCount = 0;
};

MLNMapViewMetalImpl::MLNMapViewMetalImpl(MLNMapView* nativeView_)
    : MLNMapViewImpl(nativeView_),
      mln::mtl::RendererBackend(mln::gfx::ContextMode::Unique),
      mln::gfx::Renderable({0, 0}, std::make_unique<MLNMapViewMetalRenderableResource>(*this)) {}

MLNMapViewMetalImpl::~MLNMapViewMetalImpl() = default;

void MLNMapViewMetalImpl::setOpaque(const bool opaque) {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  resource.mtlView.opaque = opaque;
  resource.mtlView.layer.opaque = opaque;
}

void MLNMapViewMetalImpl::setPresentsWithTransaction(const bool value) {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  resource.presentsWithTransaction = value;

  if (@available(iOS 13.0, *)) {
    if (CAMetalLayer* metalLayer = MLN_OBJC_DYNAMIC_CAST(resource.mtlView.layer, CAMetalLayer)) {
      metalLayer.presentsWithTransaction = value;
    }
  }
}

void MLNMapViewMetalImpl::display() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();

  // Calling `display` here directly causes the stuttering bug (if
  // `presentsWithTransaction` is `YES` - see above)
  // as reported in https://github.com/mapbox/mapbox-gl-native-ios/issues/350
  //
  // Since we use `presentsWithTransaction` to synchronize with UIView
  // annotations, we now let the system handle when the view is rendered. This
  // has the potential to increase latency
  [resource.mtlView setNeedsDisplay];
}

void MLNMapViewMetalImpl::createView() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  if (resource.mtlView) {
    return;
  }

  id<MTLDevice> device = (__bridge id<MTLDevice>)resource.getBackend().getDevice().get();
  const auto scaleFactor = MLNEffectiveScaleFactorForView(mapView);

  resource.mtlView = [[MTKView alloc] initWithFrame:mapView.bounds device:device];
  resource.mtlView.delegate = resource.delegate;
  resource.mtlView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  // We own drawable sizing in layoutChanged(); disable MTKView's automatic recompute so
  // setting contentScaleFactor cannot race with explicit drawableSize assignment.
  resource.mtlView.autoResizeDrawable = NO;
  resource.mtlView.contentScaleFactor = scaleFactor;
  resource.mtlView.contentMode = UIViewContentModeCenter;
  resource.mtlView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  resource.mtlView.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
  // Performance round, Phase 1 item 2: the main pass's depth/stencil is cleared every frame
  // and never read back (the terrain occlusion depth is a separate render target of its own),
  // so it lives in tile memory only. At 3x on a 6.1 inch phone that is a 1179x2556x5-byte
  // attachment, about 15 MB, that no longer exists in system memory.
  if (@available(iOS 16.0, *)) {
    resource.mtlView.depthStencilStorageMode = MTLStorageModeMemoryless;
  }
  resource.mtlView.opaque = mapView.opaque;
  resource.mtlView.layer.opaque = mapView.opaque;
  resource.mtlView.enableSetNeedsDisplay = YES;
  if (@available(iOS 13.0, *)) {
    CAMetalLayer* metalLayer = MLN_OBJC_DYNAMIC_CAST(resource.mtlView.layer, CAMetalLayer);
    metalLayer.presentsWithTransaction = resource.presentsWithTransaction;
  }

  [mapView insertSubview:resource.mtlView atIndex:0];
}

UIView* MLNMapViewMetalImpl::getView() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  return resource.mtlView;
}

void MLNMapViewMetalImpl::deleteView() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  [resource.mtlView releaseDrawables];
}

void MLNMapViewMetalImpl::activate() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  if (resource.activationCount++) {
    return;
  }
}

void MLNMapViewMetalImpl::deactivate() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  if (--resource.activationCount) {
    return;
  }
}

UIImage* MLNMapViewMetalImpl::snapshot() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  return nil;  // TODO: resource.mtlView.snapshot;
}

void MLNMapViewMetalImpl::layoutChanged() {
  // Fix: unconditionally rewriting contentScaleFactor/drawableSize every
  // layout pass caused a feedback loop under iOS 26 Smart Display Zoom (non-integer scale).
  // Also skip pre-layout/detached passes — MLNEffectiveScaleFactorForView falls back to
  // mainScreen without a window, wrong for CarPlay.
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  const auto viewSize = mapView.bounds.size;
  if (!resource.mtlView || viewSize.width <= 0 || viewSize.height <= 0 || !mapView.window) {
    return;
  }

  // Round 4: the drawable follows the render scale in effect (1 at rest, the moving dial's
  // fraction during a gesture); the view's contentScaleFactor is the drawable's own scale so
  // the layer maps it to the same points.
  const auto scaleFactor = MLNEffectiveScaleFactorForView(mapView) * mapView.renderScaleInEffect;
  const CGSize target = CGSizeMake(std::round(viewSize.width * scaleFactor),
                                   std::round(viewSize.height * scaleFactor));

  if (scaleFactor != resource.lastAppliedScaleFactor) {
    resource.mtlView.contentScaleFactor = resource.lastAppliedScaleFactor = scaleFactor;
  }
  if (!CGSizeEqualToSize(target, resource.lastAppliedDrawableSize)) {
    resource.mtlView.drawableSize = resource.lastAppliedDrawableSize = target;
  }

  setRenderableSize({static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)});
#if DEBUG
  NSLog(@"[duckmaps-rs] layoutChanged scale=%.3f target=%.0fx%.0f drawable=%.0fx%.0f csf=%.3f auto=%d",
        scaleFactor, target.width, target.height, resource.mtlView.drawableSize.width,
        resource.mtlView.drawableSize.height, resource.mtlView.contentScaleFactor,
        (int)resource.mtlView.autoResizeDrawable);
#endif
}

MLNBackendResource* MLNMapViewMetalImpl::getObject() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  auto renderPassDescriptor = resource.getRenderPassDescriptor().get();

  return [[MLNBackendResource alloc] initWithMTKView:resource.mtlView
                                              device:resource.mtlView.device
                                renderPassDescriptor:[MTLRenderPassDescriptor renderPassDescriptor]
                                       commandBuffer:resource.commandBuffer];
}

MLNPresentedFrameHandler MLNMapViewMetalImpl::getPresentedFrameHandler() const {
  return mapView.presentedFrameHandler;
}
