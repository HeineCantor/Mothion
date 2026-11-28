#include "canvas/ColorAdjust.h"
#include "canvas/FrameRenderer.h"
#include "canvas/Resample.h"
#include "canvas/StrokeSession.h"
#include "canvas/Symmetry.h"
#include "canvas/VectorErase.h"
#include "canvas/VectorPoints.h"

#include <map>
#include "canvas/VectorRender.h"
#include "canvas/SelectionMask.h"
#include "canvas/VectorWarp.h"
#include "core/puppet/PuppetRig.h"
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

// A stroke along x from 0 to 2 * (n - 1), a point every 2 px, at y 0, pressure 0.5.
doc::VectorStroke straightStroke(int n)
{
    doc::VectorStroke s;
    for (int i = 0; i < n; ++i) {
        brush::BrushInput in;
        in.x = 2.0 * i;
        in.pressure = 0.5;
        s.points.push_back(in);
    }
    return s;
}

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
private slots:
    // The anchors of a stroke: its ends, its corners, and the points where the path bends past the tolerance.
    void controlPointsKeepTheEndsCornersAndBends()
    {
        doc::VectorStroke line = straightStroke(51);
        QCOMPARE(controlPoints(line, 1.0), (std::vector<int>{0, 50}));
        line.points[20].corner = true;
        QCOMPARE(controlPoints(line, 1.0), (std::vector<int>{0, 20, 50}));
        doc::VectorStroke bent = straightStroke(51); // an L: up from the middle on
        for (int i = 26; i <= 50; ++i) {
            bent.points[std::size_t(i)].x = 50.0;
            bent.points[std::size_t(i)].y = -2.0 * (i - 25);
        }
        QCOMPARE(controlPoints(bent, 1.0), (std::vector<int>{0, 25, 50}));
        QCOMPARE(controlPoints(doc::VectorStroke{}, 1.0), std::vector<int>());
        // A smooth curve is one Bezier curve, not a polyline: no anchors along an arc (a polyline would need several).
        doc::VectorStroke arc;
        for (int i = 0; i <= 90; ++i) {
            brush::BrushInput in;
            in.x = 100 * std::cos(i * std::numbers::pi / 180);
            in.y = 100 * std::sin(i * std::numbers::pi / 180);
            arc.points.push_back(in);
        }
        QCOMPARE(controlPoints(arc, 1.0), (std::vector<int>{0, 90}));
        // An anchor put by hand stays one.
        arc.points[30].keptAnchor = true;
        QCOMPARE(controlPoints(arc, 1.0), (std::vector<int>{0, 30, 90}));
    }

    // An anchor moved takes its handles along: the runs beside it bend to the curves their ends make now, the anchors beside
    // it stay; what is near it goes along with it as a whole, halfway about half as far. Pressure stays; points are put in
    // where a run grew longer.
    void movingAnAnchorBendsTheLineRoundIt()
    {
        const doc::VectorStroke line = straightStroke(51); // x from 0 to 100
        const std::vector<int> anchors{0, 25, 50};
        const std::vector<QPointF> offsets{{}, {0, 20}, {}};
        std::vector<int> after;
        const doc::VectorStroke smooth = movedAnchors(line, anchors, offsets, &after);
        QCOMPARE(after.size(), anchors.size());
        QCOMPARE(int(smooth.points.size()), after.back() + 1);
        QCOMPARE(smooth.points[std::size_t(after[1])].y, 20.0);
        QCOMPARE(smooth.points[std::size_t(after[1])].x, 50.0);
        QCOMPARE(smooth.points.front().y, 0.0);
        QCOMPARE(smooth.points.back().y, 0.0);
        QCOMPARE(smooth.points.back().x, 100.0);
        auto yAt = [&](double x) { // the line's height where it passes x
            for (std::size_t i = 1; i < smooth.points.size(); ++i)
                if (smooth.points[i - 1].x <= x && smooth.points[i].x >= x) {
                    const double w = (x - smooth.points[i - 1].x) / std::max(1e-9, smooth.points[i].x - smooth.points[i - 1].x);
                    return smooth.points[i - 1].y + w * (smooth.points[i].y - smooth.points[i - 1].y);
                }
            return 0.0;
        };
        QVERIFY(yAt(48) > 19.0);                     // along with it as a whole
        QVERIFY(std::abs(yAt(25) - 10.0) < 2.5);     // halfway, about half as far
        QVERIFY(yAt(2) < 2.0);                       // the anchor beside it stays
        QCOMPARE(smooth.points[30].pressure, 0.5);
        QCOMPARE(anchorWeight(0.5, false, false), 0.5);
        QCOMPARE(anchorWeight(0.25, true, true), 0.25);
        QCOMPARE(movedAnchors(line, anchors, {{}, {}, {}}), line); // nothing moved: the same stroke
    }

    // Pulled hard many times, a point moved far first and its handles thrown about (each drag from where the last left them,
    // as the tool does): the line bends, it never breaks into spikes or jumps (no point far from the one before it, no turn
    // back on itself from one point to the next).
    void handlesPulledHardNeverBreakTheLine()
    {
        doc::VectorStroke st; // a wave drawn fast: its points far apart
        for (int i = 0; i <= 120; ++i) {
            brush::BrushInput in;
            in.x = 200 + 12.0 * i;
            in.y = 600 + 200 * std::sin(i / 18.0) + 3 * std::sin(i * 1.3);
            st.points.push_back(in);
        }
        auto worst = [](const doc::VectorStroke &x, double *sharpest) {
            double gap = 0, turn = 0;
            for (std::size_t i = 1; i < x.points.size(); ++i) {
                const QPointF a(x.points[i].x - x.points[i - 1].x, x.points[i].y - x.points[i - 1].y);
                gap = std::max(gap, std::hypot(a.x(), a.y()));
                if (i + 1 < x.points.size()) {
                    const QPointF b(x.points[i + 1].x - x.points[i].x, x.points[i + 1].y - x.points[i].y);
                    turn = std::max(turn, std::abs(std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y())));
                }
            }
            *sharpest = turn;
            return gap;
        };
        double turn = 0;
        const double gap = worst(st, &turn);
        std::vector<int> anchors = controlPoints(st, 6.0);
        QVERIFY2(anchors.size() >= 4 && anchors.size() <= 8, qPrintable(QString::number(anchors.size()))); // the bends, not each wobble
        std::vector<QPointF> offsets(anchors.size());
        offsets[1] = QPointF(350, 250);
        st = movedAnchors(st, anchors, offsets, &anchors);
        double moveTurn = 0;
        QVERIFY2(worst(st, &moveTurn) <= gap * 1.2, qPrintable(QString::number(worst(st, &moveTurn))));
        QVERIFY2(moveTurn < 2.0, qPrintable(QString::number(moveTurn)));
        std::map<std::pair<int, bool>, QPointF> memory;
        const KnownHandle known = [&](int k, bool out) -> std::optional<QPointF> {
            const auto it = memory.find({k, out});
            return it == memory.end() ? std::nullopt : std::optional<QPointF>(it->second);
        };
        const QPointF a(st.points[std::size_t(anchors[1])].x, st.points[std::size_t(anchors[1])].y);
        const std::pair<int, QPointF> pulls[] = {{1, a + QPointF(400, -300)}, {1, a + QPointF(-500, -100)},
                                                 {1, a + QPointF(50, 600)}, {2, QPointF(1500, 100)}};
        for (const auto &[k, to] : pulls) {
            HandleChange change;
            st = movedHandle(st, anchors, k, k == 1, to, true, known, &change);
            anchors = change.anchors;
            for (const auto &h : change.handles)
                memory[{h.k, h.out}] = h.at;
            double sharpest = 0;
            QVERIFY2(worst(st, &sharpest) <= gap * 1.2, qPrintable(QString::number(worst(st, &sharpest))));
            QVERIFY2(sharpest < 2.0, qPrintable(QString::number(sharpest))); // under 115 degrees from one point to the next
        }
    }

    // An anchor's Bezier handles: on a straight run a third of the way to the anchors beside it; one dragged bends the run on
    // its side (its ends stay, pressure too), and on a smooth anchor the run on the other side turns with it, unless not
    // mirrored, or at a corner.
    void aHandleDraggedBendsTheRunOnItsSide()
    {
        const doc::VectorStroke line = straightStroke(61); // x from 0 to 120, y 0; the middle anchor at 60
        const std::vector<int> anchors{0, 30, 60};
        const QPointF out = *anchorHandle(line, anchors, 1, true), in = *anchorHandle(line, anchors, 1, false);
        QVERIFY(std::hypot(out.x() - 80, out.y()) < 0.5);
        QVERIFY(std::hypot(in.x() - 40, in.y()) < 0.5);
        QVERIFY(!anchorHandle(line, anchors, 0, false)); // no run before the first anchor
        QVERIFY(!anchorHandle(line, anchors, 2, true));
        // The out handle pulled down: the run after the anchor bulges down, the anchors stay.
        canvas::HandleChange change; // (the run grew longer: points were put in, and the last anchor moved along)
        const doc::VectorStroke alone = movedHandle(line, anchors, 1, true, out + QPointF(0, 30), false, {}, &change);
        const std::vector<int> &moved = change.anchors;
        QCOMPARE(moved[0], 0);
        QCOMPARE(moved[1], 30);
        QVERIFY(moved[2] > 60);
        QCOMPARE(int(alone.points.size()), moved[2] + 1);
        QCOMPARE(alone.points[30].y, 0.0);
        QCOMPARE(alone.points[std::size_t(moved[2])].y, 0.0);
        QCOMPARE(alone.points[std::size_t(moved[2])].x, 120.0);
        double deepest = 0;
        for (int i = 30; i <= moved[2]; ++i)
            deepest = std::max(deepest, alone.points[std::size_t(i)].y);
        QVERIFY(deepest > 5.0);
        QCOMPARE(alone.points[15].y, 0.0); // the other side, not mirrored: as it was
        QCOMPARE(alone.points[40].pressure, line.points[40].pressure);
        // Mirrored on a smooth anchor: the run before turns the other way (up), so the line stays smooth through it.
        canvas::HandleChange smoothChange;
        const doc::VectorStroke smooth = movedHandle(line, anchors, 1, true, out + QPointF(0, 30), true, {}, &smoothChange);
        // The handle found again the way it was let go (a fit: its length only about; the tool shows it where it was put).
        const QPointF found = *anchorHandle(smooth, smoothChange.anchors, 1, true) - QPointF(60, 0);
        const QPointF put = out + QPointF(0, 30) - QPointF(60, 0);
        const double angle = std::abs(std::atan2(found.x() * put.y() - found.y() * put.x(), found.x() * put.x() + found.y() * put.y()));
        QVERIFY2(angle < 15.0 * std::numbers::pi / 180.0, qPrintable(QStringLiteral("%1 %2").arg(found.x()).arg(found.y())));
        double highest = 0; // (y down: the most negative)
        for (int i = 0; i <= 30; ++i)
            highest = std::min(highest, smooth.points[std::size_t(i)].y);
        QVERIFY(highest < -5.0);
        QCOMPARE(smooth.points[0].y, 0.0);
        // At a corner the other side stays.
        doc::VectorStroke corner = line;
        corner.points[30].corner = true;
        QCOMPARE(movedHandle(corner, anchors, 1, true, out + QPointF(0, 30), true).points[22].y, 0.0);
        // A handle pulled far out: the run's points stay evenly along the curve (no long straight pieces between a few
        // points, which would draw the curve with corners).
        canvas::HandleChange farChange;
        const doc::VectorStroke far = movedHandle(line, anchors, 1, true, QPointF(100, 160), false, {}, &farChange);
        const std::vector<int> &farAnchors = farChange.anchors;
        double length = 0, widest = 0;
        for (int i = 31; i <= farAnchors[2]; ++i) {
            const double step = std::hypot(far.points[std::size_t(i)].x - far.points[std::size_t(i - 1)].x,
                                           far.points[std::size_t(i)].y - far.points[std::size_t(i - 1)].y);
            length += step;
            widest = std::max(widest, step);
        }
        QVERIFY2(widest < 1.5 * length / (farAnchors[2] - 30), qPrintable(QStringLiteral("%1 of %2").arg(widest).arg(length / 30)));
        QVERIFY(widest < 2.0 * 2.0); // and as dense as the line was (a point every 2 px), or about
        // Not moved: the same line.
        const doc::VectorStroke same = movedHandle(line, anchors, 1, true, out, true);
        for (std::size_t i = 0; i < line.points.size(); ++i)
            QVERIFY(std::abs(same.points[i].y - line.points[i].y) < 1e-9);
    }

    // A line carried by the puppet warp is as wide as the mesh was stretched across it, not along it: squeezed along the
    // line it keeps its width; and a tip held at an angle (a flat brush) turns as far as the mesh turned there.
    void aWarpedLineFollowsTheMeshAcrossIt()
    {
        puppet::Bitmask m(0, 0, 220, 60);
        for (int y = 20; y < 40; ++y)
            for (int x = 10; x < 210; ++x)
                m.set(x, y, true); // a long bar
        doc::VectorStroke line;
        line.size = 10;
        for (int x = 20; x <= 200; x += 5) {
            brush::BrushInput in;
            in.x = x;
            in.y = 30;
            line.points.push_back(in);
        }
        {
            puppet::PuppetRig rig(m, {2, 3});
            QVERIFY(rig.addPin({20, 30}));
            const auto handle = rig.addPin({200, 30});
            QVERIFY(handle);
            rig.movePin(*handle, {120, 30}, 30); // squeezed along the line, towards the anchor
            const std::vector<doc::VectorStroke> warped = canvas::warpedStrokes({line}, rig);
            QCOMPARE(warped.size(), std::size_t(1));
            QVERIFY(warped[0].points.back().x < 150); // carried along
            for (const brush::BrushInput &in : warped[0].points)
                QVERIFY2(in.sizeScale > 0.6 && in.sizeScale < 1.6, qPrintable(QString::number(in.sizeScale))); // not as the area went (0.2)
            QCOMPARE(warped[0].size, 10.0);
        }
        {
            puppet::PuppetRig rig(m, {2, 3});
            QVERIFY(rig.addPin({20, 30}));
            const auto handle = rig.addPin({200, 30});
            QVERIFY(handle);
            rig.movePin(*handle, {20 + 180 * std::cos(0.5), 30 + 180 * std::sin(0.5)}, 60); // turned about 29° down
            const std::vector<doc::VectorStroke> warped = canvas::warpedStrokes({line}, rig);
            const brush::BrushInput &far = warped[0].points.back();
            QVERIFY2(far.tipAngle > 15 && far.tipAngle < 40, qPrintable(QString::number(far.tipAngle)));
        }
    }

    // A shape of colour (a fill on a vector layer) is drawn as its outlines filled, holes by the even-odd rule; it is cut
    // along a selection's outline when that is given (whole otherwise, inside only when all of it is); the vector eraser
    // leaves it alone; the puppet warp keeps its outlines apart.
    void aFillIsAShapeOfOutlines()
    {
        tools::ToolState state;
        const doc::VectorStroke shape = canvas::makeFill({{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, {{30, 30}, {70, 30}, {70, 70}, {30, 70}}},
                                                         0xff2040c0u, 1.0);
        QVERIFY(shape.fill);
        QCOMPARE(shape.outlines, std::vector<int>({4}));
        QCOMPARE(int(canvas::fillOutlines(shape).size()), 2);
        doc::TiledImage image;
        canvas::drawStrokes(image, {shape}, state, {});
        QCOMPARE(image.pixel(10, 50), doc::Pixel(0xff2040c0u));
        QCOMPARE(qAlpha(image.pixel(50, 50)), 0); // the hole
        QCOMPARE(qAlpha(image.pixel(150, 50)), 0);

        QPainterPath left;
        left.addRect(-10, -10, 60, 130); // x up to 50
        const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(left);
        const canvas::SplitStroke cut = canvas::splitStrokeByMask(shape, mask, left);
        QCOMPARE(cut.inside.size(), std::size_t(1));
        QCOMPARE(cut.outside.size(), std::size_t(1));
        doc::TiledImage halves;
        canvas::drawStrokes(halves, cut.inside, state, {});
        QCOMPARE(halves.pixel(10, 50), doc::Pixel(0xff2040c0u));
        QCOMPARE(qAlpha(halves.pixel(90, 50)), 0);
        QCOMPARE(qAlpha(halves.pixel(40, 50)), 0); // the hole's half stays a hole
        const canvas::SplitStroke whole = canvas::splitStrokeByMask(shape, mask);
        QVERIFY(whole.inside.empty());
        QCOMPARE(whole.outside.size(), std::size_t(1));

        const canvas::VectorErasure erased = canvas::eraseFromStrokes({shape}, {QPointF(10, 50), QPointF(90, 50)}, 20,
                                                                      canvas::VectorEraseMode::WholeLine);
        QVERIFY(!erased.changed);
        QCOMPARE(erased.strokes.size(), std::size_t(1));
    }

    // The width at an anchor, and less and less towards the anchors beside it.
    void theWidthChangesRoundAnAnchor()
    {
        const doc::VectorStroke line = straightStroke(51);
        const doc::VectorStroke wide = scaledWidths(line, {0, 25, 50}, {1.0, 2.0, 1.0});
        QCOMPARE(wide.points[25].sizeScale, 2.0);
        QCOMPARE(wide.points[0].sizeScale, 1.0);
        QCOMPARE(wide.points[50].sizeScale, 1.0);
        QVERIFY(wide.points[12].sizeScale > 1.3 && wide.points[12].sizeScale < 1.7);
        QVERIFY(strokeReach(wide).height() > strokeReach(line).height());
    }

    // An anchor added on the line: a point of it when one is near, else a point put in; the anchors put right.
    void anAnchorIsAddedOnTheLine()
    {
        doc::VectorStroke two = straightStroke(2); // from (0, 0) to (2, 0)
        two.points[1].x = 100;
        std::vector<int> anchors{0, 1};
        const auto place = nearestPlace(two, QPointF(40, 5));
        QVERIFY(place);
        QCOMPARE(place->segment, 0);
        QCOMPARE(place->distance, 5.0);
        QCOMPARE(addAnchor(two, anchors, *place, 1.0), 1);
        QCOMPARE(two.points.size(), std::size_t(3));
        QCOMPARE(two.points[1].x, 40.0);
        QCOMPARE(anchors, (std::vector<int>{0, 1, 2}));

        doc::VectorStroke dense = straightStroke(51);
        std::vector<int> denseAnchors{0, 50};
        QCOMPARE(addAnchor(dense, denseAnchors, *nearestPlace(dense, QPointF(40.4, 3)), 1.0), 1);
        QCOMPARE(dense.points.size(), std::size_t(51)); // the point at 40 was near enough
        QCOMPARE(denseAnchors, (std::vector<int>{0, 20, 50}));
    }

    // An anchor taken away: inside, the line is laid smooth again between the anchors beside it; at an end, the line
    // is cut short to the next one; two anchors are the fewest.
    void anAnchorIsTakenAway()
    {
        std::vector<int> anchors{0, 25, 50};
        doc::VectorStroke bent = movedAnchors(straightStroke(51), anchors, {{}, {0, 20}, {}}, &anchors);
        const std::size_t count = bent.points.size();
        QVERIFY(removeAnchor(bent, anchors, 1));
        QCOMPARE(anchors, (std::vector<int>{0, int(count) - 1}));
        for (const brush::BrushInput &in : bent.points)
            QVERIFY(std::abs(in.y) < 1e-9); // straight between the two ends left
        QCOMPARE(bent.points.size(), count);
        QVERIFY(!removeAnchor(bent, anchors, 0));

        doc::VectorStroke line = straightStroke(51);
        std::vector<int> three{0, 20, 50};
        QVERIFY(removeAnchor(line, three, 0));
        QCOMPARE(line.points.front().x, 40.0);
        QCOMPARE(three, (std::vector<int>{0, 30}));
        QCOMPARE(line.points.size(), std::size_t(31));
        std::vector<int> again{0, 10, 30};
        QVERIFY(removeAnchor(line, again, 2));
        QCOMPARE(line.points.back().x, 60.0);
        QCOMPARE(line.points.size(), std::size_t(11));
    }

    // A corner made smooth rounds the angle: the line passes through the anchor without turning there.
    void aCornerMadeSmoothIsRounded()
    {
        doc::VectorStroke bent = straightStroke(51); // an L
        for (int i = 26; i <= 50; ++i) {
            bent.points[std::size_t(i)].x = 50.0;
            bent.points[std::size_t(i)].y = -2.0 * (i - 25);
        }
        const std::vector<int> anchors{0, 25, 50};
        toggleCorner(bent, anchors, 1);
        QVERIFY(bent.points[25].corner);
        QCOMPARE(bent.points[24].y, 0.0); // made a corner: the line stays
        toggleCorner(bent, anchors, 1);
        QVERIFY(!bent.points[25].corner);
        QCOMPARE(bent.points[25].x, 50.0); // the anchor stays
        QCOMPARE(bent.points[25].y, 0.0);
        // The angle is rounded: the line goes through the anchor without turning there.
        const QLineF in(QPointF(bent.points[24].x, bent.points[24].y), QPointF(bent.points[25].x, bent.points[25].y));
        const QLineF out(QPointF(bent.points[25].x, bent.points[25].y), QPointF(bent.points[26].x, bent.points[26].y));
        const double turn = std::min(in.angleTo(out), 360.0 - in.angleTo(out));
        QVERIFY2(turn < 15.0, qPrintable(QString::number(turn)));
    }

    // Two strokes merged: from the first's other end to the second's other end, with the first's brush; a gap filled
    // with a curve that leaves and meets the ends along their lines; ends on each other made one point.
    void twoStrokesAreMerged()
    {
        doc::VectorStroke left = straightStroke(11); // 0..20 on y = 0
        left.brush = "G-pen";
        left.size = 9;
        doc::VectorStroke right = straightStroke(11);
        right.brush = "Pencil";
        for (brush::BrushInput &in : right.points)
            in.x += 40.0; // 40..60
        right.points.front().corner = true;

        const doc::VectorStroke m = mergedStrokes(left, true, right, false);
        QCOMPARE(m.brush, std::string("G-pen"));
        QCOMPARE(m.size, 9.0);
        QCOMPARE(m.points.front().x, 0.0);
        QCOMPARE(m.points.back().x, 60.0);
        QVERIFY(m.points.size() > 22); // the gap filled with points
        for (std::size_t i = 1; i < m.points.size(); ++i) {
            QVERIFY(std::abs(m.points[i].y) < 1e-9); // straight on: both lines go the same way
            QVERIFY(m.points[i].x > m.points[i - 1].x);
            QVERIFY(m.points[i].x - m.points[i - 1].x <= 2.0 + 1e-9);
            QVERIFY(!m.points[i].corner);
        }

        // Joined by their starts: the first goes backwards, then along the second.
        const doc::VectorStroke back = mergedStrokes(right, false, left, false);
        QCOMPARE(back.brush, std::string("Pencil"));
        QCOMPARE(back.points.front().x, 60.0);
        QCOMPARE(back.points.back().x, 20.0);
        QCOMPARE(back.points[back.points.size() - 11].x, 0.0); // the second, its start first

        // A gap between lines going different ways: the curve leaves each along its line.
        doc::VectorStroke down = straightStroke(11);
        for (brush::BrushInput &in : down.points) {
            in.y = 40.0 + in.x; // from (40, 40) down to (40, 60)
            in.x = 40.0;
        }
        const doc::VectorStroke bend = mergedStrokes(left, true, down, false);
        const brush::BrushInput &after = bend.points[11]; // the first point of the curve
        QVERIFY(after.x > 20.0);
        QVERIFY(std::abs(after.y) < std::abs(after.x - 20.0)); // leaving along the first line (to the right)
        const brush::BrushInput &before = bend.points[bend.points.size() - 12]; // the last one before the second
        QVERIFY(std::abs(before.x - 40.0) < std::abs(before.y - 40.0)); // meeting the second along its line (down)

        // Ends on each other: one point.
        doc::VectorStroke touching = straightStroke(11);
        for (brush::BrushInput &in : touching.points)
            in.x += 20.0; // 20..40
        QCOMPARE(mergedStrokes(left, true, touching, false).points.size(), std::size_t(21));
    }

    // A line made a loop: the gap filled, the last point on the first; opened again it is one line from the cut to the
    // cut. The control points at the joint work as any other; the eraser and the selection leave no piece cut at it.
    void aLineIsClosedIntoALoopAndOpened()
    {
        // Three sides of a square of side 100, the fourth left open (from (0, 100) back up to (0, 0)).
        doc::VectorStroke open;
        auto put = [&open](double x, double y) {
            brush::BrushInput in = point(x, y);
            open.points.push_back(in);
        };
        for (int i = 0; i < 50; ++i)
            put(2.0 * i, 0);
        for (int i = 0; i < 50; ++i)
            put(100, 2.0 * i);
        for (int i = 0; i <= 50; ++i)
            put(100 - 2.0 * i, 100);
        open.size = 4;
        const doc::VectorStroke loop = canvas::closedStroke(open);
        QVERIFY(loop.closed);
        QVERIFY(loop.points.size() > open.points.size() + 20);
        QCOMPARE(loop.points.front().x, loop.points.back().x);
        QCOMPARE(loop.points.front().y, loop.points.back().y);
        for (std::size_t i = 1; i < loop.points.size(); ++i) // no jumps, nothing run twice
            QVERIFY(std::hypot(loop.points[i].x - loop.points[i - 1].x, loop.points[i].y - loop.points[i - 1].y) < 5.0);
        QCOMPARE(canvas::closedStroke(loop), loop);

        // Opened at the bottom side: both ends there, the same length of line.
        const auto place = canvas::nearestPlace(loop, QPointF(50, 100));
        const doc::VectorStroke opened = canvas::openedStroke(loop, *place, 2.0);
        QVERIFY(!opened.closed);
        QVERIFY(std::abs(opened.points.front().x - 50) < 2.1 && std::abs(opened.points.front().y - 100) < 1e-6);
        QCOMPARE(opened.points.front().x, opened.points.back().x);
        QVERIFY(opened.points.size() >= loop.points.size());

        // The node at the joint moved: both its points, and the loop stays closed.
        const std::vector<int> anchors = canvas::controlPoints(loop, 3.0);
        std::vector<QPointF> offsets(anchors.size());
        offsets.front() = QPointF(-10, -10);
        const doc::VectorStroke moved = canvas::movedAnchors(loop, anchors, offsets);
        QVERIFY(moved.closed);
        QCOMPARE(moved.points.front().x, loop.points.front().x - 10);
        QCOMPARE(moved.points.back().x, moved.points.front().x);
        QCOMPARE(moved.points.back().y, moved.points.front().y);
        // A handle of the joint's, a corner there, and the node taken away: still a loop.
        std::vector<int> after = anchors;
        const auto in = canvas::anchorHandle(loop, anchors, int(anchors.size()) - 1, false);
        QVERIFY(in);
        canvas::HandleChange change;
        const doc::VectorStroke bent = canvas::movedHandle(loop, anchors, 0, true, QPointF(30, -20), true, {}, &change);
        QVERIFY(bent.closed);
        QCOMPARE(change.anchors.size(), anchors.size());
        QCOMPARE(change.anchors.front(), 0);
        QCOMPARE(change.anchors.back(), int(bent.points.size()) - 1);
        QCOMPARE(bent.points.front().x, loop.points.front().x);
        QCOMPARE(bent.points.back().x, loop.points.front().x);
        doc::VectorStroke cornered = loop;
        canvas::toggleCorner(cornered, anchors, 0);
        QVERIFY(cornered.closed);
        QCOMPARE(cornered.points.front().corner, !loop.points.front().corner);
        QCOMPARE(cornered.points.back().corner, cornered.points.front().corner);
        doc::VectorStroke fewer = loop;
        QVERIFY(canvas::removeAnchor(fewer, after, 0));
        QVERIFY(fewer.closed);
        QCOMPARE(after.size(), anchors.size() - 1);
        QCOMPARE(fewer.points.front().x, fewer.points.back().x);
        QCOMPARE(fewer.points.front().y, fewer.points.back().y);

        // The eraser through the joint (the middle of the left side, in the gap that was filled), or anywhere: one open
        // line is left.
        for (const QPointF at : {QPointF(-25, 50), QPointF(100, 50)}) { // (the gap's curve bulges to the left)
            const canvas::VectorErasure erased =
                canvas::eraseFromStrokes({loop}, {at}, 8, canvas::VectorEraseMode::Part);
            QCOMPARE(erased.strokes.size(), std::size_t(1));
            QVERIFY(!erased.strokes[0].closed);
        }
        // Up to the intersection, touched on one side of the joint: it goes from a line crossing it to the other,
        // round past the joint.
        doc::VectorStroke across;
        for (int i = -40; i <= 120; ++i) // crossing both sides of the loop
            across.points.push_back(point(i, 50));
        across.size = 2;
        doc::VectorStroke rolled = loop;
        const canvas::VectorErasure half =
            canvas::eraseFromStrokes({rolled, across}, {QPointF(50, 0)}, 3, canvas::VectorEraseMode::ToIntersection);
        QCOMPARE(half.strokes.size(), std::size_t(2));
        for (const brush::BrushInput &p : half.strokes[0].points)
            QVERIFY(p.y >= 50 - 1.5); // the half below the line crossing it
    }

    // Hue / Saturation / Value: the hue turned round the wheel, saturation and value moved towards none or full; the
    // alpha kept, a premultiplied pixel shifted as its color is.
    void colorsAreShiftedInHsv()
    {
        using canvas::shiftedColor;
        QCOMPARE(shiftedColor(0xffff0000u, {120, 0, 0}), 0xff00ff00u); // red to green
        QCOMPARE(shiftedColor(0xffff0000u, {-120, 0, 0}), 0xff0000ffu); // to blue
        QCOMPARE(shiftedColor(0x80ff0000u, {180, 0, 0}), 0x8000ffffu);  // to cyan, the alpha as it was
        QCOMPARE(shiftedColor(0xffff0000u, {0, -100, 0}), 0xffffffffu); // no saturation: gray as bright (white here)
        QCOMPARE(shiftedColor(0xff804040u, {0, 100, 0}), 0xff800000u);  // full
        QCOMPARE(shiftedColor(0xff336699u, {0, 0, -100}), 0xff000000u); // no value: black
        QCOMPARE(shiftedColor(0xff400000u, {0, 0, 100}), 0xffff0000u);  // full value
        QCOMPARE(shiftedColor(0xff336699u, {}), 0xff336699u);
        QCOMPARE(shiftedColor(0xff808080u, {90, 0, 0}), 0xff808080u); // a gray has no hue to turn

        QCOMPARE(canvas::shiftedPixel(0x80800000u, {120, 0, 0}), doc::Pixel(0x80008000u)); // half red, half green
        QCOMPARE(canvas::shiftedPixel(0u, {120, 50, 50}), doc::Pixel(0));

        doc::TiledImage image;
        image.setPixel(3, 4, 0xffff0000u);
        image.setPixel(200, 90, 0xff0000ffu);
        const doc::TiledImage shifted = canvas::shiftedImage(image, {120, 0, 0});
        QCOMPARE(shifted.pixel(3, 4), doc::Pixel(0xff00ff00u));
        QCOMPARE(shifted.pixel(200, 90), doc::Pixel(0xffff0000u));
        QCOMPARE(shifted.pixel(5, 5), doc::Pixel(0));
        QCOMPARE(image.pixel(3, 4), doc::Pixel(0xffff0000u)); // the image itself as it was
    }

    // A stroke cut in two: the pieces share the point of the cut and keep the stroke's brush; not at an end.
    void aStrokeIsCutInTwo()
    {
        doc::VectorStroke line = straightStroke(51);
        line.brush = "G-pen";
        line.seed = 7;
        const auto pieces = cutStroke(line, *nearestPlace(line, QPointF(31, 2)), 0.5);
        QVERIFY(pieces);
        QCOMPARE(pieces->first.points.back().x, 31.0);
        QCOMPARE(pieces->second.points.front().x, 31.0);
        QCOMPARE(pieces->first.points.size() + pieces->second.points.size(), std::size_t(53)); // a point put in, shared
        QCOMPARE(pieces->second.brush, std::string("G-pen"));
        QCOMPARE(pieces->second.seed, 7u);
        QVERIFY(!cutStroke(line, *nearestPlace(line, QPointF(-5, 0)), 0.5));
    }
    // The copies of the symmetry: mirrored round the axes through the center (turned by the angle), turned round it,
    // or moved cell to cell; the first is the stroke itself.
    void symmetryCopiesGoRoundTheAxes()
    {
        using Kind = tools::SymmetrySettings::Kind;
        const QPointF center(100, 50);
        const QSizeF page(200, 100);
        auto mapped = [&](Kind kind, int count, double angle, QPointF p, QPointF start = {}) {
            tools::SymmetrySettings s;
            s.kind = kind;
            s.count = count;
            s.tiles = count;
            std::vector<QPointF> out;
            const auto transforms = symmetryTransforms(s, center, angle, page, start);
            if (transforms.empty() || !transforms.front().isIdentity())
                return std::vector<QPointF>{};
            for (const QTransform &t : transforms)
                out.push_back(t.map(p));
            return out;
        };
        auto has = [](const std::vector<QPointF> &points, QPointF p) {
            return std::any_of(points.begin(), points.end(), [&](QPointF q) { return QLineF(p, q).length() < 1e-9; });
        };
        const QPointF p(130, 70);
        auto bilateral = mapped(Kind::Bilateral, 0, 0, p);
        QCOMPARE(bilateral.size(), std::size_t(2));
        QVERIFY(has(bilateral, QPointF(70, 70)));
        QVERIFY(has(mapped(Kind::Bilateral, 0, 90, p), QPointF(130, 30))); // the mirror turned level
        QVERIFY(has(mapped(Kind::Vertical, 0, 0, p), QPointF(130, 30)));
        const auto both = mapped(Kind::Both, 0, 0, p);
        QCOMPARE(both.size(), std::size_t(4));
        QVERIFY(has(both, QPointF(70, 30)));
        const auto rotate = mapped(Kind::Rotate, 4, 0, QPointF(100, 20)); // 30 above the center
        QCOMPARE(rotate.size(), std::size_t(4));
        QVERIFY(has(rotate, QPointF(130, 50)) && has(rotate, QPointF(100, 80)) && has(rotate, QPointF(70, 50)));
        const auto kaleidoscope = mapped(Kind::RotateMirror, 3, 0, p);
        QCOMPARE(kaleidoscope.size(), std::size_t(6));
        QVERIFY(has(kaleidoscope, QPointF(70, 70)));

        // Cells of the page, from the center: 2 x 2 cells of 100 x 50 from (0, 0) when the center is there.
        tools::SymmetrySettings tile;
        tile.kind = Kind::Tile;
        tile.tiles = 2;
        auto cells = symmetryTransforms(tile, QPointF(0, 0), 30, page, QPointF(30, 20));
        std::vector<QPointF> tiled;
        for (const QTransform &t : cells)
            tiled.push_back(t.map(QPointF(30, 20)));
        QCOMPARE(tiled.size(), std::size_t(4));
        QVERIFY(has(tiled, QPointF(130, 20)) && has(tiled, QPointF(30, 70)) && has(tiled, QPointF(130, 70)));
        tile.kind = Kind::HalfDrop;
        tiled.clear();
        for (const QTransform &t : symmetryTransforms(tile, QPointF(0, 0), 0, page, QPointF(30, 20)))
            tiled.push_back(t.map(QPointF(30, 20)));
        QVERIFY(has(tiled, QPointF(130, 45)) && has(tiled, QPointF(30, 70))); // the second column half a cell down

        // The way a point of the brush points is mirrored with it.
        brush::BrushInput in;
        in.direction = 0;
        in.penAngle = 30;
        const brush::BrushInput mirrored = transformedInput(in, symmetryTransforms({}, center, 0, page, {})[1]);
        QVERIFY(std::abs(std::abs(mirrored.direction) - 180.0) < 1e-9);
        QVERIFY(std::abs(mirrored.penAngle - 150.0) < 1e-9);
    }

    // The guides: the mirror's line through the center, a ray for each copy turned, the lines between the cells.
    void symmetryGuidesShowTheAxes()
    {
        using Kind = tools::SymmetrySettings::Kind;
        tools::SymmetrySettings s;
        const auto mirror = symmetryGuides(s, QPointF(100, 50), 0, QSizeF(200, 100), 1000);
        QCOMPARE(mirror.size(), std::size_t(1));
        QVERIFY(std::abs(mirror[0].p1().x() - 100) < 1e-9 && std::abs(mirror[0].p2().x() - 100) < 1e-9);
        s.kind = Kind::Rotate;
        s.count = 5;
        QCOMPARE(symmetryGuides(s, QPointF(100, 50), 0, QSizeF(200, 100), 1000).size(), std::size_t(5));
        s.kind = Kind::Tile;
        s.tiles = 2;
        const auto grid = symmetryGuides(s, QPointF(0, 0), 0, QSizeF(200, 100), 1000);
        QCOMPARE(grid.size(), std::size_t(2));
        QVERIFY(std::any_of(grid.begin(), grid.end(), [](const QLineF &l) { return l.p1().x() == 100 && l.p2().x() == 100; }));
        QVERIFY(std::any_of(grid.begin(), grid.end(), [](const QLineF &l) { return l.p1().y() == 50 && l.p2().y() == 50; }));
        QVERIFY(!symmetryTurns(Kind::Tile));
        QVERIFY(symmetryTurns(Kind::RotateMirror));
    }
};

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"
