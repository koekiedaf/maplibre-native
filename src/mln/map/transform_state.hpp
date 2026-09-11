#pragma once

#include <mln/map/camera.hpp>
#include <mln/map/mode.hpp>
#include <mln/util/camera.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/geometry.hpp>
#include <mln/util/mat4.hpp>
#include <mln/util/projection.hpp>
#include <mln/util/size.hpp>

#include <cstdint>
#include <array>
#include <limits>
#include <optional>

namespace mln {

class UnwrappedTileID;
class TileCoordinate;

struct TransformStateProperties {
    TransformStateProperties& withX(const std::optional<double>& val) {
        x = val;
        return *this;
    }
    TransformStateProperties& withY(const std::optional<double>& val) {
        y = val;
        return *this;
    }
    TransformStateProperties& withZ(const std::optional<double>& val) {
        z = val;
        return *this;
    }
    TransformStateProperties& withScale(const std::optional<double>& val) {
        scale = val;
        return *this;
    }
    TransformStateProperties& withFov(const std::optional<double>& val) {
        fov = val;
        return *this;
    }
    TransformStateProperties& withBearing(const std::optional<double>& val) {
        bearing = val;
        return *this;
    }
    TransformStateProperties& withPitch(const std::optional<double>& val) {
        pitch = val;
        return *this;
    }
    TransformStateProperties& withRoll(const std::optional<double>& val) {
        roll = val;
        return *this;
    }
    TransformStateProperties& withXSkew(const std::optional<double>& val) {
        xSkew = val;
        return *this;
    }
    TransformStateProperties& withYSkew(const std::optional<double>& val) {
        ySkew = val;
        return *this;
    }
    TransformStateProperties& withAxonometric(const std::optional<bool>& val) {
        axonometric = val;
        return *this;
    }
    TransformStateProperties& withPanningInProgress(const std::optional<bool>& val) {
        panning = val;
        return *this;
    }
    TransformStateProperties& withScalingInProgress(const std::optional<bool>& val) {
        scaling = val;
        return *this;
    }
    TransformStateProperties& withRotatingInProgress(const std::optional<bool>& val) {
        rotating = val;
        return *this;
    }
    TransformStateProperties& withEdgeInsets(const std::optional<EdgeInsets>& val) {
        edgeInsets = val;
        return *this;
    }
    TransformStateProperties& withSize(const std::optional<Size>& val) {
        size = val;
        return *this;
    }
    TransformStateProperties& withConstrainMode(const std::optional<ConstrainMode>& val) {
        constrain = val;
        return *this;
    }
    TransformStateProperties& withNorthOrientation(const std::optional<NorthOrientation>& val) {
        northOrientation = val;
        return *this;
    }
    TransformStateProperties& withViewportMode(const std::optional<ViewportMode>& val) {
        viewPortMode = val;
        return *this;
    }
    TransformStateProperties withFrustumOffset(const std::optional<EdgeInsets>& val) {
        frustumOffset = val;
        return *this;
    }

    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    std::optional<double> fov;
    std::optional<double> scale;
    std::optional<double> bearing;
    std::optional<double> pitch;
    std::optional<double> roll;
    std::optional<double> xSkew;
    std::optional<double> ySkew;
    std::optional<bool> axonometric;
    std::optional<bool> panning;
    std::optional<bool> scaling;
    std::optional<bool> rotating;
    std::optional<EdgeInsets> edgeInsets;
    std::optional<Size> size;
    std::optional<ConstrainMode> constrain;
    std::optional<NorthOrientation> northOrientation;
    std::optional<ViewportMode> viewPortMode;
    std::optional<EdgeInsets> frustumOffset;
};

class TransformState {
public:
    TransformState(ConstrainMode = ConstrainMode::HeightOnly, ViewportMode = ViewportMode::Default);

    void setProperties(const TransformStateProperties& properties);

    // Matrix
    void matrixFor(mat4&, const UnwrappedTileID&) const;
    void getProjMatrix(mat4& matrix, uint16_t nearZ = 1, bool aligned = false) const;

    // Dimensions
    Size getSize() const;
    void setSize(const Size& size_);

    EdgeInsets getFrustumOffset() const;
    void setFrustumOffset(const EdgeInsets& frustumOffset_);

    // North Orientation
    NorthOrientation getNorthOrientation() const;
    double getNorthOrientationAngle() const;
    void setNorthOrientation(NorthOrientation);

    // Constrain mode
    ConstrainMode getConstrainMode() const;
    void setConstrainMode(ConstrainMode);

    // Viewport mode
    ViewportMode getViewportMode() const;
    void setViewportMode(ViewportMode val);

    CameraOptions getCameraOptions(const std::optional<EdgeInsets>&) const;

    // EdgeInsects
    EdgeInsets getEdgeInsets() const { return edgeInsets; }
    void setEdgeInsets(const EdgeInsets&);

    // Position
    LatLng getLatLng(LatLng::WrapMode = LatLng::Unwrapped) const;
    double getCenterAltitude() const;
    double pixel_x() const;
    double pixel_y() const;

    // Zoom
    double getZoom() const;
    uint8_t getIntegerZoom() const;
    double getZoomFraction() const;

    // Scale
    double getScale() const;
    void setScale(double);

    // Positions
    double getX() const;
    void setX(double);
    double getY() const;
    void setY(double);
    double getZ() const;
    void setZ(double);

    // Bounds
    void setLatLngBounds(LatLngBounds);
    LatLngBounds getLatLngBounds() const;
    void setMinZoom(double);
    double getMinZoom() const;
    void setMaxZoom(double);
    double getMaxZoom() const;
    void setMinPitch(double);
    double getMinPitch() const;
    void setMaxPitch(double);
    double getMaxPitch() const;
    double getMinFieldOfView() const;
    double getMaxFieldOfView() const;

    // Rotation
    double getBearing() const;
    void setBearing(double);
    float getFieldOfView() const;
    void setFieldOfView(double);
    float getCameraToCenterDistance() const;
    double getPitch() const;
    void setPitch(double);
    double getRoll() const;
    void setRoll(double);

    double getXSkew() const;
    void setXSkew(double);
    double getYSkew() const;
    void setYSkew(double);
    bool getAxonometric() const;
    void setAxonometric(bool);

    // State
    bool isChanging() const;
    bool isRotating() const;
    void setRotatingInProgress(bool val) { rotating = val; }
    bool isScaling() const;
    void setScalingInProgress(bool val) { scaling = val; }
    bool isPanning() const;
    void setPanningInProgress(bool val) { panning = val; }
    bool isGestureInProgress() const;
    /// An iOS pinch is not one transition: `MLNMapView`'s `handlePinchGesture` sends a fresh
    /// `jumpTo(CameraOptions().withZoom(...).withAnchor(...))` for every touch-move, so
    /// `Transform::startTransition` captures a new `previousZoom`/`previousPitch` on every one of
    /// them - a per-transition floor is not a floor at all, since the previous frame's clamp has
    /// already lowered the value the next frame floors against, and the ratchet from before the
    /// floor existed walks the zoom down exactly as it did without it. This records the zoom and
    /// pitch on the false-to-true edge as the floors for the gesture that is about to begin. They
    /// are NOT cleared on the true-to-false edge: they survive the gesture, because the render
    /// thread's own terrain-rise correction (the bare `CameraOptions()` `constrainCameraAboveTerrain`
    /// also floors, below) keeps firing after the gesture has ended and must not be free to pull
    /// the camera further out than the gesture that provoked it started from. They are refreshed
    /// to the camera's current zoom and pitch by the next transition that requests a CENTRE
    /// change while no gesture is in progress (see `constrainCameraAboveTerrain`) - a programmatic
    /// move, which means the ground under the camera has changed by something other than a
    /// gesture and the old floor is meaningless. The same hook and reasoning as the centre-altitude freeze
    /// (Map::Impl::onTerrainCenterElevationChanged skips while a gesture is in progress).
    void setGestureInProgress(bool val);

    // Conversion
    ScreenCoordinate latLngToScreenCoordinate(const LatLng&) const;
    ScreenCoordinate latLngToScreenCoordinate(const LatLng&, vec4&) const;
    /// Projects a point sitting `elevationMeters` above sea level (3D terrain, exaggeration
    /// already applied). World z is metres here, not pixels: Camera::getWorldToCamera scales the
    /// z column by pixelsPerMeter, so metres is what the matrix expects.
    ScreenCoordinate latLngToScreenCoordinate(const LatLng&, double elevationMeters, vec4&) const;
    LatLng screenCoordinateToLatLng(const ScreenCoordinate&, LatLng::WrapMode = LatLng::Unwrapped) const;
    /// Unprojects onto the horizontal plane `elevationMeters` above sea level instead of onto sea
    /// level. Same units as the projection above.
    LatLng screenCoordinateToLatLng(const ScreenCoordinate&,
                                    double elevationMeters,
                                    LatLng::WrapMode = LatLng::Unwrapped) const;
    /// The plane every camera gesture is solved on: the terrain height under the map centre,
    /// which `centerClampedToGround` keeps the camera's own orbit plane at. Sea level is the
    /// wrong plane once terrain is on - the view ray crosses the real surface long before z=0,
    /// so a sea-level solve grabs a point centerAltitude*tan(pitch) or more beyond the ground
    /// under the finger and amplifies every pan, pinch and tilt by that much.
    ///
    /// While a gesture is running it is the altitude the gesture GRABBED, captured once on the
    /// gesture's first frame (`setGestureInProgress`) and held until the fingers lift; otherwise
    /// it is the centre's own altitude. Task C2 separated the two, and the separation is the
    /// whole point: the camera's orbit altitude has to keep following the ground under the centre
    /// every frame (see `Map::Impl::onTerrainCenterElevationChanged`), while the plane a gesture
    /// is solved on must not move under the fingers that grabbed it. MapLibre GL JS holds the
    /// same two numbers apart, as `HandlerManager._terrainGestureElevation` against the
    /// transform's own elevation.
    ///
    /// Task 2.0 made these one number and recorded why: an earlier attempt at a separate frozen
    /// plane turned a 120 point drag into 19 m instead of 480. That attempt failed for a reason
    /// that is now fixed rather than avoided - `Transform::moveBy` re-cast the CENTRE as a ray on
    /// the frozen plane, and a centre ray solved on any plane but the centre's own lands
    /// (plane - centerAltitude) * tan(pitch) away, which the moving centre altitude then fought
    /// every frame. `moveBy` now takes the difference of two rays on the SAME plane, so the
    /// plane's own offset cancels exactly and a frozen plane cannot drift the centre. That is the
    /// rule `moveLatLng` below already followed and the one GL JS's `setLocationAtPoint` states.
    double getGroundPlaneAltitude() const { return gesturePlaneAltitude.value_or(getCenterAltitude()); }
    // Implements mapbox-gl-js pointCoordinate() : MercatorCoordinate.
    // `targetZ` is the world z of the plane to intersect, in metres above sea level.
    TileCoordinate screenCoordinateToTileCoordinate(const ScreenCoordinate&,
                                                    uint8_t atZoom,
                                                    double targetZ = 0.0) const;

    /// The camera's own altitude in metres, twin of MapLibre GL JS's
    /// `MercatorTransform.getCameraAltitude` (mercator_transform.ts:830). `getCameraToCenterDistance`
    /// is in pixels and does not vary with zoom; multiplying it by the metres-per-pixel at the
    /// current zoom converts the pitch-projected camera offset above the centre into metres, which
    /// is then added to the centre's own altitude.
    double getCameraAltitudeMeters() const;

    /// The ground point directly under the camera, twin of the web's `getCameraLngLat`
    /// (mercator_transform.ts:834). Derived from the camera position the engine already computes
    /// (`updateCameraState`) rather than re-deriving the pitch/bearing trigonometry.
    LatLng getCameraLatLng() const;

    /// The renderer's most recent RISE: the DEM height under the camera's own ground point minus
    /// the DEM height under the map centre, both sampled in the same render frame, exaggeration
    /// already applied to both. `std::nullopt` means there is no render terrain this frame, so
    /// the clamp in `constrainCameraAboveTerrain` is off entirely rather than clamped to sea
    /// level. A rise, not an absolute altitude, is what crosses this boundary on purpose: the
    /// map thread's own centre altitude (`getCenterAltitude`) can still be one frame behind the
    /// terrain at the moment the clamp runs, and comparing that stale number against a fresh
    /// absolute ground height poisons the result. The rise carries no dependency on either
    /// thread's notion of centre altitude, so it cannot be poisoned by it.
    void setTerrainCameraGroundRise(std::optional<double> metres) { terrainCameraGroundRise = metres; }
    std::optional<double> getTerrainCameraGroundRise() const { return terrainCameraGroundRise; }

    /// Extra clearance, in metres, `constrainCameraAboveTerrain` holds the camera above the
    /// sampled ground. Default 60, chosen by measurement rather than taste: the DEM this clamp
    /// samples carries roughly 30 m posts, and along 8 bearings x 3 km at Cirque de Gavarnie and
    /// Lauterbrunnen the greatest height a sampler at that spacing misses BETWEEN posts was 55.1 m
    /// (p99 24 m, median 1.6 m). 60 m covers that worst case, so the camera is not put on top of a
    /// ridge the sampler could not see. It also closes the near-field cover gap at the clamped
    /// camera, which sitting exactly on the sampled surface does not.
    void setTerrainCameraMarginMeters(double metres) { terrainCameraMarginMeters = metres; }
    double getTerrainCameraMarginMeters() const { return terrainCameraMarginMeters; }

    /// Keeps the camera's own altitude above the terrain under it. Copies the test in MapLibre GL
    /// JS's `Camera._elevateCameraIfInsideTerrain` (src/ui/camera.ts:891-905): camera altitude
    /// versus the DEM height at the camera's own ground point. The response differs from the web on
    /// purpose. The web raises the camera vertically at a fixed ground position, which changes
    /// pitch and zoom together, so a pinch would tilt the map flat under the user's fingers
    /// mid-gesture; this clamps only the axis the caller is actually changing.
    ///
    /// `pitchRequested && !zoomRequested` is the two-finger tilt gesture (`MLNMapView`'s
    /// `handleTwoFingerDragGesture` sends `jumpTo(CameraOptions().withPitch(...).withAnchor(...))`):
    /// clamp PITCH only, leaving the zoom exactly where it is, so a tilt is answered as a tilt
    /// rather than an unrelated zoom-out. Every other case - a zoom change (pinch), a centre
    /// change (pan), a bare `CameraOptions()` (the terrain-elevation-changed correction), or both
    /// at once - clamps ZOOM first, then PITCH as a backstop for when the zoom clamp ran into
    /// `minZoom` and the camera is still under the terrain.
    ///
    /// `previousZoom`/`previousPitch` are the state's own zoom and pitch from before this
    /// transition's change was applied - a floor, not a target. A pinch that asks to zoom in can
    /// walk the camera's ground point back onto higher terrain as zoom rises, which raises the
    /// rise, which lowers the clamp, which walks the camera back further: measured live at
    /// Cirque de Gavarnie, a two-finger spread-apart (a zoom-in gesture) starting at zoom 15.000
    /// (rise 673.4 m, camera altitude 1150.8 m) settled at zoom 14.874 (rise 665.8 m, altitude
    /// 1256.2 m) - the gesture asked to zoom in and the map zoomed out instead. A clamp may stop
    /// a change, never reverse it: if the caller requested a zoom change, a pitch change, or
    /// both, NEITHER the zoom nor the pitch may end up below where it was before this transition
    /// began, whichever axis actually ends up doing the clamping. A first cut floored only the
    /// axis the caller had directly requested, which left the pitch backstop free to fire on a
    /// zoom-only gesture and tilt the map flatter under a pinch that asked to zoom in - exactly
    /// the MapLibre GL JS response section 3b of the collision design doc rejects, reintroduced
    /// by a narrower door. Both floors now stand together whenever either axis was requested, so
    /// a zoom-only gesture cannot lose ground on pitch and a pitch-only gesture cannot lose ground
    /// on zoom. The floors apply to the bare `CameraOptions()` the terrain-rise channel sends too
    /// (see below): that correction may stop a camera going under the terrain, but it may not
    /// pull the map further out than the gesture that provoked it started from. Only a transition
    /// that requested a CENTRE change is inert to the floors and may reduce either axis freely,
    /// because panning onto higher ground genuinely does have to lift the camera. The deliberate
    /// consequence: while a floor holds, the camera can sit closer to the terrain than the margin
    /// asks, because it was already there and the alternative is moving a camera the user did not
    /// ask to move; the next pan, unfloored, is what restores the clearance. MapLibre GL JS does
    /// not need this floor because `Camera.applyUpdatedTransform` (src/ui/camera.ts:915-944)
    /// clamps a clone of `_requestedCameraState` (camera.ts:873-878) every frame and never writes
    /// the clamp back into the state driving the next frame, so nothing accumulates; this engine
    /// clamps the state itself, so the floor has to do that job instead.
    ///
    /// `previousZoom`/`previousPitch` only floor a single transition, and an iOS pinch is not one
    /// transition: `MLNMapView`'s `handlePinchGesture` sends a separate
    /// `jumpTo(CameraOptions().withZoom(...).withAnchor(...))` for every touch-move, so this floor
    /// reset on every one of them and the ratchet walked the zoom down exactly as before the floor
    /// existed - measured live, a spread-apart pinch (zoom in) at zoom 15.000 still settled at
    /// 14.873. `previousZoom`/`previousPitch` are used only as the fallback for a transition run
    /// before any gesture has ever recorded a floor. Once `setGestureInProgress` has recorded one,
    /// it is used instead, and it is NOT reset when the gesture ends: it survives until the next
    /// transition that requests a CENTRE change while no gesture is in progress refreshes it to
    /// the camera's current zoom and pitch (`centerRequested` below). That is what lets the
    /// render thread's own unfloored-during-the-old-design terrain-rise correction keep firing
    /// after a pinch has ended without ratcheting the camera past where the pinch began, and it is
    /// also why the pinch's own anchor-drift `moveBy` - a centre change that runs WHILE the
    /// gesture is in progress - does not refresh the floor: `centerRequested` is true for it, but
    /// `isGestureInProgress()` is true too, so the refresh condition (centre requested AND no
    /// gesture in progress) does not fire.
    void constrainCameraAboveTerrain(bool zoomRequested, bool pitchRequested, bool centerRequested,
                                     double previousZoom, double previousPitch);

    double zoomScale(double zoom) const;
    double scaleZoom(double scale) const;

    bool valid() const { return !size.isEmpty() && (scale >= min_scale && scale <= max_scale); }

    float getCameraToTileDistance(const UnwrappedTileID&) const;
    float maxPitchScaleFactor() const;

    /** Recenter the map so that the given coordinate is located at the given
        point on screen. */
    void moveLatLng(const LatLng&, const ScreenCoordinate&);
    void setLatLngZoom(const LatLng& latLng, double zoom);
    void setCenterAltitude(double alt_m);

    void constrain(double& scale, double& x, double& y) const;
    bool constrainScreen(double& scale_, double& x_, double& y_) const;
    void constrainCameraAndZoomToBounds(CameraOptions& camera, double& zoom) const;

    const mat4& getProjectionMatrix() const;
    const mat4& getInvProjectionMatrix() const;

    FreeCameraOptions getFreeCameraOptions() const;
    void setFreeCameraOptions(const FreeCameraOptions& options);

private:
    bool rotatedNorth() const;

    // Viewport center offset, from [size.width / 2, size.height / 2], defined
    // by |edgeInsets| in screen coordinates, with top left origin.
    ScreenCoordinate getCenterOffset() const;

    LatLngBounds bounds;

    // Limit the amount of zooming possible on the map.
    double min_scale = std::pow(2, 0);
    double max_scale = std::pow(2, util::DEFAULT_MAX_ZOOM);

    // Limit the amount of pitch
    double minPitch = util::PITCH_MIN;
    double maxPitch = util::DEFAULT_PITCH_MAX;
    double minFov = util::deg2rad(0.1);
    double maxFov = util::deg2rad(150.0);

    NorthOrientation orientation = NorthOrientation::Upwards;

    // logical dimensions
    Size size;
    EdgeInsets frustumOffset;

    mat4 coordinatePointMatrix(const mat4& projMatrix) const;
    mat4 getPixelMatrix() const;

    void setScalePoint(double scale, const ScreenCoordinate& point);

    void updateMatricesIfNeeded() const;
    bool needsMatricesUpdate() const { return requestMatricesUpdate; }

    bool setCameraPosition(const vec3& position);
    bool setCameraOrientation(const Quaternion& orientation);
    void updateCameraState() const;
    void updateStateFromCamera();

    const mat4& getCoordMatrix() const;
    const mat4& getInvertedMatrix() const;

private:
    ConstrainMode constrainMode;
    ViewportMode viewportMode;

    // animation state
    bool rotating = false;
    bool scaling = false;
    bool panning = false;
    bool gestureInProgress = false;

    /// The ground plane the running gesture was solved on, in metres above sea level, captured on
    /// the gesture's false-to-true edge and cleared when it ends. Empty means no gesture, and
    /// `getGroundPlaneAltitude` then reads the centre's own altitude. See that accessor for why
    /// this is not the same number as the camera's orbit altitude.
    std::optional<double> gesturePlaneAltitude;
    // The zoom/pitch floor: set the moment a gesture begins (the false-to-true edge of
    // setGestureInProgress) and NOT cleared when the gesture ends - it persists so the terrain-
    // rise correction cannot ratchet the camera past where the gesture that provoked it started.
    // Refreshed to the camera's current zoom/pitch by a transition that requests a CENTRE change
    // while no gesture is in progress (a programmatic move, not a gesture). See
    // constrainCameraAboveTerrain.
    std::optional<double> terrainCameraFloorZoom;
    std::optional<double> terrainCameraFloorPitch;

    // map position
    double x = 0, y = 0, z = 0;
    double bearing = 0;
    double scale = 1;
    double fov = util::DEFAULT_FOV;
    double pitch = 0.0;
    double roll = 0.0;
    double xSkew = 0.0;
    double ySkew = 1.0;
    bool axonometric = false;

    EdgeInsets edgeInsets;
    mutable util::Camera camera;

    // cache values for spherical mercator math
    double Bc = Projection::worldSize(scale) / util::DEGREES_MAX;
    double Cc = Projection::worldSize(scale) / util::M2PI;

    mutable bool requestMatricesUpdate{true};
    mutable mat4 projectionMatrix;
    mutable mat4 invProjectionMatrix;
    mutable mat4 coordMatrix;
    mutable mat4 invertedMatrix;

    // Terrain camera clamp (task 2.0b): the render thread's most recent rise (camera ground DEM
    // height minus centre DEM height, same frame), and the clearance constrainCameraAboveTerrain
    // holds above it.
    std::optional<double> terrainCameraGroundRise;
    double terrainCameraMarginMeters = 60.0;
};

} // namespace mln
