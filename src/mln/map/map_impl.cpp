#include <mln/layermanager/layer_manager.hpp>
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

#include <algorithm>
#include <cmath>

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
    // Sub-metre differences are the terrain cover shifting under a camera that just moved,
    // not the ground actually changing height; acting on them would chase itself.
    constexpr double minimumChangeMeters = 0.5;
    if (std::abs(transform.getState().getCenterAltitude() - elevationMeters) < minimumChangeMeters) {
        return;
    }
    // Raising the centre onto the terrain moves the orbit plane, not the centre's lng/lat,
    // so this settles rather than feeding back into the next frame's sample.
    transform.jumpTo(CameraOptions().withCenterAltitude(elevationMeters));
    onUpdate();
}

void Map::Impl::onTerrainFlightAssessment(const TerrainFlightAssessment& assessment) {
    const auto current = transform.getState().getFoundationFlightIntent();
    // Render results are asynchronous. Never let an older envelope move or
    // block the camera after a newer recognizer sample has replaced it. This
    // also rejects a settled (sequence zero) result once an intent exists.
    if (!foundationFlightAssessmentMatches(current, assessment.intentSequence)) {
        return;
    }
    if (assessment.intentSequence != 0) {
        foundationFlightTelemetry.lookaheadMeters = assessment.lookaheadMeters;
        foundationFlightPinchObstacleDistance = assessment.pinchObstacleDistanceMeters;
        foundationFlightPathMeters = assessment.pathMeters;
        foundationFlightCommittableFraction = assessment.committableFraction;
        foundationFlightTerrainProfile = assessment.terrainProfile;
        foundationFlightTruncatedByUnknownDEM = assessment.truncatedByUnknownDEM;
        if (!assessment.elevationMeters) {
            foundationFlightUnknownDEMLatched = true;
        }
    }
    onTerrainFlightElevationChanged(assessment.elevationMeters);
}

void Map::Impl::onTerrainFlightElevationChanged(std::optional<double> elevationMeters) {
    terrainFlightDEMAvailable = elevationMeters.has_value();
    const auto intent = transform.getState().getFoundationFlightIntent();
    if (!terrainFlightControllerEnabled || !elevationMeters) {
        // Unknown DEM deliberately leaves the last trusted MSL floor intact.
        if (intent) {
            foundationFlightTelemetry.acceptedDistanceMeters = 0.0;
            foundationFlightTelemetry.demAvailable = false;
            foundationFlightTelemetry.blocked = true;
            foundationFlightTelemetry.stopReason = "unknown_dem";
            transform.getState().setFoundationFlightIntent(std::nullopt);
            foundationFlightQueuedIntent.reset();
        }
        return;
    }
    foundationFlightTelemetry.demAvailable = true;

    if (intent) {
        // The renderer has sampled every point through the submitted travel
        // envelope. This is the sole camera-translation commit: recognizers
        // submit intent, this arbiter accepts a safe fraction and jumpTo
        // applies centre and altitude together without changing orientation.
        const double eyeMSL = transform.getState().getEyeAltitudeMSL();
        const double gestureEyeMSL = intent->gestureStartEyeMSL.value_or(eyeMSL);
        const double heldFloor = foundationFlightHeldEyeMSL.value_or(-std::numeric_limits<double>::infinity());
        double requestedEyeMSL = gestureEyeMSL + intent->verticalEyeMSLDeltaMeters;
        if (!intent->pinch) requestedEyeMSL = std::max(requestedEyeMSL, heldFloor);
        const TimePoint now = Clock::now();
        const double rawElapsed = std::chrono::duration<double>(now - foundationFlightLastCommit).count();
        const double elapsed = std::clamp(
            rawElapsed, 1.0 / 120.0, 0.10);
        const double horizontalElapsed = std::clamp(rawElapsed, 1.0 / 120.0, 0.50);
        foundationFlightLastCommit = now;
        // Stand-off can shorten the prefix, but terrain/climb safety is then
        // evaluated against every station in that actual prefix. It never
        // derives travel from max-elevation climb ratios.
        const auto pinchPolicy = foundationFlightPinchPolicy(true,
                                                             intent->pinch,
                                                             intent->requestedDistanceMeters,
                                                             foundationFlightPinchObstacleDistance);
        const double speedLimitedPathMeters = intent->pinch
            ? std::abs(intent->requestedDistanceMeters)
            : foundationFlightPathMeters;
        const double speedLimit = foundationFlightSpeedLimitedFraction(speedLimitedPathMeters,
                                                                       intent->speedMetersPerSecond,
                                                                       horizontalElapsed);
        const double requestedLimit = std::min({foundationFlightCommittableFraction,
                                                pinchPolicy.acceptedFraction,
                                                speedLimit});
        const auto trajectory = foundationFlightSafeTrajectory(
            true,
            eyeMSL,
            requestedEyeMSL,
            transform.getState().getTerrainFlightClearanceMeters(),
            requestedLimit,
            foundationFlightTerrainProfile,
            foundationFlightVerticalVelocity,
            elapsed,
            foundationFlightPathMeters,
            intent->speedMetersPerSecond,
            !intent->pinch);
        foundationFlightVerticalVelocity = trajectory.nextVerticalVelocity;
        const double fraction = trajectory.acceptedFraction;
        const double targetEyeMSL = trajectory.targetEyeMSL;
        const double altitudeDelta = targetEyeMSL - eyeMSL;
        const double ascent = std::max(0.0, altitudeDelta);
        const double safeDistance = std::abs(intent->requestedDistanceMeters) * fraction;

        const auto current = transform.getState().getLatLng();
        double lonDelta = intent->target.longitude() - current.longitude();
        if (lonDelta > 180.0) lonDelta -= 360.0;
        if (lonDelta < -180.0) lonDelta += 360.0;
        const LatLng acceptedTarget{current.latitude() + (intent->target.latitude() - current.latitude()) * fraction,
                                    current.longitude() + lonDelta * fraction};
        cameraMutated = true;
        transform.jumpToFoundationFlightTarget(acceptedTarget, targetEyeMSL);
        onUpdate();
        if (!intent->pinch && ascent > 0.0) {
            foundationFlightHeldEyeMSL = foundationFlightHeldEyeMSL
                ? std::max(*foundationFlightHeldEyeMSL, targetEyeMSL)
                : targetEyeMSL;
        }
        transform.getState().setFoundationFlightIntent(std::nullopt);
        foundationFlightTelemetry.acceptedDistanceMeters =
            std::copysign(safeDistance, intent->requestedDistanceMeters);
        foundationFlightTelemetry.lookaheadMeters = foundationFlightLookahead(intent->speedMetersPerSecond);
        foundationFlightTelemetry.obstructionDistanceMeters =
            foundationFlightPathMeters * trajectory.obstructionFraction;
        foundationFlightTelemetry.safeDistanceMeters = safeDistance;
        foundationFlightTelemetry.ascentMeters = ascent;
        foundationFlightTelemetry.blocked = fraction <= 0.0;
        const auto reason = pinchPolicy.reason != FoundationFlightStopReason::None
            ? pinchPolicy.reason
            : trajectory.reason;
        switch (reason) {
            case FoundationFlightStopReason::None: foundationFlightTelemetry.stopReason.clear(); break;
            case FoundationFlightStopReason::UnknownDEM: foundationFlightTelemetry.stopReason = "unknown_dem"; break;
            case FoundationFlightStopReason::AscentLimited: foundationFlightTelemetry.stopReason = "ascent_limited"; break;
            case FoundationFlightStopReason::PinchDeceleration: foundationFlightTelemetry.stopReason = "pinch_deceleration_150m"; break;
            case FoundationFlightStopReason::PinchStandOff: foundationFlightTelemetry.stopReason = "pinch_standoff_50m"; break;
        }
        if (foundationFlightTruncatedByUnknownDEM) {
            foundationFlightUnknownDEMLatched = true;
            foundationFlightQueuedIntent.reset();
            foundationFlightTelemetry.stopReason = "unknown_dem";
            foundationFlightTelemetry.blocked = fraction <= 0.0;
        }
        if (foundationFlightQueuedIntent && !foundationFlightUnknownDEMLatched) {
            transform.getState().setFoundationFlightIntent(std::move(foundationFlightQueuedIntent));
            foundationFlightQueuedIntent.reset();
            onUpdate();
        }
        return;
    }
    // Keep the last decoded local terrain floor for the next contact, but do
    // not snap the camera here. All flight altitude changes pass through the
    // bounded arbiter above.
    transform.getState().setTerrainFlightMinimumEyeMSL(elevationMeters);
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
