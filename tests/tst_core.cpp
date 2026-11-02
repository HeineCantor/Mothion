// Tests for the Qt-free core (QtTest is only the test runner here).

#include "core/PressureCurve.h"
#include "core/RateMeter.h"
#include "core/StrokeSmoother.h"
#include "core/fill/FloodFill.h"

#include <QTest>

using namespace mothion;

class TestCore : public QObject
{
    Q_OBJECT

private slots:
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
