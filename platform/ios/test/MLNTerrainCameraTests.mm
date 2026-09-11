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
// (a) A zoom breach clamps the zoom. Well-above-centre terrain (a 3300 m rise, the same as
// 5000 m ground over a 1700 m centre) plus a 100 m margin, at pitch 45, then a jump to zoom 20,
// which at that pitch would put the camera at ~1750 m - underground. Both floors sit under this
// gesture (it requested a zoom change), so neither axis may end up below its pre-gesture value:
// the terrain-safe zoom alone (13.89) sits below the pre-gesture zoom (14.2), so the zoom floor
// holds zoom at 14.2; the terrain-safe pitch backstop at that zoom (28.52) also sits below the
// pre-gesture pitch (45), so the pitch floor holds pitch at 45 too. Both axes land back exactly
// where the gesture started, so the request is fully refused rather than partly honoured by
// tilting the map - and the camera altitude this produces sits below ground + margin (about
// 4436 m against a 5100 m target), which is the deliberate consequence documented on
// `constrainCameraAboveTerrain`: the floors hold a camera the user did not ask to move, closer to
// the terrain than the margin asks, until the next unfloored pan restores the clearance.
- (void)testZoomBreachClampsZoomToGroundPlusMargin {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  transform.setTerrainCameraMarginMeters(100.0);
  transform.setTerrainCameraGroundRise(3300.0);
  // Start below the terrain-safe zoom for this rise and margin, so the floor (which never lets a
  // clamp leave the camera further out than the gesture began) is inert here and the clamp can be
  // read for what it computes: the camera held at exactly rise plus margin.
  transform.jumpTo(mln::CameraOptions().withZoom(12.0));

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

// (b') The existing "no terrain" test above only asks for a zoom change, so a bug that clamped
// pitch specifically while leaving zoom alone under a nullopt rise would slip past it. This is
// the defect this task actually fixes: `RenderTerrain::getElevationForLatLng` cannot tell "no
// DEM loaded here" from genuine sea level, so when the camera's own ground point is off screen
// (common at higher pitch) and its DEM tile has simply not loaded, the old code read that back
// as a rise of exactly -centerElevation and the clamp fired on a fabricated number instead of
// switching itself off. Reported as std::nullopt (the render side already found no data for
// either sample), a combined zoom-and-pitch request must be honoured on both axes untouched.
- (void)testNoReportedGroundLeavesBothZoomAndPitchUntouched {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1753.24);
  // No setTerrainCameraGroundRise call: nullopt, as if the camera's own DEM tile were simply not
  // loaded (the reported defect), not as if the terrain were flat sea level.

  transform.jumpTo(mln::CameraOptions().withZoom(20.0).withPitch(60.0));

  XCTAssertEqualWithAccuracy(transform.getZoom(), 20.0, 1e-6);
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), 60.0, 1e-6);
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
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), 29.74, 0.5);
}

// (c') A zoom-only change over the same starting pitch (45) leaves the pitch untouched and
// clamps the zoom instead - the mirror of (c), and the pinch gesture's own shape (jumpTo with
// zoom and an anchor but no pitch). Same 3300 m rise as testZoomBreachClampsZoomToGroundPlusMargin
// but the default 60 m margin rather than that test's 100 m: the terrain-safe zoom alone (13.90)
// still sits below the pre-gesture zoom (14.2), so the zoom floor holds zoom at 14.2, and the
// terrain-safe pitch backstop at that zoom (29.74) sits below the pre-gesture pitch (45), so the
// pitch floor holds pitch at exactly 45 too - a zoom-only gesture must never tilt the map, which
// is the regression this task exists to prevent.
- (void)testZoomBreachClampsZoomAndLeavesThePitchAlone {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  transform.setTerrainCameraGroundRise(3300.0);

  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertLessThan(transform.getZoom(), 20.0);
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), 45.0, 0.01);
}

// (c'') The floor: a clamp may stop a change, never reverse it. Starting comfortably clear (no
// rise reported yet, so nothing has clamped), a jumpTo asking for a HIGHER zoom, with a rise
// large enough that the clamp fires, must leave the zoom no lower than where the gesture
// started - and still no higher than what was requested. This is the reported defect: a pinch
// asking to zoom in walked the camera's ground point onto higher terrain as zoom rose, which
// grew the rise, which cut the zoom, which walked the camera back further, settling below the
// zoom the gesture started from. Without the floor this test fails because the clamp lands on
// zoomMax alone, which sits below startZoom by construction here.
- (void)testZoomFloorNeverLowersZoomBelowWhereTheGestureStarted {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  const double startZoom = transform.getZoom();
  XCTAssertEqualWithAccuracy(startZoom, 14.2, 1e-6, @"comfortably clear: no rise reported yet");

  // A rise large enough that honouring a jump to zoom 20 at this pitch would put the camera
  // underground, well below where the gesture started.
  transform.setTerrainCameraGroundRise(20000.0);
  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertGreaterThanOrEqual(transform.getZoom(), startZoom - 1e-6);
  XCTAssertLessThanOrEqual(transform.getZoom(), 20.0 + 1e-6);
}

// The regression this round exists to prevent: with the zoom floor holding, the pitch backstop
// must not fire and tilt the map under a gesture that never asked for a pitch change. A zoom-only
// jumpTo asking for MORE zoom, over a rise big enough that the zoom clamp needs the pitch
// backstop to absorb the remainder, must still leave the pitch exactly where the gesture started
// - not just above where the unfloored backstop alone would have put it. A fix that only floors
// the axis the caller directly requested passes testZoomFloorNeverLowersZoomBelowWhereTheGesture-
// Started above but fails this one, because the pitch backstop it leaves unfloored lowers pitch
// to answer a zoom request with a tilt.
- (void)testZoomOnlyBreachLeavesThePitchExactlyWhereItStarted {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  const double startPitchDeg = mln::util::rad2deg(transform.getPitch());

  transform.setTerrainCameraGroundRise(3300.0);
  transform.jumpTo(mln::CameraOptions().withZoom(20.0));

  XCTAssertLessThan(transform.getZoom(), 20.0);
  XCTAssertEqualWithAccuracy(mln::util::rad2deg(transform.getPitch()), startPitchDeg, 1e-6);
}

// (c''') The same shape for pitch: a pitch-only jumpTo asking for MORE pitch must never leave
// the pitch below where it started, for the same reason as the zoom floor above. Without the
// floor this test fails because the clamp lands on pitchMax alone, below startPitch here.
- (void)testPitchFloorNeverLowersPitchBelowWhereTheGestureStarted {
  mln::Transform transform;
  gavarnie(transform, 30.0, 1700.0);
  const double startPitchDeg = mln::util::rad2deg(transform.getPitch());
  XCTAssertEqualWithAccuracy(startPitchDeg, 30.0, 1e-6, @"comfortably clear: no rise reported yet");

  // A rise large enough that honouring a pitch-only jump to 70 degrees would put the camera
  // underground, well below the pitch the gesture started from.
  transform.setTerrainCameraGroundRise(20000.0);
  transform.jumpTo(mln::CameraOptions().withPitch(70.0));

  const double resultPitchDeg = mln::util::rad2deg(transform.getPitch());
  XCTAssertGreaterThanOrEqual(resultPitchDeg, startPitchDeg - 1e-6);
  XCTAssertLessThanOrEqual(resultPitchDeg, 70.0 + 1e-6);
}

// The floor only guards a gesture that actually requested a zoom or a pitch change. A centre-only
// change - no zoom, no pitch in the CameraOptions, the shape moveBy reduces to - requests neither,
// so both floors stay inert and the clamp must still be free to reduce the zoom when panning
// walks the camera onto terrain high enough to need it, exactly as before this task.
- (void)testPanShapedChangeCanStillReduceZoomFreely {
  mln::Transform transform;
  gavarnie(transform, 45.0, 1700.0);
  const double startZoom = transform.getZoom();

  transform.setTerrainCameraGroundRise(20000.0);
  transform.jumpTo(mln::CameraOptions().withCenter(mln::LatLng{42.696, -0.004}));

  XCTAssertLessThan(transform.getZoom(), startZoom);
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
