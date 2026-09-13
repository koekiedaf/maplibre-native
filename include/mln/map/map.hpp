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
#include <array>
#include <cstddef>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace mln {

/// A single Foundation gesture sample. UIKit only submits an intent; the map
/// waits for the renderer's decoded terrain envelope before it changes camera
/// position or altitude. This keeps gesture input and terrain avoidance out of
/// competing UI-side jumpTo paths.
struct FoundationFlightIntent {
    LatLng target;
    double requestedDistanceMeters = 0.0;
    double speedMetersPerSecond = 0.0;
    bool pinch = false;
    /// Map pitch measured from top-down. Pinch uses this to project an actual
    /// view ray without changing the camera's pitch or zoom.
    double rayPitchDegrees = 90.0;
    /// Signed change in eye height requested by a pinch travelling along the
    /// same 3D ray as `target`. Positive is up; positive forward ray travel
    /// at a top-down pitch is therefore negative (down).
    double verticalEyeMSLDeltaMeters = 0.0;
    /// The contact's eye altitude, so a coalesced callback can replace an
    /// older pending target without losing the distance already travelled by
    /// the fingers.
    std::optional<double> gestureStartEyeMSL;
    /// Resets contact-scoped safety latches. UIKit sets this on the first
    /// translation sample, not on every recognizer callback.
    bool beginsContact = false;
    std::optional<double> bearing;
    std::optional<double> pitch;
    uint64_t sequence = 0;
};

struct FoundationFlightTelemetry {
    double requestedDistanceMeters = 0.0;
    double acceptedDistanceMeters = 0.0;
    double speedMetersPerSecond = 0.0;
    double lookaheadMeters = 0.0;
    double obstructionDistanceMeters = 0.0;
    double safeDistanceMeters = 0.0;
    double ascentMeters = 0.0;
    bool demAvailable = false;
    bool blocked = false;
    bool pinch = false;
    std::string stopReason;
};

enum class FoundationFlightStopReason : uint8_t {
    None,
    UnknownDEM,
    AscentLimited,
    PinchDeceleration,
    PinchStandOff,
};

struct FoundationFlightPolicyResult {
    double acceptedFraction = 0.0;
    double ascentMeters = 0.0;
    double nextVerticalVelocity = 0.0;
    FoundationFlightStopReason reason = FoundationFlightStopReason::None;
};

constexpr double foundationFlightMaximumCommittableMeters = 50.0;
constexpr double foundationFlightMaximumLookaheadMeters = 150.0;
constexpr double foundationFlightMaximumHorizontalSpeedMetersPerSecond =
    foundationFlightMaximumLookaheadMeters / 4.0;

struct FoundationFlightTerrainSample {
    /// Fraction of the current eye-to-target horizontal path. Values above
    /// one are optional lookahead and are never committed by this assessment.
    double pathFraction = 0.0;
    /// Highest decoded DEM elevation across the swept corridor at this
    /// longitudinal station.
    double elevationMeters = 0.0;
};

struct FoundationFlightTrajectoryResult {
    double acceptedFraction = 0.0;
    double targetEyeMSL = 0.0;
    double automaticAscentMeters = 0.0;
    double nextVerticalVelocity = 0.0;
    double obstructionFraction = 1.0;
    FoundationFlightStopReason reason = FoundationFlightStopReason::None;
};

/// Chooses a safe prefix from the actual ordered terrain profile. Climb budget
/// is applied before translation, so every accepted station is checked against
/// the same reachable eye altitude. User ray descent remains signed and is
/// clamped by decoded terrain rather than by the contact's previous altitude.
inline FoundationFlightTrajectoryResult foundationFlightSafeTrajectory(
    bool demAvailable,
    double currentEyeMSL,
    double requestedTargetEyeMSL,
    double clearanceMeters,
    double committableFraction,
    const std::vector<FoundationFlightTerrainSample>& samples,
    double previousVerticalVelocity,
    double elapsedSeconds,
    double pathMeters = 0.0,
    double speedMetersPerSecond = 0.0,
    bool anticipatoryClimb = false) {
    if (!demAvailable || samples.empty()) {
        return {0.0, currentEyeMSL, 0.0, 0.0, 0.0, FoundationFlightStopReason::UnknownDEM};
    }
    const double limit = std::clamp(committableFraction, 0.0, 1.0);
    const double velocity = std::min(30.0, std::max(0.0, previousVerticalVelocity) +
                                             12.0 * std::clamp(elapsedSeconds, 0.0, 0.10));
    const double climbBudget = velocity * std::clamp(elapsedSeconds, 0.0, 0.10);
    double accepted = 0.0;
    double requiredBoost = 0.0;
    double appliedBoost = 0.0;
    double obstruction = 1.0;
    // Begin a bounded climb while an obstacle is still in the decoded
    // lookahead. Speed only affects urgency after it has been constrained to
    // what the 150m horizon can cover in four seconds.
    if (anticipatoryClimb && std::isfinite(speedMetersPerSecond) && pathMeters > 1e-6) {
        const double effectiveSpeed = std::clamp(std::abs(speedMetersPerSecond),
                                                 0.0,
                                                 foundationFlightMaximumHorizontalSpeedMetersPerSecond);
        for (const auto& sample : samples) {
            if (!std::isfinite(sample.pathFraction) || !std::isfinite(sample.elevationMeters)) {
                return {0.0, currentEyeMSL, 0.0, 0.0, 0.0, FoundationFlightStopReason::UnknownDEM};
            }
            if (sample.pathFraction <= limit + 1e-9) continue;
            const double rayEye = currentEyeMSL +
                (requestedTargetEyeMSL - currentEyeMSL) * sample.pathFraction;
            const double needed = sample.elevationMeters + clearanceMeters - rayEye;
            if (needed <= 0.0 || effectiveSpeed <= 1e-6) continue;
            const double obstacleDistance = std::max(0.0, pathMeters * sample.pathFraction);
            const double timeToObstacle = std::max(std::clamp(elapsedSeconds, 0.0, 0.10),
                                                   obstacleDistance / effectiveSpeed);
            appliedBoost = std::max(appliedBoost,
                                    std::min(climbBudget,
                                             needed * std::clamp(elapsedSeconds, 0.0, 0.10) /
                                                 timeToObstacle));
            obstruction = std::min(obstruction, sample.pathFraction);
        }
    }
    for (const auto& sample : samples) {
        if (!std::isfinite(sample.pathFraction) || !std::isfinite(sample.elevationMeters)) {
            return {0.0, currentEyeMSL, 0.0, 0.0, 0.0, FoundationFlightStopReason::UnknownDEM};
        }
        if (sample.pathFraction < -1e-9) continue;
        // A velocity-limited endpoint can fall between the fixed 5m stations.
        // Validate it conservatively with the first decoded station beyond
        // the endpoint, then stop before consuming farther lookahead.
        const bool bracketsLimit = sample.pathFraction > limit + 1e-9;
        if (bracketsLimit && accepted + 1e-9 >= limit) break;
        const double fraction = bracketsLimit ? limit : std::clamp(sample.pathFraction, 0.0, limit);
        const double rayEye = currentEyeMSL + (requestedTargetEyeMSL - currentEyeMSL) * fraction;
        const double candidateBoost = std::max(requiredBoost,
            sample.elevationMeters + clearanceMeters - rayEye);
        if (candidateBoost > climbBudget + 1e-9) {
            obstruction = fraction;
            // Hold position at the last proven station but spend this frame's
            // bounded climb budget toward the obstruction. Without this, a
            // camera starting centimetres below its corridor floor can never
            // recover enough clearance to make later progress.
            appliedBoost = std::max(requiredBoost, climbBudget);
            break;
        }
        requiredBoost = std::max(0.0, candidateBoost);
        appliedBoost = std::max(appliedBoost, requiredBoost);
        accepted = fraction;
        if (bracketsLimit) break;
    }
    // The renderer always includes a station at the commit boundary. If it
    // did not survive the loop, never extrapolate beyond the last proven one.
    const double targetEye = currentEyeMSL + (requestedTargetEyeMSL - currentEyeMSL) * accepted + appliedBoost;
    return {accepted,
            targetEye,
            appliedBoost,
            appliedBoost > 0.0 ? velocity : 0.0,
            obstruction,
            accepted + 1e-9 < limit ? FoundationFlightStopReason::AscentLimited
                                    : FoundationFlightStopReason::None};
}

/// Limits forward pinch travel from the first actual surface intersection on
/// the sampled ray. Reverse travel is always an escape and is never blocked by
/// terrain that lies in front of the camera.
inline FoundationFlightPolicyResult foundationFlightPinchPolicy(bool demAvailable,
                                                                bool pinch,
                                                                double signedRequestedRayMeters,
                                                                double obstacleDistanceMeters) {
    if (!demAvailable) return {0.0, 0.0, 0.0, FoundationFlightStopReason::UnknownDEM};
    if (!pinch || signedRequestedRayMeters <= 0.0 || !std::isfinite(obstacleDistanceMeters)) {
        return {1.0, 0.0, 0.0, FoundationFlightStopReason::None};
    }
    if (obstacleDistanceMeters <= 50.0) {
        return {0.0, 0.0, 0.0, FoundationFlightStopReason::PinchStandOff};
    }
    if (obstacleDistanceMeters < 150.0) {
        const double remaining = obstacleDistanceMeters - 50.0;
        const double stopFraction = remaining / std::max(std::abs(signedRequestedRayMeters), 1e-9);
        const double decelerationFraction = remaining / 100.0;
        return {std::clamp(std::min(stopFraction, decelerationFraction), 0.0, 1.0),
                0.0,
                0.0,
                FoundationFlightStopReason::PinchDeceleration};
    }
    return {1.0, 0.0, 0.0, FoundationFlightStopReason::None};
}

inline bool foundationFlightAssessmentMatches(const std::optional<FoundationFlightIntent>& intent,
                                              uint64_t assessedSequence) {
    // Sequence zero is a settled-camera assessment. It must not be applied to
    // an intent that arrived after the renderer snapshot was taken.
    return assessedSequence == 0 ? !intent : (intent && intent->sequence == assessedSequence);
}

inline double foundationFlightLookahead(double speedMetersPerSecond) {
    // The safety baseline and query-budget ceiling are both 150m. Excess
    // gesture speed is constrained at commit time; it must not outrun this
    // fixed decoded horizon.
    (void)speedMetersPerSecond;
    return foundationFlightMaximumLookaheadMeters;
}

inline double foundationFlightSpeedLimitedFraction(double pathMeters,
                                                   double speedMetersPerSecond,
                                                   double elapsedSeconds) {
    if (!std::isfinite(pathMeters) || !std::isfinite(speedMetersPerSecond)) return 0.0;
    if (pathMeters <= 1e-9) return 1.0;
    if (std::abs(speedMetersPerSecond) <= foundationFlightMaximumHorizontalSpeedMetersPerSecond) return 1.0;
    const double allowed = foundationFlightMaximumHorizontalSpeedMetersPerSecond *
                           std::clamp(elapsedSeconds, 0.0, 0.50);
    return std::clamp(allowed / pathMeters, 0.0, 1.0);
}

inline double foundationFlightRayHorizontalDistance(double rayDistanceMeters, double pitchDegrees) {
    const double pitchRadians = std::clamp(pitchDegrees, 0.0, 90.0) * std::numbers::pi / 180.0;
    return rayDistanceMeters * std::sin(pitchRadians);
}

inline double foundationFlightRayVerticalDisplacement(double rayDistanceMeters, double pitchDegrees) {
    const double pitchRadians = std::clamp(pitchDegrees, 0.0, 90.0) * std::numbers::pi / 180.0;
    return -rayDistanceMeters * std::cos(pitchRadians);
}

inline double foundationFlightCumulativePinchRayDistance(double scale, double startScale) {
    return std::clamp(std::log(std::max(scale, 0.001) / std::max(startScale, 0.001)) * 250.0,
                      -250.0,
                      250.0);
}

constexpr double foundationFlightMaximumLongitudinalSampleSpacingMeters = 5.0;
constexpr double foundationFlightCorridorHalfWidthMeters = 15.0;

/// Number of equal longitudinal intervals required for the bounded per-frame
/// commit prefix plus lookahead. Clamp distance rather than widening the 5m
/// sampling gap, keeping work at no more than 41 stations x three tracks.
inline std::size_t foundationFlightLongitudinalIntervals(double pathMeters, double lookaheadMeters) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(
        (std::clamp(pathMeters, 0.0, foundationFlightMaximumCommittableMeters) +
         std::clamp(lookaheadMeters, 0.0, foundationFlightMaximumLookaheadMeters)) /
        foundationFlightMaximumLongitudinalSampleSpacingMeters)));
}

inline constexpr std::array<double, 3> foundationFlightCorridorCrossTrackOffsets() {
    return {-foundationFlightCorridorHalfWidthMeters, 0.0, foundationFlightCorridorHalfWidthMeters};
}

inline bool foundationFlightSampleRequiresDEM(double distanceMeters, double committableMeters) {
    return distanceMeters <= committableMeters + 1e-9;
}

struct FoundationFlightCrossTrackOffset {
    double northMeters = 0.0;
    double eastMeters = 0.0;
};

inline FoundationFlightCrossTrackOffset foundationFlightCrossTrackOffset(double travelNorthMeters,
                                                                          double travelEastMeters,
                                                                          double crossTrackMeters) {
    const double distance = std::hypot(travelNorthMeters, travelEastMeters);
    if (distance <= 1e-6) return {};
    return {-travelEastMeters * crossTrackMeters / distance,
            travelNorthMeters * crossTrackMeters / distance};
}

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
    LatLng latLngForPixel(const ScreenCoordinate&) const;
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

    /// Opt in to the Foundation eye/MSL controller. It is intentionally
    /// independent of centre clamping: one controller owns correction.
    void setTerrainFlightControllerEnabled(bool enabled);
    bool getTerrainFlightControllerEnabled() const;
    void setTerrainFlightClearanceMeters(double metres);
    double getTerrainFlightClearanceMeters() const;
    double getTerrainFlightEyeAltitudeMSL() const;
    /// Whether the renderer has a decoded DEM sample for the current flight
    /// corridor.  This is deliberately separate from the last trusted floor:
    /// a missing tile must stop Foundation translation rather than invent a
    /// surface at sea level.
    bool getTerrainFlightDEMAvailable() const;
    void submitFoundationFlightIntent(const FoundationFlightIntent&);
    void cancelFoundationFlightIntent();
    FoundationFlightTelemetry getFoundationFlightTelemetry() const;

    /// Debug: when enabled, RenderTerrain logs the camera eye's clearance over the terrain
    /// ("ABOVE-GROUND ...") each frame it is near/below the surface. Off by default; the
    /// per-frame elevation sampling is skipped entirely when off, so it has no cost otherwise.
    void setDebugAboveGroundLog(bool enabled);
    bool getDebugAboveGroundLog() const;

    ClientOptions getClientOptions() const;

    const std::unique_ptr<util::ActionJournal>& getActionJournal();

protected:
    class Impl;
    const std::unique_ptr<Impl> impl;

    // For testing only.
    Map(std::unique_ptr<Impl>, const util::ActionJournalOptions& = {});
};

} // namespace mln
