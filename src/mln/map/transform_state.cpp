#include <mln/map/transform_state.hpp>
#include <mln/math/angles.hpp>
#include <mln/math/clamp.hpp>
#include <mln/math/log2.hpp>
#include <mln/tile/tile_id.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/interpolate.hpp>
#include <mln/util/logging.hpp>
#include <mln/util/projection.hpp>
#include <mln/util/tile_coordinate.hpp>

#include <numbers>

using namespace std::numbers;

namespace mln {

/*
 * The maximum angle to use for the Mercator horizon. This must be less than 90
 * to prevent errors in `MercatorTransform::_calcMatrices()`. It shouldn't be too close
 * to 90, or the distance to the horizon will become very large, unnecessarily increasing
 * the number of tiles needed to render the map.
 */
const double maxMercatorHorizonAngle = util::deg2rad(89.25);

namespace {
LatLng latLngFromMercator(Point<double> mercatorCoordinate, LatLng::WrapMode wrapMode = LatLng::WrapMode::Unwrapped) {
    return {util::rad2deg(2 * std::atan(std::exp(pi - mercatorCoordinate.y * 2 * pi)) - (pi / 2)),
            mercatorCoordinate.x * 360.0 - 180.0,
            wrapMode};
}
constexpr double kEpsilon = 1e-9;
// To avoid flickering issue due to "zoom = 13.9999999..".
double roundForAccuracy(double x) {
    double round_x = std::round(x);
    double diff = std::abs(round_x - x);
    if (diff < kEpsilon && diff > 0) {
        return round_x;
    } else {
        return x;
    }
}
} // namespace

TransformState::TransformState(ConstrainMode constrainMode_, ViewportMode viewportMode_)
    : bounds(LatLngBounds()),
      constrainMode(constrainMode_),
      viewportMode(viewportMode_) {}

void TransformState::setProperties(const TransformStateProperties& properties) {
    if (properties.x) {
        setX(*properties.x);
    }
    if (properties.y) {
        setY(*properties.y);
    }
    if (properties.z) {
        setZ(*properties.z);
    }
    if (properties.scale) {
        setScale(*properties.scale);
    }
    if (properties.bearing) {
        setBearing(*properties.bearing);
    }
    if (properties.fov) {
        setFieldOfView(*properties.fov);
    }
    if (properties.pitch) {
        setPitch(*properties.pitch);
    }
    if (properties.roll) {
        setRoll(*properties.roll);
    }
    if (properties.xSkew) {
        setXSkew(*properties.xSkew);
    }
    if (properties.ySkew) {
        setYSkew(*properties.ySkew);
    }
    if (properties.axonometric) {
        setAxonometric(*properties.axonometric);
    }
    if (properties.panning) {
        setPanningInProgress(*properties.panning);
    }
    if (properties.scaling) {
        setScalingInProgress(*properties.scaling);
    }
    if (properties.rotating) {
        setRotatingInProgress(*properties.rotating);
    }
    if (properties.edgeInsets) {
        setEdgeInsets(*properties.edgeInsets);
    }
    if (properties.size) {
        setSize(*properties.size);
    }
    if (properties.constrain) {
        setConstrainMode(*properties.constrain);
    }
    if (properties.northOrientation) {
        setNorthOrientation(*properties.northOrientation);
    }
    if (properties.viewPortMode) {
        setViewportMode(*properties.viewPortMode);
    }
    if (properties.frustumOffset) {
        setFrustumOffset(*properties.frustumOffset);
    }
}

// MARK: - Matrix

void TransformState::matrixFor(mat4& matrix, const UnwrappedTileID& tileID) const {
    const uint64_t tileScale = 1ull << tileID.canonical.z;
    const double s = Projection::worldSize(scale) / tileScale;

    matrix::identity(matrix);
    matrix::translate(matrix,
                      matrix,
                      int64_t(tileID.canonical.x + tileID.wrap * static_cast<int64_t>(tileScale)) * s,
                      int64_t(tileID.canonical.y) * s,
                      0);
    matrix::scale(matrix, matrix, s / util::EXTENT, s / util::EXTENT, 1);
}

void TransformState::getProjMatrix(mat4& projMatrix, uint16_t nearZ, bool aligned) const {
    if (size.isEmpty()) {
        return;
    }

    const double cameraToCenterDistance = getCameraToCenterDistance();
    const ScreenCoordinate offset = getCenterOffset();

    const double limitedPitch = util::clamp(getPitch(), 0.0, maxMercatorHorizonAngle);
    const double cameraToSeaLevelDistance = cameraToCenterDistance + std::abs(z) / std::cos(limitedPitch);

    // Find the Z distance from the viewport center point
    // [width/2 + offset.x, height/2 + offset.y] to the top edge; to point
    // [width/2 + offset.x, 0] in Z units.
    // 1 Z unit is equivalent to 1 horizontal px at the center of the map
    // (the distance between[width/2, height/2] and [width/2 + 1, height/2])
    // See https://github.com/mapbox/mapbox-gl-native/pull/15195 for details.
    // See TransformState::fov description: fov = 2 * arctan((height / 2) / (height * 1.5)).
    const double tanFovAboveCenter = (0.5 + (offset.y - frustumOffset.top()) / size.height) * 2.0 *
                                     std::tan(fov / 2.0) *
                                     (std::abs(std::cos(roll)) + std::abs(std::sin(roll)) * size.width / size.height);
    const double tanMultiple = util::clamp(tanFovAboveCenter * std::tan(limitedPitch), 0.0, 0.99);
    assert(tanMultiple < 1);
    // Calculate z distance of the farthest fragment that should be rendered.
    const double furthestDistance = cameraToSeaLevelDistance / (1 - tanMultiple);
    // Add a bit extra to avoid precision problems when a fragment's distance is exactly `furthestDistance`
    const double farZ = furthestDistance * 1.01;

    // Make sure the camera state is up-to-date
    updateCameraState();

    mat4 worldToCamera = camera.getWorldToCamera(scale, viewportMode == ViewportMode::FlippedY);
    mat4 cameraToClip = camera.getCameraToClipPerspective(
        getFieldOfView(), static_cast<double>(size.width) / size.height, nearZ, farZ);

    // Move the center of perspective to center of specified edgeInsets.
    // Values are in range [-1, 1] where the upper and lower range values
    // position viewport center to the screen edges. This is overridden
    // if using axonometric perspective (not in public API yet, Issue #11882).
    // TODO(astojilj): Issue #11882 should take edge insets into account, too.
    if (!axonometric) {
        cameraToClip[8] = -offset.x * 2.0 / size.width;
        cameraToClip[9] = offset.y * 2.0 / size.height;
    }

    // Apply north orientation angle
    if (getNorthOrientation() != NorthOrientation::Upwards) {
        matrix::rotate_z(cameraToClip, cameraToClip, -getNorthOrientationAngle());
    }

    matrix::multiply(projMatrix, cameraToClip, worldToCamera);

    if (axonometric) {
        // mat[11] controls perspective
        projMatrix[11] = 0.0;

        // mat[8], mat[9] control x-skew, y-skew
        double pixelsPerMeter = 1.0 / Projection::getMetersPerPixelAtLatitude(getLatLng().latitude(), getZoom());
        projMatrix[8] = xSkew * pixelsPerMeter;
        projMatrix[9] = ySkew * pixelsPerMeter;
    }

    // Make a second projection matrix that is aligned to a pixel grid for
    // rendering raster tiles. We're rounding the (floating point) x/y values to
    // achieve to avoid rendering raster images to fractional coordinates.
    // Additionally, we adjust by half a pixel in either direction in case that
    // viewport dimension is an odd integer to preserve rendering to the pixel
    // grid. We're rotating this shift based on the angle of the transformation
    // so that 0°, 90°, 180°, and 270° rasters are crisp, and adjust the shift
    // so that it is always <= 0.5 pixels.

    if (aligned) {
        const double worldSize = Projection::worldSize(scale);
        const double dx = x - 0.5 * worldSize;
        const double dy = y - 0.5 * worldSize;

        const auto xShift = static_cast<double>(size.width % 2) / 2.0;
        const auto yShift = static_cast<double>(size.height % 2) / 2.0;
        const double bearingCos = std::cos(bearing);
        const double bearingSin = std::sin(bearing);
        double devNull;
        const double dxa = -std::modf(dx, &devNull) + bearingCos * xShift + bearingSin * yShift;
        const double dya = -std::modf(dy, &devNull) + bearingCos * yShift + bearingSin * xShift;
        matrix::translate(projMatrix, projMatrix, dxa > 0.5 ? dxa - 1 : dxa, dya > 0.5 ? dya - 1 : dya, 0);
    }
}

void TransformState::updateCameraState() const {
    if (!valid()) {
        return;
    }

    const double worldSize = Projection::worldSize(scale);
    const double cameraToCenterDistance = getCameraToCenterDistance();

    // x & y tracks the center of the map in pixels. However as rendering is
    // done in pixel coordinates the rendering origo is actually in the middle
    // of the map (0.5 * worldSize). x&y positions have to be negated because it
    // defines position of the map, not the camera. Moving map 10 units left has
    // the same effect as moving camera 10 units to the right.
    const double dx = 0.5 * worldSize - x;
    const double dy = 0.5 * worldSize - y;

    // Set camera orientation and move it to a proper distance from the map
    camera.setOrientation(getRoll(), getPitch(), getBearing());

    const vec3 forward = camera.forward();
    const vec3 orbitPosition = {{-forward[0] * cameraToCenterDistance,
                                 -forward[1] * cameraToCenterDistance,
                                 -forward[2] * cameraToCenterDistance}};
    vec3 cameraPosition = {{dx + orbitPosition[0], dy + orbitPosition[1], z + orbitPosition[2]}};

    cameraPosition[0] /= worldSize;
    cameraPosition[1] /= worldSize;
    cameraPosition[2] /= worldSize;

    camera.setPosition(cameraPosition);
}

void TransformState::updateStateFromCamera() {
    const vec3 position = camera.getPosition();
    const vec3 forward = camera.forward();

    const double dx = forward[0];
    const double dy = forward[1];
    const double dz = forward[2];

    // Compute bearing and pitch
    double newBearing;
    double newPitch;
    double newRoll;
    camera.getOrientation(newRoll, newPitch, newBearing);
    newPitch = util::clamp(newPitch, minPitch, maxPitch);

    // Compute zoom level from the camera altitude
    const double centerDistance = getCameraToCenterDistance();
    // The centre sits on the plane at the current centre altitude, not at sea level.
    // Intersecting the view ray with z = 0 instead would drop the centre to sea level and
    // zero the altitude, so a get/setFreeCameraOptions round trip moved the map - visible
    // as a jump the moment anything drove the free camera over raised terrain.
    const double worldSz = Projection::worldSize(scale);
    const double planeZ = worldSz > 0.0 ? z / worldSz : 0.0;
    const double heightAbovePlane = position[2] - planeZ;
    double zoom;
    double newScale;
    double travel;
    if (dz < -1.0e-9 && heightAbovePlane > 1.0e-9 && newPitch <= maxMercatorHorizonAngle) {
        zoom = util::log2(centerDistance / (heightAbovePlane / std::cos(newPitch) * util::tileSize_D));
        newScale = util::clamp(std::pow(2.0, zoom), min_scale, max_scale);
        travel = -heightAbovePlane / dz;
    } else {
        zoom = 14;
        newScale = util::clamp(std::pow(2.0, zoom), min_scale, max_scale);
        travel = centerDistance / newScale / util::tileSize_D;
    }

    // Compute center point of the map
    const Point<double> mercatorPoint = {position[0] + dx * travel, position[1] + dy * travel};
    setLatLngZoom(latLngFromMercator(mercatorPoint), scaleZoom(newScale));

    // Lands back on planeZ by construction, so the centre altitude survives the round trip.
    const double mercatorZ = position[2] + dz * travel;
    double alt_m = mercatorZ * Projection::getMetersPerPixelAtLatitude(getLatLng().latitude(), 0) * util::tileSize_D;
    setCenterAltitude(alt_m);
    setBearing(newBearing);
    setPitch(newPitch);
    setRoll(newRoll);
}

FreeCameraOptions TransformState::getFreeCameraOptions() const {
    updateCameraState();

    FreeCameraOptions options;
    options.position = camera.getPosition();
    options.orientation = camera.getOrientation().m;

    return options;
}

bool TransformState::setCameraPosition(const vec3& position) {
    if (std::isnan(position[0]) || std::isnan(position[1]) || std::isnan(position[2])) return false;

    const double maxWorldSize = Projection::worldSize(std::pow(2.0, getMaxZoom()));
    const double minWorldSize = Projection::worldSize(std::pow(2.0, getMinZoom()));
    const double distToCenter = getCameraToCenterDistance();

    const vec3 updatedPos = vec3{
        {position[0], position[1], util::clamp(position[2], distToCenter / maxWorldSize, distToCenter / minWorldSize)}};

    camera.setPosition(updatedPos);
    return true;
}

bool TransformState::setCameraOrientation(const Quaternion& orientation_) {
    const vec4& c = orientation_.m;
    if (std::isnan(c[0]) || std::isnan(c[1]) || std::isnan(c[2]) || std::isnan(c[3])) {
        return false;
    }

    // Zero-length quaternions are not valid
    if (orientation_.length() == 0.0) {
        return false;
    }

    Quaternion unitQuat = orientation_.normalized();
    const vec3 forward = unitQuat.transform({{0.0, 0.0, -1.0}});
    const vec3 up = unitQuat.transform({{0.0, -1.0, 0.0}});

    const std::optional<Quaternion> updatedOrientation = util::Camera::orientationFromFrame(forward, up);
    if (!updatedOrientation) return false;

    camera.setOrientation(updatedOrientation.value());
    return true;
}

void TransformState::setFreeCameraOptions(const FreeCameraOptions& options) {
    if (!valid()) {
        return;
    }

    if (!options.position && !options.orientation) return;

    // Check if the state is dirty and camera needs to be synchronized
    updateMatricesIfNeeded();

    bool changed = false;
    if (options.orientation && options.orientation.value() != camera.getOrientation().m) {
        changed |= setCameraOrientation(options.orientation.value());
    }

    if (options.position && options.position.value() != camera.getPosition()) {
        changed |= setCameraPosition(options.position.value());
    }

    if (changed) {
        updateStateFromCamera();
        requestMatricesUpdate = true;
    }
}

void TransformState::updateMatricesIfNeeded() const {
    if (!needsMatricesUpdate() || size.isEmpty()) return;

    getProjMatrix(projectionMatrix);
    coordMatrix = coordinatePointMatrix(projectionMatrix);

    bool err = matrix::invert(invProjectionMatrix, projectionMatrix);
    if (err) throw std::runtime_error("failed to invert projectionMatrix");

    err = matrix::invert(invertedMatrix, coordMatrix);
    if (err) throw std::runtime_error("failed to invert coordinatePointMatrix");

    requestMatricesUpdate = false;
}

const mat4& TransformState::getProjectionMatrix() const {
    updateMatricesIfNeeded();
    return projectionMatrix;
}

const mat4& TransformState::getInvProjectionMatrix() const {
    updateMatricesIfNeeded();
    return invProjectionMatrix;
}

const mat4& TransformState::getCoordMatrix() const {
    updateMatricesIfNeeded();
    return coordMatrix;
}

const mat4& TransformState::getInvertedMatrix() const {
    updateMatricesIfNeeded();
    return invertedMatrix;
}

// MARK: - Dimensions

Size TransformState::getSize() const {
    return size;
}

void TransformState::setSize(const Size& size_) {
    if (size != size_) {
        size = size_;
        requestMatricesUpdate = true;
    }
}

EdgeInsets TransformState::getFrustumOffset() const {
    return frustumOffset;
}

void TransformState::setFrustumOffset(const EdgeInsets& frustumOffset_) {
    if (frustumOffset != frustumOffset_) {
        frustumOffset = frustumOffset_;
        requestMatricesUpdate = true;
    }
}

// MARK: - North Orientation

NorthOrientation TransformState::getNorthOrientation() const {
    return orientation;
}

void TransformState::setNorthOrientation(const NorthOrientation val) {
    if (orientation != val) {
        orientation = val;
        requestMatricesUpdate = true;
    }
}

double TransformState::getNorthOrientationAngle() const {
    double angleOrientation = 0;
    if (orientation == NorthOrientation::Rightwards) {
        angleOrientation += pi / 2;
    } else if (orientation == NorthOrientation::Downwards) {
        angleOrientation += pi;
    } else if (orientation == NorthOrientation::Leftwards) {
        angleOrientation -= pi / 2;
    }
    return angleOrientation;
}

// MARK: - Constrain mode

ConstrainMode TransformState::getConstrainMode() const {
    return constrainMode;
}

void TransformState::setConstrainMode(const ConstrainMode val) {
    if (constrainMode != val) {
        constrainMode = val;
        requestMatricesUpdate = true;
    }
}

// MARK: - ViewportMode

ViewportMode TransformState::getViewportMode() const {
    return viewportMode;
}

void TransformState::setViewportMode(ViewportMode val) {
    if (viewportMode != val) {
        viewportMode = val;
        requestMatricesUpdate = true;
    }
}

// MARK: - Camera options

CameraOptions TransformState::getCameraOptions(const std::optional<EdgeInsets>& padding) const {
    return CameraOptions()
        .withCenter(getLatLng())
        .withCenterAltitude(getCenterAltitude())
        .withPadding(padding ? padding : edgeInsets)
        .withZoom(getZoom())
        .withBearing(util::rad2deg(-bearing))
        .withPitch(util::rad2deg(pitch))
        .withRoll(util::rad2deg(roll))
        .withFov(util::rad2deg(fov));
}

// MARK: - EdgeInsets

void TransformState::setEdgeInsets(const EdgeInsets& val) {
    if (edgeInsets != val) {
        edgeInsets = val;
        requestMatricesUpdate = true;
    }
}

// MARK: - Position

LatLng TransformState::getLatLng(LatLng::WrapMode wrapMode) const {
    return {util::rad2deg(2 * std::atan(std::exp(y / Cc)) - 0.5 * pi), -x / Bc, wrapMode};
}

double TransformState::getCenterAltitude() const {
    return z * Projection::getMetersPerPixelAtLatitude(getLatLng().latitude(), getZoom());
}

double TransformState::getCameraAltitudeMeters() const {
    return getCenterAltitude() + std::cos(getPitch()) * getCameraToCenterDistance() *
                                     Projection::getMetersPerPixelAtLatitude(getLatLng().latitude(), getZoom());
}

LatLng TransformState::getCameraLatLng() const {
    updateCameraState();
    const vec3 position = camera.getPosition();
    return latLngFromMercator(Point<double>{position[0], position[1]});
}

double TransformState::pixel_x() const {
    const double center = (size.width - Projection::worldSize(scale)) / 2;
    return center + x;
}

double TransformState::pixel_y() const {
    const double center = (size.height - Projection::worldSize(scale)) / 2;
    return center + y;
}

// MARK: - Zoom

double TransformState::getZoom() const {
    return scaleZoom(scale);
}

uint8_t TransformState::getIntegerZoom() const {
    return static_cast<uint8_t>(getZoom());
}

double TransformState::getZoomFraction() const {
    return getZoom() - getIntegerZoom();
}

// MARK: - Bounds

void TransformState::setLatLngBounds(LatLngBounds bounds_) {
    if (bounds_ != bounds) {
        bounds = bounds_;
        setLatLngZoom(getLatLng(LatLng::Unwrapped), getZoom());
    }
}

LatLngBounds TransformState::getLatLngBounds() const {
    return bounds;
}

void TransformState::setMinZoom(const double minZoom) {
    if (minZoom <= getMaxZoom()) {
        min_scale = zoomScale(util::clamp(minZoom, util::MIN_ZOOM, util::MAX_ZOOM));
    }
}

double TransformState::getMinZoom() const {
    double test_scale = min_scale;
    double unused_x = x;
    double unused_y = y;
    constrain(test_scale, unused_x, unused_y);

    return scaleZoom(test_scale);
}

void TransformState::setMaxZoom(const double maxZoom) {
    if (maxZoom >= getMinZoom()) {
        max_scale = zoomScale(util::clamp(maxZoom, util::MIN_ZOOM, util::MAX_ZOOM));
    }
}

double TransformState::getMaxZoom() const {
    return scaleZoom(max_scale);
}

void TransformState::setMinPitch(const double pitch_) {
    if (pitch_ <= maxPitch) {
        minPitch = util::clamp(pitch_, util::PITCH_MIN, maxPitch);
    } else {
        Log::Warning(Event::General,
                     "Trying to set minimum pitch to larger than maximum pitch, no "
                     "changes made.");
    }
}

double TransformState::getMinPitch() const {
    return minPitch;
}

void TransformState::setMaxPitch(const double pitch_) {
    if (pitch_ >= minPitch) {
        maxPitch = util::clamp(pitch_, minPitch, util::PITCH_MAX);
    } else {
        Log::Warning(Event::General,
                     "Trying to set maximum pitch to smaller than minimum pitch, no "
                     "changes made.");
    }
}

double TransformState::getMaxPitch() const {
    return maxPitch;
}

double TransformState::getMinFieldOfView() const {
    return minFov;
}

double TransformState::getMaxFieldOfView() const {
    return maxFov;
}

// MARK: - Scale
double TransformState::getScale() const {
    return scale;
}

void TransformState::setScale(double val) {
    if (scale != val) {
        scale = val;
        requestMatricesUpdate = true;
    }
}

// MARK: - Positions

double TransformState::getX() const {
    return x;
}

void TransformState::setX(double val) {
    if (x != val) {
        x = val;
        requestMatricesUpdate = true;
    }
}

double TransformState::getY() const {
    return y;
}

void TransformState::setY(double val) {
    if (y != val) {
        y = val;
        requestMatricesUpdate = true;
    }
}

double TransformState::getZ() const {
    return z;
}

void TransformState::setZ(double val) {
    if (z != val) {
        z = val;
        requestMatricesUpdate = true;
    }
}

// MARK: - Rotation

double TransformState::getBearing() const {
    return bearing;
}

void TransformState::setBearing(double val) {
    if (bearing != val) {
        bearing = val;
        requestMatricesUpdate = true;
    }
}

float TransformState::getFieldOfView() const {
    return static_cast<float>(fov);
}

void TransformState::setFieldOfView(double val) {
    if (fov != val) {
        fov = val;
        requestMatricesUpdate = true;
    }
}

double TransformState::getRoll() const {
    return roll;
}

void TransformState::setRoll(double val) {
    if (roll != val) {
        roll = val;
        requestMatricesUpdate = true;
    }
}

float TransformState::getCameraToCenterDistance() const {
    return static_cast<float>(0.5 * size.height / std::tan(fov / 2.0));
}

double TransformState::getPitch() const {
    return pitch;
}

void TransformState::setPitch(double val) {
    if (pitch != val) {
        pitch = val;
        requestMatricesUpdate = true;
    }
}

double TransformState::getXSkew() const {
    return xSkew;
}

void TransformState::setXSkew(double val) {
    if (xSkew != val) {
        xSkew = val;
        requestMatricesUpdate = true;
    }
}
double TransformState::getYSkew() const {
    return ySkew;
}

void TransformState::setYSkew(double val) {
    if (ySkew != val) {
        ySkew = val;
        requestMatricesUpdate = true;
    }
}

bool TransformState::getAxonometric() const {
    return axonometric;
}

void TransformState::setAxonometric(bool val) {
    if (axonometric != val) {
        axonometric = val;
        requestMatricesUpdate = true;
    }
}

// MARK: - State

bool TransformState::isChanging() const {
    return rotating || scaling || panning || gestureInProgress;
}

bool TransformState::isRotating() const {
    return rotating;
}

bool TransformState::isScaling() const {
    return scaling;
}

bool TransformState::isPanning() const {
    return panning;
}

bool TransformState::isGestureInProgress() const {
    return gestureInProgress;
}

// MARK: - Projection

double TransformState::zoomScale(double zoom) const {
    return roundForAccuracy(std::pow(2.0, zoom));
}

double TransformState::scaleZoom(double s) const {
    return roundForAccuracy(util::log2(s));
}

ScreenCoordinate TransformState::latLngToScreenCoordinate(const LatLng& latLng) const {
    vec4 p;
    return latLngToScreenCoordinate(latLng, p);
}

ScreenCoordinate TransformState::latLngToScreenCoordinate(const LatLng& latLng, vec4& p) const {
    return latLngToScreenCoordinate(latLng, 0.0, p);
}

ScreenCoordinate TransformState::latLngToScreenCoordinate(const LatLng& latLng,
                                                          double elevationMeters,
                                                          vec4& p) const {
    if (size.isEmpty()) {
        return {};
    }

    Point<double> pt = Projection::project(latLng, scale) / util::tileSize_D;
    vec4 c = {{pt.x, pt.y, elevationMeters, 1}};
    matrix::transformMat4(p, c, getCoordMatrix());
    return {p[0] / p[3], size.height - p[1] / p[3]};
}

TileCoordinate TransformState::screenCoordinateToTileCoordinate(const ScreenCoordinate& point,
                                                                uint8_t atZoom,
                                                                double targetZ) const {
    if (size.isEmpty()) {
        return {.p = {}, .z = 0};
    }

    double flippedY = size.height - point.y;

    // since we don't know the correct projected z value for the point,
    // unproject two points to get a line and then find the point on that
    // line with z=targetZ (metres above sea level; 0 is sea level)

    vec4 coord0;
    vec4 coord1;
    vec4 point0 = {{point.x, flippedY, 0, 1}};
    vec4 point1 = {{point.x, flippedY, 1, 1}};
    matrix::transformMat4(coord0, point0, getInvertedMatrix());
    matrix::transformMat4(coord1, point1, getInvertedMatrix());

    double w0 = coord0[3];
    double w1 = coord1[3];

    Point<double> p0 = Point<double>(coord0[0], coord0[1]) / w0;
    Point<double> p1 = Point<double>(coord1[0], coord1[1]) / w1;

    double z0 = coord0[2] / w0;
    double z1 = coord1[2] / w1;
    double t = z0 <= z1 ? 0 : (targetZ - z0) / (z1 - z0);

    Point<double> p = util::interpolate(p0, p1, t) / scale * static_cast<double>(1 << atZoom);
    return {.p = {p.x, p.y}, .z = static_cast<double>(atZoom)};
}

LatLng TransformState::screenCoordinateToLatLng(const ScreenCoordinate& point, LatLng::WrapMode wrapMode) const {
    auto coord = screenCoordinateToTileCoordinate(point, 0);
    return Projection::unproject(coord.p, 1. / util::tileSize_D, wrapMode);
}

LatLng TransformState::screenCoordinateToLatLng(const ScreenCoordinate& point,
                                                double elevationMeters,
                                                LatLng::WrapMode wrapMode) const {
    auto coord = screenCoordinateToTileCoordinate(point, 0, elevationMeters);
    return Projection::unproject(coord.p, 1. / util::tileSize_D, wrapMode);
}

mat4 TransformState::coordinatePointMatrix(const mat4& projMatrix) const {
    mat4 proj = projMatrix;
    matrix::scale(proj, proj, util::tileSize_D, util::tileSize_D, 1);
    matrix::multiply(proj, getPixelMatrix(), proj);
    return proj;
}

mat4 TransformState::getPixelMatrix() const {
    mat4 m;
    matrix::identity(m);
    matrix::scale(m, m, static_cast<double>(size.width) / 2, -static_cast<double>(size.height) / 2, 1);
    matrix::translate(m, m, 1, -1, 0);
    return m;
}

// MARK: - (private helper functions)

bool TransformState::rotatedNorth() const {
    using NO = NorthOrientation;
    return (orientation == NO::Leftwards || orientation == NO::Rightwards);
}

bool TransformState::constrainScreen(double& scale_, double& lat, double& lon) const {
    if (constrainMode == ConstrainMode::Screen) {
        double zoom = scaleZoom(scale_);
        CameraOptions options = CameraOptions();
        constrainCameraAndZoomToBounds(options, zoom);

        scale_ = zoomScale(zoom);

        if (options.center) {
            LatLng center = options.center.value();
            lat = center.latitude();
            lon = center.longitude();

            return true;
        }
    }
    return false;
}

void TransformState::constrain(double& scale_, double& x_, double& y_) const {
    if (constrainMode == ConstrainMode::None || constrainMode == ConstrainMode::Screen) {
        return;
    }

    // Constrain scale to avoid zooming out far enough to show off-world areas on the Y axis.
    const double ratioY = (rotatedNorth() ? size.width : size.height) / util::tileSize_D;
    scale_ = util::max(scale_, ratioY);

    // Constrain min/max pan to avoid showing off-world areas on the Y axis.
    double max_y = (scale_ * util::tileSize_D - (rotatedNorth() ? size.width : size.height)) / 2;
    y_ = std::max(-max_y, std::min(y_, max_y));

    if (constrainMode == ConstrainMode::WidthAndHeight) {
        // Constrain min/max pan to avoid showing off-world areas on the X axis.
        double max_x = (scale_ * util::tileSize_D - (rotatedNorth() ? size.height : size.width)) / 2;
        x_ = std::max(-max_x, std::min(x_, max_x));
    }
}

void TransformState::constrainCameraAndZoomToBounds(CameraOptions& requestedCamera, double& requestedZoom) const {
    if (constrainMode != ConstrainMode::Screen || getLatLngBounds() == LatLngBounds()) {
        return;
    }

    LatLng centerLatLng = getLatLng();

    if (requestedCamera.center) {
        centerLatLng = requestedCamera.center.value();
    }

    Point<double> anchorOffset{0, 0};
    double requestedScale = zoomScale(requestedZoom);

    // Since the transition calculations will include any specified anchor in the result
    // we need to do the same when testing if the requested center and zoom is outside the bounds or not.
    if (requestedCamera.anchor) {
        ScreenCoordinate anchor = requestedCamera.anchor.value();
        anchor.y = getSize().height - anchor.y;
        LatLng anchorLatLng = screenCoordinateToLatLng(anchor);

        // The screenCoordinateToLatLng function requires the matrices inside the state to reflect
        // the requested scale. So we create a copy and set the requested zoom before the conversion.
        // This will give us the same result as the transition calculations.
        TransformState state{*this};
        state.setLatLngZoom(getLatLng(), scaleZoom(requestedScale));
        LatLng screenLatLng = state.screenCoordinateToLatLng(anchor);

        auto latLngCoord = Projection::project(anchorLatLng, requestedScale);
        auto anchorCoord = Projection::project(screenLatLng, requestedScale);
        anchorOffset = latLngCoord - anchorCoord;
    }

    mln::LatLngBounds currentBounds = getLatLngBounds();
    mln::ScreenCoordinate neBounds = Projection::project(currentBounds.northeast(), requestedScale);
    mln::ScreenCoordinate swBounds = Projection::project(currentBounds.southwest(), requestedScale);
    mln::ScreenCoordinate center = Projection::project(centerLatLng, requestedScale);
    mln::ScreenCoordinate currentCenter = Projection::project(getLatLng(), requestedScale);

    double minY = neBounds.y;
    double maxY = swBounds.y;
    double minX = swBounds.x;
    double maxX = neBounds.x;

    double startX = center.x;
    double startY = center.y;

    double resultX = startX;
    double resultY = startY;

    uint32_t screenWidth = getSize().width;
    uint32_t screenHeight = getSize().height;

    double h2 = screenHeight / 2.0;
    if (startY - h2 + anchorOffset.y < minY) {
        resultY = minY + h2;
    }
    if (startY + anchorOffset.y + h2 > maxY) {
        resultY = maxY - h2;
    }

    double w2 = screenWidth / 2.0;
    if (startX + anchorOffset.x - w2 < minX) {
        resultX = minX + w2;
    }
    if (startX + anchorOffset.x + w2 > maxX) {
        resultX = maxX - w2;
    }

    double scaleY = 0;
    if (maxY - minY < screenHeight) {
        scaleY = screenHeight / (maxY - minY);
        resultY = (maxY + minY) / 2.0;
    }

    double scaleX = 0;
    if (maxX - minX < screenWidth) {
        scaleX = screenWidth / (maxX - minX);
        resultX = (maxX + minX) / 2.0;
    }

    double maxScale = scaleX > scaleY ? scaleX : scaleY;

    // Max scale will be 1 when the screen is exactly the same size as the max bounds in either the X or Y direction.
    // To avoid numerical instabilities we add small amount to the check to make sure we don't try to scale when we
    // don't actually need it.
    if (maxScale > 1.000001) {
        requestedZoom += scaleZoom(maxScale);

        if (scaleY > scaleX) {
            // If we scaled the y direction we want the resulting x position to be the same as the current x position.
            resultX = currentCenter.x;
        } else {
            // If we scaled the x direction we want the resulting y position to be the same as the current y position.
            resultY = currentCenter.y;
        }

        // Since we changed the scale, we might display something outside the bounds.
        // When checking we need to take into consideration that we just changed the scale,
        // since the resultX and minX were calculated with the requested scale, and not the scale we
        // just calculated to make sure we stay inside the bounds.
        if (resultX * maxScale - w2 <= minX * maxScale) {
            resultX = minX * maxScale + w2;
            resultX /= maxScale;
        } else if (resultX * maxScale + w2 >= maxX * maxScale) {
            resultX = maxX * maxScale - w2;
            resultX /= maxScale;
        }

        if (resultY * maxScale - h2 <= minY * maxScale) {
            resultY = minY * maxScale + h2;
            resultY /= maxScale;
        } else if (resultY * maxScale + h2 >= maxY * maxScale) {
            resultY = maxY * maxScale - h2;
            resultY /= maxScale;
        }
    }

    if (resultX != startX || resultY != startY) {
        // If we made changes just drop any anchor point
        requestedCamera.anchor.reset();
        requestedCamera.center = std::optional(Projection::unproject({resultX, resultY}, requestedScale));
    }
}

ScreenCoordinate TransformState::getCenterOffset() const {
    return {0.5 * (edgeInsets.left() - edgeInsets.right()), 0.5 * (edgeInsets.top() - edgeInsets.bottom())};
}

void TransformState::moveLatLng(const LatLng& latLng, const ScreenCoordinate& anchor) {
    auto centerCoord = Projection::project(getLatLng(LatLng::Unwrapped), scale);
    auto latLngCoord = Projection::project(latLng, scale);
    // The anchor ray is solved on the ground plane, but the centre is taken exactly rather than
    // re-cast as a ray. That is deliberate and load-bearing: a ray through the centre solved on
    // any plane other than the centre's own lands (planeAltitude - centerAltitude)*tan(pitch)
    // away from the true centre, and repeated frames of a gesture accumulate that as drift.
    auto anchorCoord = Projection::project(screenCoordinateToLatLng(anchor, getGroundPlaneAltitude()), scale);
    setLatLngZoom(Projection::unproject(centerCoord + latLngCoord - anchorCoord, scale), getZoom());
}

void TransformState::setLatLngZoom(const LatLng& latLng, double zoom) {
    LatLng constrained = latLng;
    constrained = bounds.constrain(latLng);

    double newScale = util::clamp(zoomScale(zoom), min_scale, max_scale);
    const double newWorldSize = newScale * util::tileSize_D;
    Bc = newWorldSize / util::DEGREES_MAX;
    Cc = newWorldSize / util::M2PI;

    const double m = 1 - 1e-15;
    const double f = util::clamp(std::sin(util::deg2rad(constrained.latitude())), -m, m);

    ScreenCoordinate point = {
        -constrained.longitude() * Bc,
        0.5 * Cc * std::log((1 + f) / (1 - f)),
    };
    setScalePoint(newScale, point);
}

void TransformState::setCenterAltitude(double alt_m) {
    z = alt_m / Projection::getMetersPerPixelAtLatitude(getLatLng().latitude(), getZoom());
    requestMatricesUpdate = true;
}

void TransformState::constrainCameraAboveTerrain(bool zoomRequested, bool pitchRequested, double previousZoom,
                                                 double previousPitch) {
    if (!terrainCameraGroundRise || !valid()) {
        return;
    }

    // The requirement is cameraAltitude >= groundUnderCamera + margin, and cameraAltitude ==
    // centerAltitude + cos(pitch) * D (D the camera-to-centre distance in metres). Substituting:
    //   centerAltitude + cos(pitch) * D >= groundUnderCamera + margin
    // The engine already pins centerAltitude to the terrain under the centre
    // (Map::Impl::onTerrainCenterElevationChanged), so centerAltitude and groundUnderCentre are
    // the same quantity up to that pinning's own lag, and the inequality reduces to:
    //   cos(pitch) * D >= (groundUnderCamera - groundUnderCentre) + margin
    // which contains no absolute altitude at all - only the rise, which the render thread already
    // computes from two DEM samples taken in the same frame. That is why R is built from the rise
    // and nothing else: an absolute groundUnderCamera compared against this thread's own
    // getCenterAltitude() can be poisoned the instant a jumpTo changes zoom or pitch and this
    // clamp runs before the map thread's centre altitude has caught up to the new terrain sample -
    // measured at Gavarnie, pitch 0, where the rise is identically zero (camera sits directly over
    // the centre, so the ground under it IS the ground under the centre) but the old absolute form
    // still reported R as the full ~1765 m ground height and clamped a jumpTo that should never
    // have been touched. The rise is exactly zero at pitch 0 for any terrain, by construction, so
    // it cannot make that mistake.
    const double R = *terrainCameraGroundRise + terrainCameraMarginMeters;
    if (!(R > 0.0) || !std::isfinite(R)) {
        return;
    }

    const double cosPitch = std::cos(getPitch());
    const double C = getCameraToCenterDistance();
    if (!(cosPitch > 0.0) || !(C > 0.0)) {
        return;
    }

    const double lat = getLatLng().latitude();
    const double k = Projection::getMetersPerPixelAtLatitude(lat, 0.0);
    if (!(k > 0.0)) {
        return;
    }

    // A pitch-only change (the two-finger tilt gesture) is answered as a tilt: clamp PITCH only
    // and leave the zoom exactly where the caller put it. Every other case - a zoom change, a
    // centre change, both at once, or neither (the terrain-elevation-changed correction) - clamps
    // ZOOM first and then PITCH as a backstop, in case the zoom clamp ran into minZoom and the
    // camera is still under the terrain.
    const bool pitchOnly = pitchRequested && !zoomRequested;

    if (!pitchOnly) {
        // Zoom clamp: solves cos(pitch) * C * metersPerPixel(lat, zoom) == R for zoom, since
        // metersPerPixel(lat, zoom) is k * 2^-zoom: cos(pitch) * C * k * 2^-zoom == R
        // => zoom == log2(cos(pitch) * C * k / R).
        const double zoomArg = cosPitch * C * k / R;
        if (zoomArg > 0.0 && std::isfinite(zoomArg)) {
            double zoomMax = util::log2(zoomArg);
            const double requestedZoom = getZoom();
            if (std::isfinite(zoomMax) && requestedZoom > zoomMax) {
                if (zoomRequested || pitchRequested) {
                    // A clamp may stop a change, never reverse it: whenever the caller asked for a
                    // zoom change, a pitch change, or both, this axis is floored at the zoom from
                    // before this transition, but never above what was requested (a zoom-out
                    // request that the terrain clamps even further is left as a clamp, not raised
                    // back up). The floor applies here even when only pitch was requested, and
                    // below even when only zoom was requested, because the two clamps are one
                    // constraint (cos(pitch) * D == R): whichever axis ends up absorbing the
                    // remainder must not undo camera the user did not ask to move on that axis
                    // either. Only a pan's centre change or the bare CameraOptions() the terrain-
                    // rise channel sends - neither axis requested - leaves both floors inert.
                    zoomMax = std::min(std::max(zoomMax, previousZoom), requestedZoom);
                }
                // setLatLngZoom changes zoom but not z, and getCenterAltitude() reinterprets that
                // raw z at whatever zoom is current - so the centre altitude has to be re-pinned
                // after a zoom change here, exactly as every other zoom-changing transition in this
                // file already does, or R's own definition (which assumes centerAltitude fixed)
                // stops holding the moment this clamp changes the zoom it was computed against.
                const double centerAltitude = getCenterAltitude();
                setLatLngZoom(getLatLng(), zoomMax);
                setCenterAltitude(centerAltitude);
            }
        }
    }

    // Pitch clamp. In the zoom-first branch this runs after the zoom clamp with the (possibly
    // just-clamped) zoom: with the zoom already holding cos(pitch) * D == R exactly, this is a
    // no-op by construction and only bites as a backstop when the zoom clamp could not reach far
    // enough. In the pitch-only branch this is the only clamp that runs, against the zoom the
    // caller asked for, unchanged.
    const double D_m = C * Projection::getMetersPerPixelAtLatitude(lat, getZoom());
    if (D_m > 0.0 && std::isfinite(D_m)) {
        double pitchMax = std::acos(util::clamp(R / D_m, 0.0, 1.0));
        const double requestedPitch = getPitch();
        if (std::isfinite(pitchMax) && requestedPitch > pitchMax) {
            if (zoomRequested || pitchRequested) {
                // Same floor, same reason, for the pitch axis: whenever the caller asked for a
                // zoom change, a pitch change, or both, this axis (whether it is carrying the
                // caller's own request or just absorbing the backstop remainder from the zoom
                // clamp above) may not be lowered past the pitch the transition started from.
                pitchMax = std::min(std::max(pitchMax, previousPitch), requestedPitch);
            }
            setPitch(util::clamp(pitchMax, minPitch, maxPitch));
        }
    }
}

void TransformState::setScalePoint(const double newScale, const ScreenCoordinate& point) {
    double constrainedScale = newScale;
    ScreenCoordinate constrainedPoint = point;
    constrain(constrainedScale, constrainedPoint.x, constrainedPoint.y);

    scale = constrainedScale;
    x = constrainedPoint.x;
    y = constrainedPoint.y;
    Bc = Projection::worldSize(scale) / util::DEGREES_MAX;
    Cc = Projection::worldSize(scale) / util::M2PI;
    requestMatricesUpdate = true;
}

float TransformState::getCameraToTileDistance(const UnwrappedTileID& tileID) const {
    mat4 tileProjectionMatrix;
    matrixFor(tileProjectionMatrix, tileID);
    matrix::multiply(tileProjectionMatrix, getProjectionMatrix(), tileProjectionMatrix);
    vec4 tileCenter = {{util::tileSize_D / 2, util::tileSize_D / 2, 0, 1}};
    vec4 projectedCenter;
    matrix::transformMat4(projectedCenter, tileCenter, tileProjectionMatrix);
    return static_cast<float>(projectedCenter[3]);
}

float TransformState::maxPitchScaleFactor() const {
    if (size.isEmpty()) {
        return {};
    }
    auto latLng = screenCoordinateToLatLng({0, static_cast<float>(getSize().height)});

    Point<double> pt = Projection::project(latLng, scale) / util::tileSize_D;
    vec4 p = {{pt.x, pt.y, 0, 1}};
    vec4 topPoint;
    matrix::transformMat4(topPoint, p, getCoordMatrix());
    return static_cast<float>(topPoint[3]) / getCameraToCenterDistance();
}

} // namespace mln
