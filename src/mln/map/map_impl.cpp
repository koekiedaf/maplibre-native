#include <mln/layermanager/layer_manager.hpp>
#include <algorithm>
#include <mln/map/map_impl.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/storage/file_source.hpp>
#include <mln/style/style_impl.hpp>
#include <mln/util/exception.hpp>
#include <mln/util/logging.hpp>
#include <mln/util/traits.hpp>
#include <mln/util/action_journal.hpp>
#include <mln/util/action_journal_impl.hpp>
#include <mln/gfx/rendering_stats.hpp>

namespace mln {

#if !defined(NDEBUG)
namespace {
void logStyleDependencies(EventSeverity severity, Event event, const style::Style& style) {
    using Dependency = style::expression::Dependency;
    constexpr auto maskCount = underlying_type(Dependency::MaskCount);
    std::array<std::size_t, maskCount + 1> counts = {0};
    const auto layers = style.getLayers();
    for (const auto& layer : layers) {
        const auto deps = layer->getDependencies();
        if (deps == Dependency::None) {
            counts[0]++;
        } else {
            for (size_t i = 0; i < maskCount; ++i) {
                if (deps & Dependency{1u << i}) {
                    counts[i + 1]++;
                }
            }
        }
    }
    std::ostringstream ss;
    ss << "Style '" << style.getName() << "' has " << layers.size() << " layers:\n";
    ss << "  " << Dependency::None << ": " << counts[0] << "\n";
    for (size_t i = 0; i < maskCount; ++i) {
        if (counts[i + 1]) {
            ss << "  " << Dependency{1u << i} << ": " << counts[i + 1] << "\n";
        }
    }
    Log::Record(severity, event, ss.str());
}
} // namespace
#endif

Map::Impl::Impl(RendererFrontend& frontend_,
                MapObserver& observer_,
                std::shared_ptr<FileSource> fileSource_,
                const MapOptions& mapOptions)
    : observer(observer_),
      rendererFrontend(frontend_),
      transform(*this, mapOptions.constrainMode(), mapOptions.viewportMode()),
      mode(mapOptions.mapMode()),
      pixelRatio(mapOptions.pixelRatio()),
      crossSourceCollisions(mapOptions.crossSourceCollisions()),
      fastPFOREnabled(mapOptions.fastPFOREnabled()),
      fileSource(std::move(fileSource_)),
      style(std::make_unique<style::Style>(fileSource, pixelRatio, frontend_.getThreadPool())),
      annotationManager(*style) {
    transform.setNorthOrientation(mapOptions.northOrientation());
    style->impl->setObserver(this);
    rendererFrontend.setObserver(*this);
    transform.resize(mapOptions.size());
}

Map::Impl::~Impl() {
    // Explicitly reset the RendererFrontend first to ensure it releases
    // All shared resources (AnnotationManager)
    rendererFrontend.reset();
};

void Map::Impl::onCameraWillChange(MapObserver::CameraChangeMode cameraMode) {
    observer.onCameraWillChange(cameraMode);

    if (actionJournal) {
        actionJournal->impl->onCameraWillChange(cameraMode);
    }
}

void Map::Impl::onCameraIsChanging() {
    observer.onCameraIsChanging();

    if (actionJournal) {
        actionJournal->impl->onCameraIsChanging();
    }
}

void Map::Impl::onCameraDidChange(MapObserver::CameraChangeMode cameraMode) {
    observer.onCameraDidChange(cameraMode);

    if (actionJournal) {
        actionJournal->impl->onCameraDidChange(cameraMode);
    }
}

// MARK: - Map::Impl StyleObserver

void Map::Impl::onSourceChanged(style::Source& source) {
    observer.onSourceChanged(source);

    if (actionJournal) {
        actionJournal->impl->onSourceChanged(source);
    }
}

void Map::Impl::onUpdate() {
    // Don't load/render anything in still mode until explicitly requested.
    if (mode != MapMode::Continuous && !stillImageRequest) {
        return;
    }

    TimePoint timePoint = mode == MapMode::Continuous ? Clock::now() : Clock::time_point::max();

    transform.updateTransitions(timePoint);

    std::optional<Immutable<style::Terrain::Impl>> terrainImpl;
    if (auto* terrain = style->impl->getTerrain()) {
        terrainImpl = terrain->impl;
    }

    // DuckMaps fork only, task T3: the style spec's `sky` root property.
    std::optional<Immutable<style::Sky::Impl>> skyImpl;
    if (auto* sky = style->impl->getSky()) {
        skyImpl = sky->impl;
    }

    UpdateParameters params = {.styleLoaded = style->impl->isLoaded(),
                               .mode = mode,
                               .pixelRatio = pixelRatio,
                               .debugOptions = debugOptions,
                               .timePoint = timePoint,
                               .transformState = transform.getState(),
                               .glyphURL = style->impl->getGlyphURL(),
                               .fontFaces = style->impl->getFontFaces(),
                               .spriteLoaded = style->impl->areSpritesLoaded(),
                               .transitionOptions = style->impl->getTransitionOptions(),
                               .light = style->impl->getLight()->impl,
                               .terrain = terrainImpl,
                               .sky = skyImpl,
                               .images = style->impl->getImageImpls(),
                               .sources = style->impl->getSourceImpls(),
                               .layers = style->impl->getLayerImpls(),
                               .annotationManager = annotationManager.makeWeakPtr(),
                               .fileSource = fileSource,
                               .prefetchZoomDelta = prefetchZoomDelta,
                               .stillImageRequest = bool(stillImageRequest),
                               .crossSourceCollisions = crossSourceCollisions,
                               .fastPFOREnabled = fastPFOREnabled,
                               .tileLodMinRadius = tileLodMinRadius,
                               .tileLodScale = tileLodScale,
                               .tileLodPitchThreshold = tileLodPitchThreshold,
                               .tileLodZoomShift = tileLodZoomShift,
                               .tileLodMode = tileLodMode,
                               .terrainLoadMode = terrainLoadMode,
                               .terrainSkirtLength = terrainSkirtLength,
                               .drapeDistanceCurve = drapeDistanceCurve,
                               .drapeFarSizeFactor = drapeFarSizeFactor,
                               .drapeDialEpoch = drapeDialEpoch,
                               .debugAboveGroundLog = debugAboveGroundLog};

    rendererFrontend.update(std::make_shared<UpdateParameters>(std::move(params)));
}

void Map::Impl::onStyleLoading() {
    loading = true;
    rendererFullyLoaded = false;
    observer.onWillStartLoadingMap();

    if (actionJournal) {
        actionJournal->impl->onWillStartLoadingMap();
    }
}

void Map::Impl::onStyleLoaded() {
    if (!cameraMutated) {
        jumpTo(style->getDefaultCamera());
    }
    if (LayerManager::annotationsEnabled) {
        annotationManager.onStyleLoaded();
    }

    observer.onDidFinishLoadingStyle();

    if (actionJournal) {
        actionJournal->impl->onDidFinishLoadingStyle();
    }

#if !defined(NDEBUG)
    logStyleDependencies(EventSeverity::Info, Event::Style, *style);
#endif
}

void Map::Impl::onStyleError(std::exception_ptr error) {
    MapLoadError type;
    std::string description;

    try {
        std::rethrow_exception(error);
    } catch (const mln::util::StyleParseException& e) {
        type = MapLoadError::StyleParseError;
        description = e.what();
    } catch (const mln::util::StyleLoadException& e) {
        type = MapLoadError::StyleLoadError;
        description = e.what();
    } catch (const mln::util::NotFoundException& e) {
        type = MapLoadError::NotFoundError;
        description = e.what();
    } catch (const std::exception& e) {
        type = MapLoadError::UnknownError;
        description = e.what();
    }

    observer.onDidFailLoadingMap(type, description);

    if (actionJournal) {
        actionJournal->impl->onDidFailLoadingMap(type, description);
    }
}

void Map::Impl::onSpriteLoaded(const std::optional<style::Sprite>& sprite) {
    observer.onSpriteLoaded(sprite);

    if (actionJournal) {
        actionJournal->impl->onSpriteLoaded(sprite);
    }
}

void Map::Impl::onSpriteError(const std::optional<style::Sprite>& sprite, std::exception_ptr ex) {
    observer.onSpriteError(sprite, ex);

    if (actionJournal) {
        actionJournal->impl->onSpriteError(sprite, ex);
    }
}

void Map::Impl::onSpriteRequested(const std::optional<style::Sprite>& sprite) {
    observer.onSpriteRequested(sprite);

    if (actionJournal) {
        actionJournal->impl->onSpriteRequested(sprite);
    }
}

// MARK: - Map::Impl RendererObserver

void Map::Impl::onInvalidate() {
    onUpdate();
}

void Map::Impl::onResourceError(std::exception_ptr error) {
    if (mode != MapMode::Continuous && stillImageRequest) {
        auto request = std::move(stillImageRequest);
        request->callback(error);
    }
}

void Map::Impl::onWillStartRenderingFrame() {
    if (mode == MapMode::Continuous) {
        observer.onWillStartRenderingFrame();

        if (actionJournal) {
            actionJournal->impl->onWillStartRenderingFrame();
        }
    }
}

void Map::Impl::onDidFinishRenderingFrame(RenderMode renderMode,
                                          bool needsRepaint,
                                          bool placemenChanged,
                                          const gfx::RenderingStats& stats) {
    rendererFullyLoaded = renderMode == RenderMode::Full;

    if (renderingStatsView && style) {
        renderingStatsView->update(*style, stats);
    }

    // Task "break the frame down by section": record this frame's six section times,
    // unconditionally like the CPU/GPU recorders (not gated on MapMode::Continuous below),
    // since a still-image render's frame breakdown is just as real as a continuous one's.
    // gfx::RenderingStats reports seconds; FrameTimingRecorder::record wants milliseconds,
    // matching recordFrameCPUMs/recordFrameGPUMs's own convention.
    // Performance round, Phase 0: diagnostic-only, gated on the owner's panel.
    if (frameTimingEnabled.load(std::memory_order_relaxed)) {
        tileCoverTiming.record(stats.tileCoverTime * 1000.0);
        terrainMeshTiming.record(stats.terrainUpdateTime * 1000.0);
        drapeTargetsTiming.record(stats.drapeTargetsTime * 1000.0);
        layerPrepareTiming.record(stats.layerPrepareTime * 1000.0);
        uploadTiming.record(stats.uploadTime * 1000.0);
        placementTiming.record(stats.placementTime * 1000.0);
    }
    drapeRenderCount.store(static_cast<std::uint64_t>(std::max(0, stats.numDrapeTargetsRendered)),
                           std::memory_order_relaxed);
    terrainMeshBuildCount.store(static_cast<std::uint64_t>(std::max(0, stats.numTerrainMeshBuilds)),
                                std::memory_order_relaxed);
    textureMemoryBytes.store(static_cast<std::uint64_t>(std::max<int64_t>(0, stats.memTextures)),
                             std::memory_order_relaxed);

    if (mode == MapMode::Continuous) {
        const MapObserver::RenderFrameStatus frameStatus{.mode = static_cast<MapObserver::RenderMode>(renderMode),
                                                         .needsRepaint = needsRepaint,
                                                         .placementChanged = placemenChanged,
                                                         .renderingStats = stats};
        observer.onDidFinishRenderingFrame(frameStatus);

        if (actionJournal) {
            actionJournal->impl->onDidFinishRenderingFrame(frameStatus);
        }

        if (needsRepaint || transform.inTransition()) {
            onUpdate();
        } else if (rendererFullyLoaded) {
            observer.onDidBecomeIdle();

            if (actionJournal) {
                actionJournal->impl->onDidBecomeIdle();
            }
        }
    } else if (stillImageRequest && rendererFullyLoaded) {
        const auto request = std::move(stillImageRequest);
        request->callback(nullptr);
    }
}

void Map::Impl::onWillStartRenderingMap() {
    if (mode == MapMode::Continuous) {
        observer.onWillStartRenderingMap();

        if (actionJournal) {
            actionJournal->impl->onWillStartRenderingMap();
        }
    }
}

void Map::Impl::onDidFinishRenderingMap() {
    if (mode == MapMode::Continuous && loading) {
        observer.onDidFinishRenderingMap(MapObserver::RenderMode::Full);

        if (actionJournal) {
            actionJournal->impl->onDidFinishRenderingMap(MapObserver::RenderMode::Full);
        }

        if (loading) {
            loading = false;
            observer.onDidFinishLoadingMap();

            if (actionJournal) {
                actionJournal->impl->onDidFinishLoadingMap();
            }
        }
    }
};

void Map::Impl::onTerrainCenterElevationChanged(double elevationMeters) {
    if (!centerClampedToGround) {
        return;
    }
    // Task C2: this used to drop the message whenever a finger was down, and to drop it again
    // below a 0.5 m deadband. Both filters had their memory on the wrong side of the boundary.
    // Renderer::Impl::render advances its own `lastReportedCenterElevation` when it SENDS, so
    // every value discarded here was one the render side already believed delivered, and it sent
    // nothing further until the terrain differed from that stale reported value by more than its
    // own 0.25 m gate. Measured (task C1, development/app-bench/traces/c1-marbore.jsonl and
    // c1-wall.jsonl): a scripted pinch and pan at Marbore left the camera orbiting a plane
    // 292.49 m below the ground under the centre, on every frame to the end of the trace, and at
    // the Gavarnie wall 126.57 m. The eventual correction, whenever some later frame happened to
    // cross the gate, paid the whole accumulated debt in one step. That is David's altitude flip
    // and his click on pinch release, and it was a lost message rather than a DEM query.
    //
    // So there is now exactly one filter, the render side's 0.25 m gate, and its memory is the
    // value the camera actually holds, because everything it sends is applied here. The gesture
    // is still protected, but by the right number: the plane a gesture is solved on is frozen at
    // the grabbed altitude for the gesture's length (TransformState::setGestureInProgress), so
    // the camera's orbit altitude is free to follow the ground continuously without moving the
    // ground under the fingers. MapLibre GL JS separates the same two numbers, and its own
    // changelog records the same symptom as a bug it fixed by sampling the rendered surface
    // rather than by freezing harder ("Fix the camera jumping at the end of a pan or zoom gesture
    // on terrain", #7989, #3982; "gestures are now solved against the elevation of the terrain
    // under the gesture instead of the frozen center elevation", #8067).
    // Task C7, 16 September 2026. This used to be `jumpTo(withCenterAltitude(elevationMeters))`,
    // which made the camera's own altitude a readout of the DEM under the map centre. David flew
    // it and reported the consequence exactly: "the camera goes down and up, very jittery, when
    // moving forward and backward ... even when the camera is above flat ground with no relief
    // under it". Measured before this change (development/app-bench/traces/diag-utrecht-flat.jsonl):
    // on dead-flat Utrecht a single drag sawtoothed the camera over 5 m, because the probe
    // quantises to about a metre and flips between an exact z14 tile and a z13 ancestor while the
    // centre moves. At the Gavarnie wall (diag-gavarnie-wall.jsonl) the same mechanism swung the
    // camera 997 m up and then 386 m back down inside one drag.
    //
    // The sample is still needed, and still sampled: the gesture solve plane is right to sit on
    // the ground under the centre, and the terrain clamp reasons about it too. It is simply
    // stored now instead of being written into the camera. The camera's altitude is held, and is
    // moved only by the anticipatory climb below and by a pinch.
    //
    // The one exception is the very first sample. Before terrain has been reported at all the
    // camera is placed relative to sea level, so at Gavarnie it would sit 1850 m underground.
    // Establishing the altitude once, when there is nothing held yet, is placement rather than
    // following, and the trace shows it as the single 2003 -> 3854 m step when terrain loads.
    const bool establishing = !transform.getGroundUnderCentre().has_value();
    transform.setGroundUnderCentre(elevationMeters);
    if (establishing) {
        transform.jumpTo(CameraOptions().withCenterAltitude(elevationMeters));
    }
    onUpdate();
}

void Map::Impl::onTerrainCenterRayHitChanged(std::optional<RendererObserver::CenterRayHit> hit) {
    // Task C9: stored only. It is read on the first frame of a rotate or tilt gesture
    // (TransformState::setGestureInProgress) and held for the gesture's length, so the render
    // thread's per-frame re-aiming of the laser never moves a pivot that fingers are holding.
    if (hit) {
        transform.setCenterRayHit(TransformState::CenterRayHit{
            LatLng{hit->latitude, hit->longitude}, hit->altitudeMeters, hit->distanceMeters});
    } else {
        transform.setCenterRayHit(std::nullopt);
    }
}

void Map::Impl::onTerrainCenterRayClearanceChanged(std::optional<double> metres) {
    transform.setCenterRayClearance(metres);
}

void Map::Impl::onTerrainDrapeTargetCountChanged(std::size_t count, std::size_t colorBytes) {
    terrainDrapeTargetCount = count;
    terrainDrapeTextureBytes = colorBytes;
}

void Map::Impl::onTerrainMeshTileCountChanged(std::size_t count) {
    terrainMeshTileCount = count;
}

void Map::Impl::onTerrainCenterRayMaxPitchChanged(std::optional<double> radians) {
    transform.setCenterRayMaxPitch(radians);
}

void Map::Impl::onTerrainForwardRequirementChanged(std::optional<double> requirementMsl) {
    transform.setForwardRequirement(requirementMsl);
    if (!requirementMsl) {
        return;
    }
    // Task C7, the anticipatory climb, now expressed as altitude rather than as a zoom clamp.
    //
    // The requirement arriving here is already the highest ground along the flight line ahead,
    // discounted by task C6's climb gradient, so it grows continuously as a wall is approached
    // instead of stepping when the wall crosses a window edge. The camera must clear it by the
    // same stand-off the clamp uses, measured rather than chosen: 60 m is the largest height the
    // DEM's 30 m posts can hide between them.
    //
    // Raise only, and rate limited. The discount already makes the target smooth, but the target
    // can still jump when a better DEM level arrives under the line, and a jump in the target
    // must not become a jump in the camera. The limit is a ceiling on how much altitude one
    // frame may add, so a genuinely urgent climb is still answered promptly while a resampling
    // artefact is spread over several frames and is usually overtaken by the next sample.
    // Nothing here can ever lower the camera: once gained, the altitude is kept, which is the
    // property David asked for in "once clear it holds the NEW MSL altitude, never sinks back".
    constexpr double kMaxClimbPerFrameMeters = 12.0;
    const double target = *requirementMsl + transform.getTerrainCameraMarginMeters();
    const double current = transform.getCameraAltitudeMeters();
    if (!(target > current)) {
        return;
    }
    const double step = std::min(target - current, kMaxClimbPerFrameMeters);
    if (transform.raiseCameraAltitudeTo(current + step)) {
        onUpdate();
    }
}

void Map::Impl::onTerrainCameraGroundRiseChanged(std::optional<double> riseMeters) {
    if (transform.getTerrainCameraGroundRise() == riseMeters) {
        return;
    }
    transform.setTerrainCameraGroundRise(riseMeters);
    // Unlike the centre elevation, this must not be skipped while a gesture is in progress: the
    // whole point of this channel is to stop a pinch or tilt under the user's fingers, which is
    // exactly when the camera is closing on the terrain fastest. jumpTo(CameraOptions()) is used
    // rather than a direct state write, so the correction runs through the same
    // startTransition/constrainCameraAboveTerrain path (and the same observer notifications) as
    // every other camera change, instead of a second, untested way of moving the camera.
    transform.jumpTo(CameraOptions());
    onUpdate();
}

void Map::Impl::onSettleBoundGivenUp(const std::optional<std::string>& boundNames) {
    // Purely a stored reading for `Map::getSettleBoundGivenUp` (band-aid audit item 6) -
    // unlike the camera-ground rise above, nothing on the map thread acts on this, so there
    // is no jumpTo/onUpdate here, only the store.
    lastSettleBoundGivenUp = boundNames;
}

void Map::Impl::jumpTo(const CameraOptions& camera) {
    cameraMutated = true;
    transform.jumpTo(camera);
    onUpdate();
}

bool Map::Impl::isRenderingStatsViewEnabled() const {
    return !!renderingStatsView;
}

void Map::Impl::enableRenderingStatsView(bool value) {
    if (value) {
        if (!renderingStatsView) {
            renderingStatsView = std::make_unique<gfx::RenderingStatsView>();
            if (style) {
                renderingStatsView->create(*style);
            }
        }
    } else {
        if (renderingStatsView) {
            if (style) {
                renderingStatsView->destroy(*style);
            }
            renderingStatsView = nullptr;
        }
    }
}

void Map::Impl::onStyleImageMissing(const std::string& id, const std::function<void()>& done) {
    if (!style->getImage(id)) {
        observer.onStyleImageMissing(id);

        if (actionJournal) {
            actionJournal->impl->onStyleImageMissing(id);
        }
    }

    done();
    onUpdate();
}

void Map::Impl::onRemoveUnusedStyleImages(const std::vector<std::string>& unusedImageIDs) {
    for (const auto& unusedImageID : unusedImageIDs) {
        if (observer.onCanRemoveUnusedStyleImage(unusedImageID)) {
            style->removeImage(unusedImageID);
        }
    }
}

void Map::Impl::onRegisterShaders(gfx::ShaderRegistry& registry) {
    observer.onRegisterShaders(registry);

    if (actionJournal) {
        actionJournal->impl->onRegisterShaders(registry);
    }
}

void Map::Impl::onPreCompileShader(shaders::BuiltIn shaderID,
                                   gfx::Backend::Type type,
                                   const std::string& additionalDefines) {
    observer.onPreCompileShader(shaderID, type, additionalDefines);

    if (actionJournal) {
        actionJournal->impl->onPreCompileShader(shaderID, type, additionalDefines);
    }
}

void Map::Impl::onPostCompileShader(shaders::BuiltIn shaderID,
                                    gfx::Backend::Type type,
                                    const std::string& additionalDefines) {
    observer.onPostCompileShader(shaderID, type, additionalDefines);

    if (actionJournal) {
        actionJournal->impl->onPostCompileShader(shaderID, type, additionalDefines);
    }
}

void Map::Impl::onShaderCompileFailed(shaders::BuiltIn shaderID,
                                      gfx::Backend::Type type,
                                      const std::string& additionalDefines) {
    observer.onShaderCompileFailed(shaderID, type, additionalDefines);

    if (actionJournal) {
        actionJournal->impl->onShaderCompileFailed(shaderID, type, additionalDefines);
    }
}

void Map::Impl::onGlyphsLoaded(const FontStack& fontStack, const GlyphRange& ranges) {
    observer.onGlyphsLoaded(fontStack, ranges);

    if (actionJournal) {
        actionJournal->impl->onGlyphsLoaded(fontStack, ranges);
    }
}

void Map::Impl::onGlyphsError(const FontStack& fontStack, const GlyphRange& ranges, std::exception_ptr ex) {
    observer.onGlyphsError(fontStack, ranges, ex);

    if (actionJournal) {
        actionJournal->impl->onGlyphsError(fontStack, ranges, ex);
    }
}

void Map::Impl::onGlyphsRequested(const FontStack& fontStack, const GlyphRange& ranges) {
    observer.onGlyphsRequested(fontStack, ranges);

    if (actionJournal) {
        actionJournal->impl->onGlyphsRequested(fontStack, ranges);
    }
}

void Map::Impl::onTileAction(TileOperation op, const OverscaledTileID& id, const std::string& sourceID) {
    observer.onTileAction(op, id, sourceID);

    if (actionJournal) {
        actionJournal->impl->onTileAction(op, id, sourceID);
    }
}

void Map::Impl::onRenderError(std::exception_ptr error) {
    observer.onRenderError(error);

    if (actionJournal) {
        actionJournal->impl->onRenderError(error);
    }
}

void Map::Impl::onSymbolError(const std::string& message) {
    observer.onSymbolError(message);

    if (actionJournal) {
        actionJournal->impl->onSymbolError(message);
    }
}

} // namespace mln
