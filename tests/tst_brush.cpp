// Tests for the Qt-free brush engine core (QtTest is only the test runner here).
//
// Reference values come from two real .sut brushes ("soft circle", "milk"):
// their curves are copied here as numbers, the files themselves stay out of
// the repository.

#include "core/brush/DabGenerator.h"
#include "core/brush/Effector.h"
#include "core/brush/Hardness.h"
#include "core/brush/VelocityMeter.h"

#include <QTest>

#include <cmath>

using namespace mothion::brush;

namespace {

std::vector<std::uint8_t> fromHex(const char *hex)
{
    std::vector<std::uint8_t> out;
    for (const char *p = hex; p[0] && p[1]; p += 2)
        out.push_back(std::uint8_t(std::stoi(std::string(p, 2), nullptr, 16)));
    return out;
}

// BrushSizeEffector of "soft circle": pressure (linear) and velocity 100% → 300%.
const char *kSoftCircleSizeEffector = "00000030000001f0000000300000000000000064000000000000000000000000"
                                      "0000002c0000005c0000012c000000000000000c000000020000001000000000"
                                      "0000000000000000000000003ff00000000000003ff00000000000000000000c"
                                      "000000050000001000000000000000003ff00000000000003fc3c8253c8253c8"
                                      "3fd4f2094f2094f23fe000000000000000000000000000003fe6666666666666"
                                      "00000000000000003ff00000000000000000000000000000";

BrushInput at(double x, double y, double pressure = 1.0)
{
    BrushInput in;
    in.x = x;
    in.y = y;
    in.pressure = pressure;
    return in;
}

// A plain hard round brush with constant size and spacing.
BrushStyle constantStyle(double radius, double spacingFraction)
{
    BrushStyle s;
    s.size = radius;
    s.sizeEffector = Effector{};
    s.spacing = spacingFraction;
    return s;
}

} // namespace

class TestBrush : public QObject
{
    Q_OBJECT

private slots:
    void lcgMatchesReferenceSequence()
    {
        Lcg rng(1);
        QCOMPARE(rng.next01(), 0.514434814453125);
        QCOMPARE(rng.next01(), 0.464874267578125);
        QCOMPARE(rng.next01(), 0.79351806640625);
    }

    void twoPointCurveIsALine()
    {
        const EffectorCurve curve({{0.0, 0.4727272727272727}, {1.0, 1.0}}); // milk's size curve
        QCOMPARE(curve.map(0.0), 0.4727272727272727);
        QCOMPARE(curve.map(1.0), 1.0);
        QVERIFY(qAbs(curve.map(0.5) - 0.7363636363636363) < 1e-12);
        QCOMPARE(curve.map(-1.0), curve.map(0.0)); // clamped
    }

    void midpointSplineMatchesReference()
    {
        // Soft circle's velocity curve; expected values from sut_reader.py.
        const EffectorCurve curve({{0.0, 1.0}, {0.15454545454545454, 0.32727272727272727}, {0.5, 0.0},
                                   {0.7, 0.0}, {1.0, 0.0}});
        QVERIFY(qAbs(curve.map(0.0) - 1.0) < 1e-12);
        QVERIFY(qAbs(curve.map(0.05) - 0.7974596215561361) < 1e-9);
        QVERIFY(qAbs(curve.map(0.1) - 0.6240395572580115) < 1e-9);
        QVERIFY(qAbs(curve.map(0.3) - 0.19278159291614058) < 1e-9);
        QVERIFY(qAbs(curve.map(0.6)) < 1e-12);
        QVERIFY(qAbs(curve.map(1.0)) < 1e-12);
    }

    void parsesRealEffectorBlob()
    {
        const auto blob = fromHex(kSoftCircleSizeEffector);
        const std::optional<Effector> e = Effector::parse(blob);
        QVERIFY(e.has_value());
        QCOMPARE(e->available, 0x1F0u);
        QCOMPARE(e->inputs(), std::uint32_t(InputPressure | InputVelocity));
        QCOMPARE(e->velocityFrom, 1.0);
        QCOMPARE(e->velocityTo, 3.0);
        QCOMPARE(int(e->pressure.points().size()), 2);
        QCOMPARE(int(e->velocity.points().size()), 5);
        QCOMPARE(e->velocity.points()[1].x, 0.15454545454545454);

        // Size 300: slow strokes swell to 3×, fast ones stay at the base size.
        BrushInput in;
        in.pressure = 1.0;
        in.velocity = 0.0;
        QVERIFY(qAbs(e->evaluate(300.0, in, nullptr) - 900.0) < 1e-9);
        in.velocity = 1.0;
        QVERIFY(qAbs(e->evaluate(300.0, in, nullptr) - 300.0) < 1e-9);
        in.pressure = 0.5;
        QVERIFY(qAbs(e->evaluate(300.0, in, nullptr) - 150.0) < 1e-9);
    }

    void rejectsTruncatedBlob()
    {
        auto blob = fromHex(kSoftCircleSizeEffector);
        blob.resize(100);
        QVERIFY(!Effector::parse(blob).has_value());
        QVERIFY(!Effector::parse({}).has_value());
    }

    void randomInputNeedsAGenerator()
    {
        Effector e;
        e.active = InputRandom;
        BrushInput in;
        QCOMPARE(e.evaluate(10.0, in, nullptr), 10.0);
        Lcg rng(1);
        QCOMPARE(e.evaluate(10.0, in, &rng), 10.0 * 0.514434814453125);
    }

    void constantSpacingAlongALine()
    {
        DabGenerator gen(constantStyle(5.0, 0.5)); // 5 px spacing
        QCOMPARE(gen.begin(at(0, 0)).size(), std::size_t(1));
        const auto dabs = gen.continueTo(at(20, 0));
        QCOMPARE(dabs.size(), std::size_t(3)); // 5, 10, 15 (20 is the next segment's)
        for (std::size_t i = 0; i < dabs.size(); ++i)
            QVERIFY(qAbs(dabs[i].x - 5.0 * double(i + 1)) < 1e-9);
    }

    void spacingCarriesOverSegments()
    {
        DabGenerator gen(constantStyle(5.0, 0.5));
        gen.begin(at(0, 0));
        std::vector<double> xs;
        for (double x : {3.0, 7.0, 12.0, 23.0})
            for (const BrushDab &d : gen.continueTo(at(x, 0)))
                xs.push_back(d.x);
        QCOMPARE(xs.size(), std::size_t(4));
        for (std::size_t i = 0; i < xs.size(); ++i)
            QVERIFY(qAbs(xs[i] - 5.0 * double(i + 1)) < 1e-9);
    }

    void pressureScalesTheRadius()
    {
        BrushStyle s = constantStyle(10.0, 0.1);
        s.sizeEffector = Effector::pressureOnly(EffectorCurve({{0.0, 0.4727272727272727}, {1.0, 1.0}}));
        DabGenerator gen(s);
        QVERIFY(qAbs(gen.computeDab(at(0, 0, 1.0), false).dab.radius - 10.0) < 1e-9);
        QVERIFY(qAbs(gen.computeDab(at(0, 0, 0.0), false).dab.radius - 4.727272727272727) < 1e-9);
    }

    void softDabsGrowToHoldTheirEdge()
    {
        BrushStyle s = constantStyle(10.0, 0.5);
        s.hardness = 0.8;
        DabGenerator gen(s);
        QVERIFY(qAbs(gen.computeDab(at(0, 0), false).dab.radius - 11.0) < 1e-9);
    }

    void sameSeedSameDabs()
    {
        BrushStyle s = constantStyle(8.0, 0.5);
        s.spacingEffector.active = InputRandom; // milk: random spacing
        s.tipCount = 6;
        s.tipOrder = TipOrder::Random;
        s.rotationMode = RotationOn | RotationDirection;
        s.rotation = 90.0;
        auto run = [&](std::uint32_t seed) {
            DabGenerator gen(s, seed);
            std::vector<BrushDab> all = gen.begin(at(0, 0));
            for (int i = 1; i <= 10; ++i)
                for (const BrushDab &d : gen.continueTo(at(30.0 * i, 10.0 * std::sin(i))))
                    all.push_back(d);
            return all;
        };
        const auto a = run(7);
        const auto b = run(7);
        const auto c = run(8);
        QCOMPARE(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].x, b[i].x);
            QCOMPARE(a[i].tip, b[i].tip);
            QVERIFY(a[i].tip >= 0 && a[i].tip < 6);
        }
        bool differs = a.size() != c.size();
        for (std::size_t i = 0; !differs && i < a.size(); ++i)
            differs = a[i].x != c[i].x || a[i].tip != c[i].tip;
        QVERIFY(differs);
    }

    void rotationFollowsTheStroke()
    {
        BrushStyle s = constantStyle(4.0, 0.5);
        s.rotationMode = RotationOn | RotationDirection; // milk's mode 0x43
        s.rotation = 90.0;
        DabGenerator gen(s);
        gen.begin(at(0, 0));
        const auto down = gen.continueTo(at(0, 20)); // y grows downwards: 90°
        QVERIFY(!down.empty());
        QVERIFY(qAbs(down.back().angle - 180.0) < 1e-9);
    }

    void tipOrders()
    {
        BrushStyle s = constantStyle(4.0, 1.0);
        s.tipCount = 3;
        s.tipOrder = TipOrder::PingPong;
        DabGenerator gen(s);
        std::vector<int> tips;
        for (const BrushDab &d : gen.begin(at(0, 0)))
            tips.push_back(d.tip);
        for (const BrushDab &d : gen.continueTo(at(49, 0)))
            tips.push_back(d.tip);
        QCOMPARE(tips, (std::vector<int>{0, 1, 2, 1, 0, 1, 2}));

        s.tipOrder = TipOrder::Once;
        DabGenerator once(s);
        std::size_t drawn = once.begin(at(0, 0)).size();
        drawn += once.continueTo(at(49, 0)).size();
        QCOMPARE(drawn, std::size_t(3));
    }

    void sprayScattersParticlesAroundThePoint()
    {
        BrushStyle s = constantStyle(10.0, 1.0);
        s.spray = true;
        s.spraySize = 1.0; // synced: the spray disc is as wide as the dab
        s.sprayDensity = 5.0;
        DabGenerator gen(s, 3);
        const auto dabs = gen.begin(at(100, 100));
        QCOMPARE(dabs.size(), std::size_t(5));
        for (const BrushDab &d : dabs)
            QVERIFY(std::hypot(d.x - 100.0, d.y - 100.0) <= 10.0 + 1e-9);
    }

    void hardnessProfile()
    {
        QCOMPARE(hardnessCoverage(0.0, 0.5), 1.0);
        QCOMPARE(hardnessCoverage(1.0, 0.5), 0.0);
        QCOMPARE(hardnessCoverage(0.99, 1.0), 1.0);
        double previous = 1.0;
        for (int i = 0; i <= 100; ++i) {
            const double c = hardnessCoverage(i / 100.0, 0.3);
            QVERIFY(c <= previous + 1e-12);
            QVERIFY(c >= 0.0 && c <= 1.0);
            previous = c;
        }
        // The two halves of the S meet: no jump in the middle of the falloff.
        const double inner = 0.8 * 1.3 - 0.3;
        const double knee = 1.0 - (1.0 - inner) * 0.5;
        QVERIFY(qAbs(hardnessCoverage(knee - 1e-9, 0.8) - hardnessCoverage(knee + 1e-9, 0.8)) < 1e-6);
    }

    void velocityFromDistanceOverTime()
    {
        VelocityMeter meter(2);
        QCOMPARE(meter.sample(0.000, 0, 0), 0.0);
        meter.sample(0.010, 40, 0);                            // 4 px/ms
        QVERIFY(qAbs(meter.sample(0.020, 80, 0) - 0.5) < 1e-9); // 8 px/ms reads as 1
        QCOMPARE(meter.sample(0.021, 500, 0), 1.0);             // capped
    }
};

QTEST_GUILESS_MAIN(TestBrush)
#include "tst_brush.moc"
