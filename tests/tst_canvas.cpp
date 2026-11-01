#include "canvas/FrameRenderer.h"
#include "canvas/StrokeSession.h"
#include "core/brush/DabGenerator.h"
#include "canvas/ViewTransform.h"
#include "core/document/Document.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"

#include <QTest>

using namespace mothion;
using namespace mothion::canvas;

namespace {

ViewTransform makeView()
{
    ViewTransform v;
    v.setDocumentSize({1920, 1080});
    v.setViewportSize({800, 600});
    v.fitToView();
    return v;
}

bool near(const QPointF &a, const QPointF &b) { return QLineF(a, b).length() < 1e-6; }

} // namespace

class TestCanvas : public QObject
{
    Q_OBJECT

private slots:
    void fitCentersDocument()
    {
        const ViewTransform v = makeView();
        QVERIFY(near(v.documentToView().map(QPointF(960, 540)), QPointF(400, 300)));
    }

    void zoomKeepsAnchorFixed()
    {
        ViewTransform v = makeView();
        const QPointF anchor(123, 456);
        const QPointF doc = v.mapToDocument(anchor);
        v.zoomAround(anchor, 3.0);
        QVERIFY(near(v.documentToView().map(doc), anchor));
    }

    void rotateKeepsAnchorFixedAndWraps()
    {
        ViewTransform v = makeView();
        const QPointF anchor(600, 100);
        const QPointF doc = v.mapToDocument(anchor);
        v.rotateAround(anchor, 170);
        v.rotateAround(anchor, 30);
        QVERIFY(near(v.documentToView().map(doc), anchor));
        QCOMPARE(qRound(v.rotation()), -160);
    }

    void viewToDocumentRoundTrip()
    {
        ViewTransform v = makeView();
        v.rotateAround({10, 20}, 33);
        v.zoomAround({300, 200}, 1.7);
        v.panBy({15, -40});
        const QPointF p(250.5, 120.25);
        QVERIFY(near(v.documentToView().map(v.mapToDocument(p)), p));
    }

    void flipKeepsAnchorAndMirrors()
    {
        ViewTransform v = makeView();
        const QPointF anchor(400, 300); // viewport center = document center after fit
        v.setFlippedAround(anchor, true, false);
        QVERIFY(near(v.documentToView().map(QPointF(960, 540)), anchor));
        // Left of the document now appears right of center.
        QVERIFY(v.documentToView().map(QPointF(0, 540)).x() > 400);
        v.setFlippedAround(anchor, false, false);
        QVERIFY(v.documentToView().map(QPointF(0, 540)).x() < 400);
    }

    void centerOnMovesPointToCenter()
    {
        ViewTransform v = makeView();
        v.rotateAround({100, 100}, 40);
        v.centerOn({200, 300});
        QVERIFY(near(v.documentToView().map(QPointF(200, 300)), v.viewportCenter()));
    }

    // --- StrokeSession ---------------------------------------------------------

    void strokeLeavesNoGaps()
    {
        doc::TiledImage image;
        stroke(image, Qt::black, 1.0, false, {10, 25}, {190, 25}, 2.0); // one long jump
        for (int x = 10; x <= 190; ++x)
            QVERIFY2(doc::alphaOf(image.pixel(x, 25)) > 200, qPrintable(QString::number(x)));
        QCOMPARE(image.tileCount(), std::size_t(3)); // 0..63, 64..127, 128..191
    }

    void strokeOpacityDoesNotAccumulate()
    {
        doc::TiledImage image;
        stroke(image, Qt::black, 0.5, false, {10, 10}, {90, 10}, 4.0); // many overlapping dabs
        const int alpha = doc::alphaOf(image.pixel(50, 10));
        QVERIFY2(qAbs(alpha - 128) <= 2, qPrintable(QString::number(alpha)));
    }

    void eraserRemovesPaintAndDropsEmptyTiles()
    {
        doc::TiledImage image;
        stroke(image, Qt::black, 1.0, false, {10, 10}, {20, 10}, 3.0);
        stroke(image, Qt::black, 1.0, true, {10, 10}, {20, 10}, 8.0);
        QVERIFY(image.isEmpty());
    }

    void strokeIsClippedToDocument()
    {
        doc::TiledImage image;
        canvas::StrokeSession session(image, Qt::black, 1.0, false, QRect(0, 0, 100, 100));
        brush::DabGenerator gen(roundStyle(10.0));
        session.addDabs(gen.begin(point(-30, 50)));
        session.addDabs(gen.continueTo(point(50, 50)));
        QCOMPARE(image.pixel(-5, 50), doc::Pixel(0));
        QVERIFY(doc::alphaOf(image.pixel(5, 50)) > 200);
    }

    void strokeCaptureUndoRedo()
    {
        doc::Document document(200, 100);
        doc::AddLayerCommand addLayer(document, 0, {"L"});
        addLayer.apply(document);
        doc::NewDrawing nd = doc::makeNewDrawing(document, addLayer.layerId(), 0);
        nd.command->apply(document);
        doc::Drawing *drawing = document.layer(addLayer.layerId())->drawing(nd.id);

        canvas::StrokeSession session(drawing->image, Qt::black, 1.0, false);
        brush::DabGenerator gen(roundStyle(3.0));
        session.addDabs(gen.begin(point(10, 10)));
        session.addDabs(gen.continueTo(point(150, 10)));
        auto paint = doc::PaintCommand::capture(addLayer.layerId(), nd.id, session.before(), drawing->image);
        QVERIFY(paint);
        QCOMPARE(paint->swaps().size(), std::size_t(3));

        paint->revert(document);
        QVERIFY(drawing->image.isEmpty());
        paint->apply(document);
        QVERIFY(doc::alphaOf(drawing->image.pixel(100, 10)) > 200);
    }

    void cancelRestoresTarget()
    {
        doc::TiledImage image;
        stroke(image, Qt::black, 1.0, false, {10, 10}, {20, 10}, 3.0);
        const doc::TiledImage original = image;
        canvas::StrokeSession session(image, Qt::red, 1.0, false);
        session.addDabs(brush::DabGenerator(roundStyle(5.0)).begin(point(100, 100)));
        session.cancel();
        QCOMPARE(image.tileCount(), original.tileCount());
        QCOMPARE(image.tile({0, 0}).get(), original.tile({0, 0}).get());
    }

    void dabsTakeTheSecondaryColor()
    {
        doc::TiledImage image;
        auto stamper = std::make_shared<canvas::DabStamper>(Qt::black, 1.0);
        stamper->setSecondaryColor(Qt::red);
        canvas::StrokeSession session(image, stamper, 1.0, false);
        brush::BrushStyle s = roundStyle(5.0);
        s.color.subColor = 1.0; // all the way to the secondary color
        brush::DabGenerator gen(s);
        session.addDabs(gen.begin(point(20, 20)));
        QCOMPARE(QColor::fromRgba(image.pixel(20, 20)), QColor(Qt::red));
    }

    void watercolorPicksUpThePaintUnderneath()
    {
        doc::TiledImage image;
        stroke(image, Qt::red, 1.0, false, {0, 20}, {100, 20}, 10.0);
        auto stamper = std::make_shared<canvas::DabStamper>(Qt::blue, 1.0);
        canvas::StrokeSession session(image, stamper, 1.0, false);
        brush::BrushStyle s = roundStyle(4.0);
        s.mix.on = true;
        s.mix.amount = 0.0; // no paint of its own: only what it picks up
        session.setStyle(s);
        brush::DabGenerator gen(s);
        session.addDabs(gen.begin(point(50, 20)));
        QCOMPARE(QColor::fromRgba(image.pixel(50, 20)), QColor(Qt::red));
        // On an empty spot there is nothing to pick up, and no paint of its own.
        session.addDabs(brush::DabGenerator(s).begin(point(50, 80)));
        QCOMPARE(qAlpha(image.pixel(50, 80)), 0);
    }

    void ribbonFillsBetweenDabs()
    {
        doc::TiledImage image;
        canvas::StrokeSession session(image, Qt::black, 1.0, false);
        brush::BrushStyle s = roundStyle(5.0);
        s.ribbon = true;
        s.spacing = 4.0; // dabs 40 px apart
        session.setStyle(s);
        brush::DabGenerator gen(s);
        session.addDabs(gen.begin(point(10, 30)));
        session.addDabs(gen.continueTo(point(90, 30)));
        QCOMPARE(qAlpha(image.pixel(30, 30)), 255); // between two dabs
        QCOMPARE(qAlpha(image.pixel(30, 34)), 255); // across: the dab's half width
        QCOMPARE(qAlpha(image.pixel(30, 40)), 0);
    }

    void waterEdgeDarkensTheRim()
    {
        doc::TiledImage image;
        auto stamper = std::make_shared<canvas::DabStamper>(QColor(200, 200, 200), 1.0);
        canvas::StrokeSession session(image, stamper, 1.0, false);
        brush::BrushStyle s = roundStyle(20.0);
        s.flow = 0.5;
        s.waterEdge.on = true;
        s.waterEdge.radius = 4.0;
        s.waterEdge.valuePower = 0.5;
        session.setStyle(s);
        session.addDabs(brush::DabGenerator(s).begin(point(50, 50)));
        const QRgb centreBefore = image.pixel(50, 50);
        QVERIFY(!session.finish().isEmpty());
        QCOMPARE(image.pixel(50, 50), centreBefore); // deep inside: untouched
        const QColor rim = QColor::fromRgba(image.pixel(50, 31)); // 1 px inside the edge
        const QColor centre = QColor::fromRgba(centreBefore);
        QVERIFY(rim.alpha() >= centre.alpha());
        QVERIFY(qRed(image.pixel(50, 31)) * 255 / std::max(1, rim.alpha()) < 200); // darker once unpremultiplied
    }

    void restartClearsTheStrokeForTheFinalPass()
    {
        doc::TiledImage image;
        canvas::StrokeSession session(image, Qt::black, 1.0, false);
        brush::DabGenerator gen(roundStyle(5.0));
        session.addDabs(gen.begin(point(10, 10)));
        QVERIFY(!session.restart().isEmpty());
        QCOMPARE(qAlpha(image.pixel(10, 10)), 0);
        session.addDabs(brush::DabGenerator(roundStyle(5.0)).begin(point(40, 10)));
        QCOMPARE(qAlpha(image.pixel(40, 10)), 255);
    }

    // --- FrameRenderer ---------------------------------------------------------

    void frameRendererComposesVisibleLayersAtFrame()
    {
        doc::Document document(128, 64);
        // Bottom layer: red on frame 0, nothing on frame 1.
        doc::AddLayerCommand bottom(document, 0, {"B"});
        bottom.apply(document);
        doc::NewDrawing red = doc::makeNewDrawing(document, bottom.layerId(), 0);
        red.command->apply(document);
        document.layer(bottom.layerId())->drawing(red.id)->image.setPixel(5, 5, 0xffff0000);
        // Top layer, half opacity: blue on frames 0-1.
        doc::AddLayerCommand top(document, 1, {"T", true, false, 0.5f});
        top.apply(document);
        doc::NewDrawing blue = doc::makeNewDrawing(document, top.layerId(), 0, 2);
        blue.command->apply(document);
        document.layer(top.layerId())->drawing(blue.id)->image.setPixel(5, 5, 0xff0000ff);

        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QColor c = renderer.image().pixelColor(5, 5);
        QVERIFY(qAbs(c.red() - 128) <= 2 && qAbs(c.blue() - 128) <= 2); // blue at 50 % over red
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::white)); // paper

        renderer.renderAll(document, 1);
        c = renderer.image().pixelColor(5, 5);
        QVERIFY(c.red() > 120 && c.green() > 120 && c.blue() == 255); // 50 % blue over paper

        doc::LayerProperties hidden = document.layer(top.layerId())->properties();
        hidden.visible = false;
        document.layer(top.layerId())->setProperties(hidden);
        renderer.render(document, 0, QRect(0, 0, 10, 10)); // partial refresh
        QCOMPARE(renderer.image().pixelColor(5, 5), QColor(Qt::red));
    }

private:
    // Hard round dabs of a constant radius, densely spaced (the default brush without pressure).
    static brush::BrushStyle roundStyle(double radius)
    {
        brush::BrushStyle s;
        s.size = radius;
        s.sizeEffector = {};
        return s;
    }

    static brush::BrushInput point(double x, double y)
    {
        brush::BrushInput in;
        in.x = x;
        in.y = y;
        return in;
    }

    // One straight stroke into an image.
    static void stroke(doc::TiledImage &image, const QColor &color, double opacity, bool erase, QPointF a,
                       QPointF b, double radius)
    {
        canvas::StrokeSession session(image, color, opacity, erase);
        brush::DabGenerator gen(roundStyle(radius));
        session.addDabs(gen.begin(point(a.x(), a.y())));
        session.addDabs(gen.continueTo(point(b.x(), b.y())));
    }
};

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"
