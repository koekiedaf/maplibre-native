#import <XCTest/XCTest.h>

#include <mln/map/camera.hpp>
#include <mln/map/transform.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/projection.hpp>

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

@end
