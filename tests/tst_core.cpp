// Tests for the Qt-free core (QtTest is only the test runner here).

#include "core/PressureCurve.h"
#include "core/RateMeter.h"
#include "core/StrokeSmoother.h"
#include "core/fill/FloodFill.h"
#include "core/perspective/PerspectiveGrid.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace mothion;

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    // The perspective grid: facing the viewer it is a flat square grid; tilted
    // its far side shrinks towards the horizon; moved away it gets smaller,
    // moved aside it shifts; nothing behind the eye.
    void perspectiveGridProjects()
    {
        // Rotations: right-handed in a space with y down, so x turns to y (clockwise on screen).
        const perspective::Vec3 turned = perspective::rotate(perspective::axisAngle({0, 0, 1}, 90.0), {1, 0, 0});
        QVERIFY(qAbs(turned.x) < 1e-12 && qAbs(turned.y - 1.0) < 1e-12 && qAbs(turned.z) < 1e-12);
        const perspective::Rotation a = perspective::axisAngle({0, 0, 1}, 30.0);
        const perspective::Rotation twice = a * a, sixty = perspective::axisAngle({0, 0, 1}, 60.0);
        QVERIFY(qAbs(twice.w - sixty.w) < 1e-12 && qAbs(twice.z - sixty.z) < 1e-12);
        const perspective::Vec3 tipped = perspective::rotate(perspective::fromAngles(0, 90, 0), {0, -1, 0});
        QVERIFY(qAbs(tipped.z - 1.0) < 1e-12); // tilted 90°, the top lies away

        perspective::Grid g = perspective::defaultGrid(1000, 600);
        g.orientation = {}; // facing
        g.cell = 50.0;
        g.cells = 2;
        QCOMPARE(g.originX, 500.0);
        const auto c = perspective::project(g, 0, 0);
        QVERIFY(c && qAbs(c->x - 500.0) < 1e-9 && qAbs(c->y - 300.0) < 1e-9);
        const auto corner = perspective::project(g, 100, -50); // facing, on the picture: 1 unit = 1 px
        QVERIFY(qAbs(corner->x - 600.0) < 1e-9 && qAbs(corner->y - 250.0) < 1e-9);
        QCOMPARE(perspective::gridSegments(g).size(), std::size_t(10)); // 5 lines each way
        QVERIFY(!perspective::horizonLine(g, 100.0));                  // at infinity

        // Moved: aside it shifts, away it shrinks around where the camera looks.
        const double focal = perspective::focalLength(g);
        QVERIFY(qAbs(focal - 500.0 / std::tan(std::numbers::pi / 6.0)) < 1e-9);
        g.position = {200, 0, 0};
        QVERIFY(qAbs(perspective::project(g, 0, 0)->x - 700.0) < 1e-9);
        g.position = {0, 0, focal}; // twice as far
        const auto far2 = perspective::project(g, 100, -50);
        QVERIFY(qAbs(far2->x - 550.0) < 1e-9 && qAbs(far2->y - 275.0) < 1e-9);
        g.position = {};

        g.orientation = perspective::fromAngles(0, 60, 0);
        const auto top = perspective::project(g, 0, -100), bottom = perspective::project(g, 0, 100);
        QVERIFY(top->y > 200.0 && bottom->y < 400.0); // foreshortened, both halves
        QVERIFY(300.0 - top->y < bottom->y - 300.0);  // the far one more than the near one
        const auto left = perspective::project(g, -100, -100), right = perspective::project(g, 100, -100);
        QVERIFY(right->x - left->x < 200.0); // the far side is narrower
        const auto h = perspective::horizonLine(g, 100.0);
        QVERIFY(h);
        const double horizonY = 300.0 - focal / std::tan(std::numbers::pi / 3.0);
        QVERIFY(qAbs(h->a.y - horizonY) < 1e-9 && qAbs(h->b.y - horizonY) < 1e-9);
        QVERIFY(qAbs(std::abs(h->b.x - h->a.x) - 200.0) < 1e-9);
        // Far points of the plane fall towards the horizon, never past it.
        const auto far = perspective::project(g, 0, -1e6);
        QVERIFY(far->y > horizonY && far->y - horizonY < 1.0);
        // Moving the plane does not move its horizon.
        g.position = {300, -120, 500};
        const auto same = perspective::horizonLine(g, 100.0);
        QVERIFY(qAbs(same->a.y - horizonY) < 1e-9);
        g.position = {};

        // Rolled a quarter turn the horizon stands upright.
        g.orientation = perspective::fromAngles(0, 60, 90);
        const auto upright = perspective::horizonLine(g, 100.0);
        QVERIFY(qAbs(upright->a.x - upright->b.x) < 1e-9);

        // Near the eye a line is cut where it would pass behind it.
        g.orientation = perspective::fromAngles(0, 80, 0);
        g.cells = 40;
        g.cell = 200.0; // up to 8000 px towards the eye: far behind it
        for (const perspective::Segment &s : perspective::gridSegments(g))
            QVERIFY(std::isfinite(s.a.x) && std::isfinite(s.a.y) && std::isfinite(s.b.x) && std::isfinite(s.b.y));
        QVERIFY(!perspective::project(g, 0, 8000));
    }

    // A closed outline stops the fill; growing slips it under the line.
    void floodFillStopsAtLinesAndGrows()
    {
        constexpr int w = 20, h = 20;
        std::vector<mothion::doc::Pixel> px(w * h, 0);
        for (int i = 5; i <= 14; ++i) { // a square outline, 5..14
            px[5 * w + i] = px[14 * w + i] = 0xff000000;
            px[i * w + 5] = px[i * w + 14] = 0xff000000;
        }
        auto inside = mothion::fill::floodRegion(px.data(), w, h, 10, 10, 10);
        QCOMPARE(inside.at(10, 10), 255);
        QCOMPARE(inside.at(6, 6), 255);
        QCOMPARE(inside.at(5, 10), 0); // the line
        QCOMPARE(inside.at(2, 2), 0);  // outside
        QCOMPARE(inside.left, 6);
        QCOMPARE(inside.bottom, 13);
        mothion::fill::grow(inside, 1);
        QCOMPARE(inside.at(5, 10), 255);
        QCOMPARE(inside.at(4, 10), 0);

        auto outside = mothion::fill::floodRegion(px.data(), w, h, 0, 0, 10);
        QCOMPARE(outside.at(19, 19), 255);
        QCOMPARE(outside.at(10, 10), 0);

        px[5 * w + 10] = 0x40000000; // a faint gap: within tolerance it leaks
        QCOMPARE(mothion::fill::floodRegion(px.data(), w, h, 10, 10, 0x40).at(0, 0), 255);
        QCOMPARE(mothion::fill::floodRegion(px.data(), w, h, 10, 10, 0x3f).at(0, 0), 0);
        QVERIFY(mothion::fill::floodRegion(px.data(), w, h, -1, 0, 10).isEmpty());
    }

    void linearCurveIsIdentity()
    {
        const PressureCurve curve;
        for (float p : {0.0f, 0.1f, 0.25f, 0.5f, 0.9f, 1.0f})
            QVERIFY(qAbs(curve.map(p) - p) < 0.01f);
    }

    void curveKeepsEndpointsAndClamps()
    {
        const PressureCurve soft({0.0f, 0.6f}, {0.4f, 1.0f});
        QCOMPARE(soft.map(0.0f), 0.0f);
        QVERIFY(qAbs(soft.map(1.0f) - 1.0f) < 1e-4f);
        QCOMPARE(soft.map(-1.0f), soft.map(0.0f));
        QCOMPARE(soft.map(2.0f), soft.map(1.0f));
        QVERIFY(soft.map(0.3f) > 0.3f); // "soft" curve lifts light pressure
    }

    void curveIsMonotonic()
    {
        const PressureCurve hard({0.7f, 0.0f}, {1.0f, 0.3f});
        float previous = 0.0f;
        for (int i = 0; i <= 100; ++i) {
            const float v = hard.map(i / 100.0f);
            QVERIFY(v >= previous - 1e-6f);
            previous = v;
        }
    }

    void smootherPassesFirstSampleAndLags()
    {
        StrokeSmoother smoother;
        smoother.setStrength(0.5);
        PenSample a;
        a.x = 0;
        a.pressure = 0.0f;
        QCOMPARE(smoother.filter(a).x, 0.0);
        PenSample b;
        b.x = 10;
        b.pressure = 1.0f;
        const PenSample out = smoother.filter(b);
        QCOMPARE(out.x, 5.0);
        QCOMPARE(out.pressure, 0.5f);

        smoother.reset();
        QCOMPARE(smoother.filter(b).x, 10.0);
    }

    void zeroStrengthIsPassThrough()
    {
        StrokeSmoother smoother;
        smoother.setStrength(0.0);
        PenSample s;
        smoother.filter(s);
        s.x = 7;
        s.y = 3;
        QCOMPARE(smoother.filter(s).x, 7.0);
    }

    void rateMeterCountsSlidingWindow()
    {
        RateMeter meter(1.0);
        for (int i = 0; i < 200; ++i)
            meter.add(i * 0.005); // 200 Hz for one second
        QCOMPARE(meter.rateHz(0.999), 200.0);
        QCOMPARE(meter.rateHz(1.5), 100.0); // the first half fell out of the window
        QCOMPARE(meter.rateHz(10.0), 0.0);
    }
};

QTEST_APPLESS_MAIN(TestCore)
#include "tst_core.moc"
