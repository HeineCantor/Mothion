#include "core/document/Camera.h"

#include <QtTest>

#include <cmath>

using namespace mothion::doc;

namespace {

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) < tolerance;
}

} // namespace

class TestCamera : public QObject
{
    Q_OBJECT

private slots:
    void easingEndsAreFixedAndLinearIsStraight()
    {
        for (const Easing e : {Easing::linear(), Easing::easeIn(), Easing::easeOut(), Easing::easeInOut(),
                               Easing::bezier(0.1, 0.9, 0.9, 0.1)}) {
            QVERIFY(near(e.apply(0.0), 0.0));
            QVERIFY(near(e.apply(1.0), 1.0));
        }
        QVERIFY(near(Easing::linear().apply(0.3), 0.3));
        QVERIFY(near(Easing::hold().apply(0.99), 0.0)); // held until the next key
        QVERIFY(near(Easing::hold().apply(1.0), 1.0));
    }

    void bezierCurvesBehaveLikeTheirNames()
    {
        QVERIFY(near(Easing::easeInOut().apply(0.5), 0.5, 1e-4)); // symmetric
        QVERIFY(Easing::easeIn().apply(0.25) < 0.25);              // slow at first
        QVERIFY(Easing::easeOut().apply(0.25) > 0.25);             // fast at first
        // Monotonic for a curve that does not overshoot.
        double last = 0;
        for (int i = 1; i <= 100; ++i) {
            const double v = Easing::easeInOut().apply(i / 100.0);
            QVERIFY(v >= last - 1e-12);
            last = v;
        }
        // CSS's "ease": cubic-bezier(0.25, 0.1, 0.25, 1) is 0.8024 at half way.
        QVERIFY(near(Easing::bezier(0.25, 0.1, 0.25, 1.0).apply(0.5), 0.8024, 1e-3));
        // A curve may overshoot (y beyond 1): the value goes past the next key and comes back.
        QVERIFY(Easing::bezier(0.3, 1.8, 0.7, 1.0).apply(0.4) > 1.0);
    }

    void aCameraStaysWhereItsKeysPutIt()
    {
        Camera camera;
        QCOMPARE(camera.poseAt(5), CameraPose()); // an empty one
        camera.setKey(10, {100, 200, 2, 30});
        QCOMPARE(camera.poseAt(0).centerX, 100.0); // before the first key
        QCOMPARE(camera.poseAt(10).zoom, 2.0);
        QCOMPARE(camera.poseAt(500).rotation, 30.0); // after the last
    }

    void positionAndRotationGoStraightAndZoomByRatios()
    {
        Camera camera;
        camera.setKey(0, {0, 100, 1, 0}, Easing::linear());
        camera.setKey(10, {100, 300, 4, 360}, Easing::linear());
        const CameraPose mid = camera.poseAt(5);
        QVERIFY(near(mid.centerX, 50));
        QVERIFY(near(mid.centerY, 200));
        QVERIFY(near(mid.rotation, 180)); // a full turn does not wrap: 360 is further than 0
        QVERIFY(near(mid.zoom, 2)); // 1 to 4 by ratios: 2 halfway, not 2.5
        QVERIFY(near(camera.poseAt(10).zoom, 4));
        QVERIFY(near(camera.poseAt(2).zoom, std::pow(4.0, 0.2)));
    }

    void aSegmentGoesAlongTheEasingOfItsFirstKey()
    {
        Camera camera;
        camera.setKey(0, {0, 0, 1, 0}, Easing::hold());
        camera.setKey(10, {100, 0, 1, 0}, Easing::linear());
        camera.setKey(20, {200, 0, 1, 0});
        QCOMPARE(camera.poseAt(9).centerX, 0.0);  // held until the next key
        QCOMPARE(camera.poseAt(10).centerX, 100.0);
        QVERIFY(near(camera.poseAt(15).centerX, 150)); // straight on
        // Eased: the same time, less distance at the start.
        camera.setEasing(10, Easing::easeIn());
        QVERIFY(camera.poseAt(12).centerX < 120);
        QVERIFY(!camera.setEasing(11, Easing::linear())); // no key there
    }

    void keysAreKeptOneForEachFrameInOrder()
    {
        Camera camera;
        camera.setKey(20, {20, 0, 1, 0});
        camera.setKey(5, {5, 0, 1, 0});
        camera.setKey(12, {12, 0, 1, 0});
        camera.setKey(12, {99, 0, 1, 0}); // replaces
        QCOMPARE(camera.keys().size(), size_t(3));
        QCOMPARE(camera.keys()[0].frame, 5);
        QCOMPARE(camera.keys()[1].pose.centerX, 99.0);
        QCOMPARE(camera.keys()[2].frame, 20);
        // A replaced key keeps its easing unless another is given.
        camera.setEasing(12, Easing::hold());
        camera.setKey(12, {7, 0, 1, 0});
        QCOMPARE(camera.keyAt(12)->easing, Easing::hold());
        camera.setKey(12, {7, 0, 1, 0}, Easing::linear());
        QCOMPARE(camera.keyAt(12)->easing, Easing::linear());
        QVERIFY(camera.keyAt(13) == nullptr);

        QVERIFY(camera.moveKey(12, 30));
        QCOMPARE(camera.keys().back().frame, 30);
        QVERIFY(!camera.moveKey(5, 20)); // taken
        QVERIFY(!camera.moveKey(6, 7));  // nothing at 6
        QVERIFY(camera.removeKey(20));
        QVERIFY(!camera.removeKey(20));
        QCOMPARE(camera.keys().size(), size_t(2));
        camera.setKey(-4, {}); // no frames before the first
        QCOMPARE(camera.keys().front().frame, 0);
    }

    void aPoseThatShowsTheWholeSheetLeavesItAsItIs()
    {
        const CameraPose pose = CameraPose::wholeSheet(1920, 1080);
        const Matrix2x3 m = pose.sheetToPicture(1920, 1080);
        double x = 0, y = 0;
        m.map(300, 700, x, y);
        QVERIFY(near(x, 300) && near(y, 700));
        const auto frame = pose.frame(1920, 1080);
        QVERIFY(near(frame[0][0], 0) && near(frame[0][1], 0));
        QVERIFY(near(frame[2][0], 1920) && near(frame[2][1], 1080));
    }

    void zoomAndCenterSelectTheSheetsPart()
    {
        CameraPose pose{500, 400, 2, 0}; // twice as close, looking at (500, 400)
        const Matrix2x3 m = pose.sheetToPicture(1000, 800);
        double x = 0, y = 0;
        m.map(500, 400, x, y);
        QVERIFY(near(x, 500) && near(y, 400)); // its centre is the picture's
        m.map(600, 400, x, y);
        QVERIFY(near(x, 700)); // 100 sheet px away are 200 picture px away
        const auto frame = pose.frame(1000, 800); // it sees half the width and height
        QVERIFY(near(frame[0][0], 250) && near(frame[0][1], 200));
        QVERIFY(near(frame[2][0], 750) && near(frame[2][1], 600));
    }

    void aCameraTurnedClockwiseTurnsThePictureTheOtherWay()
    {
        CameraPose pose{500, 400, 1, 90}; // the camera turned a quarter clockwise
        const Matrix2x3 m = pose.sheetToPicture(1000, 800);
        double x = 0, y = 0;
        m.map(510, 400, x, y); // a point to the right of its centre...
        QVERIFY(near(x, 500) && near(y, 390)); // ...is above the picture's centre: the camera's up
        // The frame's corners are the picture's corners carried back, and map to them.
        const auto frame = pose.frame(1000, 800);
        m.map(frame[1][0], frame[1][1], x, y);
        QVERIFY(near(x, 1000) && near(y, 0));
    }

    void aMatrixCanBeInvertedAndChained()
    {
        const Matrix2x3 m = CameraPose{123, 456, 1.7, 33}.sheetToPicture(640, 360);
        const Matrix2x3 identity = m.then(m.inverted());
        double x = 0, y = 0;
        identity.map(77, -12, x, y);
        QVERIFY(near(x, 77, 1e-9) && near(y, -12, 1e-9));
        Matrix2x3 singular{0, 0, 0, 0, 5, 5};
        const Matrix2x3 back = singular.inverted();
        QCOMPARE(back.a, 1.0); // not invertible: identity
    }

    void aCameraThatShowsTheWholeSheetStartsWithOneKey()
    {
        const Camera camera = Camera::wholeSheet(800, 600);
        QCOMPARE(camera.keys().size(), size_t(1));
        QCOMPARE(camera.keys()[0].frame, 0);
        QCOMPARE(camera.poseAt(0), CameraPose::wholeSheet(800, 600));
        QVERIFY(camera.visible);
    }
};

QTEST_APPLESS_MAIN(TestCamera)
#include "tst_camera.moc"
