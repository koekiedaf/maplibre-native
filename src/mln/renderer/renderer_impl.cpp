#include <mln/renderer/renderer_impl.hpp>

#include <mln/geometry/line_atlas.hpp>
#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/render_pass.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/gfx/renderable.hpp>
#include <mln/gfx/upload_pass.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/pattern_atlas.hpp>
#include <mln/renderer/renderer_observer.hpp>
#include <mln/renderer/render_static_data.hpp>
#include <mln/renderer/render_tree.hpp>
#include <mln/renderer/render_tile.hpp>
#include <mln/renderer/texture_pool.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/program_parameters.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/string.hpp>
#include <mln/util/logging.hpp>
#include <mln/util/instrumentation.hpp>

#include <mln/gfx/drawable_tweaker.hpp>
#include <mln/renderer/layer_tweaker.hpp>
#include <mln/renderer/render_target.hpp>
#include <mln/renderer/layer_group.hpp> // drape signature: TileLayerGroup::visitDrawables
#include <mln/renderer/bucket.hpp>      // drape signature: Bucket::getID content revision
#include <mln/util/hash.hpp>            // drape signature: hash_combine
#include <map>
#include <mln/renderer/render_terrain.hpp>
#include <mln/style/sky_impl.hpp> // DuckMaps fork only, task T3
#include <mln/renderer/dem_elevation_provider.hpp>
#include <mln/renderer/layers/terrain_layer_tweaker.hpp>
#include <mln/util/tile_cover.hpp>
#include <mln/util/geo.hpp>             // elevation trace: complete LatLng type (task C1)
#include <mln/util/monotonic_timer.hpp> // elevation trace: tMs (task C1)
#include <mln/util/projection.hpp>      // C6: project/unproject the forward sample line

#include <algorithm> // C6: std::max for the forward sample line
#include <cmath>   // C6: hypot for the forward sample line
#include <cstdio>  // elevation trace: fopen/fwrite/fflush (task C1)
#include <cstdlib> // elevation trace: getenv (task C1)
#include <iomanip> // elevation trace: setprecision (task C1)
#include <sstream> // elevation trace: ostringstream (task C1)

#if MLN_RENDER_BACKEND_METAL
#include <mln/mtl/renderer_backend.hpp>
#include <mln/mtl/context.hpp>       // DuckMaps fork only, task T3: mtl::Context::renderSky
#include <mln/shaders/mtl/sky.hpp>   // DuckMaps fork only, task T3: shaders::SkyUBO
#include <Metal/MTLCaptureManager.hpp>
#include <Metal/MTLCaptureScope.hpp>
/// Enable programmatic Metal frame captures for specific frame numbers.
/// Requires iOS 13
constexpr auto EnableMetalCapture = 0;
constexpr auto CaptureFrameStart = 0; // frames are 0-based
constexpr auto CaptureFrameCount = 1;
#elif MLN_RENDER_BACKEND_OPENGL
#include <mln/gl/defines.hpp>
#include <mln/gl/drawable_gl.hpp>
#endif // !MLN_RENDER_BACKEND_METAL

namespace mln {

using namespace style;

namespace {

RendererObserver& nullObserver() {
    static RendererObserver observer;
    return observer;
}

} // namespace

Renderer::Impl::Impl(gfx::RendererBackend& backend_,
                     float pixelRatio_,
                     const std::optional<std::string>& localFontFamily_)
    : orchestrator(!backend_.contextIsShared(), backend_.getThreadPool(), localFontFamily_),
      backend(backend_),
      observer(&nullObserver()),
      pixelRatio(pixelRatio_) {}

Renderer::Impl::~Impl() {
    assert(gfx::BackendScope::exists());
};

void Renderer::Impl::onPreCompileShader(shaders::BuiltIn shaderID,
                                        gfx::Backend::Type type,
                                        const std::string& additionalDefines) {
    observer->onPreCompileShader(shaderID, type, additionalDefines);
}

void Renderer::Impl::onPostCompileShader(shaders::BuiltIn shaderID,
                                         gfx::Backend::Type type,
                                         const std::string& additionalDefines) {
    observer->onPostCompileShader(shaderID, type, additionalDefines);
}

void Renderer::Impl::onShaderCompileFailed(shaders::BuiltIn shaderID,
                                           gfx::Backend::Type type,
                                           const std::string& additionalDefines) {
    observer->onShaderCompileFailed(shaderID, type, additionalDefines);
}

void Renderer::Impl::onRenderError(std::exception_ptr error) {
    observer->onRenderError(error);
}

void Renderer::Impl::setObserver(RendererObserver* observer_) {
    observer = observer_ ? observer_ : &nullObserver();
}

void Renderer::Impl::render(const RenderTree& renderTree, const std::shared_ptr<UpdateParameters>& updateParameters) {
    MLN_TRACE_FUNC();
    auto& context = backend.getContext();
    context.setObserver(this);

    assert(updateParameters);

#if MLN_RENDER_BACKEND_METAL
#if MLN_CREATE_AUTORELEASEPOOL
    NS::SharedPtr pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
#endif

    if constexpr (EnableMetalCapture) {
        const auto& mtlBackend = static_cast<mtl::RendererBackend&>(backend);

        const auto& mtlDevice = mtlBackend.getDevice();

        if (!commandCaptureScope) {
            if (const auto& cmdQueue = mtlBackend.getCommandQueue()) {
                if (const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager())) {
                    // NOLINTNEXTLINE(bugprone-assignment-in-if-condition)
                    if ((commandCaptureScope = NS::TransferPtr(captureManager->newCaptureScope(cmdQueue.get())))) {
                        const auto label = "Renderer::Impl frame=" + util::toString(frameCount);
                        commandCaptureScope->setLabel(NS::String::string(label.c_str(), NS::UTF8StringEncoding));
                        captureManager->setDefaultCaptureScope(commandCaptureScope.get());
                    }
                }
            }
        }

        // "When you capture a frame programmatically, you can capture Metal commands that span multiple
        //  frames by using a custom capture scope. For example, by calling begin() at the start of frame
        //  1 and end() after frame 3, the trace will contain command data from all the buffers that were
        //  committed in the three frames."
        // https://developer.apple.com/documentation/metal/debugging_tools/capturing_gpu_command_data_programmatically
        if constexpr (0 < CaptureFrameStart && 0 < CaptureFrameCount) {
            if (commandCaptureScope) {
                const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager());
                if (frameCount == CaptureFrameStart) {
                    constexpr auto captureDest = MTL::CaptureDestination::CaptureDestinationDeveloperTools;
                    if (captureManager && !captureManager->isCapturing() &&
                        captureManager->supportsDestination(captureDest)) {
                        if (auto captureDesc = NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init())) {
                            captureDesc->setCaptureObject(mtlDevice.get());
                            captureDesc->setDestination(captureDest);
                            NS::Error* errorPtr = nullptr;
                            if (captureManager->startCapture(captureDesc.get(), &errorPtr)) {
                                Log::Warning(Event::Render, "Capture Started");
                            } else {
                                std::string errStr = "<none>";
                                if (auto error = NS::TransferPtr(errorPtr)) {
                                    if (auto str = error->localizedDescription()) {
                                        if (auto cstr = str->utf8String()) {
                                            errStr = cstr;
                                        }
                                    }
                                }
                                Log::Warning(Event::Render, "Capture Failed: " + errStr);
                            }
                        }
                    }
                }
            }
        }
        if (commandCaptureScope) {
            commandCaptureScope->beginScope();

            const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager());
            if (captureManager->isCapturing()) {
                Log::Info(Event::Render, "Capturing frame " + util::toString(frameCount));
            }
        }
    }
#endif // MLN_RENDER_BACKEND_METAL

    // Blocks execution until the renderable is available.
    backend.getDefaultRenderable().wait();
    context.beginFrame();

    if (!staticData) {
        staticData = std::make_unique<RenderStaticData>(std::make_unique<gfx::ShaderRegistry>());

        // Initialize shaders for drawables
        const auto programParameters = ProgramParameters{pixelRatio, false};
        backend.initShaders(*staticData->shaders, programParameters);

        // Notify post-shader registration
        observer->onRegisterShaders(*staticData->shaders);
    }

    const auto& renderTreeParameters = renderTree.getParameters();
    staticData->has3D = renderTreeParameters.has3D;
    const Size renderableSize = backend.getDefaultRenderable().getSize();

    // Set when the drape render budget defers a target this frame, so a follow-up frame is
    // requested (via needsRepaint) to let the deferred targets catch up progressively.
    bool drapeWorkDeferred = false;

    if (renderState == RenderState::Never) {
        observer->onWillStartRenderingMap();
    }

    observer->onWillStartRenderingFrame();

    const TransformState& state = renderTreeParameters.transformParams.state;
    const EdgeInsets& frustumOffset = state.getFrustumOffset();
    const gfx::ScissorRect scissorRect = {
        .x = static_cast<int32_t>(frustumOffset.left() * pixelRatio),
#if MLN_RENDER_BACKEND_OPENGL
        .y = static_cast<int32_t>(frustumOffset.bottom() * pixelRatio),
#else
        .y = static_cast<int32_t>(frustumOffset.top() * pixelRatio),
#endif
        .width = renderableSize.width -
                 static_cast<uint32_t>((frustumOffset.left() + frustumOffset.right()) * pixelRatio),
        .height = renderableSize.height -
                  static_cast<uint32_t>((frustumOffset.top() + frustumOffset.bottom()) * pixelRatio),
    };

    PaintParameters parameters{
        context,
        pixelRatio,
        backend,
        renderTreeParameters.light,
        renderTreeParameters.mapMode,
        renderTreeParameters.debugOptions,
        renderTreeParameters.timePoint,
        renderTreeParameters.transformParams,
        *staticData,
        renderTree.getLineAtlas(),
        renderTree.getPatternAtlas(),
        texturePool,
        frameCount,
        updateParameters->tileLodMinRadius,
        updateParameters->tileLodScale,
        updateParameters->tileLodPitchThreshold,
        updateParameters->tileLodMode,
        renderableSize,
        scissorRect,
    };

    parameters.symbolFadeChange = renderTreeParameters.symbolFadeChange;
    parameters.opaquePassCutoff = renderTreeParameters.opaquePassCutOff;
    parameters.terrain = orchestrator.getRenderTerrain();
    const auto& sourceRenderItems = renderTree.getSourceRenderItems();

    const auto& layerRenderItems = renderTree.getLayerRenderItemMap();

    // Number of terrain drape targets in this frame's cover; drives the re-enable follow-up
    // frame and the drape re-bake trigger below (0 while terrain is off or its cover is empty).
    std::size_t frameDrapeTargetCount = 0;
    if (auto* terrain = orchestrator.getRenderTerrain()) {
        // Drape targets must exist before RenderTerrain::update drapes into them,
        // so build the same mesh cover it will use - the elevation-aware LOD ideal
        // cover taken from the view (not the DEM's loaded tiles) - and allocate one
        // drape target per tile. Same `state`/`updateParameters` as the update call,
        // so the two sets match; stale targets (tiles that left the cover) are
        // released so the pool tracks the view instead of growing unbounded.
        // Bind the DEM source before computing the cover: it is otherwise first
        // bound inside RenderTerrain::update (which runs after this pool is
        // built), leaving the first frame with an empty cover and no terrain -
        // permanent blankness in single-frame still renders (the render tests).
        terrain->prepareSource(orchestrator);
        const std::set<UnwrappedTileID> demTileIDs = terrain->computeMeshCover(state, updateParameters);
        for (const auto& id : demTileIDs) {
            texturePool.createRenderTarget(context, id, renderTreeParameters.backgroundColor);
        }
        texturePool.removeStaleRenderTargets(demTileIDs);
        frameDrapeTargetCount = demTileIDs.size();
        // Hand the exact cover to RenderTerrain so its mesh matches this pool
        terrain->setFrameMeshCover(demTileIDs);
    } else {
        // The pool persists across frames, so release the drape targets when
        // terrain is disabled instead of holding their textures indefinitely
        texturePool.removeStaleRenderTargets({});
    }

    // - UPLOAD PASS -------------------------------------------------------------------------------
    // Uploads all required buffers and images before we do any actual rendering.
    {
        const auto uploadPass = parameters.encoder->createUploadPass("upload",
                                                                     parameters.backend.getDefaultRenderable());
#if !defined(NDEBUG)
        const auto debugGroup = uploadPass->createDebugGroup("upload");
#endif

        // Update all clipping IDs + upload buckets.
        for (const RenderItem& item : sourceRenderItems) {
            item.upload(*uploadPass);
        }
        for (const RenderItem& item : layerRenderItems) {
            item.upload(*uploadPass);
        }
        staticData->upload(*uploadPass);
        renderTree.getLineAtlas().upload(*uploadPass);
        renderTree.getPatternAtlas().upload(*uploadPass);
    }

    // - LAYER GROUP UPDATE ------------------------------------------------------------------------
    // Updates all layer groups and process changes
    if (staticData && staticData->shaders) {
        orchestrator.updateLayers(*staticData->shaders,
                                  context,
                                  renderTreeParameters.transformParams.state,
                                  updateParameters,
                                  parameters,
                                  renderTree,
                                  texturePool);
    }

    orchestrator.processChanges();
    orchestrator.addRenderTargets(texturePool);

    // Create the terrain occlusion depth target before the upload phase, so the
    // symbol tweaker binds the real depth texture for THIS frame instead of the
    // far-plane placeholder (in single-frame renders like the render tests the
    // placeholder would otherwise never be replaced and occlusion never engage).
    if (auto* terrain = orchestrator.getRenderTerrain()) {
        terrain->prepareDepthTarget(parameters);
    }

    // Draped layer groups are not routed into individual render targets here;
    // each drape RenderTarget renders every overlapping draped drawable itself
    // (RenderTarget::renderDrapedLayerGroups), so a tile at a different zoom than
    // the terrain cover is drawn into every target it covers, like gl-js.

    // Per-drape-target content signatures live in the member perTargetDrapeSignature, cached
    // across frames and rebuilt only when the drape content changes (below). parameters
    // points into it for the drape targets pass.

    // Upload layer groups
    {
        const auto uploadPass = parameters.encoder->createUploadPass("layerGroup-upload",
                                                                     parameters.backend.getDefaultRenderable());
#if !defined(NDEBUG)
        const auto debugGroup = uploadPass->createDebugGroup("layerGroup-upload");
#endif

        // Update the debug layer groups
        orchestrator.updateDebugLayerGroups(renderTree, parameters);

        // Compute the frame-global draped-content signature once (see
        // PaintParameters::drapedContentSignature) and each drape target's own signature
        // (perTargetDrapeSignature), so RenderTarget::render can skip re-rendering - and
        // re-scanning - a target whose content is unchanged. Both fold the covering-tile
        // ids of all draped drawables, the draped group count, and the INTEGER tile-zoom
        // (not the continuous zoom), so pan / pitch / in-level pinch reuse the baked drape
        // textures and drapes re-render only when the covering tiles or integer zoom change
        // (matches maplibre-gl-js). The paint-property epoch is deliberately not folded in:
        // a paint change that alters draped geometry rebuilds its drawables (new covering
        // set, captured below), whereas folding the global epoch would re-render every drape
        // on any transition (e.g. a single label fading in).
        const bool terrainActive = orchestrator.getRenderTerrain() != nullptr;
        bool drapedContentChanged = true;
        if (terrainActive) {
            const int32_t zoomLevel = static_cast<int32_t>(parameters.state.getZoom());
            // Single pass over all draped drawables: fold each covering-tile hash into the
            // global signature and record (tile, hash) in a flat vector. A target's own
            // signature is then an order-independent sum of the hashes whose tile overlaps
            // it. Key on the COVERING TILE id, not the drawable-instance id, which churns on
            // every bucket rebuild/fade (consistent with RenderTarget::computeDrapeCoverage).
            std::size_t signature = 0;
            std::size_t drapedGroupCount = 0;
            std::vector<std::pair<UnwrappedTileID, std::size_t>> drapedTiles;
            drapedTiles.reserve(1024);
            orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
                if (layerGroup.getType() != LayerGroupBase::Type::TileLayerGroup ||
                    !layerGroup.shouldRenderToTerrain()) {
                    return;
                }
                drapedGroupCount++;
                static_cast<TileLayerGroup&>(layerGroup).visitDrawables([&](const gfx::Drawable& drawable) {
                    if (!drawable.getEnabled() || !drawable.getTileID()) {
                        return;
                    }
                    const UnwrappedTileID tile = drawable.getTileID()->toUnwrapped();
                    std::size_t h = 0;
                    util::hash_combine(h, tile.wrap);
                    util::hash_combine(h, tile.canonical.z);
                    util::hash_combine(h, tile.canonical.x);
                    util::hash_combine(h, tile.canonical.y);
                    // Fold the source bucket's identity so an in-place content upgrade - a drape
                    // built from an over-zoomed ancestor bucket, then the tile's own native bucket
                    // loads under the same covering-tile id - re-renders the drape instead of
                    // serving the stale low-detail bake until the next zoom. Keyed on the bucket id
                    // (stable across paint/fade), not the drawable id (which churns every frame).
                    // Excluded for hillshade: its drape samples a separate DEM "prepare" target with
                    // its own render lifecycle, and folding its bucket here broke it on initial load.
                    if (drawable.getName() != "hillshade") {
                        if (const auto& bucket = drawable.getBucket()) {
                            util::hash_combine(h, bucket->getID().id());
                        }
                    }
                    util::hash_combine(signature, h);
                    drapedTiles.emplace_back(tile, h);
                });
            });
            util::hash_combine(signature, drapedGroupCount);
            util::hash_combine(signature, zoomLevel);
            parameters.drapedContentSignature = signature;
            // Also treat the content as changed on the frame the drape cover reappears (fresh
            // targets after terrain was off / had an empty cover): the draped drawables still
            // hold screen-space UBOs from rendering to screen, so the tweakers must run to
            // re-establish tile-local drape UBOs before the targets are baked, or the map is
            // blank until the view moves.
            const bool coverJustReappeared = frameDrapeTargetCount > 0 && !terrainHadCoverLastFrame;
            drapedContentChanged = (lastDrapedContentSignature != signature) || coverJustReappeared;
            lastDrapedContentSignature = signature;

            // Rebuild the per-target signature map only when the drape content actually
            // changed. When it did not (steady panning, idle, in-level pinch) the map is
            // identical to last frame's, so reusing the cached member skips this
            // O(targets x draped-tiles) pass - pure per-frame overhead otherwise, and the
            // dominant per-frame cost on CPU-encode-bound low-end GPUs. Because the global
            // signature folds the covering-tile set, group count and integer zoom, an
            // unchanged signature guarantees the same targets with the same per-target sums.
            if (drapedContentChanged) {
                perTargetDrapeSignature.clear();
                orchestrator.visitRenderTargets([&](RenderTarget& renderTarget) {
                    const auto& tid = renderTarget.getDrapeTileID();
                    if (!tid) {
                        return;
                    }
                    std::size_t sig = 0;
                    for (const auto& [tile, h] : drapedTiles) {
                        if (tile == *tid || tile.isChildOf(*tid) || tid->isChildOf(tile)) {
                            sig += h;
                        }
                    }
                    util::hash_combine(sig, drapedGroupCount);
                    util::hash_combine(sig, zoomLevel);
                    perTargetDrapeSignature[*tid] = sig;
                });
            }
            parameters.perTargetDrapeSignature = &perTargetDrapeSignature;
        }
        // Latch whether the drape cover had targets this frame, for the re-bake trigger above.
        terrainHadCoverLastFrame = frameDrapeTargetCount > 0;

        // Tweakers are run in the upload pass so they can set up uniforms.
        parameters.currentLayer = 0;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            // Skip a draped layer group's tweaker when the drape content is unchanged: its
            // cached drape texture is not re-rendered this frame, and drapes are
            // camera-independent (tile-local matrix), so the recomputed per-drawable camera
            // UBOs would go unused. A real change moves the signature and re-runs the tweaker
            // the same frame the drape re-renders, keeping the two consistent.
            const bool skipDrapedTweaker = terrainActive && !drapedContentChanged && layerGroup.shouldRenderToTerrain();
            if (!skipDrapedTweaker) {
                layerGroup.runTweakers(renderTree, parameters);
            }
            parameters.currentLayer++;
        });

        // Run terrain tweaker if terrain is enabled
        if (auto* terrain = orchestrator.getRenderTerrain()) {
            if (auto* terrainTweaker = terrain->getTweaker()) {
                if (const auto& layerGroup = terrain->getLayerGroup()) {
                    terrainTweaker->execute(*layerGroup, parameters);
                }
                if (const auto& depthLayerGroup = terrain->getDepthLayerGroup()) {
                    terrainTweaker->execute(*depthLayerGroup, parameters);
                }
            }
        }

        parameters.currentLayer = 0;
        orchestrator.visitDebugLayerGroups([&](LayerGroupBase& layerGroup) {
            layerGroup.runTweakers(renderTree, parameters);
            parameters.currentLayer++;
        });

        // Give the layers a chance to upload
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) { layerGroup.upload(*uploadPass); });

        // The terrain depth layer group is deliberately not registered with the
        // orchestrator (it renders only in RenderTerrain::renderDepth), so the
        // visitLayerGroups upload above does not cover it. Upload it explicitly:
        // without this its drawables never get vertex buffers, the Vulkan binds
        // fail silently and the depth pass records nothing, so the occlusion
        // depth texture stays at the far plane and symbols are never hidden
        // behind terrain (GL builds attribute state at draw time and got away
        // with it).
        if (auto* terrain = orchestrator.getRenderTerrain()) {
            if (const auto& depthLayerGroup = terrain->getDepthLayerGroup()) {
                depthLayerGroup->upload(*uploadPass);
            }
        }

        // Give the render targets a chance to upload
        orchestrator.visitRenderTargets([&](RenderTarget& renderTarget) { renderTarget.upload(*uploadPass); });

        // Upload the Debug layer group
        orchestrator.visitDebugLayerGroups([&](LayerGroupBase& layerGroup) { layerGroup.upload(*uploadPass); });
    }

    const Size atlasSize = parameters.patternAtlas.getPixelSize();
    const auto& worldSize = parameters.renderableSize;
    const shaders::GlobalPaintParamsUBO globalPaintParamsUBO = {
        .pattern_atlas_texsize = {static_cast<float>(atlasSize.width), static_cast<float>(atlasSize.height)},
        .units_to_pixels = {1.0f / parameters.pixelsToGLUnits[0], 1.0f / parameters.pixelsToGLUnits[1]},
        .world_size = {static_cast<float>(worldSize.width), static_cast<float>(worldSize.height)},
        .camera_to_center_distance = parameters.state.getCameraToCenterDistance(),
        .symbol_fade_change = parameters.symbolFadeChange,
        .aspect_ratio = parameters.state.getSize().aspectRatio(),
        .pixel_ratio = parameters.pixelRatio,
        .map_zoom = static_cast<float>(parameters.state.getZoom()),
        .pad1 = 0,
        // Target tile while drawing into a terrain drape target (w != 0);
        // the per-target buffers set it in RenderTarget::updateDrapeGlobalUBO
        .drape_tile = {0.0f, 0.0f, 0.0f, 0.0f},
    };
    auto& globalUniforms = context.mutableGlobalUniformBuffers();
    globalUniforms.createOrUpdate(shaders::idGlobalPaintParamsUBO, &globalPaintParamsUBO, context);

    // Refresh each terrain drape target's copy of the global paint params
    // (same values plus the target tile in drape_tile)
    if (orchestrator.getRenderTerrain()) {
        texturePool.visitRenderTargets([&](std::shared_ptr<RenderTarget>& renderTarget) {
            renderTarget->updateDrapeGlobalUBO(globalPaintParamsUBO, context);
        });
    }

    // - 3D PASS
    // -------------------------------------------------------------------------------------
    // Renders any 3D layers bottom-to-top to unique FBOs with texture
    // attachments, but share the same depth rbo between them.
    const auto common3DPass = [&] {
        if (parameters.staticData.has3D) {
            const auto debugGroup(parameters.encoder->createDebugGroup("common-3d"));
            parameters.pass = RenderPass::Pass3D;
#if MLN_RENDER_BACKEND_OPENGL
            parameters.updateStencilBufferAvailability();
#endif

            // TODO is this needed?
            // if (!parameters.staticData.depthRenderbuffer ||
            //    parameters.staticData.depthRenderbuffer->getSize() != parameters.renderableSize) {
            //    parameters.staticData.depthRenderbuffer =
            //        parameters.context.createRenderbuffer<gfx::RenderbufferPixelType::Depth>(
            //            parameters.renderableSize);
            //}
            // parameters.staticData.depthRenderbuffer->setShouldClear(true);
        }
    };

    const auto drawable3DPass = [&] {
        const auto debugGroup(parameters.encoder->createDebugGroup("drawables-3d"));
        assert(parameters.pass == RenderPass::Pass3D);

        // draw layer groups, 3D pass
        parameters.currentLayer = static_cast<uint32_t>(orchestrator.numLayerGroups()) - 1;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            layerGroup.render(orchestrator, parameters);
            if (parameters.currentLayer > 0) {
                parameters.currentLayer--;
            }
        });
    };

    const auto drawableTargetsPass = [&] {
        // Render targets are held in insertion order, but the terrain drape targets
        // consume the others: draping the hillshade layer samples the texture its
        // prepare pass renders. A drape target added in an earlier frame therefore
        // sits ahead of a prepare target added later and would sample it before it
        // was drawn this frame - reading black, which the hillshade decodes as the
        // maximum slope (the prepare pass encodes flat as 0.5, not 0), shading the
        // whole tile solid. Draw the producers first, then the drapes that sample
        // them.
        orchestrator.visitRenderTargets([&](RenderTarget& renderTarget) {
            if (!renderTarget.getDrapeTileID()) {
                renderTarget.render(orchestrator, renderTree, parameters);
            }
        });
        // Drape render budget: cap how many drape targets actually re-render per frame. A
        // burst of dirty targets (tilt/pan changing coverage) otherwise stalls one frame for
        // tens of ms each; instead render up to the budget and defer the rest (they keep their
        // stale texture), requesting a follow-up frame so they catch up progressively.
        // Never-rendered targets always render (avoid blank tiles). The cap comes from the
        // map's TerrainLoadMode; Quality (default) is unlimited.
        const int drapeCap = terrainLoadBudget(updateParameters->terrainLoadMode).drapeRerendersPerFrame;
        int drapeBudget = drapeCap > 0 ? drapeCap : (1 << 30);
        orchestrator.visitRenderTargets([&](RenderTarget& renderTarget) {
            if (renderTarget.getDrapeTileID()) {
                const auto res = renderTarget.render(
                    orchestrator, renderTree, parameters, /*canRerender=*/drapeBudget > 0);
                if (res == RenderTarget::RenderResult::Rendered) {
                    --drapeBudget;
                } else if (res == RenderTarget::RenderResult::Deferred) {
                    drapeWorkDeferred = true;
                }
            }
        });
    };

    const auto commonClearPass = [&] {
        // - CLEAR
        // -------------------------------------------------------------------------------------
        // Renders the backdrop of the OpenGL view. This also paints in areas where
        // we don't have any tiles whatsoever.
        {
            std::optional<Color> color;
            if (parameters.debugOptions & MapDebugOptions::Overdraw) {
                color = Color::black();
            } else if (!backend.contextIsShared()) {
                color = renderTreeParameters.backgroundColor;
            }
            parameters.renderPass = parameters.encoder->createRenderPass(
                "main buffer",
                {.renderable = parameters.backend.getDefaultRenderable(),
                 .clearColor = color,
                 .clearDepth = 1.0f,
                 .clearStencil = 0});
#if MLN_RENDER_BACKEND_OPENGL
            parameters.updateStencilBufferAvailability();
#endif
        }
    };

    // DuckMaps fork only, task T3: the style spec's `sky` root property
    // (https://maplibre.org/maplibre-style-spec/sky/). Draws a full-screen sky gradient once
    // per frame, in the main render pass, before any layer group - so terrain and every layer
    // paint over it, exactly as maplibre-gl-js's own sky pass behaves (its background layer,
    // drawn into the drape targets rather than full screen when terrain is on, does not cover
    // this for the same reason). When the style carries no `sky` root property at all
    // (orchestrator.getSky() empty), this draws NOTHING - no drawable, no pass - which is half
    // of this feature's own acceptance bar (see the task's own final report).
    //
    // Only Metal is implemented (see mtl::Context::renderSky and include/mln/shaders/mtl/sky.hpp
    // for the shader itself); GL/Vulkan/WebGPU have unused ShaderSource specializations only,
    // following this tree's own convention for a new shader (see terrain_contour's own gl.hpp,
    // similarly untested).
    const auto skyPass = [&] {
#if MLN_RENDER_BACKEND_METAL
        const auto& sky = orchestrator.getSky();
        if (!sky) {
            return;
        }

        // Reuses the enclosing render()'s own `state` (renderTreeParameters.transformParams.state,
        // aliased by parameters.state too) rather than redeclaring it, to avoid shadowing it.
        const double pitch = state.getPitch();
        const double roll = state.getRoll();
        const float cameraToCenterDistance = state.getCameraToCenterDistance();

        // getMercatorHorizon(transform), ported verbatim from maplibre-gl-js (including the
        // 0.85 constant) - see the task's own final report for the exact quote and where
        // getPitch()/getCameraToCenterDistance() come from in TransformState.
        const double horizonOffset = std::tan(M_PI_2 - pitch) * cameraToCenterDistance * 0.85;

        // r = cos(roll), i = sin(roll) - written in the general form even though this app never
        // rolls the camera (r=1, i=0 always today); see the task's own instructions on why a
        // hardcoded zero would be a lie in the code.
        const double r = std::cos(roll);
        const double i = std::sin(roll);

        const Size& size = state.getSize(); // CSS pixels, matching gl-js's transform.width/height
        const float n = parameters.pixelRatio;

        const float horizonX = static_cast<float>((static_cast<double>(size.width) / 2.0 - horizonOffset * i) * n);
        const float horizonY = static_cast<float>((static_cast<double>(size.height) / 2.0 + horizonOffset * r) * n);
        const float viewportHeight = static_cast<float>(parameters.renderableSize.height);

        const std::array<float, 4> skyColor = (*sky)->skyColor;
        const std::array<float, 4> horizonColor = (*sky)->horizonColor;

        const shaders::SkyUBO skyUBO = {
            .sky_color = skyColor,
            .horizon_color = horizonColor,
            .horizon = {horizonX, horizonY},
            .horizon_normal = {static_cast<float>(-i), static_cast<float>(r)},
            .sky_horizon_blend = (*sky)->skyHorizonBlend * static_cast<float>(size.height) / 2.0f * n,
            // u_sky_blend = projectionTransition, 0 in mercator - the only projection this
            // engine has (see mtl/sky.hpp's own comment on this uniform).
            .sky_blend = 0.0f,
            .viewport_height = viewportHeight,
            .pad0 = 0.0f,
        };

        auto& mtlContext = static_cast<mtl::Context&>(context);
        mtlContext.renderSky(*parameters.renderPass, parameters.staticData, skyUBO);
#endif // MLN_RENDER_BACKEND_METAL
    };

    // Actually render the layers
    // Drawables
    const auto drawableOpaquePass = [&] {
        const auto debugGroup(parameters.renderPass->createDebugGroup("drawables-opaque"));
        parameters.pass = RenderPass::Opaque;
        parameters.depthRangeSize = 1 - (orchestrator.numLayerGroups() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;

        // draw layer groups, opaque pass
        parameters.currentLayer = 0;
        orchestrator.visitLayerGroupsReversed([&](LayerGroupBase& layerGroup) {
            if (!(parameters.terrain && layerGroup.getType() == LayerGroupBase::Type::TileLayerGroup &&
                  layerGroup.shouldRenderToTerrain())) {
                layerGroup.render(orchestrator, parameters);
            }
            parameters.currentLayer++;
        });
    };

    const auto drawableTranslucentPass = [&] {
        const auto debugGroup(parameters.renderPass->createDebugGroup("drawables-translucent"));
        parameters.pass = RenderPass::Translucent;
        parameters.depthRangeSize = 1 - (orchestrator.numLayerGroups() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;

        // draw layer groups, translucent pass; draped groups render only into the
        // terrain render targets (RenderTarget::renderDrapedLayerGroups)
        parameters.currentLayer = static_cast<uint32_t>(orchestrator.numLayerGroups()) - 1;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            if (!(parameters.terrain && layerGroup.getType() == LayerGroupBase::Type::TileLayerGroup &&
                  layerGroup.shouldRenderToTerrain())) {
                layerGroup.render(orchestrator, parameters);
            }
            if (parameters.currentLayer > 0) {
                parameters.currentLayer--;
            }
        });

        // Finally, render any legacy layers which have not been converted to drawables.
        // Note that they may be out of order, this is just a temporary fix for `RenderLocationIndicatorLayer` (#2216)
        parameters.depthRangeSize = 1 - (layerRenderItems.size() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;
        int32_t i = static_cast<int32_t>(layerRenderItems.size()) - 1;
        for (auto it = layerRenderItems.begin(); it != layerRenderItems.end() && i >= 0; ++it, --i) {
            parameters.currentLayer = i;
            const RenderItem& item = *it;
            if (item.hasRenderPass(parameters.pass)) {
                item.render(parameters);
            }
        }
    };

    const auto drawableDebugOverlays = [&] {
        // Renders debug overlays.
        {
            const auto debugGroup(parameters.renderPass->createDebugGroup("debug"));
            parameters.currentLayer = 0;
            orchestrator.visitDebugLayerGroups([&](LayerGroupBase& layerGroup) {
                layerGroup.render(orchestrator, parameters);
                parameters.currentLayer++;
            });
        }
    };

    if (parameters.staticData.has3D) {
        common3DPass();
        drawable3DPass();
    }
    drawableTargetsPass();
    // Terrain depth pass for symbol occlusion (sampled by calculate_visibility)
    if (auto* terrain = orchestrator.getRenderTerrain()) {
        terrain->renderDepth(orchestrator, renderTree, parameters);
    }
    commonClearPass();
    context.bindGlobalUniformBuffers(*parameters.renderPass);
    // DuckMaps fork only, task T3: right after the main pass is created and bound, before any
    // layer group draws into it - see skyPass's own comment above for why.
    skyPass();
    drawableOpaquePass();
    drawableTranslucentPass();
    drawableDebugOverlays();

    // Give the layers a chance to do cleanup
    orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) { layerGroup.postRender(orchestrator, parameters); });
    context.unbindGlobalUniformBuffers(*parameters.renderPass);

    // Ends the RenderPass
    parameters.renderPass.reset();

    const auto startRendering = util::MonotonicTimer::now().count();
    // present submits render commands
    parameters.encoder->present(parameters.backend.getDefaultRenderable());
    context.renderingStats().renderingTime = util::MonotonicTimer::now().count() - startRendering;

    parameters.encoder.reset();
    context.endFrame();

#if MLN_RENDER_BACKEND_METAL
    if constexpr (EnableMetalCapture) {
        if (commandCaptureScope) {
            commandCaptureScope->endScope();

            const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager());
            if (frameCount == CaptureFrameStart + CaptureFrameCount - 1 && captureManager->isCapturing()) {
                captureManager->stopCapture();
            }
        }
    }
#endif // MLN_RENDER_BACKEND_METAL

    context.renderingStats().encodingTime = renderTree.getElapsedTime() - context.renderingStats().renderingTime;

    // A terrain that is enabled but produced an empty drape cover this frame (its DEM source
    // is not resolved until RenderTerrain::update, which runs after computeMeshCover) needs a
    // follow-up frame or it would idle blank until the view is panned - most visible when
    // terrain is toggled back on over an otherwise static map. Bounded so it cannot spin.
    bool terrainCoverPending = false;
    if (orchestrator.getRenderTerrain() && frameDrapeTargetCount == 0) {
        if (terrainCoverRetryFrames > 0) {
            --terrainCoverRetryFrames;
            terrainCoverPending = true;
        }
    } else {
        terrainCoverRetryFrames = 4;
    }

    // Report the rendered terrain height under the map centre, and under the camera's own ground
    // point (task 2.0b). Only the render side has the DEM, so this is the channel; whether the
    // camera acts on either sample is the map's call (Map::setCenterClampedToGround for the
    // centre, always for the camera-above-terrain clamp). Gated so a still map does not post a
    // message a frame.
    bool centerElevationSettling = false;
    // C6: kept for the debug trace below, which is otherwise blind to the forward rise.
    std::optional<double> traceCameraGroundRise;
    {
        // Terrain switched off (or never on) has to be reported too, as a height of zero: the
        // camera's centre altitude is sticky, and a centre left lifted over a map that is now
        // drawn at sea level would keep the camera high and every gesture solved on a plane that
        // no longer exists. nullptr terrain therefore reports 0 rather than reporting nothing.
        auto* terrain = orchestrator.getRenderTerrain();
        const double centerElevation =
            terrain ? terrain->getElevationForLatLng(updateParameters->transformState.getLatLng()) : 0.0;
        const bool centerChanged = std::abs(centerElevation - lastReportedCenterElevation) > 0.25;
        if (centerChanged) {
            lastReportedCenterElevation = centerElevation;
            observer->onTerrainCenterElevationChanged(centerElevation);
        }

        // The camera-ground rise: how much higher (or lower) the terrain under the camera's own
        // ground point is than the terrain under the map centre, both sampled here in the same
        // frame. Reporting the difference rather than either absolute height means the map
        // thread's clamp never has to compare against its own (possibly still-catching-up)
        // centre altitude - the two DEM samples that make up the rise come from the same frame,
        // so they cannot be out of step with each other, only with what the map thread does with
        // them next.
        //
        // Both samples are taken with queryElevationForLatLng, not getElevationForLatLng: the
        // camera's own ground point is very often off screen (behind the visible area at higher
        // pitch), so its DEM tile can simply not be loaded yet even though the map is well inside
        // the built world - not over the sea. getElevationForLatLng cannot tell those two cases
        // apart and folds a missing tile into 0.0, which used to be read back downstream as "sea
        // level" and turn the clamp off exactly where the camera is most likely to be near a
        // hillside. Reporting nullopt whenever EITHER sample has no DEM to read - here, or with
        // no render terrain at all - is the honest answer: we cannot see the ground under the
        // camera this frame, so the clamp stays off rather than clamping to a fabricated rise.
        const std::optional<double> queriedCenterElevation =
            terrain ? terrain->queryElevationForLatLng(updateParameters->transformState.getLatLng())
                    : std::nullopt;
        // Task C6, the bird climb. The rise is the HIGHEST ground along the flight path
        // ahead, not the single sample under the camera. Sampling only under the camera is
        // what every other renderer does (MapLibre GL JS's _elevateCameraIfInsideTerrain
        // takes one DEM sample at the camera's own lng/lat; Cesium's adjustHeightForTerrain
        // reads Scene.globeHeight under the camera) and it is why theirs jumps: the ground
        // under the camera is BEHIND the centre at any pitch, so the camera only learns
        // about a wall once the wall is already underneath it, and the correction is then
        // a step. Sampling forward means the climb begins while the wall is still ahead and
        // is spread over the whole approach, which is what makes it continuous. There is no
        // rate limiter anywhere in this: the smoothness comes from the window sliding, not
        // from filtering a value, so no wrong number is ever being smoothed over.
        //
        // The line sampled runs from the camera's own ground point, through the map centre,
        // and on for kForwardLookahead times that distance again. It is the straight line
        // the camera flies along, so it is interpolated in projected coordinates rather than
        // in degrees. At pitch 0 the camera's ground point IS the centre, the line has zero
        // length, every sample lands on the centre and the rise is identically zero for any
        // terrain - the property the whole rise formulation exists to keep.
        //
        // Samples with no loaded DEM are skipped rather than read as sea level; the rise is
        // reported as nullopt (clamp off, honestly) only when nothing along the line could be
        // sampled at all, exactly as the single-sample form did.
        // Each sample's requirement is DISCOUNTED by how far ahead it is. A bird does not
        // need to be above a wall three kilometres away; it needs to be on a climb that
        // reaches the wall's height by the time it arrives. So the height a sample at
        // forward distance d demands is `elevation(d) - kClimbGradient * d`, and the
        // requirement is the maximum of that over the whole line. At d = 0, the ground
        // directly under the camera, the discount is zero and the requirement is the bare
        // ground height, which is right: you must already be above what you are over.
        //
        // Without the discount the requirement steps. Measured on the first build of this
        // task, flying south into the Gavarnie wall at zoom 15.5: the plain forward maximum
        // went 794 m, 1061 m, 1491 m over three consecutive frames as the top of the cliff
        // crossed the far edge of a hard-edged window, and the clamp answered with a 0.93
        // zoom-level correction in about 110 ms, which is a snap. The discount turns the
        // same wall into a requirement that grows continuously all the way in, because a
        // sample's contribution rises smoothly as its own d shrinks. This is geometry, not
        // a rate limiter: no value is filtered, delayed or averaged, and the requirement is
        // exact and immediate at every instant. It is the shape of the climb, which is what
        // David asked for ("a gradual, continuous rise to clear the summit").
        //
        // The gradient is a slope in metres of altitude per metre of ground. 0.2 is about
        // 11 degrees, an ordinary aircraft climb, and it is the measured choice rather than
        // the first guess: 0.5 (27 degrees) discounted the Gavarnie cirque, 2862 m of rock
        // 2.9 km ahead, to below the ground under the camera, so the requirement stayed
        // negative and the clamp never armed until the wall was inside two kilometres, at
        // which point the climb would have been steep again. 0.2 leaves that same wall
        // demanding 2282 m from 2.9 km out, so the climb begins early and grows all the
        // way in, which is what "gradual" asks for.
        // The line is at least kMinForwardMeters long however far in the camera is zoomed.
        // Scaling the window with the camera-to-centre distance alone is right in spirit (a
        // wide view should look further) but it fails exactly where it matters: measured at
        // zoom 16.6 in the Gavarnie approach the window reached only 1.3 km, the cirque was
        // 2.5 km out, and the requirement read as -55 m - the camera had no idea a 1400 m
        // wall was coming, and would have learned about it all at once. 5 km is the distance
        // a climb at this gradient needs to gain a thousand metres, so it is the distance
        // over which a thousand-metre obstacle can be cleared gradually rather than suddenly.
        constexpr int kForwardSamples = 48;
        constexpr double kForwardLookahead = 2.0;
        constexpr double kMinForwardMeters = 5000.0;
        constexpr double kClimbGradient = 0.2;
        std::optional<double> forwardMaxElevation;
        if (terrain && queriedCenterElevation) {
            const auto& ts = updateParameters->transformState;
            const LatLng cameraLatLng = ts.getCameraLatLng();
            const LatLng centerLatLng = ts.getLatLng();
            // A fixed scale for the projection: the interpolation only needs a linear space,
            // and the samples are unprojected back before they are read.
            constexpr double kSampleProjectionScale = 1.0;
            const auto camPoint = Projection::project(cameraLatLng, kSampleProjectionScale);
            const auto ctrPoint = Projection::project(centerLatLng, kSampleProjectionScale);
            const double dx = ctrPoint.x - camPoint.x;
            const double dy = ctrPoint.y - camPoint.y;
            // Ground metres from the camera's own ground point to the centre, which is the
            // unit the sample parameter t is measured in. Projection::project at scale 1 is
            // Mercator pixels with a world size of one tile, so one of its units is exactly
            // getMetersPerPixelAtLatitude(lat, 0) metres - the same `k` the camera clamp in
            // TransformState::constrainCameraAboveTerrain uses, so the two agree by
            // construction rather than by coincidence.
            const double metersPerUnit = Projection::getMetersPerPixelAtLatitude(centerLatLng.latitude(), 0.0);
            const double cameraToCenterGroundM = std::hypot(dx, dy) * metersPerUnit;
            const double forwardMeters = std::max((1.0 + kForwardLookahead) * cameraToCenterGroundM,
                                                  kMinForwardMeters);
            // At pitch 0 the camera's ground point IS the centre, so the direction is
            // undefined and there is no "ahead" to sample: the rise is zero, by
            // construction, for any terrain. Guard the normalisation rather than divide by
            // a zero length.
            const double dLen = std::hypot(dx, dy);
            if (dLen > 0.0) {
                const double ux = dx / dLen;
                const double uy = dy / dLen;
                for (int i = 0; i <= kForwardSamples; ++i) {
                    const double d = forwardMeters * static_cast<double>(i) / static_cast<double>(kForwardSamples);
                    const double units = d / metersPerUnit;
                    const auto sample = terrain->queryElevationForLatLng(Projection::unproject(
                        {camPoint.x + ux * units, camPoint.y + uy * units}, kSampleProjectionScale));
                    if (!sample) {
                        continue;
                    }
                    const double demanded = *sample - kClimbGradient * d;
                    if (!forwardMaxElevation || demanded > *forwardMaxElevation) {
                        forwardMaxElevation = demanded;
                    }
                }
            }
        }
        const std::optional<double> cameraGroundRise =
            (queriedCenterElevation && forwardMaxElevation)
                ? std::optional<double>(*forwardMaxElevation - *queriedCenterElevation)
                : std::nullopt;
        traceCameraGroundRise = cameraGroundRise;
        const bool cameraGroundValidityChanged =
            cameraGroundRise.has_value() != lastReportedCameraGroundRise.has_value();
        const bool cameraGroundChanged =
            cameraGroundValidityChanged ||
            (cameraGroundRise && lastReportedCameraGroundRise &&
             std::abs(*cameraGroundRise - *lastReportedCameraGroundRise) > 0.25);
        if (cameraGroundChanged) {
            lastReportedCameraGroundRise = cameraGroundRise;
            observer->onTerrainCameraGroundRiseChanged(cameraGroundRise);
        }

        if (centerChanged || cameraGroundChanged) {
            // The camera only moves onto the terrain on a following frame, so this one was
            // drawn before either clamp caught up. Report the frame as not settled: a still
            // render would otherwise capture the pre-clamp image, which for terrain taller than
            // the camera is empty. One shared, bounded counter for both channels, so a DEM that
            // never settles on either cannot spin the loop forever.
            centerElevationSettling = ++centerElevationSettleFrames <= kMaxCenterElevationSettleFrames;
        } else {
            centerElevationSettleFrames = 0;
        }
    }

    // DuckMaps fork only, task C1: debug-only, off-by-default per-frame elevation trace. Purely
    // observational - it reads transformState and calls RenderTerrain's own (also new, also
    // read-only) probe accessors, and changes no camera or elevation behaviour. The env var is
    // read once, on the first frame rendered by this process; unset or empty skips this whole
    // block every frame after, including the probe calls, so tracing costs nothing when off.
    {
        struct ElevationTraceSink {
            bool enabled = false;
            std::FILE* file = nullptr;
        };
        static const ElevationTraceSink sink = [] {
            ElevationTraceSink s;
            const char* path = std::getenv("DUCKMAPS_ELEVATION_TRACE");
            if (path && *path) {
                s.file = std::fopen(path, "a");
                s.enabled = (s.file != nullptr);
            }
            return s;
        }();

        if (sink.enabled) {
            static uint64_t elevationTraceFrame = 0;
            static double elevationTraceStartS = -1.0;
            const double nowS = util::MonotonicTimer::now().count();
            if (elevationTraceStartS < 0.0) {
                elevationTraceStartS = nowS;
            }
            const double tMs = (nowS - elevationTraceStartS) * 1000.0;

            auto* traceTerrain = orchestrator.getRenderTerrain();
            const auto& transformState = updateParameters->transformState;
            const LatLng traceCenterLatLng = transformState.getLatLng();
            const LatLng traceCameraLatLng = transformState.getCameraLatLng();

            const RenderTerrain::ElevationProbe centerProbe =
                traceTerrain ? traceTerrain->probeElevationForLatLng(traceCenterLatLng)
                             : RenderTerrain::ElevationProbe{};
            const RenderTerrain::ElevationProbe cameraProbe =
                traceTerrain ? traceTerrain->probeElevationForLatLng(traceCameraLatLng)
                             : RenderTerrain::ElevationProbe{};

            std::ostringstream os;
            os << std::fixed << std::setprecision(8);
            os << "{\"frame\":" << elevationTraceFrame << ",\"tMs\":" << tMs << ",\"lng\":"
               << traceCenterLatLng.longitude() << ",\"lat\":" << traceCenterLatLng.latitude()
               << ",\"zoom\":" << transformState.getZoom() << ",\"pitch\":" << transformState.getPitch()
               << ",\"bearing\":" << transformState.getBearing()
               << ",\"centerAltitudeM\":" << transformState.getCenterAltitude() // already metres:
               // TransformState::getCenterAltitude() returns z (pixel-space altitude) multiplied
               // by metres-per-pixel at the centre latitude/zoom, so no pixel->metre conversion
               // is needed here.
               << ",\"exaggeration\":" << (traceTerrain ? traceTerrain->getExaggeration() : 0.0f)
               << ",\"gestureInProgress\":" << (transformState.isGestureInProgress() ? "true" : "false")
               << ",\"forwardRiseM\":";
            if (traceCameraGroundRise) {
                os << *traceCameraGroundRise;
            } else {
                os << "null";
            }
            os << ",\"cameraAltitudeM\":" << transformState.getCameraAltitudeMeters()
               << ",\"terrain\":" << (traceTerrain ? "true" : "false") << ",\"center\":{\"m\":"
               << centerProbe.meters << ",\"demZ\":" << static_cast<int>(centerProbe.demZ)
               << ",\"demX\":" << centerProbe.demX << ",\"demY\":" << centerProbe.demY
               << ",\"exact\":" << (centerProbe.exact ? "true" : "false")
               << ",\"hit\":" << (centerProbe.hit ? "true" : "false") << "}" << ",\"cameraGround\":{\"m\":"
               << cameraProbe.meters << ",\"demZ\":" << static_cast<int>(cameraProbe.demZ)
               << ",\"demX\":" << cameraProbe.demX << ",\"demY\":" << cameraProbe.demY
               << ",\"exact\":" << (cameraProbe.exact ? "true" : "false")
               << ",\"hit\":" << (cameraProbe.hit ? "true" : "false") << "}" << ",\"demTiles\":[";
            if (traceTerrain) {
                bool firstDemTile = true;
                for (const auto& id : traceTerrain->getResidentDemTileIds()) {
                    if (!firstDemTile) {
                        os << ",";
                    }
                    firstDemTile = false;
                    os << "\"" << static_cast<int>(id.z) << "/" << id.x << "/" << id.y << "\"";
                }
            }
            // DuckMaps fork only, task C5: the terrain mesh cover the last rendered frame
            // actually used (see RenderTerrain::setFrameMeshCover /
            // getLastFrameMeshCoverTileIds), as a canonical-z histogram plus the count and
            // min/max z, so the cover's zoom spread is visible in the trace without dumping
            // every tile id. Same debug-only, off-by-default guard as the rest of this block.
            std::map<int, int> meshCoverHistogram;
            std::size_t meshCoverCount = 0;
            uint8_t meshCoverMinZ = 0;
            uint8_t meshCoverMaxZ = 0;
            if (traceTerrain) {
                const auto meshCoverIds = traceTerrain->getLastFrameMeshCoverTileIds();
                meshCoverCount = meshCoverIds.size();
                bool firstMeshTile = true;
                for (const auto& id : meshCoverIds) {
                    ++meshCoverHistogram[id.z];
                    if (firstMeshTile || id.z < meshCoverMinZ) {
                        meshCoverMinZ = id.z;
                    }
                    if (firstMeshTile || id.z > meshCoverMaxZ) {
                        meshCoverMaxZ = id.z;
                    }
                    firstMeshTile = false;
                }
            }
            os << "],\"meshCover\":[";
            {
                bool firstMeshEntry = true;
                for (const auto& [z, count] : meshCoverHistogram) {
                    if (!firstMeshEntry) {
                        os << ",";
                    }
                    firstMeshEntry = false;
                    os << "{\"z\":" << z << ",\"count\":" << count << "}";
                }
            }
            os << "],\"meshCoverCount\":" << meshCoverCount
               << ",\"meshCoverMinZ\":" << static_cast<int>(meshCoverMinZ)
               << ",\"meshCoverMaxZ\":" << static_cast<int>(meshCoverMaxZ);
            os << ",\"elevationQueries\":" << DEMElevationProvider::debugDrainElevationQueries();
            os << "}\n";

            const std::string line = os.str();
            std::fwrite(line.data(), 1, line.size(), sink.file);
            std::fflush(sink.file); // the process is killed, not quit; flush every line
            ++elevationTraceFrame;
        }
    }

    observer->onDidFinishRenderingFrame(
        (renderTreeParameters.loaded && !centerElevationSettling) ? RendererObserver::RenderMode::Full
                                                                  : RendererObserver::RenderMode::Partial,
        // Request a follow-up frame if the drape budget deferred any target or the tile-build
        // budget deferred any new tile, so deferred drapes/tiles catch up progressively even
        // after the interaction stops.
        renderTreeParameters.needsRepaint || drapeWorkDeferred || context.newTileBuildWasDeferred() ||
            terrainCoverPending || centerElevationSettling,
        renderTreeParameters.placementChanged,
        context.threadSafeCopyRenderingStats());

    if (!renderTreeParameters.loaded) {
        renderState = RenderState::Partial;
    } else if (renderState != RenderState::Fully) {
        renderState = RenderState::Fully;
        observer->onDidFinishRenderingMap();
    }

    frameCount += 1;
    MLN_END_FRAME();
}

void Renderer::Impl::reduceMemoryUse() {
    assert(gfx::BackendScope::exists());
    // The drape targets are the largest reclaimable GPU allocation (one
    // tile-sized texture per terrain tile); they are rebuilt on the next frame
    texturePool.removeStaleRenderTargets({});
    backend.getContext().reduceMemoryUsage();
}

} // namespace mln
