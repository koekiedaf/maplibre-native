#pragma once

#include <mln/annotation/annotation_manager.hpp>
#include <mln/map/map.hpp>
#include <mln/map/map_observer.hpp>
#include <mln/map/map_options.hpp>
#include <mln/map/mode.hpp>
#include <mln/map/transform.hpp>
#include <mln/renderer/renderer_frontend.hpp>
#include <mln/renderer/renderer_observer.hpp>
#include <mln/style/observer.hpp>
#include <mln/style/source.hpp>
#include <mln/style/style.hpp>
#include <mln/util/size.hpp>
#include <mln/tile/tile_operation.hpp>
#include <mln/util/frame_timing.hpp>

#include <atomic>

#include <numbers>

namespace mln {

class FileSource;
class ResourceTransform;

namespace gfx {
class ShaderRegistry;
} // namespace gfx

namespace util {
class ActionJournal;
} // namespace util

struct StillImageRequest {
    StillImageRequest(Map::StillImageCallback&& callback_)
        : callback(std::move(callback_)) {}

    Map::StillImageCallback callback;
};

class Map::Impl final : public TransformObserver, public style::Observer, public RendererObserver {
public:
    Impl(RendererFrontend&, MapObserver&, std::shared_ptr<FileSource>, const MapOptions&);
    ~Impl() final;

    // TransformObserver
    void onCameraWillChange(MapObserver::CameraChangeMode) final;
    void onCameraIsChanging() final;
    void onCameraDidChange(MapObserver::CameraChangeMode) final;

    // StyleObserver
    void onSourceChanged(style::Source&) final;
    void onUpdate() final;
    void onStyleLoading() final;
    void onStyleLoaded() final;
    void onStyleError(std::exception_ptr) final;
    void onSpriteLoaded(const std::optional<style::Sprite>&) final;
    void onSpriteError(const std::optional<style::Sprite>&, std::exception_ptr) final;
    void onSpriteRequested(const std::optional<style::Sprite>&) final;

    // RendererObserver
    void onInvalidate() final;
    void onResourceError(std::exception_ptr) final;
    void onWillStartRenderingFrame() final;
    void onDidFinishRenderingFrame(RenderMode, bool, bool, const gfx::RenderingStats&) final;
    void onWillStartRenderingMap() final;
    void onDidFinishRenderingMap() final;
    void onTerrainCenterElevationChanged(double elevationMeters) final;
    void onTerrainForwardRequirementChanged(std::optional<double> requirementMsl) final;
    void onTerrainCenterRayHitChanged(std::optional<RendererObserver::CenterRayHit> hit) final;
    void onTerrainCenterRayClearanceChanged(std::optional<double> metres) final;
    void onTerrainCenterRayMaxPitchChanged(std::optional<double> radians) final;
    void onTerrainMeshTileCountChanged(std::size_t count) final;
    std::size_t terrainMeshTileCount = 0;
    void onTerrainDrapeTargetCountChanged(std::size_t count, std::size_t colorBytes) final;
    std::size_t terrainDrapeTargetCount = 0;
    std::size_t terrainDrapeTextureBytes = 0;
    void onTerrainCameraGroundRiseChanged(std::optional<double> riseMeters) final;
    void onSettleBoundGivenUp(const std::optional<std::string>& boundNames) final;
    void onStyleImageMissing(const std::string&, const std::function<void()>&) final;
    void onRemoveUnusedStyleImages(const std::vector<std::string>&) final;
    void onRegisterShaders(gfx::ShaderRegistry&) final;

    void onPreCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) final;
    void onPostCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) final;
    void onShaderCompileFailed(shaders::BuiltIn, gfx::Backend::Type, const std::string&) final;
    void onGlyphsLoaded(const FontStack&, const GlyphRange&) final;
    void onGlyphsError(const FontStack&, const GlyphRange&, std::exception_ptr) final;
    void onGlyphsRequested(const FontStack&, const GlyphRange&) final;
    void onTileAction(TileOperation op, const OverscaledTileID&, const std::string&) final;
    void onRenderError(std::exception_ptr) final;
    void onSymbolError(const std::string&) final;

    // Map
    void jumpTo(const CameraOptions&);

    bool isRenderingStatsViewEnabled() const;
    void enableRenderingStatsView(bool value);

    MapObserver& observer;
    RendererFrontend& rendererFrontend;
    std::unique_ptr<util::ActionJournal> actionJournal;

    Transform transform;

    const MapMode mode;
    const float pixelRatio;
    const bool crossSourceCollisions;
    const bool fastPFOREnabled;

    MapDebugOptions debugOptions{MapDebugOptions::NoDebug};
    std::unique_ptr<gfx::RenderingStatsView> renderingStatsView;

    std::shared_ptr<FileSource> fileSource;

    std::unique_ptr<style::Style> style;
    AnnotationManager annotationManager;

    bool cameraMutated = false;

    uint8_t prefetchZoomDelta = util::DEFAULT_PREFETCH_ZOOM_DELTA;

    bool loading = false;
    bool rendererFullyLoaded;
    std::unique_ptr<StillImageRequest> stillImageRequest;

    double tileLodMinRadius = 3;
    double drapeDistanceCurve = 0.5;
    std::uint64_t drapeDialEpoch = 0;
    double drapeFarSizeFactor = 0.25;
    double drapeTexelsPerPixel = 2.0;
    std::size_t terrainMeshTileBudget = 0;
    std::size_t terrainFarMeshGrid = 128;
    std::size_t drapeRerenderBudget = 0;
    double tileLodScale = 1;
    double tileLodPitchThreshold = (60.0 / 180.0) * std::numbers::pi;
    double tileLodZoomShift = 0;
    TileLodMode tileLodMode = TileLodMode::Default;
    TerrainLoadMode terrainLoadMode = TerrainLoadMode::Quality;
    TerrainSkirtLength terrainSkirtLength = TerrainSkirtLength::Auto;
    bool centerClampedToGround = true;
    bool debugAboveGroundLog = false;
    /// Band-aid audit item 6: the render side's last report of which settle bound(s), if
    /// any, gave up rather than genuinely resolved on the most recently rendered frame - see
    /// `onSettleBoundGivenUp`'s own comment and `Map::getSettleBoundGivenUp`. nullopt is the
    /// common, healthy case.
    std::optional<std::string> lastSettleBoundGivenUp;

    /// Task "make it measurable": real per-frame CPU/GPU timing, fed by the platform layer
    /// (`Map::recordFrameCPUMs`/`recordFrameGPUMs`) and read back by
    /// `Map::getFrameTimingReport`. See `mln::util::FrameTimingRecorder`'s own comment.
    util::FrameTimingRecorder cpuFrameTiming;
    util::FrameTimingRecorder gpuFrameTiming;
    /// Performance round, Phase 0: off unless the owner's panel is open (Map::setFrameTimingEnabled).
    std::atomic<bool> frameTimingEnabled{false};
    /// Copies of the last frame's cumulative counters (Map::getDrapeRenderCount and friends).
    std::atomic<std::uint64_t> drapeRenderCount{0};
    std::atomic<std::uint64_t> terrainMeshBuildCount{0};
    std::atomic<std::uint64_t> textureMemoryBytes{0};

    /// Task "break the frame down by section": six more windows of the same recorder, one per
    /// named section, fed from `Map::Impl::onDidFinishRenderingFrame` - unlike the CPU/GPU pair
    /// above these are never called into from platform code; the per-frame `gfx::RenderingStats`
    /// this method already receives every frame carries the section times straight from the
    /// renderer, so this class just records them. Same units (record() takes milliseconds; the
    /// stats fields are seconds, converted on the way in), same reset story
    /// (`Map::resetFrameTiming`), same read API (`Map::getFrameTimingReport`, which grew six more
    /// named members rather than a second call).
    util::FrameTimingRecorder tileCoverTiming;
    util::FrameTimingRecorder terrainMeshTiming;
    util::FrameTimingRecorder drapeTargetsTiming;
    util::FrameTimingRecorder layerPrepareTiming;
    util::FrameTimingRecorder uploadTiming;
    util::FrameTimingRecorder placementTiming;
};

// Forward declaration of this method is required for the MapProjection class
CameraOptions cameraForLatLngs(const std::vector<LatLng>& latLngs,
                               const Transform& transform,
                               const EdgeInsets& padding);

} // namespace mln
