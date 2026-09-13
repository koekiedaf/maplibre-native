#pragma once

#include <mln/util/chrono.hpp>
#include <mln/map/bound_options.hpp>
#include <mln/map/map_observer.hpp>
#include <mln/map/map_options.hpp>
#include <mln/map/mode.hpp>
#include <mln/util/noncopyable.hpp>
#include <mln/util/size.hpp>
#include <mln/annotation/annotation.hpp>
#include <mln/map/camera.hpp>
#include <mln/util/geometry.hpp>
#include <mln/map/projection_mode.hpp>
#include <mln/storage/resource_options.hpp>
#include <mln/util/client_options.hpp>
#include <mln/util/action_journal_options.hpp>

#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <optional>

namespace mln {

class RendererFrontend;
class TransformState;

namespace style {
class Image;
class Style;
} // namespace style

namespace util {
class ActionJournal;
} // namespace util

class Map : private util::noncopyable {
public:
    explicit Map(RendererFrontend&,
                 MapObserver&,
                 const MapOptions&,
                 const ResourceOptions&,
                 const ClientOptions& = ClientOptions(),
                 const util::ActionJournalOptions& = util::ActionJournalOptions());
    ~Map();

    /// Register a callback that will get called (on the render thread) when all
    /// resources have been loaded and a complete render occurs.
    using StillImageCallback = std::function<void(std::exception_ptr)>;
    void renderStill(StillImageCallback);
    void renderStill(const CameraOptions&, MapDebugOptions, StillImageCallback);

    /// Triggers a repaint.
    void triggerRepaint();

    style::Style& getStyle();
    const style::Style& getStyle() const;

    void setStyle(std::unique_ptr<style::Style>);

    // Transition
    void cancelTransitions();
    void setGestureInProgress(bool);
    bool isGestureInProgress() const;
    bool isRotating() const;
    bool isScaling() const;
    bool isPanning() const;

    // Camera
    CameraOptions getCameraOptions(const std::optional<EdgeInsets>& = std::nullopt) const;
    void jumpTo(const CameraOptions&);
    void easeTo(const CameraOptions&, const AnimationOptions&);
    void flyTo(const CameraOptions&, const AnimationOptions&);
    void moveBy(const ScreenCoordinate&, const AnimationOptions& = {});
    void scaleBy(double scale, const std::optional<ScreenCoordinate>& anchor, const AnimationOptions& animation = {});
    void pitchBy(double pitch, const AnimationOptions& animation = {});
    void rotateBy(const ScreenCoordinate& first, const ScreenCoordinate& second, const AnimationOptions& = {});
    CameraOptions cameraForLatLngBounds(const LatLngBounds&,
                                        const EdgeInsets&,
                                        const std::optional<double>& bearing = std::nullopt,
                                        const std::optional<double>& pitch = std::nullopt) const;
    CameraOptions cameraForLatLngs(const std::vector<LatLng>&,
                                   const EdgeInsets&,
                                   const std::optional<double>& bearing = std::nullopt,
                                   const std::optional<double>& pitch = std::nullopt) const;
    CameraOptions cameraForGeometry(const Geometry<double>&,
                                    const EdgeInsets&,
                                    const std::optional<double>& bearing = std::nullopt,
                                    const std::optional<double>& pitch = std::nullopt) const;
    LatLngBounds latLngBoundsForCamera(const CameraOptions&) const;
    LatLngBounds latLngBoundsForCameraUnwrapped(const CameraOptions&) const;

    /// @name Bounds
    /// @{

    void setBounds(const BoundOptions& options);

    /// Returns the current map bound options. All optional fields in BoundOptions are set.
    BoundOptions getBounds() const;

    /// @}

    // Map Options
    void setNorthOrientation(NorthOrientation);
    void setConstrainMode(ConstrainMode);
    void setViewportMode(ViewportMode);
    void setSize(Size);
    void setFrustumOffset(const EdgeInsets&);
    EdgeInsets getFrustumOffset();
    MapOptions getMapOptions() const;

    // Projection Mode
    void setProjectionMode(const ProjectionMode&);
    ProjectionMode getProjectionMode() const;

    // Projection
    ScreenCoordinate pixelForLatLng(const LatLng&) const;
    /// Screen position of a point `elevationMeters` above sea level, for markers and probes that
    /// must sit on the draped 3D surface rather than at sea level.
    ScreenCoordinate pixelForLatLng(const LatLng&, double elevationMeters) const;
    LatLng latLngForPixel(const ScreenCoordinate&) const;
    /// Geographic position where the pixel's view ray crosses the plane `elevationMeters` above
    /// sea level. Same units as pixelForLatLng above; the two are inverses on that plane.
    LatLng latLngForPixel(const ScreenCoordinate&, double elevationMeters) const;
    std::vector<ScreenCoordinate> pixelsForLatLngs(const std::vector<LatLng>&) const;
    std::vector<LatLng> latLngsForPixels(const std::vector<ScreenCoordinate>&) const;

    // Transform
    TransformState getTransfromState() const;

    // Annotations
    void addAnnotationImage(std::unique_ptr<style::Image>);
    void removeAnnotationImage(const std::string&);
    double getTopOffsetPixelsForAnnotationImage(const std::string&);

    AnnotationID addAnnotation(const Annotation&);
    void updateAnnotation(AnnotationID, const Annotation&);
    void removeAnnotation(AnnotationID);

    // Tile prefetching
    //
    /// When loading a map, if `PrefetchZoomDelta` is set to any number greater
    /// than 0, the map will first request a tile for `zoom - delta` in a
    /// attempt to display a full map at lower resolution as quick as possible.
    /// It will get clamped at the tile source minimum zoom. The default `delta`
    /// is 4.
    void setPrefetchZoomDelta(uint8_t delta);
    uint8_t getPrefetchZoomDelta() const;

    // Debug
    void setDebug(MapDebugOptions);
    MapDebugOptions getDebug() const;

    bool isRenderingStatsViewEnabled() const;
    void enableRenderingStatsView(bool value);

    bool isFullyLoaded() const;
    void dumpDebugLogs() const;

    /// FreeCameraOptions provides more direct access to the underlying camera
    /// entity. For backwards compatibility the state set using this API must be
    /// representable with `CameraOptions` as well. Parameters are clamped to a
    /// valid range or discarded as invalid if the conversion to the pitch and
    /// bearing presentation is ambiguous. For example orientation can be
    /// invalid if it leads to the camera being upside down or the quaternion
    /// has zero length.
    void setFreeCameraOptions(const FreeCameraOptions& camera);
    FreeCameraOptions getFreeCameraOptions() const;

    // Tile LOD controls
    //
    /// The number of map tile requests can be reduced by using a lower level
    /// of details (Lower zoom level) away from the camera.
    /// This can improve performance, particularly when the camera pitch is high.
    /// The LOD calculation uses a heuristic based on the distance to the camera
    /// view point. The heuristic behavior is controlled with 3 parameters:
    /// - `TileLodMinRadius` is a radius around the view point in unit of tiles
    /// in which the fine grained zoom level tiles are always used
    /// - `TileLodScale` is a scale factor for the distance to the camera view
    /// point. A value larger than 1 increases the distance to the camera view
    /// point in which case the LOD is reduced
    /// - `TileLodPitchThreshold` is the pitch angle in radians above which LOD
    /// calculation is performed.
    /// LOD calculation is always performed if `TileLodPitchThreshold` is zero.
    /// LOD calculation is never performed if `TileLodPitchThreshold` is pi.
    /// - `TileLodZoomShift` shifts the the Zoom level used for LOD calculation
    /// A value of zero (default) does not change the Zoom level
    /// A positive value increases the Zoom level and a negative value decreases
    /// the Zoom level
    /// A negative values typically improves performance but reduces quality.
    /// For instance, a value of -1 reduces the zoom level by 1 and this
    /// reduces the number of tiles by a factor of 4 for the same camera view.
    /// - `TileLodMode` selects the tile cover algorithm. The possible algorithms
    /// are "default" and "distance". When "distance" is selected, the tile LOD is
    /// based on the distance from camera to tile. This algorithm causes tiles
    /// closer to the camera to be rendered at higher LOD, in contrast with the
    /// default algorithm, which renders the highest LOD at the center of the
    /// screen. The distance based algorithm observes `TileLodScale` and
    /// `TileLodPitchThreshold` and ignores `TileLodMinRadius` and
    /// `TileLodZoomShift`.
    /// When "adaptive" is selected, the zoom is chosen per tile from the field of
    /// view and a tile-count budget - maplibre-gl-js's tile zoom function - at every
    /// pitch rather than past a threshold. It observes none of the four settings
    /// above.
    void setTileLodMinRadius(double radius);
    double getTileLodMinRadius() const;
    void setTileLodScale(double scale);
    double getTileLodScale() const;
    void setTileLodPitchThreshold(double threshold);
    double getTileLodPitchThreshold() const;
    void setTileLodZoomShift(double shift);
    double getTileLodZoomShift() const;
    void setTileLodMode(TileLodMode mode);
    TileLodMode getTileLodMode() const;

    /// Selects the 3D-terrain progressive-loading budget (see TerrainLoadMode). Default
    /// is Quality (no budget). Balanced/Performance spread new-tile builds and drape
    /// re-renders across frames for smoother interaction on weaker GPUs, at the cost of a
    /// brief progressive fill-in. No effect when terrain is not enabled.
    void setTerrainLoadMode(TerrainLoadMode mode);
    TerrainLoadMode getTerrainLoadMode() const;

    /// Selects whether terrain tiles are skirted (see TerrainSkirtLength). Default is Auto.
    /// None suits a map drawn over a transparent background, where the skirts would show as
    /// vertical artifacts. Changing it rebuilds the terrain mesh and every tile drawable.
    /// No effect when terrain is not enabled.
    void setTerrainSkirtLength(TerrainSkirtLength length);
    TerrainSkirtLength getTerrainSkirtLength() const;

    /// Whether the map centre rides the terrain surface rather than sea level. With it on
    /// (the default, as in maplibre-gl-js) the centre altitude follows the rendered terrain
    /// height under the centre, so pitching over tall ground keeps the camera above it
    /// instead of sinking in. Inert when terrain is not enabled.
    void setCenterClampedToGround(bool clamped);
    bool getCenterClampedToGround() const;

    /// Extra clearance, in metres, the camera holds above the terrain under it once
    /// task 2.0b's clamp is active (`TransformState::constrainCameraAboveTerrain`). Default 0.0
    /// until the gesture bench has measured a value. Inert when there is no render terrain.
    void setTerrainCameraMarginMeters(double margin);
    double getTerrainCameraMarginMeters() const;

    /// Read-only measurement of the current camera, not a request: the render thread's most
    /// recent camera-ground RISE (task 2.0b) - the terrain height under the camera's own ground
    /// point minus the terrain height under the map centre, both sampled in the same frame.
    /// `std::nullopt` when there is no render terrain. Exists so `constrainCameraAboveTerrain`'s
    /// clamp can be measured from a test harness instead of guessed.
    std::optional<double> getTerrainCameraGroundRiseMeters() const;

    /// Band-aid audit item 6 (docs/plans/2026-09-11-band-aids.md): a reading of the render
    /// side, not a request. `Renderer::Impl::render` holds four bounded counters that keep a
    /// frame `RenderMode::Partial` while terrain is still converging under the map centre or
    /// the camera; once a bound is exhausted the frame is reported fully rendered even though
    /// the condition it was waiting on is still true, with nothing otherwise distinguishing
    /// that from a genuine settle. `std::nullopt` when the most recently rendered frame
    /// needed no bound to give up (the common, healthy case). Otherwise, a comma-joined list
    /// of the counter member name(s) declared in `renderer_impl.hpp` that gave up keeping
    /// this frame back, in their declaration order.
    std::optional<std::string> getSettleBoundGivenUp() const;

    /// Read-only measurement of the current camera, not a request: the left-hand side of the
    /// clamp's inequality, `cos(pitch) * cameraToCenterDistance * metresPerPixel(lat, zoom)` -
    /// how many metres of altitude the camera currently holds above the centre plane. Exists so
    /// the clamp's margin can be chosen by measurement.
    double getTerrainCameraAltitudeAboveCentreMeters() const;

    /// Read-only measurement of the current camera, not a request: `TransformState::getCenterAltitude()`,
    /// the altitude the ground-plane pin has set the map centre to. Exists so the clamp's margin
    /// can be chosen by measurement.
    double getTerrainCentreAltitudeMeters() const;

    /// Debug: when enabled, RenderTerrain logs the camera eye's clearance over the terrain
    /// ("ABOVE-GROUND ...") each frame it is near/below the surface. Off by default; the
    /// per-frame elevation sampling is skipped entirely when off, so it has no cost otherwise.
    void setDebugAboveGroundLog(bool enabled);
    bool getDebugAboveGroundLog() const;

    ClientOptions getClientOptions() const;

    const std::unique_ptr<util::ActionJournal>& getActionJournal();

    /// Task "make it measurable": a per-frame timing sample in milliseconds, pushed by the
    /// platform layer. `recordFrameCPUMs` is the wall time `RendererFrontend::render()` itself
    /// took to prepare a frame (build the command buffer), timed synchronously around that call
    /// - see `MLNMapView.renderSync` on iOS. `recordFrameGPUMs` is a Metal command buffer's own
    /// `GPUEndTime - GPUStartTime`, delivered asynchronously by a completion handler once the
    /// GPU has actually finished the frame - see `MLNMapViewMetalRenderableResource::swap()`.
    /// These are two different clocks measuring two different things on two different threads;
    /// neither implies the other.
    void recordFrameCPUMs(double milliseconds);
    void recordFrameGPUMs(double milliseconds);

    /// Discards every sample recorded so far in every timing recorder (CPU, GPU and the six
    /// per-section recorders below), so a caller (bench.py's sustained-motion mode) can start a
    /// clean window right before driving a gesture and read back a distribution that describes
    /// only that interval.
    void resetFrameTiming();

    struct FrameTimingStats {
        std::size_t count = 0;
        double medianMs = 0.0;
        double p95Ms = 0.0;
        double meanMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
    };
    struct FrameTimingReport {
        FrameTimingStats cpu;
        FrameTimingStats gpu;

        // Task "break the frame down by section": where a frame's CPU time (the `cpu` stats
        // above) actually goes, sourced from the same per-frame gfx::RenderingStats the render
        // side already produces (see rendering_stats.hpp) - not a second, independently-timed
        // mechanism. These six do not have to sum exactly to `cpu`: they cover the phases that
        // could plausibly scale with camera tilt (the brief's own list), not literally every
        // instruction the frame executes (matrix math, GC/allocation, the parts of
        // RendererFrontend::render() outside RenderOrchestrator::createRenderTree and
        // Renderer::Impl::render's own named blocks).
        FrameTimingStats tileCover;    ///< computing the tile cover (per-source, incl. util::tileCover)
        FrameTimingStats terrainMesh;  ///< building/updating the terrain mesh (mesh cover + RenderTerrain::update)
        FrameTimingStats drapeTargets; ///< preparing each drape target and rendering to it
        FrameTimingStats layerPrepare; ///< per-layer per-tile preparation (RenderLayer::prepare)
        FrameTimingStats upload;       ///< uploads to the GPU (both UploadPass blocks)
        FrameTimingStats placement;    ///< symbol placement and collision (Placement::placeLayers)
    };
    /// Read-only measurement, not a request: the current rolling-window distribution of every
    /// timing recorder above, CPU/GPU and the six-section breakdown alike. `count` on each says
    /// exactly how many samples the percentiles were computed from, so a number can never again
    /// be quoted from two or three frames without that being visible right beside it.
    FrameTimingReport getFrameTimingReport() const;

protected:
    class Impl;
    const std::unique_ptr<Impl> impl;

    // For testing only.
    Map(std::unique_ptr<Impl>, const util::ActionJournalOptions& = {});
};

} // namespace mln
