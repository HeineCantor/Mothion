// Tests for the Qt-free core (QtTest is only the test runner here).

#include "core/PressureCurve.h"
#include "core/RateMeter.h"
#include "core/StrokeSmoother.h"

#include <QTest>

using namespace mothion;

class TestCore : public QObject
{
    Q_OBJECT

private slots:
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
