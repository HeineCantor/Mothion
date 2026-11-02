// Tests for the puppet warp core: outlines, triangulation, deformation.

#include "core/puppet/Contour.h"
#include "core/puppet/PuppetRig.h"
#include "core/puppet/Triangulation.h"
#include "core/puppet/WarpRaster.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace mothion::puppet;

namespace {

Bitmask disc(int size, double radius)
{
    Bitmask m(0, 0, size, size);
    const double c = size / 2.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            m.set(x, y, std::hypot(x + 0.5 - c, y + 0.5 - c) <= radius);
    return m;
}

double minAngleDegrees(const TriMesh &mesh)
{
    double smallest = 180.0;
    for (const auto &t : mesh.triangles)
        for (int k = 0; k < 3; ++k) {
            const Vec2 a = mesh.vertices[std::size_t(t[std::size_t(k)])];
            const Vec2 b = mesh.vertices[std::size_t(t[std::size_t((k + 1) % 3)])];
            const Vec2 c = mesh.vertices[std::size_t(t[std::size_t((k + 2) % 3)])];
            const double cosine = dot(b - a, c - a) / (length(b - a) * length(c - a));
            smallest = std::min(smallest, std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi);
        }
    return smallest;
}

} // namespace

class TestPuppet : public QObject
{
    Q_OBJECT

private slots:
    void tracesOneOutlinePerPart()
    {
        Bitmask m(10, 20, 40, 20);
        for (int y = 2; y < 8; ++y)
            for (int x = 2; x < 12; ++x)
                m.set(x, y, true); // a 10×6 block
        for (int y = 5; y < 15; ++y)
            for (int x = 25; x < 35; ++x)
                m.set(x, y, true); // a separate 10×10 block
        const auto contours = outerContours(m);
        QCOMPARE(contours.size(), std::size_t(2));
        // Outline of the 10×6 block: its 28 boundary pixels, at pixel centers.
        QCOMPARE(contours[0].size(), std::size_t(28));
        QCOMPARE(contours[0][0].x, 12.5);
        QCOMPARE(contours[0][0].y, 22.5);
        for (const Vec2 &p : contours[0])
            QVERIFY(p.x >= 12.5 && p.x <= 21.5 && p.y >= 22.5 && p.y <= 27.5);
    }

    void dilationGrowsTheShape()
    {
        Bitmask m(0, 0, 5, 5);
        m.set(2, 2, true);
        const Bitmask big = dilated(m, 3);
        QCOMPARE(big.originX, -3);
        QVERIFY(big.at(2 + 3 + 3, 2 + 3)); // 3 px to the right
        QVERIFY(!big.at(2 + 3 + 3, 2 + 3 + 3)); // not the corner of the square
    }

    void simplifiedOutlineStaysCloseAndSimple()
    {
        const auto contours = outerContours(disc(400, 180.0));
        QCOMPARE(contours.size(), std::size_t(1));
        const auto simple = simplifyContour(contours[0], 1.5);
        QVERIFY(simple.size() < contours[0].size() / 8);
        // Every original point stays within the tolerance of the simplified outline.
        for (const Vec2 &p : contours[0]) {
            double nearest = 1e300;
            for (std::size_t i = 0; i < simple.size(); ++i) {
                const Vec2 a = simple[i], b = simple[(i + 1) % simple.size()];
                const double t = std::clamp(dot(p - a, b - a) / length2(b - a), 0.0, 1.0);
                nearest = std::min(nearest, length(p - (a + (b - a) * t)));
            }
            QVERIFY(nearest <= 1.5 + 1e-9);
        }
    }

    void rigMeshKeepsEveryDrawnPixel()
    {
        // A jagged shape: the simplified outline must not cut into it.
        Bitmask m(0, 0, 200, 200);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 200; ++x) {
                const double a = std::atan2(y - 100.0, x - 100.0);
                const double r = 70 + 20 * std::sin(9 * a) + 6 * std::sin(31 * a);
                m.set(x, y, std::hypot(x + 0.5 - 100, y + 0.5 - 100) <= r);
            }
        for (int expand : {1, 3, 8}) {
            PuppetRig rig(m, {1, expand});
            QCOMPARE(rig.meshes().size(), std::size_t(1));
            const auto &outline = rig.meshes()[0].outline;
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 200; ++x)
                    if (m.at(x, y))
                        QVERIFY(pointInPolygon(outline, {x + 0.5, y + 0.5}));
        }
    }

    void triangulationCoversTheShapeWithGoodTriangles()
    {
        const auto outline = simplifyContour(outerContours(disc(400, 180.0))[0], 1.0);
        RefineOptions options;
        options.maxArea = 0.004 * polygonArea(outline);
        const TriMesh mesh = triangulatePolygon(outline, {}, options);
        QVERIFY(mesh.triangles.size() > 200);
        QVERIFY(std::abs(mesh.area() - polygonArea(outline)) < 1e-6 * polygonArea(outline));
        QVERIFY2(minAngleDegrees(mesh) > 19.0, qPrintable(QString::number(minAngleDegrees(mesh))));
        for (const auto &t : mesh.triangles) {
            const double a = 0.5 * orient(mesh.vertices[std::size_t(t[0])], mesh.vertices[std::size_t(t[1])],
                                          mesh.vertices[std::size_t(t[2])]);
            QVERIFY(a > 0.0);
            QVERIFY(a <= options.maxArea * 1.0001);
        }
    }

    void concaveShapesKeepTheirHollows()
    {
        // An L shape: the notch must stay empty.
        const std::vector<Vec2> outline{{0, 0}, {100, 0}, {100, 30}, {30, 30}, {30, 100}, {0, 100}};
        RefineOptions options;
        options.maxArea = 50.0;
        const TriMesh mesh = triangulatePolygon(outline, {{10, 80}}, options);
        QVERIFY(std::abs(mesh.area() - polygonArea(outline)) < 1e-6);
        bool hasExtra = false;
        for (const Vec2 &v : mesh.vertices) {
            QVERIFY(!(v.x > 30.5 && v.y > 30.5)); // nothing in the notch
            hasExtra = hasExtra || (v.x == 10 && v.y == 80);
        }
        QVERIFY(hasExtra); // the extra point became a vertex
    }

    void onePinMovesThePartRigidly()
    {
        Bitmask m(0, 0, 100, 60);
        for (int y = 10; y < 50; ++y)
            for (int x = 10; x < 90; ++x)
                m.set(x, y, true);
        PuppetRig rig(m, {});
        QCOMPARE(rig.meshes().size(), std::size_t(1));
        const auto pin = rig.addPin({30, 30});
        QVERIFY(pin.has_value());
        QVERIFY(!rig.addPin({300, 300}).has_value()); // off the shape
        rig.movePin(*pin, {60, 45}, 20);
        const auto &mesh = rig.meshes()[0];
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v) {
            const Vec2 moved = mesh.deformed[v] - mesh.rest.vertices[v];
            QVERIFY(std::abs(moved.x - 30.0) < 0.5 && std::abs(moved.y - 15.0) < 0.5);
        }
    }

    void twoPinsBendThePartAndHoldTheAnchor()
    {
        Bitmask m(0, 0, 220, 60);
        for (int y = 20; y < 40; ++y)
            for (int x = 10; x < 210; ++x)
                m.set(x, y, true); // a long bar
        PuppetRig rig(m, {2, 3});
        const auto anchor = rig.addPin({20, 30});
        const auto handle = rig.addPin({200, 30});
        rig.movePin(*handle, {200, 90}, 30); // lift the far end
        const auto &mesh = rig.meshes()[0];
        const PuppetRig::Pin &a = rig.pins()[std::size_t(*anchor)];
        QCOMPARE(mesh.deformed[std::size_t(a.vertex)].x, 20.0);
        QCOMPARE(mesh.deformed[std::size_t(a.vertex)].y, 30.0);
        // Near the anchor nothing moves much; the bar's length is kept, roughly.
        double farthest = 0.0;
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v) {
            if (mesh.rest.vertices[v].x < 30)
                QVERIFY(length(mesh.deformed[v] - mesh.rest.vertices[v]) < 6.0); // it may turn about the pin
            farthest = std::max(farthest, length(mesh.deformed[v] - Vec2{20, 30}));
        }
        QVERIFY(farthest > 170.0 && farthest < 215.0);
    }

    // The local warp holds everything out of the disc: only around the
    // grabbed point does the shape move, and the pins stay.
    void localDragMovesOnlyTheDisc()
    {
        Bitmask m(0, 0, 220, 60);
        for (int y = 20; y < 40; ++y)
            for (int x = 10; x < 210; ++x)
                m.set(x, y, true); // a long bar
        PuppetRig rig(m, {2, 3});
        const auto pin = rig.addPin({60, 30});
        QVERIFY(!rig.beginLocalDrag({300, 300}, 40)); // off the shape
        QVERIFY(rig.beginLocalDrag({180, 30}, 40));
        QVERIFY(rig.isLocalDragging());
        rig.localDragTo({180, 60}, 30); // the right end, pulled down
        rig.endLocalDrag();
        QVERIFY(!rig.isLocalDragging());

        const auto &mesh = rig.meshes()[0];
        double grabbedMoved = 0.0;
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v) {
            const double moved = length(mesh.deformed[v] - mesh.rest.vertices[v]);
            if (length(mesh.rest.vertices[v] - Vec2{180, 30}) > 45)
                QVERIFY2(moved < 1e-9, "outside the disc nothing moves");
            else
                grabbedMoved = std::max(grabbedMoved, moved);
        }
        QVERIFY(grabbedMoved > 25.0);
        const PuppetRig::Pin &p = rig.pins()[std::size_t(*pin)];
        QCOMPARE(mesh.deformed[std::size_t(p.vertex)].x, 60.0);

        rig.movePin(*pin, {60, 40}, 20); // the pins work again after it
        QVERIFY(std::abs(mesh.deformed[std::size_t(p.vertex)].y - 40.0) < 1e-6);
    }

    // Several grabbed points move together, each bending its own disc; and
    // several pins can be moved at once.
    void localDragTakesSeveralPointsAndPinsMoveTogether()
    {
        Bitmask m(0, 0, 220, 60);
        for (int y = 20; y < 40; ++y)
            for (int x = 10; x < 210; ++x)
                m.set(x, y, true); // a long bar
        PuppetRig rig(m, {2, 3});
        const auto left = rig.vertexAt({30, 30}), right = rig.vertexAt({190, 30});
        QVERIFY(left && right);
        QVERIFY(rig.beginLocalDrag({*left, *right}, {30, 30}, 30));
        rig.localDragTo({30, 60}, 30); // both ends down by 30
        QCOMPARE(rig.localDragPoints().size(), std::size_t(2));
        rig.endLocalDrag();
        QVERIFY(rig.position(*left).y > 50); // both went down (a disc gives a little)
        QVERIFY(rig.position(*right).y > 50);
        const auto &mesh = rig.meshes()[0];
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v)
            if (std::abs(mesh.rest.vertices[v].x - 110) < 40) // the middle, far from both discs
                QVERIFY(length(mesh.deformed[v] - mesh.rest.vertices[v]) < 1e-9);

        PuppetRig pinned(m, {2, 3});
        const int a = *pinned.addPin({20, 30}), b = *pinned.addPin({110, 30}), c = *pinned.addPin({200, 30});
        pinned.movePins({b, c}, {{110, 50}, {200, 50}}, 30);
        QCOMPARE(pinned.pins()[std::size_t(a)].position.y, 30.0);
        QCOMPARE(pinned.pins()[std::size_t(b)].position.y, 50.0);
        QCOMPARE(pinned.pins()[std::size_t(c)].position.y, 50.0);
        const auto &bent = pinned.meshes()[0];
        QVERIFY(std::abs(bent.deformed[std::size_t(pinned.pins()[std::size_t(c)].vertex)].y - 50.0) < 1e-6);
    }

    // A local drag bends the shape as it is now, not back from its rest:
    // a tiny move after an earlier bend moves nothing much.
    void localDragIsRelativeToTheCurrentShape()
    {
        Bitmask m(0, 0, 220, 60);
        for (int y = 20; y < 40; ++y)
            for (int x = 10; x < 210; ++x)
                m.set(x, y, true); // a long bar
        PuppetRig rig(m, {2, 3});
        QVERIFY(rig.beginLocalDrag({180, 30}, 60)); // bend the right end down
        rig.localDragTo({180, 70}, 40);
        rig.endLocalDrag();

        const auto &mesh = rig.meshes()[0];
        const std::vector<Vec2> bent = mesh.deformed;
        const auto grabbed = rig.vertexAt({150, 40});
        QVERIFY(grabbed);
        const Vec2 at = rig.position(*grabbed);
        QVERIFY(rig.beginLocalDrag({*grabbed}, at, 80)); // a disc over the bent part
        rig.localDragTo(at + Vec2{0.5, 0}, 40);
        rig.endLocalDrag();
        double most = 0.0;
        for (std::size_t v = 0; v < bent.size(); ++v)
            most = std::max(most, length(mesh.deformed[v] - bent[v]));
        QVERIFY2(most < 1.0, qPrintable(QStringLiteral("jumped by %1 px").arg(most)));
    }

    void warpDrawsTheShapeWhereItMoved()
    {
        Bitmask m(0, 0, 40, 40);
        RasterImage source(0, 0, 40, 40);
        for (int y = 10; y < 30; ++y)
            for (int x = 10; x < 30; ++x) {
                m.set(x, y, true);
                source.pixels[std::size_t(y * 40 + x)] = 0xff3366ccu;
            }
        PuppetRig rig(m, {});
        rig.movePin(*rig.addPin({20, 20}), {70, 25}, 10); // +50, +5
        int x, y, w, h;
        deformedBounds(rig, x, y, w, h);
        RasterImage target(x, y, w, h);
        warp(rig, source, target);
        auto at = [&](int px, int py) { return target.pixels[std::size_t((py - y) * w + (px - x))]; };
        QCOMPARE(at(70, 25), 0xff3366ccu);
        QCOMPARE(at(61, 16), 0xff3366ccu);  // near the moved corner, inside
        QCOMPARE(at(77, 32), 0xff3366ccu); // (a few tenths of a px off a pure move at the edges)
        QCOMPARE(at(58, 25) >> 24, 0u);     // left of the moved square
    }
};

QTEST_GUILESS_MAIN(TestPuppet)
#include "tst_puppet.moc"
