// Tests for the puppet warp core: outlines, triangulation, deformation.

#include "core/puppet/Contour.h"
#include "core/puppet/PuppetRig.h"
#include "core/puppet/Triangulation.h"
#include "core/puppet/WarpRaster.h"

#include <QTest>

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
        const auto simple = simplifyContour(contours[0]);
        QVERIFY(simple.size() < contours[0].size() * 3 / 4); // edges of at least √2 px
        for (std::size_t i = 0; i < simple.size(); ++i)
            QVERIFY(length(simple[(i + 1) % simple.size()] - simple[i]) >= std::sqrt(2.0) * 0.5);
        QVERIFY(simple.size() > 100);
        QVERIFY(std::abs(polygonArea(simple) - std::numbers::pi * 180.0 * 180.0) < 0.02 * std::numbers::pi * 180 * 180);
    }

    void triangulationCoversTheShapeWithGoodTriangles()
    {
        const auto outline = simplifyContour(outerContours(disc(400, 180.0))[0]);
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
