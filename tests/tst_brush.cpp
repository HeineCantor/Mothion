// Tests for the Qt-free brush engine core (QtTest is only the test runner here).
//
// Reference values come from two real .sut brushes ("soft circle", "milk"):
// their curves are copied here as numbers, the files themselves stay out of
// the repository.

#include "core/brush/DabGenerator.h"
#include "core/brush/Effector.h"
#include "core/brush/Hardness.h"
#include "core/brush/StrokeShaper.h"
#include "core/brush/VelocityMeter.h"

#include <QTest>

#include <cmath>
#include <numbers>

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

    // --- stroke shaping -------------------------------------------------------

    void startAndEndZonesTaperToZero()
    {
        InOutStyle io;
        io.in = io.out = true;
        io.inLength = 20.0;
        io.outLength = 40.0;
        QCOMPARE(inOutFactor(io, 0.0, 100.0), 0.0);
        QVERIFY(qAbs(inOutFactor(io, 10.0, 100.0) - std::sin(std::numbers::pi / 4.0)) < 1e-12);
        QCOMPARE(inOutFactor(io, 40.0, 100.0), 1.0); // between the zones
        QVERIFY(qAbs(inOutFactor(io, 80.0, 100.0) - std::sin(std::numbers::pi / 4.0)) < 1e-12);
        QCOMPARE(inOutFactor(io, 100.0, 100.0), 0.0);
        // While drawing the end is not known: only the start is shaped.
        QCOMPARE(inOutFactor(io, 10.0, -1.0), inOutFactor(io, 10.0, 100.0));
        QCOMPARE(inOutFactor(io, 99.0, -1.0), 1.0);
    }

    void ratioZonesFollowTheStrokeAndNeverOverlap()
    {
        InOutStyle io;
        io.in = io.out = true;
        io.type = InOutType::Ratio;
        io.inRatio = io.outRatio = 0.8; // 160% together: scaled to fit
        QCOMPARE(inOutFactor(io, 100.0, 200.0), 1.0); // where the two zones meet
        QVERIFY(inOutFactor(io, 50.0, 200.0) < 1.0);
        QCOMPARE(inOutFactor(io, 50.0, -1.0), 1.0); // fractions of an unknown length: nothing yet
    }

    void fadeRunsOutAlongTheStroke()
    {
        InOutStyle io;
        io.out = true;
        io.type = InOutType::Fade;
        io.outLength = 50.0;
        QCOMPARE(inOutFactor(io, 0.0, -1.0), 1.0);
        QCOMPARE(inOutFactor(io, 25.0, -1.0), 0.5);
        QCOMPARE(inOutFactor(io, 80.0, -1.0), 0.0);
    }

    void finalPassOnlyWhenTheEndMatters()
    {
        BrushStyle s = constantStyle(5.0, 0.1);
        s.inOut.in = s.inOut.out = true; // switched on, but no parameter follows it
        QVERIFY(!needsFinalPass(s));
        s.sizeEffector.active |= InputInOut;
        s.sizeEffector.available |= InputInOut;
        QVERIFY(needsFinalPass(s));
        s.inOut.out = false; // the start alone is drawn live
        QVERIFY(!needsFinalPass(s));
        s.inOut.type = InOutType::Ratio; // needs the length
        QVERIFY(needsFinalPass(s));
        s.inOut.type = InOutType::Fade; // drawn live
        QVERIFY(!needsFinalPass(s));
        s.startTaper = 20.0; // drawn live
        QVERIFY(!needsFinalPass(s));
    }

    void taperedStrokeShrinksAtTheEnds()
    {
        BrushStyle s = constantStyle(5.0, 0.1);
        s.sizeEffector.available = s.sizeEffector.active = InputInOut;
        s.inOut.in = s.inOut.out = true;
        s.inOut.inLength = s.inOut.outLength = 30.0;
        DabGenerator gen(s);
        gen.setStrokeLength(100.0);
        std::vector<BrushDab> dabs = gen.begin(at(0, 0));
        for (const BrushDab &d : gen.continueTo(at(100, 0)))
            dabs.push_back(d);
        QVERIFY(dabs.front().radius < 0.2); // at the start: the minimum size
        double middle = 0.0;
        for (const BrushDab &d : dabs)
            if (std::abs(d.x - 50.0) < 1.0)
                middle = d.radius;
        QCOMPARE(middle, 5.0);
        QVERIFY(dabs.back().x > 99.0 || dabs.back().radius < 1.0); // thin again at the end
    }

    void finalPassMatchesTheLiveStrokeBeforeTheEnd()
    {
        // Random spacing and tips: the redraw must not reshuffle them.
        BrushStyle s = constantStyle(5.0, 0.5);
        s.spacingEffector.available = s.spacingEffector.active = InputRandom;
        s.sizeEffector.available = s.sizeEffector.active = InputInOut;
        s.inOut.in = s.inOut.out = true;
        s.inOut.inLength = 20.0;
        s.inOut.outLength = 30.0;
        s.startTaper = 10.0;
        const std::vector<BrushInput> points{at(0, 0), at(40, 5), at(90, 0), at(150, 10)};
        auto run = [&](double length) {
            DabGenerator gen(s, 99);
            gen.setStrokeLength(length);
            std::vector<BrushDab> dabs = gen.begin(points[0]);
            for (std::size_t i = 1; i < points.size(); ++i)
                for (const BrushDab &d : gen.continueTo(points[i]))
                    dabs.push_back(d);
            return dabs;
        };
        const auto live = run(-1.0);
        const auto redrawn = run(strokeLength(points));
        const double endZone = strokeLength(points) - 30.0;
        std::size_t compared = 0;
        for (std::size_t i = 0; i < std::min(live.size(), redrawn.size()) && live[i].x < endZone - 15.0; ++i) {
            QCOMPARE(redrawn[i].x, live[i].x);
            QCOMPARE(redrawn[i].radius, live[i].radius);
            ++compared;
        }
        QVERIFY(compared > 10);
    }

    void startTaperIsLiveAndByDistance()
    {
        BrushStyle s = constantStyle(5.0, 0.1); // size follows pressure linearly
        s.sizeEffector = Effector::pressureOnly();
        s.startTaper = 20.0;
        QCOMPARE(startTaperFactor(20.0, 0.0), 0.0);
        QVERIFY(qAbs(startTaperFactor(20.0, 10.0) - std::sin(std::numbers::pi / 4.0)) < 1e-12);
        QCOMPARE(startTaperFactor(20.0, 25.0), 1.0);
        DabGenerator gen(s);
        std::vector<BrushDab> dabs = gen.begin(at(0, 0));
        for (const BrushDab &d : gen.continueTo(at(60, 0)))
            dabs.push_back(d);
        QVERIFY(dabs.front().radius < 0.2);
        QCOMPARE(dabs.back().radius, 5.0);
    }

    void simplifyStraightensAWobblyLine()
    {
        std::vector<BrushInput> points;
        for (int i = 0; i <= 100; ++i)
            points.push_back(at(i * 2.0, (i % 2) ? 0.6 : -0.6));
        const auto simple = simplify(points, 10);
        QCOMPARE(simple.front().x, points.front().x);
        QCOMPARE(simple.back().x, points.back().x);
        for (const BrushInput &p : simple)
            QVERIFY(std::abs(p.y) <= 0.61);
        // One chord end to end, redrawn every 2 px: about as many points, all on a line.
        double maxTurn = 0.0;
        for (std::size_t i = 2; i < simple.size(); ++i)
            maxTurn = std::max(maxTurn, std::abs((simple[i].y - simple[i - 1].y) - (simple[i - 1].y - simple[i - 2].y)));
        QVERIFY(maxTurn < 0.05);
    }

    void jitterFilterDropsZigzags()
    {
        QVERIFY(isJitter(at(0, 0), at(10, 0), at(8, 1)));    // back on itself
        QVERIFY(!isJitter(at(0, 0), at(10, 0), at(20, 0.5))); // carrying on
        QVERIFY(isJitter(at(0, 0), at(10, 0), at(15, 15)));   // a sharp turn over a few px is smoothed away
        QVERIFY(!isJitter(at(0, 0), at(10, 0), at(18, 4)));   // a gentle bend is kept
    }

    void colorShiftsFollowTheirEffectors()
    {
        BrushStyle s = constantStyle(5.0, 0.2);
        s.color.hue = 30.0;
        s.color.hueEffector.available = s.color.hueEffector.active = InputRandom;
        s.color.hueEffector.randomMin = -1.0; // either way
        s.color.subColor = 0.5;
        s.color.target = ColorTarget::Both;
        DabGenerator gen(s, 7);
        std::vector<BrushDab> dabs = gen.begin(at(0, 0));
        for (const BrushDab &d : gen.continueTo(at(200, 0)))
            dabs.push_back(d);
        double lo = 1e9, hi = -1e9;
        for (const BrushDab &d : dabs) {
            QCOMPARE(d.color.subColor, 0.5);
            QCOMPARE(d.color.target, ColorTarget::Both);
            lo = std::min(lo, d.color.hue);
            hi = std::max(hi, d.color.hue);
        }
        QVERIFY(lo >= -30.0 && hi <= 30.0);
        QVERIFY(lo < -10.0 && hi > 10.0); // both ways
    }

    void perStrokeColorIsTheSameForEveryDab()
    {
        BrushStyle s = constantStyle(5.0, 0.2);
        s.color.perStroke = true;
        s.color.strokeValue = 0.3;
        DabGenerator gen(s, 3);
        std::vector<BrushDab> dabs = gen.begin(at(0, 0));
        for (const BrushDab &d : gen.continueTo(at(100, 0)))
            dabs.push_back(d);
        QVERIFY(dabs.size() > 5);
        QVERIFY(dabs.front().color.value != 0.0 && std::abs(dabs.front().color.value) <= 0.3);
        for (const BrushDab &d : dabs)
            QCOMPARE(d.color.value, dabs.front().color.value);
    }
};

QTEST_GUILESS_MAIN(TestBrush)
#include "tst_brush.moc"
