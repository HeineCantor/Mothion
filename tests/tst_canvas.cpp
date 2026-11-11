#include "canvas/FrameRenderer.h"
#include "canvas/Resample.h"
#include "canvas/StrokeSession.h"
#include "core/brush/DabGenerator.h"
#include "canvas/ViewTransform.h"
#include "core/document/Document.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/PaintCommand.h"

#include <QTest>

#include <algorithm>

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

    // With a selection the stroke paints only inside it, and an eraser stroke erases only there.
    void strokeStaysInsideTheSelection()
    {
        QPainterPath area;
        area.addRect(40, 0, 20, 100); // x 40..59
        doc::TiledImage image;
        {
            canvas::StrokeSession session(image, Qt::black, 1.0, false, QRect(0, 0, 100, 100));
            session.setSelection(area);
            brush::DabGenerator gen(roundStyle(10.0));
            session.addDabs(gen.begin(point(10, 50)));
            session.addDabs(gen.continueTo(point(90, 50)));
        }
        QCOMPARE(image.pixel(20, 50), doc::Pixel(0));
        QVERIFY(doc::alphaOf(image.pixel(50, 50)) > 200);
        QCOMPARE(image.pixel(80, 50), doc::Pixel(0));

        // Erasing across the whole line takes out only the selected part.
        doc::TiledImage line;
        stroke(line, Qt::black, 1.0, false, {10, 50}, {90, 50}, 6.0);
        QVERIFY(doc::alphaOf(line.pixel(20, 50)) > 200);
        canvas::StrokeSession eraser(line, Qt::black, 1.0, true, QRect(0, 0, 100, 100));
        eraser.setSelection(area);
        brush::DabGenerator gen(roundStyle(20.0));
        eraser.addDabs(gen.begin(point(10, 50)));
        eraser.addDabs(gen.continueTo(point(90, 50)));
        QVERIFY(doc::alphaOf(line.pixel(20, 50)) > 200);
        QCOMPARE(doc::alphaOf(line.pixel(50, 50)), 0);
        QVERIFY(doc::alphaOf(line.pixel(80, 50)) > 200);
    }

    void strokeCaptureUndoRedo()
    {
        doc::Document document(200, 100);
        doc::AddCelCommand addCel(document, 0, {"L"});
        addCel.apply(document);
        doc::NewDrawing nd = doc::makeNewDrawing(document, addCel.celId(), 0);
        nd.command->apply(document);
        doc::Drawing *drawing = document.cel(addCel.celId())->drawing(nd.id);

        canvas::StrokeSession session(drawing->layers.front().image, Qt::black, 1.0, false);
        brush::DabGenerator gen(roundStyle(3.0));
        session.addDabs(gen.begin(point(10, 10)));
        session.addDabs(gen.continueTo(point(150, 10)));
        auto paint = doc::PaintCommand::capture(addCel.celId(), nd.id, nd.layer, session.before(), drawing->layers.front().image);
        QVERIFY(paint);
        QCOMPARE(paint->swaps().size(), std::size_t(3));

        paint->revert(document);
        QVERIFY(drawing->layers.front().image.isEmpty());
        paint->apply(document);
        QVERIFY(doc::alphaOf(drawing->layers.front().image.pixel(100, 10)) > 200);
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

    void frameRendererComposesVisibleCelsAtFrame()
    {
        doc::Document document(128, 64);
        // Bottom cel: red on frame 0, nothing on frame 1.
        doc::AddCelCommand bottom(document, 0, {"B"});
        bottom.apply(document);
        doc::NewDrawing red = doc::makeNewDrawing(document, bottom.celId(), 0);
        red.command->apply(document);
        document.cel(bottom.celId())->drawing(red.id)->layers.front().image.setPixel(5, 5, 0xffff0000);
        // Top cel, half opacity: blue on frames 0-1.
        doc::AddCelCommand top(document, 1, {"T", true, false, 0.5f});
        top.apply(document);
        doc::NewDrawing blue = doc::makeNewDrawing(document, top.celId(), 0, 2);
        blue.command->apply(document);
        document.cel(top.celId())->drawing(blue.id)->layers.front().image.setPixel(5, 5, 0xff0000ff);

        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QColor c = renderer.image().pixelColor(5, 5);
        QVERIFY(qAbs(c.red() - 128) <= 2 && qAbs(c.blue() - 128) <= 2); // blue at 50 % over red
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::white)); // paper

        renderer.renderAll(document, 1);
        c = renderer.image().pixelColor(5, 5);
        QVERIFY(c.red() > 120 && c.green() > 120 && c.blue() == 255); // 50 % blue over paper

        doc::CelProperties hidden = document.cel(top.celId())->properties();
        hidden.visible = false;
        document.cel(top.celId())->setProperties(hidden);
        renderer.render(document, 0, QRect(0, 0, 10, 10)); // partial refresh
        QCOMPARE(renderer.image().pixelColor(5, 5), QColor(Qt::red));
    }

    // A layer's mask: where it is white (or has no pixel) the layer shows, where it is black it does not, in the
    // layer's blend mode too; a gray lets that much through.
    void aLayerShowsThroughItsMask()
    {
        doc::Document document(64, 32);
        doc::AddCelCommand cel(document, 0, {"C"});
        cel.apply(document);
        doc::NewDrawing d = doc::makeNewDrawing(document, cel.celId(), 0);
        d.command->apply(document);
        doc::Layer &layer = document.cel(cel.celId())->drawing(d.id)->layers.front();
        for (int x = 0; x < 8; ++x)
            layer.image.setPixel(x, 5, 0xff000000); // a black line over the white paper
        layer.mask.emplace();
        layer.mask->setPixel(2, 5, 0xff000000);  // black: hidden
        layer.mask->setPixel(3, 5, 0xffffffff);  // white: shown
        layer.mask->setPixel(4, 5, 0xff808080);  // gray: about half
        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(0, 5), QColor(Qt::black)); // no mask pixel here
        QCOMPARE(renderer.image().pixelColor(1, 5), QColor(Qt::black));
        QCOMPARE(renderer.image().pixelColor(2, 5), QColor(Qt::white)); // masked out: the paper
        QCOMPARE(renderer.image().pixelColor(3, 5), QColor(Qt::black));
        QVERIFY(qAbs(renderer.image().pixelColor(4, 5).red() - 127) <= 3);

        // Through a software blend mode as well.
        layer.properties.blend = doc::BlendMode::LinearBurn;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(2, 5), QColor(Qt::white));
        QCOMPARE(renderer.image().pixelColor(3, 5), QColor(Qt::black));

        // Taking the mask away shows the whole layer.
        layer.mask.reset();
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(2, 5), QColor(Qt::black));
    }

    void modesQPainterLacksAreBlendedByHand()
    {
        doc::Document document(64, 32);
        doc::AddCelCommand bottom(document, 0, {"B"});
        bottom.apply(document);
        doc::NewDrawing red = doc::makeNewDrawing(document, bottom.celId(), 0);
        red.command->apply(document);
        document.cel(bottom.celId())->drawing(red.id)->layers.front().image.setPixel(5, 5, 0xffff0000);
        doc::CelProperties burn{"T"};
        burn.blend = doc::BlendMode::LinearBurn;
        doc::AddCelCommand top(document, 1, burn);
        top.apply(document);
        doc::NewDrawing blue = doc::makeNewDrawing(document, top.celId(), 0);
        blue.command->apply(document);
        doc::Drawing &drawing = *document.cel(top.celId())->drawing(blue.id);
        drawing.layers.front().image.setPixel(5, 5, 0xff0000ff);
        drawing.layers.front().image.setPixel(6, 5, 0xff0000ff); // over the paper, not over the red

        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(5, 5), QColor(Qt::black)); // red + blue - 1
        QCOMPARE(renderer.image().pixelColor(6, 5), QColor(Qt::blue)); // white + blue - 1
        QCOMPARE(renderer.image().pixelColor(50, 20), QColor(Qt::white));
    }

    // A layer clipped to the one below shows only over that layer's pixels; the layers stacked on it are clipped to the
    // same first one that is not clipped; nothing is erased, so letting go brings it all back.
    void aClippedLayerShowsOnlyOverTheLayerBelow()
    {
        doc::Drawing drawing;
        auto layer = [&](doc::LayerId id, const char *name, int x0, int x1, doc::Pixel color) {
            drawing.layers.push_back({id, {name}, {}});
            for (int x = x0; x < x1; ++x)
                drawing.layers.back().image.setPixel(x, 5, color);
            return &drawing.layers.back();
        };
        layer(1, "base", 0, 10, 0xff0000ff);   // blue over x 0..9
        layer(2, "red", 0, 20, 0xffff0000);    // red over x 0..19
        layer(3, "green", 0, 20, 0xff00ff00);  // green over x 0..19
        const QRect area(0, 0, 20, 10);
        auto at = [&](int x) { return canvas::composeDrawing(drawing, area).pixelColor(x, 5); };
        QCOMPARE(at(15), QColor(Qt::green)); // nothing clipped: the top one everywhere
        drawing.layers[1].properties.clipped = true;
        QCOMPARE(at(5), QColor(Qt::green));
        QCOMPARE(at(15), QColor(Qt::green));         // the green one is not clipped
        drawing.layers[2].properties.clipped = true; // both clipped to the base
        QCOMPARE(at(5), QColor(Qt::green));
        QCOMPARE(at(15).alpha(), 0);                 // outside the base nothing of either shows
        drawing.layers[2].properties.visible = false;
        QCOMPARE(at(5), QColor(Qt::red));            // the red one alone, over the base
        QCOMPARE(at(15).alpha(), 0);
        // Its opacity is its own; the base's does not reach it.
        drawing.layers[1].properties.opacity = 0.5f;
        drawing.layers[0].properties.opacity = 0.2f;
        QVERIFY(at(5).alpha() > 140 && at(5).alpha() < 165); // 0.5 of its own over the base's 0.2 (not 0.1 over it)
        drawing.layers[0].properties.opacity = 1.0f;
        drawing.layers[1].properties.opacity = 1.0f;
        // A hidden base hides what is clipped to it.
        drawing.layers[0].properties.visible = false;
        QCOMPARE(at(5).alpha(), 0);
        drawing.layers[0].properties.visible = true;
        // The mask is the pixels of the base: half of a pixel's opacity lets half through.
        drawing.layers[0].image.setPixel(7, 5, 0x800000ff); // premultiplied: half blue
        QVERIFY(at(7).alpha() > 180 && at(7).alpha() < 200); // half of the red over the half of blue: 0.5 + 0.5 * 0.5
        // With nothing below it a clipped layer shows as any other.
        drawing.layers[0].properties.clipped = true;
        QCOMPARE(at(15), QColor(Qt::red)); // no layer to clip the red one to now: it shows whole
        // Not clipped any more: all there again.
        for (doc::Layer &l : drawing.layers)
            l.properties.clipped = false;
        drawing.layers[2].properties.visible = true;
        QCOMPARE(at(15), QColor(Qt::green));
    }

    // The layers of a drawing are composed apart, like a folder, then the
    // result goes over the cels below with the cel's opacity and blend.
    void drawingLayersComposeLikeAFolder()
    {
        doc::Document document(64, 64);
        doc::AddCelCommand bottom(document, 0, {"B"});
        bottom.apply(document);
        doc::NewDrawing green = doc::makeNewDrawing(document, bottom.celId(), 0);
        green.command->apply(document);
        document.cel(bottom.celId())->drawing(green.id)->layers.front().image.setPixel(5, 5, 0xff00ff00);

        doc::AddCelCommand top(document, 1, {"T"});
        top.apply(document);
        doc::NewDrawing two = doc::makeNewDrawing(document, top.celId(), 0);
        two.command->apply(document);
        doc::Drawing &drawing = *document.cel(top.celId())->drawing(two.id);
        // A multiplying red layer over nothing in its own drawing: red, not
        // red times the green of the cel below (that would be black).
        drawing.layers.push_back({document.allocateLayerId(), {"M", true, false, 1.0f, doc::BlendMode::Multiply}, {}});
        drawing.layers.back().image.setPixel(5, 5, 0xffff0000);

        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(5, 5), QColor(Qt::red));
        QCOMPARE(canvas::drawingBounds(drawing), QRect(0, 0, 64, 64));
        QCOMPARE(canvas::composeDrawing(drawing, QRect(4, 4, 4, 4)).pixelColor(1, 1), QColor(Qt::red));

        // The cel's blend applies to the folder: multiplied with the green, black.
        doc::CelProperties multiply = document.cel(top.celId())->properties();
        multiply.blend = doc::BlendMode::Multiply;
        document.cel(top.celId())->setProperties(multiply);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(5, 5), QColor(Qt::black));

        // Hidden layers are left out; a lone one at half opacity in a cel at
        // half opacity shows at a quarter.
        drawing.layers.back().properties.visible = false;
        drawing.layers.front().image.setPixel(20, 20, 0xff0000ff);
        drawing.layers.front().properties.opacity = 0.5f;
        multiply.blend = doc::BlendMode::Normal;
        multiply.opacity = 0.5f;
        document.cel(top.celId())->setProperties(multiply);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(5, 5), QColor(Qt::green));
        const QColor quarter = renderer.image().pixelColor(20, 20);
        QVERIFY(qAbs(quarter.red() - 191) <= 2 && quarter.blue() == 255);
    }

    // A perspective layer draws its grid where the plane is; without guides
    // (export) it is left out.
    void perspectiveLayerDrawsItsGrid()
    {
        doc::Document document(400, 300);
        doc::AddCelCommand cel(document, 0, {"C"});
        cel.apply(document);
        doc::NewDrawing nd = doc::makeNewDrawing(document, cel.celId(), 0);
        nd.command->apply(document);
        doc::Drawing &drawing = *document.cel(cel.celId())->drawing(nd.id);
        doc::LayerProperties props{"P"};
        props.perspective = perspective::defaultGrid(400, 300);
        props.perspective.orientation = {}; // a flat grid, lines every 25 px from the center
        props.perspective.color = 0xff0000ff;
        drawing.layers.push_back({document.allocateLayerId(), props, {}, doc::LayerKind::Perspective});

        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        // The vertical line through the center (1 px wide, antialiased over two columns).
        const QColor onLine = renderer.image().pixelColor(200, 150 + 12);
        QVERIFY(onLine.blue() > 200 && onLine.blue() - onLine.red() > 80);
        QCOMPARE(renderer.image().pixelColor(212, 162), QColor(Qt::white)); // inside a cell: paper
        QVERIFY(canvas::drawingBounds(drawing).contains(QRect(100, 50, 200, 200)));
        QVERIFY(canvas::drawingBounds(drawing, false).isEmpty());

        renderer.setGuidesVisible(false);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(200, 162), QColor(Qt::white));
    }

    // The filters of the transform: a small picture made bigger.
    void resamplingFiltersScaleUpAndKeepWhatTheyShouldKeep()
    {
        using canvas::ResampleFilter;
        // A hard edge, black and white, 8 px wide: made four times bigger.
        QImage edge(8, 4, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
                edge.setPixel(x, y, x < 4 ? 0xff000000u : 0xffffffffu);
        const QTransform scale = QTransform::fromScale(4, 4);
        // Identity: every filter gives the picture back.
        for (int f = 0; f < canvas::kResampleFilters; ++f)
            QCOMPARE(canvas::resampleTransformed(edge, QTransform(), edge.size(), ResampleFilter(f)), edge);
        // Nearest neighbor: only the two colors. The others: a ramp over a few pixels, steeper in the middle with the
        // sharper filters (the biggest step from a pixel to the next is the measure).
        auto steepest = [&](ResampleFilter filter) {
            const QImage big = canvas::resampleTransformed(edge, scale, QSize(32, 16), filter);
            int most = 0;
            for (int x = 1; x < 32; ++x)
                most = std::max(most, std::abs(qRed(big.pixel(x, 8)) - qRed(big.pixel(x - 1, 8))));
            return most;
        };
        const QImage hard = canvas::resampleTransformed(edge, scale, QSize(32, 16), ResampleFilter::Nearest);
        for (int x = 0; x < 32; ++x)
            QVERIFY(hard.pixel(x, 8) == 0xff000000u || hard.pixel(x, 8) == 0xffffffffu);
        QCOMPARE(steepest(ResampleFilter::Nearest), 255);
        QVERIFY(steepest(ResampleFilter::Bicubic) > steepest(ResampleFilter::Bilinear) + 8);
        QVERIFY(steepest(ResampleFilter::Lanczos3) > steepest(ResampleFilter::Bilinear) + 8);
        QVERIFY(steepest(ResampleFilter::Bilinear) < 100); // a soft ramp
        // Never a color past the alpha, and nothing outside the picture.
        for (int f = 0; f < canvas::kResampleFilters; ++f) {
            QImage half(6, 6, QImage::Format_ARGB32_Premultiplied);
            half.fill(qPremultiply(qRgba(200, 40, 10, 128)));
            half.setPixel(2, 2, 0xff000000u);
            const QImage big = canvas::resampleTransformed(half, QTransform::fromTranslate(5, 5) * QTransform::fromScale(3, 3),
                                                           QSize(60, 60), ResampleFilter(f));
            for (int y = 0; y < 60; ++y)
                for (int x = 0; x < 60; ++x) {
                    const QRgb p = big.pixel(x, y);
                    QVERIFY(qRed(p) <= qAlpha(p) && qGreen(p) <= qAlpha(p) && qBlue(p) <= qAlpha(p));
                }
            QCOMPARE(qAlpha(big.pixel(2, 2)), 0);   // before the picture starts
            QVERIFY(qAlpha(big.pixel(30, 30)) > 100); // inside it
            QCOMPARE(qAlpha(big.pixel(58, 58)), 0); // after it ends
        }
        // The edge of a picture fades over a pixel of the result: not a staircase when it is turned.
        QImage solid(10, 10, QImage::Format_ARGB32_Premultiplied);
        solid.fill(Qt::white);
        const QImage turned = canvas::resampleTransformed(solid, QTransform().translate(20, 5).rotate(30) * QTransform::fromScale(1, 1),
                                                          QSize(40, 40), ResampleFilter::Lanczos3);
        bool partial = false;
        for (int y = 0; y < 40 && !partial; ++y)
            for (int x = 0; x < 40; ++x)
                partial = partial || (qAlpha(turned.pixel(x, y)) > 10 && qAlpha(turned.pixel(x, y)) < 245);
        QVERIFY(partial);
    }

    // Made smaller, the wide filters average what each pixel covers: a 1 px line pattern gives an even gray, not a
    // pattern of what the sampling happened to hit.
    void resamplingSmallerAveragesInsteadOfSkipping()
    {
        using canvas::ResampleFilter;
        QImage lines(64, 8, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 64; ++x)
                lines.setPixel(x, y, x % 2 ? 0xffffffffu : 0xff000000u); // black and white, a pixel each
        auto spread = [&](ResampleFilter filter) { // how much the values vary across the row, from 8 to 56
            const QImage small = canvas::resampleTransformed(lines, QTransform::fromScale(0.4, 0.4), QSize(25, 2), filter);
            int lo = 255, hi = 0;
            for (int x = 3; x < 22; ++x) {
                lo = std::min(lo, qRed(small.pixel(x, 1)));
                hi = std::max(hi, qRed(small.pixel(x, 1)));
            }
            return hi - lo;
        };
        QVERIFY2(spread(ResampleFilter::Lanczos3) < 10, "Lanczos3 averages the lines");
        QVERIFY2(spread(ResampleFilter::Bicubic) < 10, "bicubic does");
        QVERIFY2(spread(ResampleFilter::Bilinear) < 30, "and bilinear, stretched, nearly");
        QVERIFY(spread(ResampleFilter::Nearest) > 200); // while skipping pixels gives black or white
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
