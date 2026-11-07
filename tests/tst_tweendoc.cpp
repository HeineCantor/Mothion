#include "document/DocumentController.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"

#include <QtTest>

#include <cmath>

using namespace mothion;

namespace {

constexpr doc::Pixel kInk = 0xff000000;

// A thick curved stroke (an arc) with its middle at (cx, cy), drawn into `image`.
void stroke(doc::TiledImage &image, double cx, double cy)
{
    for (double a = -1.2; a < 1.2; a += 0.003) {
        const double x = cx + 60.0 * std::sin(a), y = cy - 90.0 * std::cos(a) + 90.0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                image.setPixel(int(x) + dx, int(y) + dy, kInk);
    }
}

// The centre of the ink of a drawing's layer.
QPointF centroid(const doc::Layer &layer)
{
    double sx = 0, sy = 0;
    long n = 0;
    for (const doc::TileCoord &c : layer.image.tileCoords())
        for (int y = 0; y < doc::kTileSize; ++y)
            for (int x = 0; x < doc::kTileSize; ++x) {
                const int px = c.x * doc::kTileSize + x, py = c.y * doc::kTileSize + y;
                if (doc::alphaOf(layer.image.pixel(px, py)) > 128) {
                    sx += px;
                    sy += py;
                    ++n;
                }
            }
    return n ? QPointF(sx / double(n), sy / double(n)) : QPointF(-1, -1);
}

// A project with a cel and two poses: `a` at frame 0, `b` at frame 8, the second 30 px to the right and 12 down.
struct Fixture
{
    document::DocumentController controller;
    doc::CelId cel = doc::kNoId;
    doc::DrawingId a = doc::kNoId, b = doc::kNoId;
    doc::LayerId inkA = doc::kNoId, inkB = doc::kNoId;

    Fixture()
    {
        cel = controller.currentCel();
        doc::Document &d = controller.documentForCommands();
        doc::NewDrawing first = doc::makeNewDrawing(d, cel, 0, 1, "ink");
        a = first.id;
        inkA = first.layer;
        controller.execute(std::move(first.command));
        doc::NewDrawing second = doc::makeNewDrawing(d, cel, 8, 1, "ink");
        b = second.id;
        inkB = second.layer;
        controller.execute(std::move(second.command));
        stroke(d.layer(cel, a, inkA)->image, 400, 300);
        stroke(d.layer(cel, b, inkB)->image, 430, 312);
        controller.setCurrentCel(cel);
    }
    const doc::Layer &layerAt(int frame, int index = 0) const
    {
        return controller.document().cel(cel)->drawingAt(frame)->layers[std::size_t(index)];
    }
};

} // namespace

class TestTweenDocument : public QObject
{
    Q_OBJECT

private slots:
    void theFramesBetweenGetDrawingsWhoseInkIsOnTheWay()
    {
        Fixture f;
        QString reason;
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8, document::TweenEase::Linear, &reason), 7);
        const doc::Cel &cel = *f.controller.document().cel(f.cel);
        const QPointF start = centroid(f.layerAt(0)), end = centroid(f.layerAt(8));
        QVERIFY(std::hypot(end.x() - start.x(), end.y() - start.y()) > 25);
        for (int frame = 1; frame <= 7; ++frame) {
            const doc::Drawing *drawing = cel.drawingAt(frame);
            QVERIFY(drawing);
            QVERIFY(drawing->id != f.a && drawing->id != f.b);
            QCOMPARE(drawing->layers.size(), size_t(1));
            QCOMPARE(drawing->layers[0].properties.name, std::string("ink"));
            const double u = frame / 8.0;
            const QPointF c = centroid(drawing->layers[0]);
            QVERIFY2(std::hypot(c.x() - (start.x() + u * (end.x() - start.x())), c.y() - (start.y() + u * (end.y() - start.y()))) < 4.0,
                     qPrintable(QStringLiteral("frame %1 at %2,%3").arg(frame).arg(c.x()).arg(c.y())));
        }
        // The poses are what they were, and each new frame has its own drawing.
        QCOMPARE(cel.drawingAt(0)->id, f.a);
        QCOMPARE(cel.drawingAt(8)->id, f.b);
        QCOMPARE(cel.drawings().size(), size_t(9));
        QCOMPARE(f.controller.undoText(), QStringLiteral("Undo tween"));
    }

    void oneUndoTakesAllTheFramesBack()
    {
        Fixture f;
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8), 7);
        f.controller.undo();
        const doc::Cel &cel = *f.controller.document().cel(f.cel);
        for (int frame = 1; frame <= 7; ++frame)
            QVERIFY(!cel.drawingAt(frame));
        QCOMPARE(cel.drawings().size(), size_t(2));
        f.controller.redo();
        QVERIFY(cel.drawingAt(4));
        QCOMPARE(cel.drawings().size(), size_t(9));
    }

    void easingMovesTheFramesLessAtFirst()
    {
        Fixture f;
        Fixture g;
        f.controller.tweenFrames(f.cel, 0, 8, document::TweenEase::Linear);
        g.controller.tweenFrames(g.cel, 0, 8, document::TweenEase::EaseIn);
        const double start = centroid(f.layerAt(0)).x();
        const double linear = centroid(f.layerAt(2)).x() - start;
        const double eased = centroid(g.layerAt(2)).x() - start;
        QVERIFY(eased < linear * 0.6); // slow at the start
        QVERIFY(centroid(g.layerAt(7)).x() < centroid(f.layerAt(7)).x() - 1.5); // and still behind at the end
    }

    void aTweenWithoutAWayIsRefusedWithAReason()
    {
        Fixture f;
        QString reason;
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 1, document::TweenEase::Linear, &reason), 0); // no frame between
        QVERIFY(!reason.isEmpty());
        reason.clear();
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 5, document::TweenEase::Linear, &reason), 0); // nothing at frame 5
        QVERIFY(!reason.isEmpty());
        reason.clear();
        QCOMPARE(f.controller.tweenFrames(f.cel, 3, 8, document::TweenEase::Linear, &reason), 0); // nothing at frame 3
        reason.clear();
        QCOMPARE(f.controller.tweenFrames(f.cel, 8, 0, document::TweenEase::Linear, &reason), 0); // backwards
        QVERIFY(!reason.isEmpty());
        QCOMPARE(f.controller.tweenFrames(doc::kNoId, 0, 8, document::TweenEase::Linear, &reason), 0);
        QVERIFY(!f.controller.canUndo() || f.controller.undoText() != QStringLiteral("Undo tween"));

        // The same drawing at both frames.
        doc::CelProperties p = f.controller.document().cel(f.cel)->properties();
        f.controller.execute(doc::makeSetExposure(f.controller.document(), f.cel, 8, 1, f.a));
        reason.clear();
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8, document::TweenEase::Linear, &reason), 0);
        QVERIFY(!reason.isEmpty());

        // A locked cel.
        Fixture g;
        p = g.controller.document().cel(g.cel)->properties();
        p.locked = true;
        g.controller.execute(doc::makeSetCelProperties(g.controller.document(), g.cel, p));
        reason.clear();
        QCOMPARE(g.controller.tweenFrames(g.cel, 0, 8, document::TweenEase::Linear, &reason), 0);
        QVERIFY(!reason.isEmpty());
    }

    void layersAreMatchedByNameAndTheOnesWithoutPartnerFade()
    {
        Fixture f;
        doc::Document &d = f.controller.documentForCommands();
        // The first pose has a second layer "colour", the second pose a "shadow" instead.
        d.cel(f.cel)->drawing(f.a)->layers.push_back({d.allocateLayerId(), {"colour"}, {}});
        d.cel(f.cel)->drawing(f.b)->layers.push_back({d.allocateLayerId(), {"shadow"}, {}});
        for (int y = 280; y < 300; ++y)
            for (int x = 380; x < 420; ++x) {
                d.cel(f.cel)->drawing(f.a)->layers.back().image.setPixel(x, y, 0xff0000ff);
                d.cel(f.cel)->drawing(f.b)->layers.back().image.setPixel(x + 30, y + 12, 0xff808080);
            }
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8), 7);
        const doc::Drawing &middle = *f.controller.document().cel(f.cel)->drawingAt(4);
        // "ink" is paired by name; "colour" and "shadow" are paired in the order that is left (they are the only ones).
        QCOMPARE(middle.layers.size(), size_t(2));
        QCOMPARE(middle.layers[0].properties.name, std::string("ink"));
        QCOMPARE(middle.layers[1].properties.name, std::string("colour")); // the look of the first pose's layer
        QVERIFY(!middle.layers[1].image.isEmpty());
    }

    void aLayerWithNoPartnerFadesOut()
    {
        Fixture f;
        doc::Document &d = f.controller.documentForCommands();
        d.cel(f.cel)->drawing(f.a)->layers.push_back({d.allocateLayerId(), {"spot"}, {}});
        for (int y = 100; y < 130; ++y)
            for (int x = 100; x < 130; ++x)
                d.cel(f.cel)->drawing(f.a)->layers.back().image.setPixel(x, y, 0xff00ff00);
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8), 7);
        const doc::Drawing &middle = *f.controller.document().cel(f.cel)->drawingAt(4);
        QCOMPARE(middle.layers.size(), size_t(2));
        const int alpha = doc::alphaOf(middle.layers[1].image.pixel(115, 115));
        QVERIFY2(alpha > 110 && alpha < 145, qPrintable(QString::number(alpha))); // half way: half there
        QVERIFY(doc::alphaOf(f.layerAt(1, 1).image.pixel(115, 115)) > 200);       // nearly all there still
        QVERIFY(doc::alphaOf(f.layerAt(7, 1).image.pixel(115, 115)) < 50);        // nearly gone
    }

    void perspectiveLayersAreLeftOut()
    {
        Fixture f;
        doc::Document &d = f.controller.documentForCommands();
        d.cel(f.cel)->drawing(f.a)->layers.push_back(
            {d.allocateLayerId(), {"grid"}, {}, doc::LayerKind::Perspective});
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8), 7);
        QCOMPARE(f.controller.document().cel(f.cel)->drawingAt(4)->layers.size(), size_t(1)); // only the ink
    }

    // The preview is small whatever the drawings' size, and follows the same way as the frames do.
    void thePreviewIsSmallAndFollowsTheWay()
    {
        Fixture f;
        doc::Document &d = f.controller.documentForCommands();
        // A layer as big as the sheet in both poses, as a background grid is.
        d.cel(f.cel)->drawing(f.a)->layers.push_back({d.allocateLayerId(), {"grid"}, {}});
        d.cel(f.cel)->drawing(f.b)->layers.push_back({d.allocateLayerId(), {"grid"}, {}});
        for (int x = 0; x < 1900; x += 100)
            for (int y = 0; y < 1000; ++y) {
                d.cel(f.cel)->drawing(f.a)->layers.back().image.setPixel(x, y, 0xff808080);
                d.cel(f.cel)->drawing(f.b)->layers.back().image.setPixel(x, y, 0xff808080);
            }
        const auto tween = document::DrawingTween::make(*d.cel(f.cel)->drawing(f.a), *d.cel(f.cel)->drawing(f.b));
        QVERIFY(tween);
        const QImage small = tween->preview(0.5);
        QVERIFY(std::max(small.width(), small.height()) <= 400);
        QCOMPARE(small.size().width(), (tween->area().width() + tween->previewScale() - 1) / tween->previewScale());
        QVERIFY(tween->previewScale() > 1);
        // The ink of the stroke is in the preview at a place between the poses (A's stroke is left of B's).
        auto inkX = [&](const QImage &image) {
            double sum = 0;
            long n = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (qAlpha(image.pixel(x, y)) > 200 && qGray(image.pixel(x, y)) < 40) {
                        sum += x;
                        ++n;
                    }
            return n ? sum / double(n) : -1.0;
        };
        const double a = inkX(tween->preview(0.0)), mid = inkX(tween->preview(0.5)), b = inkX(tween->preview(1.0));
        QVERIFY(a > 0 && b > a + 1.0);
        QVERIFY2(mid > a && mid < b, qPrintable(QStringLiteral("%1 %2 %3").arg(a).arg(mid).arg(b))); // on the way
        // The frames are made only where the layers have pixels: the grid's layer is there and as big as its content.
        QCOMPARE(f.controller.tweenFrames(f.cel, 0, 8), 7);
        QVERIFY(!f.layerAt(4, 1).image.isEmpty());
    }

    void twoEmptyPosesHaveNothingToTween()
    {
        document::DocumentController controller;
        const doc::CelId cel = controller.currentCel();
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, cel, 0).command);
        controller.execute(doc::makeNewDrawing(d, cel, 6).command);
        QString reason;
        QCOMPARE(controller.tweenFrames(cel, 0, 6, document::TweenEase::Linear, &reason), 0);
        QVERIFY(!reason.isEmpty());
    }
};

QTEST_MAIN(TestTweenDocument)
#include "tst_tweendoc.moc"
