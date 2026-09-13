#import <Mapbox.h>
#import <XCTest/XCTest.h>

#import "../../darwin/src/MLNGeometry_Private.h"
#import "MLNMapView_Private.h"
#import "MLNMockGestureRecognizers.h"

#include <mln/map/camera.hpp>
#include <mln/map/map.hpp>
#include "src/mln/map/transform.hpp"

@interface MLNMapView (MLNMapViewGestureRecognizerTests)

- (void)handlePinchGesture:(UIPinchGestureRecognizer *)pinch;
- (void)handleRotateGesture:(UIRotationGestureRecognizer *)rotate;
- (void)handleDoubleTapGesture:(UITapGestureRecognizer *)doubleTap;
- (void)handleTwoFingerTapGesture:(UITapGestureRecognizer *)twoFingerTap;
- (void)handleQuickZoomGesture:(UILongPressGestureRecognizer *)quickZoom;
- (void)handleTwoFingerDragGesture:(UIPanGestureRecognizer *)twoFingerDrag;
- (CLLocationCoordinate2D)foundationCoordinateFrom:(CLLocationCoordinate2D)coordinate
                                             heading:(CLLocationDirection)heading
                                      sidewaysMeters:(CLLocationDistance)sidewaysMeters
                                       forwardMeters:(CLLocationDistance)forwardMeters;
- (void)handleFoundationPinchGesture:(UIPinchGestureRecognizer *)pinch;
- (void)handleFoundationRotateGesture:(UIRotationGestureRecognizer *)rotate;
- (void)handleFoundationTwoFingerGesture:(UIPanGestureRecognizer *)gesture;
- (NSUInteger)foundationTwoFingerModeForTranslation:(CGPoint)translation
                                               scale:(CGFloat)scale
                                            rotation:(CGFloat)rotation;

@end

@interface MLNMapViewGestureRecognizerTests : XCTestCase <MLNMapViewDelegate>

@property (nonatomic) MLNMapView *mapView;
@property (nonatomic) UIWindow *window;
@property (nonatomic) UIViewController *viewController;
@property (nonatomic) XCTestExpectation *styleLoadingExpectation;
@property (nonatomic) XCTestExpectation *twoFingerExpectation;
@property (nonatomic) XCTestExpectation *quickZoomExpectation;
@property (nonatomic) XCTestExpectation *doubleTapExpectation;
@property (nonatomic) XCTestExpectation *twoFingerDragExpectation;
@property (assign) CGRect screenBounds;

@end

@implementation MLNMapViewGestureRecognizerTests

- (void)setUp {
  [super setUp];

  [MLNSettings setApiKey:@"pk.feedcafedeadbeefbadebede"];
  NSURL *styleURL = [[NSBundle bundleForClass:[self class]] URLForResource:@"one-liner"
                                                             withExtension:@"json"];
  self.screenBounds = UIScreen.mainScreen.bounds;
  self.mapView = [[MLNMapView alloc] initWithFrame:self.screenBounds styleURL:styleURL];
  self.mapView.zoomLevel = 16;
  self.mapView.delegate = self;

  self.viewController = [[UIViewController alloc] init];
  self.viewController.view = [[UIView alloc] initWithFrame:self.screenBounds];
  [self.viewController.view addSubview:self.mapView];
  self.window = [[UIWindow alloc] initWithFrame:self.screenBounds];
  [self.window addSubview:self.viewController.view];
  [self.window makeKeyAndVisible];

  if (!self.mapView.style) {
    _styleLoadingExpectation =
        [self expectationWithDescription:@"Map view should finish loading style."];
    [self waitForExpectationsWithTimeout:10 handler:nil];
  }
}

- (void)mapView:(MLNMapView *)mapView didFinishLoadingStyle:(MLNStyle *)style {
  XCTAssertNotNil(mapView.style);
  XCTAssertEqual(mapView.style, style);

  [_styleLoadingExpectation fulfill];
}

- (void)testHandlePinchGestureContentInset {
  UIEdgeInsets contentInset = UIEdgeInsetsMake(1, 1, 1, 1);
  self.mapView.contentInset = contentInset;
  mln::EdgeInsets padding = MLNEdgeInsetsFromNSEdgeInsets(self.mapView.contentInset);
  auto cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(padding, cameraPadding,
                 @"MLNMapView's contentInset property should match camera's padding.");
  XCTAssertTrue(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  contentInset = UIEdgeInsetsMake(20, 20, 20, 20);
  [self.mapView setCamera:self.mapView.camera
                 withDuration:0.0
      animationTimingFunction:nil
                  edgePadding:contentInset
            completionHandler:nil];
  XCTAssertFalse(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertNotEqual(padding, cameraPadding);

  UIPinchGestureRecognizerMock *pinchGesture =
      [[UIPinchGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  pinchGesture.state = UIGestureRecognizerStateBegan;
  pinchGesture.scale = 1.0;
  [self.mapView handlePinchGesture:pinchGesture];
  XCTAssertNotEqual(padding, cameraPadding);

  mln::EdgeInsets edgePadding = MLNEdgeInsetsFromNSEdgeInsets(contentInset) + padding;

  pinchGesture.state = UIGestureRecognizerStateChanged;
  [self.mapView handlePinchGesture:pinchGesture];
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");

  pinchGesture.state = UIGestureRecognizerStateEnded;
  [self.mapView handlePinchGesture:pinchGesture];
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");
}

- (void)testHandleRotateGestureContentInset {
  UIEdgeInsets contentInset = UIEdgeInsetsMake(1, 1, 1, 1);
  self.mapView.contentInset = contentInset;
  mln::EdgeInsets padding = MLNEdgeInsetsFromNSEdgeInsets(self.mapView.contentInset);
  auto cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(padding, cameraPadding,
                 @"MLNMapView's contentInset property should match camera's padding.");
  XCTAssertTrue(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  contentInset = UIEdgeInsetsMake(20, 20, 20, 20);
  [self.mapView setCamera:self.mapView.camera
                 withDuration:0.0
      animationTimingFunction:nil
                  edgePadding:contentInset
            completionHandler:nil];
  XCTAssertFalse(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertNotEqual(padding, cameraPadding);

  UIRotationGestureRecognizerMock *rotateGesture =
      [[UIRotationGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  rotateGesture.state = UIGestureRecognizerStateBegan;
  rotateGesture.rotation = 1;
  [self.mapView handleRotateGesture:rotateGesture];
  XCTAssertNotEqual(padding, cameraPadding);

  mln::EdgeInsets edgePadding = MLNEdgeInsetsFromNSEdgeInsets(contentInset) + padding;

  rotateGesture.state = UIGestureRecognizerStateChanged;
  [self.mapView handleRotateGesture:rotateGesture];
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");

  rotateGesture.state = UIGestureRecognizerStateEnded;
  [self.mapView handleRotateGesture:rotateGesture];
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");
}

- (void)testHandleDoubleTapGestureContentInset {
  UIEdgeInsets contentInset = UIEdgeInsetsMake(1, 1, 1, 1);
  self.mapView.contentInset = contentInset;
  mln::EdgeInsets padding = MLNEdgeInsetsFromNSEdgeInsets(self.mapView.contentInset);
  auto cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(padding, cameraPadding,
                 @"MLNMapView's contentInset property should match camera's padding.");
  XCTAssertTrue(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  contentInset = UIEdgeInsetsMake(20, 20, 20, 20);
  [self.mapView setCamera:self.mapView.camera
                 withDuration:0.0
      animationTimingFunction:nil
                  edgePadding:contentInset
            completionHandler:nil];
  XCTAssertFalse(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertNotEqual(padding, cameraPadding);

  UITapGestureRecognizerMock *doubleTapGesture =
      [[UITapGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  doubleTapGesture.mockTappedView = self.mapView;
  doubleTapGesture.mockTappedPoint = CGPointMake(1.0, 1.0);

  mln::EdgeInsets edgePadding = MLNEdgeInsetsFromNSEdgeInsets(contentInset) + padding;

  [self.mapView handleDoubleTapGesture:doubleTapGesture];
  _doubleTapExpectation = [self expectationWithDescription:@"Double tap gesture animation."];

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   [self->_doubleTapExpectation fulfill];
                 });
  [self waitForExpectationsWithTimeout:10 handler:nil];

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");
}

- (void)testHandleTwoFingerTapGesture {
  UIEdgeInsets contentInset = UIEdgeInsetsMake(1, 1, 1, 1);
  self.mapView.contentInset = contentInset;
  mln::EdgeInsets padding = MLNEdgeInsetsFromNSEdgeInsets(self.mapView.contentInset);
  auto cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(padding, cameraPadding,
                 @"MLNMapView's contentInset property should match camera's padding.");
  XCTAssertTrue(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  contentInset = UIEdgeInsetsMake(20, 20, 20, 20);
  [self.mapView setCamera:self.mapView.camera
                 withDuration:0.0
      animationTimingFunction:nil
                  edgePadding:contentInset
            completionHandler:nil];
  XCTAssertFalse(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertNotEqual(padding, cameraPadding);

  UITapGestureRecognizerMock *twoFingerTap =
      [[UITapGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  twoFingerTap.mockTappedView = self.mapView;
  twoFingerTap.mockTappedPoint = CGPointMake(1.0, 1.0);

  [self.mapView handleTwoFingerTapGesture:twoFingerTap];
  _twoFingerExpectation = [self expectationWithDescription:@"Two Finger tap gesture animation."];

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   [self->_twoFingerExpectation fulfill];
                 });
  [self waitForExpectationsWithTimeout:10 handler:nil];

  mln::EdgeInsets edgePadding = MLNEdgeInsetsFromNSEdgeInsets(contentInset) + padding;
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");
}

- (void)testHandleQuickZoomGesture {
  UIEdgeInsets contentInset = UIEdgeInsetsMake(1, 1, 1, 1);
  self.mapView.contentInset = contentInset;
  mln::EdgeInsets padding = MLNEdgeInsetsFromNSEdgeInsets(self.mapView.contentInset);
  auto cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(padding, cameraPadding,
                 @"MLNMapView's contentInset property should match camera's padding.");
  XCTAssertTrue(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  contentInset = UIEdgeInsetsMake(20, 20, 20, 20);
  [self.mapView setCamera:self.mapView.camera
                 withDuration:0.0
      animationTimingFunction:nil
                  edgePadding:contentInset
            completionHandler:nil];
  XCTAssertFalse(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertNotEqual(padding, cameraPadding);

  UILongPressGestureRecognizerMock *quickZoom =
      [[UILongPressGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  quickZoom.state = UIGestureRecognizerStateBegan;
  [self.mapView handleQuickZoomGesture:quickZoom];
  XCTAssertNotEqual(padding, cameraPadding);

  quickZoom.state = UIGestureRecognizerStateChanged;
  quickZoom.mockTappedPoint =
      CGPointMake(self.mapView.frame.size.width / 2, self.mapView.frame.size.height / 2);
  [self.mapView handleQuickZoomGesture:quickZoom];
  _quickZoomExpectation = [self expectationWithDescription:@"Quick zoom gesture animation."];

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   [self->_quickZoomExpectation fulfill];
                 });
  [self waitForExpectationsWithTimeout:10 handler:nil];

  mln::EdgeInsets edgePadding = MLNEdgeInsetsFromNSEdgeInsets(contentInset) + padding;

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");

  quickZoom.state = UIGestureRecognizerStateEnded;
  [self.mapView handleQuickZoomGesture:quickZoom];
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");
}

- (void)testHandleTwoFingerDragGesture {
  UIEdgeInsets contentInset = UIEdgeInsetsMake(1, 1, 1, 1);
  self.mapView.contentInset = contentInset;
  mln::EdgeInsets padding = MLNEdgeInsetsFromNSEdgeInsets(self.mapView.contentInset);
  auto cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(padding, cameraPadding,
                 @"MLNMapView's contentInset property should match camera's padding.");
  XCTAssertTrue(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  contentInset = UIEdgeInsetsMake(20, 20, 20, 20);
  [self.mapView setCamera:self.mapView.camera
                 withDuration:0.0
      animationTimingFunction:nil
                  edgePadding:contentInset
            completionHandler:nil];
  XCTAssertFalse(UIEdgeInsetsEqualToEdgeInsets(self.mapView.contentInset, contentInset));

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertNotEqual(padding, cameraPadding);

  UIPanGestureRecognizerMock *twoFingerDrag =
      [[UIPanGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  twoFingerDrag.state = UIGestureRecognizerStateBegan;
  twoFingerDrag.firstFingerPoint =
      CGPointMake(self.mapView.frame.size.width / 3, self.mapView.frame.size.height / 2);
  twoFingerDrag.secondFingerPoint =
      CGPointMake((self.mapView.frame.size.width / 2), self.mapView.frame.size.height / 2);
  twoFingerDrag.numberOfTouches = 2;
  [self.mapView handleTwoFingerDragGesture:twoFingerDrag];
  XCTAssertNotEqual(padding, cameraPadding);

  twoFingerDrag.state = UIGestureRecognizerStateChanged;
  twoFingerDrag.firstFingerPoint =
      CGPointMake(self.mapView.frame.size.width / 3, (self.mapView.frame.size.height / 2) - 10);
  twoFingerDrag.secondFingerPoint =
      CGPointMake((self.mapView.frame.size.width / 2), (self.mapView.frame.size.height / 2) - 10);
  [self.mapView handleTwoFingerDragGesture:twoFingerDrag];
  _twoFingerDragExpectation = [self expectationWithDescription:@"Quick zoom gesture animation."];

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   [self->_twoFingerDragExpectation fulfill];
                 });
  [self waitForExpectationsWithTimeout:10 handler:nil];

  mln::EdgeInsets edgePadding = MLNEdgeInsetsFromNSEdgeInsets(contentInset) + padding;

  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");

  twoFingerDrag.state = UIGestureRecognizerStateEnded;
  [self.mapView handleTwoFingerDragGesture:twoFingerDrag];
  cameraPadding = self.mapView.mbglMap.getCameraOptions().padding;
  XCTAssertEqual(edgePadding, cameraPadding,
                 @"When a gesture recognizer is performed camera paddings must not be changed.");
}

- (void)testFoundationGestureControllerOwnsStockCameraRecognizers {
  self.mapView.foundationGestureControllerEnabled = YES;

  XCTAssertTrue(self.mapView.foundationGestureControllerEnabled);
  XCTAssertFalse(self.mapView.scrollEnabled);
  XCTAssertFalse(self.mapView.zoomEnabled);
  XCTAssertFalse(self.mapView.rotateEnabled);
  XCTAssertFalse(self.mapView.pitchEnabled);
  XCTAssertEqual(self.mapView.decelerationRate, MLNMapViewDecelerationRateImmediate);
  XCTAssertEqualWithAccuracy(self.mapView.maximumPitch, 90.0, 0.001);

  self.mapView.foundationGestureControllerEnabled = NO;
  XCTAssertTrue(self.mapView.scrollEnabled);
  XCTAssertTrue(self.mapView.zoomEnabled);
  XCTAssertTrue(self.mapView.rotateEnabled);
  XCTAssertTrue(self.mapView.pitchEnabled);
}

- (void)testFoundationGroundPlaneCoordinateUsesBothCameraAxes {
  CLLocationCoordinate2D start = CLLocationCoordinate2DMake(45.0, 0.0);
  // At heading north, lateral east and forward north must remain independent.
  CLLocationCoordinate2D northEast = [self.mapView foundationCoordinateFrom:start
                                                                      heading:0
                                                               sidewaysMeters:111320
                                                                forwardMeters:111320];
  XCTAssertEqualWithAccuracy(northEast.latitude, 46.0, 0.001);
  XCTAssertEqualWithAccuracy(northEast.longitude, 1.41421, 0.002);

  // At heading east the same two camera-space axes rotate with the view.
  CLLocationCoordinate2D southEast = [self.mapView foundationCoordinateFrom:start
                                                                      heading:90
                                                               sidewaysMeters:111320
                                                                forwardMeters:111320];
  XCTAssertEqualWithAccuracy(southEast.latitude, 44.0, 0.001);
  XCTAssertEqualWithAccuracy(southEast.longitude, 1.41421, 0.002);
}

- (void)testFoundationUnknownDEMBlocksCommittedPathButNotOptionalLookahead {
  XCTAssertTrue(mln::foundationFlightSampleRequiresDEM(0.0, 50.0));
  XCTAssertTrue(mln::foundationFlightSampleRequiresDEM(50.0, 50.0));
  XCTAssertFalse(mln::foundationFlightSampleRequiresDEM(50.01, 50.0));
  XCTAssertEqual(mln::foundationFlightLongitudinalIntervals(
                     mln::foundationFlightMaximumCommittableMeters,
                     mln::foundationFlightMaximumLookaheadMeters), 40u);
  XCTAssertEqualWithAccuracy(mln::foundationFlightLookahead(std::numeric_limits<double>::infinity()),
                             150.0, 1e-12);
}

- (void)testFoundationOrderedProfileAllowsDescentAndStopsBeforeNearRidge {
  using Sample = mln::FoundationFlightTerrainSample;
  const std::vector<Sample> flat{{0.0, 0.0}, {0.5, 0.0}, {1.0, 0.0}};
  const auto descent = mln::foundationFlightSafeTrajectory(
      true, 1000.0, 900.0, 50.0, 1.0, flat, 0.0, 0.1);
  XCTAssertEqualWithAccuracy(descent.acceptedFraction, 1.0, 1e-12);
  XCTAssertEqualWithAccuracy(descent.targetEyeMSL, 900.0, 1e-12);
  const auto climb = mln::foundationFlightSafeTrajectory(
      true, 1000.0, 1100.0, 50.0, 1.0, flat, 0.0, 0.1);
  XCTAssertEqualWithAccuracy(climb.targetEyeMSL, 1100.0, 1e-12);

  // A crest five metres into a kilometre path must stop before the crest,
  // rather than authorizing ten metres from a one-percent climb ratio.
  const std::vector<Sample> nearRidge{{0.0, 900.0}, {0.005, 1050.0}, {1.0, 900.0}};
  const auto near = mln::foundationFlightSafeTrajectory(
      true, 1000.0, 1000.0, 50.0, 1.0, nearRidge, 0.0, 0.1);
  XCTAssertEqualWithAccuracy(near.acceptedFraction, 0.0, 1e-12);
  XCTAssertEqualWithAccuracy(near.obstructionFraction, 0.005, 1e-12);

  // A distant ridge leaves the proven foreground usable.
  const std::vector<Sample> farRidge{{0.0, 900.0}, {0.5, 900.0}, {0.8, 1050.0}, {1.0, 1050.0}};
  const auto far = mln::foundationFlightSafeTrajectory(
      true, 1000.0, 1000.0, 50.0, 1.0, farRidge, 0.0, 0.1);
  XCTAssertEqualWithAccuracy(far.acceptedFraction, 0.5, 1e-12);
  XCTAssertEqualWithAccuracy(far.obstructionFraction, 0.8, 1e-12);
}

- (void)testFoundationCommitAppliesSignedEyeAltitudeAndPreservesOrientation {
  mln::Transform transform;
  transform.resize({390, 844});
  transform.jumpTo(mln::CameraOptions()
      .withCenter(mln::LatLng{52.08, 5.07})
      .withCenterAltitude(0.0)
      .withZoom(14.2)
      .withPitch(60.0)
      .withBearing(200.0));
  const double initialEye = transform.getState().getEyeAltitudeMSL();
  const double pitch = transform.getState().getPitch();
  const double bearing = transform.getState().getBearing();
  const mln::LatLng center = transform.getState().getLatLng();
  transform.jumpToFoundationFlightTarget(center, initialEye - 20.0);
  XCTAssertEqualWithAccuracy(transform.getState().getEyeAltitudeMSL(), initialEye - 20.0, 1e-6);
  XCTAssertEqualWithAccuracy(transform.getState().getPitch(), pitch, 1e-12);
  XCTAssertEqualWithAccuracy(transform.getState().getBearing(), bearing, 1e-12);
  transform.jumpToFoundationFlightTarget(center, initialEye + 20.0);
  XCTAssertEqualWithAccuracy(transform.getState().getEyeAltitudeMSL(), initialEye + 20.0, 1e-6);
  XCTAssertEqualWithAccuracy(transform.getState().getPitch(), pitch, 1e-12);
  XCTAssertEqualWithAccuracy(transform.getState().getBearing(), bearing, 1e-12);
}

- (void)testFoundationUnknownDEMStopsTravelButNotTurning {
  self.mapView.foundationGestureControllerEnabled = YES;
  XCTAssertFalse(self.mapView.terrainFlightDEMAvailable);
  CLLocationCoordinate2D before = self.mapView.centerCoordinate;

  UIPinchGestureRecognizerMock *pinch = [[UIPinchGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  pinch.state = UIGestureRecognizerStateBegan;
  pinch.scale = 1.0;
  [self.mapView handleFoundationPinchGesture:pinch];
  pinch.state = UIGestureRecognizerStateChanged;
  pinch.scale = 2.0;
  [self.mapView handleFoundationPinchGesture:pinch];
  // The renderer has no RenderTerrain for this fixture. Its explicit unknown
  // assessment must latch for this contact, rather than letting a later
  // callback creep ahead before the finger is lifted.
  XCTestExpectation *unknownAssessment = [self expectationWithDescription:@"unknown DEM assessment"];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    [unknownAssessment fulfill];
  });
  [self waitForExpectations:@[ unknownAssessment ] timeout:1.0];
  XCTAssertEqualObjects([NSString stringWithUTF8String:self.mapView.mbglMap.getFoundationFlightTelemetry().stopReason.c_str()], @"unknown_dem");
  pinch.scale = 3.0;
  [self.mapView handleFoundationPinchGesture:pinch];
  XCTAssertEqualObjects([NSString stringWithUTF8String:self.mapView.mbglMap.getFoundationFlightTelemetry().stopReason.c_str()], @"unknown_dem");
  XCTAssertEqualWithAccuracy(self.mapView.centerCoordinate.latitude, before.latitude, 1e-12);
  XCTAssertEqualWithAccuracy(self.mapView.centerCoordinate.longitude, before.longitude, 1e-12);
  pinch.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationPinchGesture:pinch];

  // A fresh contact clears only the old latch. It can submit its first target
  // and will be stopped again only when this no-terrain renderer assesses it.
  UIPinchGestureRecognizerMock *newPinch = [[UIPinchGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  newPinch.state = UIGestureRecognizerStateBegan;
  newPinch.scale = 1.0;
  [self.mapView handleFoundationPinchGesture:newPinch];
  newPinch.state = UIGestureRecognizerStateChanged;
  newPinch.scale = 1.1;
  [self.mapView handleFoundationPinchGesture:newPinch];
  XCTAssertNotEqualObjects([NSString stringWithUTF8String:self.mapView.mbglMap.getFoundationFlightTelemetry().stopReason.c_str()], @"unknown_dem");
  newPinch.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationPinchGesture:newPinch];

  UIRotationGestureRecognizerMock *rotate = [[UIRotationGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  rotate.state = UIGestureRecognizerStateBegan;
  rotate.rotation = 0;
  [self.mapView handleFoundationRotateGesture:rotate];
  CLLocationDirection heading = self.mapView.camera.heading;
  rotate.state = UIGestureRecognizerStateChanged;
  rotate.rotation = 0.5;
  [self.mapView handleFoundationRotateGesture:rotate];
  XCTestExpectation *unknownRotationSettled = [self expectationWithDescription:@"rotation arbitration settled"];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    [unknownRotationSettled fulfill];
  });
  [self waitForExpectations:@[ unknownRotationSettled ] timeout:1.0];
  XCTAssertGreaterThan(fabs(self.mapView.camera.heading - heading), 0.01);
  rotate.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationRotateGesture:rotate];
}

- (void)testFoundationPinchKeepsCumulativeTargetWhileTerrainAssessmentIsPending {
  self.mapView.foundationGestureControllerEnabled = YES;
  UIPinchGestureRecognizerMock *pinch = [[UIPinchGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  pinch.state = UIGestureRecognizerStateBegan;
  pinch.scale = 1.0;
  [self.mapView handleFoundationPinchGesture:pinch];
  pinch.state = UIGestureRecognizerStateChanged;
  pinch.scale = 1.10;
  [self.mapView handleFoundationPinchGesture:pinch];
  const double first = self.mapView.mbglMap.getFoundationFlightTelemetry().requestedDistanceMeters;
  // No renderer callback is required between these samples. The second target
  // must be contact-absolute, rather than only the 1.10 -> 1.40 delta.
  pinch.scale = 1.40;
  [self.mapView handleFoundationPinchGesture:pinch];
  const double latest = self.mapView.mbglMap.getFoundationFlightTelemetry().requestedDistanceMeters;
  XCTAssertGreaterThan(latest, first);
  XCTAssertEqualWithAccuracy(latest, log(1.40) * 250.0, 1e-9);
  pinch.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationPinchGesture:pinch];
}

- (void)testFoundationTwoFingerClassifierHasOneLifetimeOwner {
  // These are deliberately direct samples: classification is pure and must be
  // stable before a renderer/DEM result is available.
  XCTAssertEqual([self.mapView foundationTwoFingerModeForTranslation:CGPointZero scale:1.0 rotation:0], 0u);
  XCTAssertEqual([self.mapView foundationTwoFingerModeForTranslation:CGPointZero scale:1.10 rotation:0], 1u);
  XCTAssertEqual([self.mapView foundationTwoFingerModeForTranslation:CGPointZero scale:1.0 rotation:0.20], 2u);
  XCTAssertEqual([self.mapView foundationTwoFingerModeForTranslation:CGPointMake(0, 20) scale:1.0 rotation:0], 3u);
}

- (void)testFoundationTwoFingerPinchDoesNotCrossTalkIntoOrientation {
  self.mapView.foundationGestureControllerEnabled = YES;
  MLNMapCamera *camera = [self.mapView.camera copy];
  camera.heading = 200;
  camera.pitch = 42;
  self.mapView.camera = camera;
  const CLLocationDirection heading = self.mapView.camera.heading;
  const CLLocationDirection pitch = self.mapView.camera.pitch;
  const double zoom = self.mapView.zoomLevel;

  UIPinchGestureRecognizerMock *gesture = [[UIPinchGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  gesture.state = UIGestureRecognizerStateBegan;
  gesture.scale = 1.0;
  [self.mapView handleFoundationPinchGesture:gesture];

  // Pinch wins first; later rotation samples from the same contact pair may
  // report their metric but cannot replace the latched owner.
  gesture.scale = 1.5;
  gesture.state = UIGestureRecognizerStateChanged;
  [self.mapView handleFoundationPinchGesture:gesture];
  UIRotationGestureRecognizerMock *rotation = [[UIRotationGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  rotation.state = UIGestureRecognizerStateBegan;
  rotation.rotation = 0;
  [self.mapView handleFoundationRotateGesture:rotation];
  rotation.state = UIGestureRecognizerStateChanged;
  rotation.rotation = 0.75;
  [self.mapView handleFoundationRotateGesture:rotation];

  XCTAssertEqualWithAccuracy(self.mapView.camera.heading, heading, 1e-12);
  XCTAssertEqualWithAccuracy(self.mapView.camera.pitch, pitch, 1e-12);
  XCTAssertEqualWithAccuracy(self.mapView.zoomLevel, zoom, 1e-12);
  rotation.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationRotateGesture:rotation];
  gesture.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationPinchGesture:gesture];
}

- (void)testFoundationTwoFingerRotateChangesOnlyHeading {
  self.mapView.foundationGestureControllerEnabled = YES;
  const CLLocationCoordinate2D center = self.mapView.centerCoordinate;
  const CLLocationDirection heading = self.mapView.camera.heading;
  const CLLocationDirection pitch = self.mapView.camera.pitch;
  const double zoom = self.mapView.zoomLevel;

  UIRotationGestureRecognizerMock *gesture = [[UIRotationGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  gesture.state = UIGestureRecognizerStateBegan;
  gesture.rotation = 0;
  [self.mapView handleFoundationRotateGesture:gesture];
  // A native rotation sampler has no scale or centroid translation metric.
  gesture.rotation = M_PI_2;
  gesture.state = UIGestureRecognizerStateChanged;
  [self.mapView handleFoundationRotateGesture:gesture];
  XCTestExpectation *rotationSettled = [self expectationWithDescription:@"rotation arbitration settled"];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    [rotationSettled fulfill];
  });
  [self waitForExpectations:@[ rotationSettled ] timeout:1.0];

  XCTAssertGreaterThan(fabs(self.mapView.camera.heading - heading), 0.01);
  XCTAssertEqualWithAccuracy(self.mapView.camera.pitch, pitch, 1e-12);
  XCTAssertEqualWithAccuracy(self.mapView.zoomLevel, zoom, 1e-12);
  XCTAssertEqualWithAccuracy(self.mapView.centerCoordinate.latitude, center.latitude, 1e-12);
  XCTAssertEqualWithAccuracy(self.mapView.centerCoordinate.longitude, center.longitude, 1e-12);
  gesture.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationRotateGesture:gesture];
}

- (void)testFoundationLatePinchEvidenceRestoresProvisionalHeading {
  self.mapView.foundationGestureControllerEnabled = YES;
  MLNMapCamera *camera = [self.mapView.camera copy];
  camera.heading = 200;
  self.mapView.camera = camera;

  UIRotationGestureRecognizerMock *rotation = [[UIRotationGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  rotation.state = UIGestureRecognizerStateBegan;
  rotation.rotation = 0;
  [self.mapView handleFoundationRotateGesture:rotation];
  rotation.state = UIGestureRecognizerStateChanged;
  rotation.rotation = 0.4;
  [self.mapView handleFoundationRotateGesture:rotation];
  XCTestExpectation *provisional = [self expectationWithDescription:@"provisional rotation"];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    [provisional fulfill];
  });
  [self waitForExpectations:@[ provisional ] timeout:1.0];
  XCTAssertGreaterThan(fabs(self.mapView.camera.heading - 200), 0.01);

  UIPinchGestureRecognizerMock *pinch = [[UIPinchGestureRecognizerMock alloc] initWithTarget:nil action:nil];
  pinch.state = UIGestureRecognizerStateBegan;
  pinch.scale = 1.0;
  [self.mapView handleFoundationPinchGesture:pinch];
  pinch.state = UIGestureRecognizerStateChanged;
  pinch.scale = 1.10;
  [self.mapView handleFoundationPinchGesture:pinch];
  XCTAssertEqualWithAccuracy(self.mapView.camera.heading, 200, 1e-9);

  rotation.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationRotateGesture:rotation];
  pinch.state = UIGestureRecognizerStateEnded;
  [self.mapView handleFoundationPinchGesture:pinch];
}

@end
