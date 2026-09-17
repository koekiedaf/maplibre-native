#import "MLNMapView+Impl.h"
#import "MLNMapView_Private.h"

#include <mln/gfx/renderable.hpp>
#include <mln/mtl/renderer_backend.hpp>

@class MLNMapViewImplDelegate;

/// Adapter responsible for bridging calls from mbgl to MLNMapView and Cocoa.
class MLNMapViewMetalImpl final : public MLNMapViewImpl,
                                  public mln::mtl::RendererBackend,
                                  public mln::gfx::Renderable {
public:
  /// Round G: the map view's presented-frame handler, read on the render thread.
  MLNPresentedFrameHandler getPresentedFrameHandler() const;
  MLNMapViewMetalImpl(MLNMapView*);
  ~MLNMapViewMetalImpl() override;

public:
  void restoreFramebufferBinding();

  /// Task "make it measurable": lets the renderable resource's `swap()` (a method of an
  /// unrelated class, so it cannot reach the protected `mapView` member this class inherits)
  /// find the live `MLNMapView` to push a completed Metal command buffer's GPU timing into,
  /// via `-[MLNMapView mbglMap]` (`MLNMapView_Private.h`, already imported above). Weak, same
  /// as the inherited field itself - never lengthens the map view's lifetime.
  MLNMapView* getMapViewForTiming() const { return mapView; }

  // Implementation of mln::gfx::RendererBackend
public:
  mln::gfx::Renderable& getDefaultRenderable() override { return *this; }

private:
  void activate() override;
  void deactivate() override;
  // End implementation of mln::gfx::RendererBackend

  // Implementation of MLNMapViewImpl
public:
  mln::gfx::RendererBackend& getRendererBackend() override { return *this; }

  void setOpaque(bool) override;
  void display() override;
  void setPresentsWithTransaction(bool) override;
  void createView() override;
  UIView* getView() override;
  void deleteView() override;
  UIImage* snapshot() override;
  void layoutChanged() override;
  MLNBackendResource* getObject() override;
  // End implementation of MLNMapViewImpl
};
