#import <XCTest/XCTest.h>

#include <mln/map/camera.hpp>
#include <mln/map/transform.hpp>
#include <mln/math/angles.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/projection.hpp>
#include <mln/util/tile_cover.hpp>

#include <cmath>

// Task 2.0's regression guards for the terrain-anchored camera.
//
// They live in the iOS test bundle rather than beside the other Transform tests because
// //test:core depends on //platform/linux:impl unconditionally (test/BUILD.bazel:93) and
// therefore cannot be built on a Mac at all; this bundle is what the repo's own macOS and iOS CI
// runs. The code under test is pure core C++, so nothing about iOS is load-bearing here.

namespace {

double groundDistanceM(const mln::LatLng &a, const mln::LatLng &b) {
  constexpr double earthRadiusM = 6378137.0;
  const double p1 = mln::util::deg2rad(a.latitude());
  const double p2 = mln::util::deg2rad(b.latitude());
  const double dp = p2 - p1;
  const double dl = mln::util::deg2rad(b.longitude() - a.longitude());
  const double h = std::sin(dp / 2) * std::sin(dp / 2) +
                   std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
  return 2 * earthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

// Transform is noncopyable, so this configures one in place rather than returning it.
void gavarnie(mln::Transform &transform, double pitchDegrees, double centerAltitudeM) {
  transform.resize({390, 844}); // an iPhone in points
  transform.jumpTo(mln::CameraOptions()
                       .withCenter(mln::LatLng{42.696, -0.004})
                       .withZoom(14.2)
                       .withPitch(pitchDegrees)
                       .withCenterAltitude(centerAltitudeM));
}

// A DEM elevation lookup that reports the same range for every tile, standing in for a real
// DEM source's decoded data: `RenderTerrain::computeMeshCover` and every source's own cover
// query this for the elevation range of a tile they're testing for visibility.
class ConstantElevationProvider final : public mln::util::TileElevationProvider {
public:
  explicit ConstantElevationProvider(mln::Range<double> range) : range_(range) {}
  std::optional<mln::Range<double>> getTileElevationRange(const mln::CanonicalTileID &) const override {
    return range_;
  }

private:
  mln::Range<double> range_;
};

} // namespace

@interface MLNTerrainCameraTests : XCTestCase
@end

@implementation MLNTerrainCameraTests

// Raising the whole world by a uniform 1700 m must not change what a gesture does. Before the
// ground plane existed, the same drag over a 1700 m plateau flew kilometres, because the view ray
// was intersected with sea level far past the surface the finger was on.
- (void)testPanIsSolvedOnTheGroundPlaneNotSeaLevel {
  for (double pitch : {0.0, 25.0, 45.0, 60.0}) {
    mln::Transform seaLevel;
    mln::Transform plateau;
    gavarnie(seaLevel, pitch, 0.0);
    gavarnie(plateau, pitch, 1700.0);

    const mln::LatLng seaBefore = seaLevel.getLatLng();
    const mln::LatLng plateauBefore = plateau.getLatLng();

    seaLevel.moveBy({0, 120});
    plateau.moveBy({0, 120});

    const double seaMoved = groundDistanceM(seaBefore, seaLevel.getLatLng());
    const double plateauMoved = groundDistanceM(plateauBefore, plateau.getLatLng());

    XCTAssertGreaterThan(seaMoved, 1.0, @"pitch %g", pitch);
    XCTAssertEqualWithAccuracy(plateauMoved / seaMoved, 1.0, 0.02, @"pitch %g", pitch);
  }
}

// At pitch 0 the plane at the centre's altitude sits exactly the camera-to-centre distance from
// the camera, whatever that altitude is, so a drag moves exactly its own points of ground. This
// is the acceptance number the app bench measures with a real finger.
- (void)testPanAtPitchZeroMovesItsOwnPointsOfGround {
  mln::Transform transform;
  gavarnie(transform, 0.0, 1700.0);
  const mln::LatLng before = transform.getLatLng();
  transform.moveBy({0, 120});

  const double metersPerPoint =
      mln::Projection::getMetersPerPixelAtLatitude(before.latitude(), 14.2);
  XCTAssertEqualWithAccuracy(
      groundDistanceM(before, transform.getLatLng()) / (120 * metersPerPoint), 1.0, 0.01);
}

// The anchored paths (pinch, two-finger tilt) must use the same plane, or the anchor captured at
// the start of a transition and the anchor re-solved on each frame would disagree and the map
// would walk under the fingers.
- (void)testAnchoredZoomKeepsTheAnchoredGroundPointStill {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  const mln::ScreenCoordinate anchor{120, 600};
  const mln::LatLng anchorBefore = transform.screenCoordinateToLatLng(anchor, 1700.0);

  transform.jumpTo(mln::CameraOptions().withZoom(15.2).withAnchor(anchor));

  const mln::LatLng anchorAfter = transform.screenCoordinateToLatLng(anchor, 1700.0);
  XCTAssertLessThan(groundDistanceM(anchorBefore, anchorAfter), 5.0);
}

// Task 2.0b's regression guards: the camera's own altitude tested against the terrain under it.
//
// (a) A zoom breach clamps the zoom, holding the camera exactly at ground + margin, pitch
// untouched. Well-above-centre terrain (a 3300 m rise, the same as 5000 m ground over a 1700 m
// centre) plus a 100 m margin, at pitch 45, then a jump to zoom 20, which at that pitch would put
// the camera at ~1750 m - underground.
- (void)testZoomBreachClampsZoomToGroundPlusMargin {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  transform.setTerrainCameraMarginMeters(100.0);
  transform.setTerrainCameraGroundRise(3300.0);

  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertLessThan(transform.getZoom(), 20.0);
  XCTAssertEqualWithAccuracy(transform.getState().getCameraAltitudeMeters(), 5100.0, 1.0);
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), 45.0, 0.01);
}

// (b) With terrain off (nullopt ground elevation, the default with nothing set), the same request
// is honoured exactly: the clamp is off entirely, not clamped to sea level.
- (void)testNoReportedGroundLeavesTheRequestedZoomUntouched {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  // No setTerrainCameraGroundRise call: nullopt, as if there is no render terrain.

  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertEqualWithAccuracy(transform.getZoom(), 20.0, 1e-6);
}

// (c) A pitch-only change - the two-finger tilt gesture's own shape, jumpTo with pitch and an
// anchor but no zoom - is clamped as a tilt: pitch alone, zoom left exactly where it was. No
// zoom floor needed to make this fire; a plain jumpTo(CameraOptions().withPitch(...)) never asks
// the zoom clamp to run at all, because the caller never requested a zoom.
- (void)testPitchBreachClampsPitchAndLeavesTheZoomAlone {
  mln::Transform transform;
  gavarnie(transform, 0.0, 1700.0);
  transform.setTerrainCameraGroundRise(3300.0);

  transform.jumpTo(mln::CameraOptions().withPitch(55.0));

  XCTAssertEqualWithAccuracy(transform.getZoom(), 14.2, 1e-6);
  XCTAssertLessThan(mln::util::rad2deg(transform.getPitch()), 55.0);
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), 31.48, 0.5);
}

// (c') A zoom-only change over the same high ground and starting pitch (45) leaves the pitch
// untouched and clamps the zoom instead - the mirror of (c), and the pinch gesture's own shape
// (jumpTo with zoom and an anchor but no pitch). This is the same scenario as
// testZoomBreachClampsZoomToGroundPlusMargin; restated here beside its pitch-only twin so the
// axis rule reads as one pair.
- (void)testZoomBreachClampsZoomAndLeavesThePitchAlone {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  transform.setTerrainCameraGroundRise(3300.0);

  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertLessThan(transform.getZoom(), 20.0);
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), 45.0, 0.01);
}

// (d) Sliding off the cliff (a lower reported rise) releases the clamp: a subsequent zoom-in
// reaches a zoom the earlier report could not, with the centre unmoved.
- (void)testLoweringGroundElevationReleasesTheClampWithNoCentreJump {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  transform.setTerrainCameraGroundRise(3300.0);
  transform.jumpTo(mln::CameraOptions().withZoom(20.0));
  const double zoomAtHighGround = transform.getZoom();
  const mln::LatLng centerBefore = transform.getLatLng();

  transform.setTerrainCameraGroundRise(1300.0);
  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertGreaterThan(transform.getZoom(), zoomAtHighGround);
  XCTAssertLessThan(groundDistanceM(centerBefore, transform.getLatLng()), 1.0);
}

// (e) The defect this task fixes: at pitch 0 the camera sits directly over the map centre, so
// the ground under the camera IS the ground under the centre and the rise is identically zero -
// the clamp must never fire, no matter what the centre altitude itself is. Before this fix, R
// was built from an absolute ground elevation minus this thread's own getCenterAltitude(), so a
// jumpTo that changes zoom (and therefore should re-pin the centre altitude before the clamp
// looks at it) could see a stale centre altitude and clamp on the full ground height instead of
// zero. Reproduced here exactly as measured at Gavarnie: a centre altitude of 1765 m set on the
// transform beforehand (as if the map thread's terrain-centre pin had already caught up, which
// is the best case for the old code and it still failed), a reported rise of 0.0 (flat ground
// under the camera, or the pitch-0 case generally) and a margin of 0, then jumpTo to zoom 18 at
// pitch 0 must be honoured exactly.
- (void)testZeroRiseAtPitchZeroNeverClampsEvenWithAStaleCentreAltitude {
  mln::Transform transform;
  gavarnie(transform, 0.0, 1765.0);
  transform.setTerrainCameraGroundRise(0.0);

  transform.jumpTo(mln::CameraOptions().withZoom(18.0));

  XCTAssertEqualWithAccuracy(transform.getZoom(), 18.0, 1e-6);
}

// Plain check: getCameraLatLng is the centre straight down at pitch 0, and at pitch 45 it sits
// sin(pitch) * cameraToCenterDistance (converted to metres at the current zoom) from the centre,
// on the opposite side from the bearing - due south of the centre for bearing 0 (north up).
- (void)testCameraLatLngIsTheGroundPointUnderTheCamera {
  mln::Transform level;
  gavarnie(level, 0.0, 0.0);
  XCTAssertLessThan(groundDistanceM(level.getLatLng(), level.getState().getCameraLatLng()), 0.01);

  mln::Transform tilted;
  gavarnie(tilted, 45.0, 0.0);
  const mln::LatLng center = tilted.getLatLng();
  const mln::LatLng cameraGround = tilted.getState().getCameraLatLng();

  const double metersPerPixel = mln::Projection::getMetersPerPixelAtLatitude(center.latitude(), 14.2);
  const double expectedDistance = std::sin(mln::util::deg2rad(45.0)) *
                                   tilted.getState().getCameraToCenterDistance() * metersPerPixel;
  XCTAssertEqualWithAccuracy(groundDistanceM(center, cameraGround), expectedDistance,
                             expectedDistance * 0.02);
  // Bearing 0 is north up, so the camera sits behind (south of) the centre it looks at.
  XCTAssertLessThan(cameraGround.latitude(), center.latitude());
}

// Regression guard for the reported defect: with 3D terrain on, Gavarnie at pitch 0 drew only
// the style's flat background above zoom ~16.45, over mountainous ground only - flat regions
// (Utrecht) and lower zooms (13-16) were unaffected. util::tileCover's elevation-aware
// visibility test (used for the terrain mesh's own cover and, once terrain is on, every
// source's cover) is the code under test here, isolated from rendering entirely.
//
// The cause: `Frustum::fromInvProjMatrix` (src/mln/util/bounding_volumes.cpp) scaled the
// projected Z coordinate by the same tile-units-at-zoom factor as X and Y, but Z comes back
// out of the inverse projection already in metres (`Camera::getWorldToCamera` folds
// `pixelsPerMeter` into its own matrix before rotating, precisely so elevation can be supplied
// in metres; its inverse hands metres back). That extra division shrank the frustum's real
// depth range towards zero as zoom rose, while a tile's elevation-extended aabb
// (`elevatedAABB` in tile_cover.cpp) was built from the DEM's real metres - so real, in-view
// relief (Gavarnie's several hundred metres) landed entirely outside the mis-scaled frustum
// bounds and read as fully separate. Every one of the tile-cover's 7 world copies failed the
// same way at the tile actually on screen, so the traversal's root gave up immediately and the
// cover came back empty - blanking the frame. Before the fix this asserted false: the elevated
// query returned zero tiles at zoom 17 exactly as measured on device.
- (void)testElevatedTileCoverIsNotEmptyForRealMountainRelief {
  mln::Transform transform;
  gavarnie(transform, 0.0, 1200.0);
  transform.jumpTo(mln::CameraOptions().withZoom(17.0));

  // Gavarnie's own relief, in metres, as reported by the DEM tiles the defect was measured
  // against (the elevated aabb's z-range immediately before it started rejecting the tile).
  ConstantElevationProvider elevation(mln::Range<double>{900.0, 1500.0});

  mln::util::TileCoverParameters params{.transformState = transform.getState(),
                                         .elevationProvider = &elevation};
  params.tileLodMode = mln::TileLodMode::Adaptive;

  const auto tiles = mln::util::tileCover(params, 15, mln::Range<uint8_t>{8, 15}, 17);
  XCTAssertFalse(tiles.empty(),
                 @"elevation-aware tile cover returned nothing for a camera and relief that are "
                 @"genuinely on screen");
}

@end
