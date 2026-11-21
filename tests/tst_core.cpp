// Tests for the Qt-free core (QtTest is only the test runner here).

#include "core/PressureCurve.h"
#include "core/RateMeter.h"
#include "core/StrokeSmoother.h"
#include "core/fill/FillShape.h"
#include "core/fill/FloodFill.h"
#include "core/document/Document.h"
#include "core/document/LipSync.h"
#include <map>
#include <tuple>
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
    // A fisheye keeps the scale at the center and curves straight lines; its inverse takes a point back.
    void fisheyeLensCurvesTheGrid()
    {
        perspective::Grid g = perspective::defaultGrid(1000, 600);
        g.orientation = {};
        g.cell = 50.0;
        g.cells = 4;
        const auto straight = perspective::project(g, 20, 0);
        g.fisheye = 1.0;
        const auto near = perspective::project(g, 20, 0);
        QVERIFY(qAbs(near->x - straight->x) < 0.5); // same scale at the center
        const auto far = perspective::project(g, 600, 0);
        g.fisheye = 0.0;
        const auto rectilinear = perspective::project(g, 600, 0);
        QVERIFY(far->x < rectilinear->x); // squeezed toward the center at the edges

        g.fisheye = 0.7;
        for (const perspective::Point p : {perspective::Point{500, 300}, {820, 140}, {60, 580}}) {
            const auto back = perspective::projectDirection(g, perspective::unprojectDirection(g, p));
            QVERIFY(back && qAbs(back->x - p.x) < 1e-6 && qAbs(back->y - p.y) < 1e-6);
        }

        // A line that does not cross the center bends: its middle is off the chord of its ends.
        g.orientation = perspective::fromAngles(0, 0, 0);
        const auto a = perspective::project(g, -200, 100), m = perspective::project(g, 0, 100),
                   b = perspective::project(g, 200, 100);
        const double chord = (b->x - a->x) * (m->y - a->y) - (b->y - a->y) * (m->x - a->x);
        QVERIFY(qAbs(chord) > 1.0);
        g.fisheye = 0.0;
        const auto a0 = perspective::project(g, -200, 100), m0 = perspective::project(g, 0, 100),
                   b0 = perspective::project(g, 200, 100);
        QVERIFY(qAbs((b0->x - a0->x) * (m0->y - a0->y) - (b0->y - a0->y) * (m0->x - a0->x)) < 1e-6);

        // The horizon of a tilted plane is a curve of segments, one line for the usual lens.
        g.orientation = perspective::fromAngles(30, 60, 0);
        QCOMPARE(perspective::horizonSegments(g, 100.0).size(), std::size_t(1));
        g.fisheye = 0.8;
        QVERIFY(perspective::horizonSegments(g, 100.0).size() > 10);
        QVERIFY(perspective::gridSegments(g).size() > 18); // lines cut in pieces
    }

    // Lip sync: a drawing named after a mouth shape (its letter alone, or first and set apart) is that shape's.
    void mouthShapesAreKnownByTheDrawingsNames()
    {
        using doc::mouthShapeOf;
        QCOMPARE(mouthShapeOf("A"), std::optional<char>('A'));
        QCOMPARE(mouthShapeOf("x"), std::optional<char>('X'));
        QCOMPARE(mouthShapeOf("  D"), std::optional<char>('D'));
        QCOMPARE(mouthShapeOf("A - MBP"), std::optional<char>('A'));
        QCOMPARE(mouthShapeOf("G_fv"), std::optional<char>('G'));
        QCOMPARE(mouthShapeOf("Alpha"), std::nullopt); // a word
        QCOMPARE(mouthShapeOf("A2"), std::nullopt);
        QCOMPARE(mouthShapeOf("I"), std::nullopt); // not a shape
        QCOMPARE(mouthShapeOf("1"), std::nullopt);
        QCOMPARE(mouthShapeOf(""), std::nullopt);

        doc::Document d(100, 100);
        const doc::CelId id = d.allocateCelId();
        doc::Cel cel(id, doc::CelProperties{"Mouth"});
        auto add = [&](const std::string &name) {
            doc::Drawing drawing;
            drawing.id = d.allocateDrawingId();
            drawing.name = name;
            cel.addDrawing(drawing);
            return drawing.id;
        };
        const doc::DrawingId a = add("A");
        add("1");
        const doc::DrawingId x = add("X rest");
        add("a"); // the same shape again: the first one counts
        QCOMPARE(doc::mouthDrawing(cel, 'A'), a);
        QCOMPARE(doc::mouthDrawing(cel, 'X'), x);
        QCOMPARE(doc::mouthDrawing(cel, 'D'), doc::kNoId);
    }

    // Lip sync from the sound: each cue's mouth from its frame (the sound's offset counted) to the next one's; a cue
    // too soon after the one before, or of a shape the chart has no drawing for, dropped; out of the scene, cut.
    void mouthCuesBecomeTheChartsTiming()
    {
        doc::Document d(100, 100);
        doc::Cel cel(d.allocateCelId(), doc::CelProperties{"Mouth"});
        std::map<char, doc::DrawingId> ids;
        for (const char shape : {'A', 'D', 'X'}) {
            doc::Drawing drawing;
            drawing.id = d.allocateDrawingId();
            drawing.name = std::string(1, shape);
            cel.addDrawing(drawing);
            ids[shape] = drawing.id;
        }
        const std::vector<doc::MouthCue> cues{{0.0, 0.25, 'X'}, {0.25, 0.5, 'D'}, {0.5, 0.75, 'E'}, {0.75, 1.0, 'A'}};
        auto runs = [](const doc::Timing &t) {
            std::vector<std::tuple<int, int, doc::DrawingId>> out;
            for (const doc::Exposure &e : t.exposures())
                out.emplace_back(e.start, e.length, e.drawing);
            return out;
        };
        using Runs = std::vector<std::tuple<int, int, doc::DrawingId>>;
        // At 24 fps: X 0..5, D 6..17 (no drawing for E: D holds), A 18..23.
        QCOMPARE(runs(doc::lipSyncTiming(cel, cues, 24.0, 0, 1, 100)),
                 (Runs{{0, 6, ids['X']}, {6, 12, ids['D']}, {18, 6, ids['A']}}));
        // The sound beginning at frame 10.
        QCOMPARE(runs(doc::lipSyncTiming(cel, cues, 24.0, 10, 1, 100)),
                 (Runs{{10, 6, ids['X']}, {16, 12, ids['D']}, {28, 6, ids['A']}}));
        // Each at least 8 frames: D (6 after X) dropped, X holds.
        QCOMPARE(runs(doc::lipSyncTiming(cel, cues, 24.0, 0, 8, 100)), (Runs{{0, 18, ids['X']}, {18, 6, ids['A']}}));
        // Begun before the scene (a negative offset) and past its end: cut to the scene.
        QCOMPARE(runs(doc::lipSyncTiming(cel, cues, 24.0, -3, 1, 20)),
                 (Runs{{0, 3, ids['X']}, {3, 12, ids['D']}, {15, 5, ids['A']}}));
        // What was on the cel elsewhere stays.
        cel.timing().setExposure(40, 5, ids['A']);
        QCOMPARE(std::get<0>(runs(doc::lipSyncTiming(cel, cues, 24.0, 0, 1, 100)).back()), 40);
        QVERIFY(doc::lipSyncTiming(cel, {}, 24.0, 0, 1, 100) == cel.timing());
    }

    // The shape of colour of a vector layer: the area clicked grown under the line round it up to the line's middle (each
    // pixel of the line to the side it is nearer), and its outlines, holes too, which filled by the even-odd rule give
    // the area back.
    void aFillShapeReachesTheLinesMiddleAndHasItsHoles()
    {
        using mothion::doc::Pixel;
        constexpr int w = 40, h = 40;
        std::vector<Pixel> px(w * h, 0xffffffffu); // white paper
        auto put = [&](int x, int y) { px[std::size_t(y * w + x)] = 0xff000000u; };
        for (int i = 5; i < 35; ++i) // a square line 6 px wide: x and y 5..10 and 29..34
            for (int t = 0; t < 6; ++t) {
                put(i, 5 + t);
                put(i, 29 + t);
                put(5 + t, i);
                put(29 + t, i);
            }
        for (int y = 18; y < 22; ++y) // and a small square of line in the middle: a hole in the area
            for (int x = 18; x < 22; ++x)
                put(x, y);
        mothion::fill::Region area = mothion::fill::floodRegion(px.data(), w, h, 15, 15, 0);
        QVERIFY(!area.isEmpty());
        QCOMPARE(int(area.at(11, 15)), 255);
        QCOMPARE(int(area.at(10, 15)), 0);
        const auto grown = mothion::fill::grownToLineMiddle(area, px.data(), 0xffffffffu, 0, 64);
        QCOMPARE(int(grown.at(8, 15)), 255); // the inner half of the line
        QCOMPARE(int(grown.at(7, 15)), 0);   // the outer half goes to the other side
        QCOMPARE(int(grown.at(31, 15)), 255);
        QCOMPARE(int(grown.at(32, 15)), 0);
        QCOMPARE(int(grown.at(19, 19)), 255); // the hole's line has nothing beyond it: all of it
        QCOMPARE(int(grown.at(2, 2)), 0);

        // The outlines of a ring (a square with a square hole): two, which by the even-odd rule hold the ring's pixels.
        mothion::fill::Region ring;
        ring.width = ring.height = 10;
        ring.mask.assign(100, 0);
        for (int y = 1; y < 9; ++y)
            for (int x = 1; x < 9; ++x)
                if (x < 3 || x > 6 || y < 3 || y > 6)
                    ring.mask[std::size_t(y * 10 + x)] = 255;
        ring.left = ring.top = 1;
        ring.right = ring.bottom = 8;
        const auto outlines = mothion::fill::regionOutlines(ring);
        QCOMPARE(int(outlines.size()), 2);
        for (const auto &outline : outlines)
            QCOMPARE(int(outline.size()), 4); // four corners each, no points along the straight edges
        auto insideEvenOdd = [&](double px0, double py0) {
            bool in = false;
            for (const auto &outline : outlines)
                for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
                    if ((outline[i].y > py0) != (outline[j].y > py0)
                        && px0 < (outline[j].x - outline[i].x) * (py0 - outline[i].y) / (outline[j].y - outline[i].y) + outline[i].x)
                        in = !in;
            return in;
        };
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                QCOMPARE(insideEvenOdd(x + 0.5, y + 0.5), ring.at(x, y) == 255);
    }

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
