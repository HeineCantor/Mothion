#include "compositing/Evaluator.h"
#include "compositing/Graph.h"
#include "compositing/MaskData.h"
#include "compositing/NodeType.h"
#include "core/document/Drawing.h"

#include <QtTest>

using namespace mothion::compositing;

namespace {

QImage frameWithDot()
{
    QImage frame(40, 40, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::white);
    frame.setPixelColor(20, 20, Qt::black);
    return frame;
}

} // namespace

class TestCompositing : public QObject
{
    Q_OBJECT

private slots:
    void everyTypeIsRegisteredOnce()
    {
        const auto &registry = NodeRegistry::instance();
        QVERIFY(registry.find(QStringLiteral("frame")));
        QVERIFY(registry.find(QStringLiteral("output")));
        QVERIFY(!registry.find(QStringLiteral("nothing")));
        QStringList seen;
        for (const auto &type : registry.all()) {
            QVERIFY(!seen.contains(type->id())); // ids are stored in files: they must be unique
            seen << type->id();
        }
    }

    void graphKeepsOneWirePerInputAndNoLoops()
    {
        Graph g;
        QCOMPARE(g.addNode(QStringLiteral("nothing")), 0);
        const NodeId a = g.addNode(QStringLiteral("blur"));
        const NodeId b = g.addNode(QStringLiteral("blur"));
        const NodeId c = g.addNode(QStringLiteral("blur"));
        QVERIFY(g.connect(a, 0, b, 0));
        QVERIFY(!g.connect(b, 0, a, 0)); // a loop
        QVERIFY(!g.connect(a, 0, a, 0));
        QVERIFY(!g.connect(a, 3, b, 0)); // no such socket
        QVERIFY(g.connect(c, 0, b, 0));  // replaces the wire from a
        QCOMPARE(g.links().size(), size_t(1));
        QCOMPARE(g.linkInto(b, 0)->from, c);
        g.removeNode(c); // its wire goes with it
        QVERIFY(g.links().empty());
        QVERIFY(!g.node(c));
    }

    void listenersHearTheChanges()
    {
        Graph g;
        QList<GraphChange::Kind> heard;
        const int token = g.addListener([&](const GraphChange &c) { heard << c.kind; });
        const NodeId a = g.addNode(QStringLiteral("blur"));
        const NodeId b = g.addNode(QStringLiteral("blur"));
        g.connect(a, 0, b, 0);
        g.setParam(a, QStringLiteral("radius"), 3);
        g.setParam(a, QStringLiteral("radius"), 3); // same value: nothing to hear
        g.setPosition(a, {5, 5});
        g.removeNode(a);                            // the wire, then the node
        using K = GraphChange::Kind;
        QCOMPARE(heard, (QList<K>{K::NodeAdded, K::NodeAdded, K::Links, K::Param, K::Moved, K::Links, K::NodeRemoved}));
        g.removeListener(token);
        g.addNode(QStringLiteral("blur"));
        QCOMPARE(heard.size(), 7);
    }

    void networkSurvivesBeingStoredAndLoaded()
    {
        Graph g;
        const NodeId frame = g.addNode(QStringLiteral("frame"), {-10, 5});
        const NodeId color = g.addNode(QStringLiteral("color"), {3, 4});
        const NodeId merge = g.addNode(QStringLiteral("merge"), {50, 60});
        g.connect(frame, 0, merge, 0);
        g.connect(color, 0, merge, 1);
        g.setParam(color, QStringLiteral("color"), QColor(1, 2, 3, 4));
        g.setParam(merge, QStringLiteral("opacity"), 35);
        const mothion::doc::GraphData stored = g.toData();
        Graph other;
        int resets = 0;
        other.addListener([&](const GraphChange &c) { resets += c.kind == GraphChange::Kind::Reset; });
        other.addNode(QStringLiteral("blur")); // gone after the load
        other.load(stored);
        QCOMPARE(resets, 1);
        QVERIFY(other.toData() == stored);
        QCOMPARE(other.node(merge)->params.value(QStringLiteral("opacity")).toInt(), 35);
        QCOMPARE(other.node(color)->params.value(QStringLiteral("color")).value<QColor>(), QColor(1, 2, 3, 4));
        QCOMPARE(other.node(frame)->pos, QPointF(-10, 5));
        QCOMPARE(other.links().size(), size_t(2));
        QCOMPARE(other.addNode(QStringLiteral("blur")), stored.nextId); // ids go on from where they were
    }

    void loadingIgnoresWhatDoesNotFit()
    {
        mothion::doc::GraphData data;
        data.nodes.push_back({1, "blur", 0, 0, {{"radius", 9999}}}); // out of range: held to the slider's
        data.nodes.push_back({2, "no-such-type", 0, 0, {}});
        data.nodes.push_back({3, "blur", 0, 0, {}}); // no settings stored: the start values
        data.links.push_back({1, 0, 2, 0});          // to a node that was dropped
        data.links.push_back({1, 0, 3, 5});          // a socket that is not there
        data.links.push_back({1, 0, 3, 0});
        data.links.push_back({3, 0, 1, 0});          // would close a loop
        Graph g;
        g.load(data);
        QCOMPARE(g.nodes().size(), size_t(2));
        QCOMPARE(g.links().size(), size_t(1));
        QCOMPARE(g.node(1)->params.value(QStringLiteral("radius")).toInt(), 60);
        QCOMPARE(g.node(3)->params.value(QStringLiteral("radius")).toInt(), 8);
    }

    // Runs one node of `type` over `input` with the settings given.
    static QImage runNode(const QString &type, const QImage &input, const QVariantMap &settings, int frameIndex = 0)
    {
        Graph g;
        const NodeId frame = g.addNode(QStringLiteral("frame"));
        const NodeId node = g.addNode(type);
        const NodeId out = g.addNode(QStringLiteral("output"));
        g.connect(frame, 0, node, 0);
        g.connect(node, 0, out, 0);
        for (auto it = settings.begin(); it != settings.end(); ++it)
            g.setParam(node, it.key(), it.value());
        Evaluator evaluator;
        EvalContext context;
        context.frame = input;
        context.frameIndex = frameIndex;
        return evaluator.evaluate(g, context);
    }

    static QImage edgeImage() // black on the left half, white on the right
    {
        QImage image(60, 20, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 60; ++x)
                image.setPixelColor(x, y, x < 30 ? Qt::black : Qt::white);
        return image;
    }

    void chromaticAberrationSplitsTheChannels()
    {
        const QImage edge = edgeImage();
        const QString type = QStringLiteral("chromatic");
        // Amount 0: nothing changes.
        QCOMPARE(runNode(type, edge, {{"amount", 0}}), edge);
        // Linear, along x: at the edge the red channel is read from the right (white already), the blue from the left.
        const QImage linear = runNode(type, edge, {{"mode", 1}, {"amount", 4}, {"angle", 0}});
        const QColor black = linear.pixelColor(29, 10), white = linear.pixelColor(31, 10);
        QVERIFY(black.red() > black.green()); // the red channel already sees the white
        QVERIFY(white.blue() < white.green()); // the blue one still sees the black
        QCOMPARE(linear.pixelColor(2, 10), QColor(Qt::black)); // far from the edge: untouched
        QCOMPARE(linear.pixelColor(57, 10), QColor(Qt::white));
        for (int x = 0; x < 60; ++x) // green is never moved
            QCOMPARE(linear.pixelColor(x, 10).green(), edge.pixelColor(x, 10).green());
        // Radial: no shift at the center, a growing one towards the corners.
        QImage dots(61, 21, QImage::Format_ARGB32_Premultiplied);
        dots.fill(Qt::black);
        for (int y = 0; y < 21; ++y)
            for (int x : {30, 56})
                dots.setPixelColor(x, y, Qt::white);
        const QImage radial = runNode(type, dots, {{"mode", 0}, {"amount", 6}});
        QCOMPARE(radial.pixelColor(30, 10), QColor(Qt::white)); // the center column stays white in all channels
        QVERIFY(radial.pixelColor(56, 10) != QColor(Qt::white)); // near the edge it has split
        // Transparent areas stay transparent where nothing is.
        QImage clear(20, 20, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QCOMPARE(runNode(type, clear, {{"mode", 1}, {"amount", 5}}), clear);
    }

    void sensorNoiseIsSteadyAndSeparatesLuminanceFromChroma()
    {
        QImage gray(64, 64, QImage::Format_ARGB32_Premultiplied);
        gray.fill(QColor(128, 128, 128));
        const QString type = QStringLiteral("sensornoise");
        const QImage luma = runNode(type, gray, {{"luminance", 60}, {"chroma", 0}});
        QVERIFY(luma != gray);
        int varying = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const QColor c = luma.pixelColor(x, y);
                QCOMPARE(c.red(), c.green()); // luminance noise moves the three channels together
                QCOMPARE(c.green(), c.blue());
                varying += c.red() != 128;
            }
        QVERIFY(varying > 64 * 64 / 2);
        // Chroma noise alone tints: the channels part, the average brightness stays about the same.
        const QImage tint = runNode(type, gray, {{"luminance", 0}, {"chroma", 60}});
        bool tinted = false;
        for (int x = 0; x < 64; ++x)
            tinted = tinted || tint.pixelColor(x, 5).red() != tint.pixelColor(x, 5).blue();
        QVERIFY(tinted);
        // The same settings give the same grain; another seed another one.
        QCOMPARE(runNode(type, gray, {{"luminance", 60}, {"chroma", 0}}), luma);
        QVERIFY(runNode(type, gray, {{"luminance", 60}, {"chroma", 0}, {"seed", 5}}) != luma);
        // The frame number matters only with Animate on.
        QCOMPARE(runNode(type, gray, {{"luminance", 60}, {"chroma", 0}}, 7), luma);
        QVERIFY(runNode(type, gray, {{"luminance", 60}, {"chroma", 0}, {"animate", 1}}, 7) != luma);
        // Nothing is added where there is nothing.
        QImage clear(16, 16, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QCOMPARE(runNode(type, clear, {{"luminance", 100}, {"chroma", 100}}), clear);
    }

    // One Glow node over `input`, on a page of color `paper`.
    static QImage runGlow(const QImage &input, const QVariantMap &settings, QRgb paper = 0xffffffff)
    {
        Graph g;
        const NodeId frame = g.addNode(QStringLiteral("frame"));
        const NodeId node = g.addNode(QStringLiteral("glow"));
        const NodeId out = g.addNode(QStringLiteral("output"));
        g.connect(frame, 0, node, 0);
        g.connect(node, 0, out, 0);
        for (auto it = settings.begin(); it != settings.end(); ++it)
            g.setParam(node, it.key(), it.value());
        Evaluator evaluator;
        EvalContext context;
        context.frame = input;
        context.paper = paper;
        return evaluator.evaluate(g, context);
    }

    static QImage squareOn(const QColor &background)
    {
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(background);
        for (int y = 28; y < 36; ++y)
            for (int x = 28; x < 36; ++x)
                image.setPixelColor(x, y, Qt::white);
        return image;
    }

    void glowSpillsLightAroundTheBrightParts()
    {
        const QImage input = squareOn(Qt::black);
        const QVariantMap settings{{"ignorePaper", 0}, {"size", 16}};
        const QImage glow = runGlow(input, settings);
        QCOMPARE(glow.size(), input.size());
        const int near = glow.pixelColor(24, 32).red(), far = glow.pixelColor(2, 2).red();
        QVERIFY(near > 10);   // light has spilled next to the square
        QVERIFY(near > far);  // and fades with the distance
        QCOMPARE(glow.pixelColor(32, 32), QColor(Qt::white)); // the square itself is as bright as it was
        // Tinted: only the red channel is lit.
        const QImage red = runGlow(input, {{"ignorePaper", 0}, {"size", 16}, {"tint", QColor(255, 0, 0)}});
        QVERIFY(red.pixelColor(24, 32).red() > 10);
        QCOMPARE(red.pixelColor(24, 32).green(), 0);
        // No intensity, or nothing over the threshold: no change.
        QCOMPARE(runGlow(input, {{"ignorePaper", 0}, {"intensity", 0}}), input);
        QImage dim(64, 64, QImage::Format_ARGB32_Premultiplied);
        dim.fill(QColor(100, 100, 100));
        QCOMPARE(runGlow(dim, {{"ignorePaper", 0}, {"threshold", 90}, {"softness", 0}}), dim);
        // A bigger Size takes the glow farther.
        const QImage wide = runGlow(input, {{"ignorePaper", 0}, {"size", 64}});
        QVERIFY(wide.pixelColor(8, 32).red() > glow.pixelColor(8, 32).red());
    }

    void glowCanLeaveThePaperOut()
    {
        QImage page(64, 64, QImage::Format_ARGB32_Premultiplied);
        const QRgb paper = qRgb(128, 128, 128);
        page.fill(paper);
        // The page alone gives light, unless it is left out.
        QVERIFY(runGlow(page, {{"threshold", 0}, {"softness", 0}, {"ignorePaper", 0}}, paper) != page);
        QCOMPARE(runGlow(page, {{"threshold", 0}, {"softness", 0}, {"ignorePaper", 1}}, paper), page);
        // A bright dot on it lights up its surroundings, the page's far side stays as it is.
        page.setPixelColor(32, 32, Qt::white);
        const QImage dot = runGlow(page, {{"threshold", 80}, {"ignorePaper", 1}, {"size", 8}}, paper);
        QVERIFY(dot.pixelColor(34, 32).red() > 128);
        QCOMPARE(dot.pixelColor(0, 0), QColor(128, 128, 128));
    }

    void glowGivesAlphaToTransparentPixelsItLights()
    {
        const QImage glow = runGlow(squareOn(Qt::transparent), {{"ignorePaper", 0}, {"size", 16}});
        const QColor near = glow.pixelColor(24, 32);
        QVERIFY(near.alpha() > 0);
        QVERIFY(near.alpha() > glow.pixelColor(2, 2).alpha());
    }

    void maskShapesAreStoredAndRasterized()
    {
        const QList<MaskShape> shapes{MaskShape::rectangle(QRectF(10, 10, 20, 10)),
                                      MaskShape::lasso(QPolygonF({QPointF(40, 40), QPointF(60, 40), QPointF(50, 60)}))};
        const QString json = maskShapesToJson(shapes);
        QVERIFY(parseMaskShapes(json) == shapes);
        QVERIFY(maskShapesToJson({}).isEmpty());
        QVERIFY(parseMaskShapes(QStringLiteral("not json")).isEmpty());
        // Shapes that are not shapes (an empty rectangle, a lasso of two points) are left out.
        QVERIFY(parseMaskShapes(QStringLiteral(R"([{"t":"rect","x":0,"y":0,"w":0,"h":5},{"t":"lasso","p":[1,2,3,4]}])")).isEmpty());

        const QImage mask = rasterizeMask(shapes, QSize(80, 80), 1.0);
        QCOMPARE(mask.format(), QImage::Format_Grayscale8);
        QCOMPARE(qGray(mask.pixel(20, 15)), 255); // inside the rectangle
        QCOMPARE(qGray(mask.pixel(5, 5)), 0);
        QCOMPARE(qGray(mask.pixel(50, 45)), 255); // inside the lasso
        QCOMPARE(qGray(mask.pixel(41, 58)), 0);   // outside its triangle, inside its box
        // A picture half the size (a preview) has the shapes half the size.
        const QImage half = rasterizeMask(shapes, QSize(40, 40), 0.5);
        QCOMPARE(qGray(half.pixel(10, 7)), 255);
        QCOMPARE(qGray(half.pixel(10, 20)), 0);
    }

    void maskNodeCutsTheImageAndGivesTheMask()
    {
        Graph g;
        const NodeId frame = g.addNode(QStringLiteral("frame"));
        const NodeId mask = g.addNode(QStringLiteral("mask"));
        const NodeId out = g.addNode(QStringLiteral("output"));
        g.connect(frame, 0, mask, 0);
        g.connect(mask, 0, out, 0);
        g.setData(mask, maskShapesToJson({MaskShape::rectangle(QRectF(10, 10, 20, 20))}));
        QImage input(40, 40, QImage::Format_ARGB32_Premultiplied);
        input.fill(Qt::red);
        Evaluator evaluator;
        EvalContext context;
        context.frame = input;
        const QImage cut = evaluator.evaluate(g, context); // output socket 0: the image
        QCOMPARE(cut.pixelColor(20, 20), QColor(Qt::red));
        QCOMPARE(cut.pixelColor(2, 2).alpha(), 0); // outside the area: gone
        const QImage matte = evaluator.evaluateNode(g, context, mask, 1); // output socket 1: the mask
        QCOMPARE(qGray(matte.pixel(20, 20)), 255);
        QCOMPARE(qGray(matte.pixel(2, 2)), 0);
        // Before anything is drawn nothing is selected.
        g.setData(mask, {});
        QCOMPARE(evaluator.evaluate(g, context).pixelColor(20, 20).alpha(), 0);
        // What goes into the node, asked for after the result: the pictures that were kept are shared.
        g.setData(mask, maskShapesToJson({MaskShape::rectangle(QRectF(0, 0, 5, 5))}));
        evaluator.evaluate(g, context);
        QCOMPARE(evaluator.evaluateInput(g, context, mask, 0), input);
        QCOMPARE(evaluator.processedCount(), 0);
        QCOMPARE(evaluator.evaluate(g, context).pixelColor(20, 20).alpha(), 0); // and the result was not forgotten
        QCOMPARE(evaluator.processedCount(), 0);
    }

    // The nodes are grouped in families: each one is in one, every family has a node, and the menu has the names.
    void everyNodeIsInACategory()
    {
        const auto &registry = NodeRegistry::instance();
        QVERIFY(registry.find(QStringLiteral("frame"))->category() == NodeCategory::InputOutput);
        QVERIFY(registry.find(QStringLiteral("colorcorrect"))->category() == NodeCategory::Color);
        QVERIFY(registry.find(QStringLiteral("transform"))->category() == NodeCategory::Transform);
        QVERIFY(registry.find(QStringLiteral("merge"))->category() == NodeCategory::Composite);
        for (int c = 0; c < kNodeCategoryCount; ++c) {
            bool any = false;
            for (const auto &type : registry.all())
                any = any || int(type->category()) == c;
            QVERIFY2(any, "a category with no node");
            QVERIFY(!categoryTitle(NodeCategory(c)).isEmpty());
        }
    }

    // Merge blends the front over the back in the layers' blend modes (QPainter's and the ones done by hand), and
    // the opacity of the front goes with it.
    void mergeBlendsInTheLayersModes()
    {
        auto merge = [](int mode, int opacity, QColor backColor, QColor frontColor) {
            Graph g;
            const NodeId back = g.addNode(QStringLiteral("color"));
            const NodeId front = g.addNode(QStringLiteral("color"));
            const NodeId node = g.addNode(QStringLiteral("merge"));
            const NodeId out = g.addNode(QStringLiteral("output"));
            g.setParam(back, QStringLiteral("color"), backColor);
            g.setParam(front, QStringLiteral("color"), frontColor);
            g.setParam(node, QStringLiteral("mode"), mode);
            g.setParam(node, QStringLiteral("opacity"), opacity);
            g.connect(back, 0, node, 0);
            g.connect(front, 0, node, 1);
            g.connect(node, 0, out, 0);
            Evaluator evaluator;
            EvalContext context;
            context.frame = QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
            context.frame.fill(Qt::white);
            return evaluator.evaluate(g, context).pixelColor(4, 4);
        };
        const QColor gray(128, 128, 128), white(255, 255, 255);
        QVERIFY(qAbs(merge(0, 100, gray, QColor(200, 0, 0)).red() - 200) <= 1); // Normal
        QVERIFY(qAbs(merge(1, 100, gray, gray).red() - 64) <= 2);      // Multiply: 0.5 * 0.5
        QVERIFY(qAbs(merge(1, 50, white, gray).red() - 191) <= 2);     // at half opacity: halfway to the product
        QVERIFY(qAbs(merge(int(mothion::doc::BlendMode::Difference), 100, white, gray).red() - 127) <= 2);
        // One done by hand: Linear Burn is back + front - 1.
        QVERIFY(qAbs(merge(int(mothion::doc::BlendMode::LinearBurn), 100, QColor(204, 204, 204), QColor(153, 153, 153)).red() - 102) <= 2);
        // The modes are in the node's menu by their number.
        const auto params = NodeRegistry::instance().find(QStringLiteral("merge"))->params();
        QCOMPARE(params.first().choices.size(), int(mothion::doc::BlendMode::AddGlow) + 1);
    }

    void colorCorrectChangesBrightnessContrastGammaHueAndSaturation()
    {
        const QString type = QStringLiteral("colorcorrect");
        QImage gray(10, 10, QImage::Format_ARGB32_Premultiplied);
        gray.fill(QColor(100, 100, 100));
        QCOMPARE(runNode(type, gray, {}), gray); // all at zero: as it came
        QVERIFY(runNode(type, gray, {{"brightness", 30}}).pixelColor(3, 3).red() > 150);
        QVERIFY(runNode(type, gray, {{"brightness", -30}}).pixelColor(3, 3).red() < 50);
        QVERIFY(runNode(type, gray, {{"gamma", 200}}).pixelColor(3, 3).red() > 100); // gamma over 1 lightens
        // Contrast pushes away from the middle gray: darker grays darker, lighter ones lighter.
        QImage lighter(10, 10, QImage::Format_ARGB32_Premultiplied);
        lighter.fill(QColor(180, 180, 180));
        QVERIFY(runNode(type, gray, {{"contrast", 50}}).pixelColor(3, 3).red() < 100);
        QVERIFY(runNode(type, lighter, {{"contrast", 50}}).pixelColor(3, 3).red() > 180);
        // Saturation -100 takes the colors out (all channels the same); hue 180 turns red towards cyan.
        QImage red(10, 10, QImage::Format_ARGB32_Premultiplied);
        red.fill(QColor(220, 40, 40));
        const QColor flat = runNode(type, red, {{"saturation", -100}}).pixelColor(3, 3);
        QVERIFY(qAbs(flat.red() - flat.green()) <= 2 && qAbs(flat.green() - flat.blue()) <= 2);
        const QColor turned = runNode(type, red, {{"hue", 180}}).pixelColor(3, 3);
        QVERIFY(turned.red() < turned.blue() && turned.red() < turned.green());
        // The alpha is left alone, and a see-through pixel stays see-through.
        QImage half(10, 10, QImage::Format_ARGB32_Premultiplied);
        half.fill(QColor(200, 200, 200, 128));
        QCOMPARE(runNode(type, half, {{"brightness", 20}}).pixelColor(3, 3).alpha(), 128);
        QImage clear(10, 10, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QCOMPARE(runNode(type, clear, {{"brightness", 50}}).pixelColor(3, 3).alpha(), 0);
    }

    void transformMovesScalesAndTurnsAroundTheCenter()
    {
        const QString type = QStringLiteral("transform");
        QImage dot(41, 41, QImage::Format_ARGB32_Premultiplied);
        dot.fill(Qt::transparent);
        for (int y = 14; y < 17; ++y)   // a 3x3 dot, up and to the left of the center
            for (int x = 9; x < 12; ++x)
                dot.setPixelColor(x, y, Qt::black);
        QCOMPARE(runNode(type, dot, {}), dot); // nothing set: as it came
        const QImage moved = runNode(type, dot, {{"x", 10}, {"y", 5}});
        QCOMPARE(moved.pixelColor(20, 20), QColor(Qt::black));
        QCOMPARE(moved.pixelColor(10, 15).alpha(), 0); // where it was
        // Turned half a turn around the center (20, 20): to the other side of it.
        const QImage turned = runNode(type, dot, {{"rotation", 180}});
        QVERIFY(turned.pixelColor(30, 25).alpha() > 200);
        QCOMPARE(turned.pixelColor(10, 15).alpha(), 0);
        // Twice as big: the dot's distance from the center doubles, and so does its size.
        const QImage big = runNode(type, dot, {{"scale", 200}});
        QVERIFY(big.pixelColor(0, 10).alpha() > 200); // the dot's center was 10 left and 5 up of the center
        QVERIFY(big.pixelColor(10, 15).alpha() == 0);
    }

    void opacityScalesTheAlphaOrTurnsItOver()
    {
        const QString type = QStringLiteral("opacity");
        QImage red(10, 10, QImage::Format_ARGB32_Premultiplied);
        red.fill(QColor(255, 0, 0));
        QCOMPARE(runNode(type, red, {}), red);
        const QColor half = runNode(type, red, {{"opacity", 50}}).pixelColor(3, 3);
        QVERIFY(qAbs(half.alpha() - 128) <= 2);
        QCOMPARE(runNode(type, red, {{"opacity", 0}}).pixelColor(3, 3).alpha(), 0);
        // Inverted, an opaque pixel goes away and a see-through one is there (black).
        QImage mixed(10, 10, QImage::Format_ARGB32_Premultiplied);
        mixed.fill(Qt::transparent);
        mixed.setPixelColor(2, 2, Qt::red);
        const QImage inverted = runNode(type, mixed, {{"invert", 1}});
        QCOMPARE(inverted.pixelColor(2, 2).alpha(), 0);
        QCOMPARE(inverted.pixelColor(5, 5), QColor(Qt::black));
    }

    void sharpenAddsBackWhatTheBlurTakes()
    {
        const QString type = QStringLiteral("sharpen");
        QImage step(40, 10, QImage::Format_ARGB32_Premultiplied); // dark gray on the left, light on the right
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 40; ++x)
                step.setPixelColor(x, y, x < 20 ? QColor(100, 100, 100) : QColor(150, 150, 150));
        QCOMPARE(runNode(type, step, {{"amount", 0}}), step);
        const QImage sharp = runNode(type, step, {{"amount", 200}, {"radius", 2}});
        QVERIFY(sharp.pixelColor(19, 5).red() < 100); // darker beside the edge on the dark side
        QVERIFY(sharp.pixelColor(20, 5).red() > 150); // lighter on the light one
        QCOMPARE(sharp.pixelColor(2, 5).red(), 100);  // flat areas stay as they are
        QCOMPARE(sharp.pixelColor(37, 5).red(), 150);
    }

    void directionalBlurSmearsAlongItsDirection()
    {
        const QString type = QStringLiteral("directionalblur");
        QImage dot(41, 41, QImage::Format_ARGB32_Premultiplied);
        dot.fill(Qt::black);
        dot.setPixelColor(20, 20, Qt::white);
        QCOMPARE(runNode(type, dot, {{"length", 0}}), dot);
        const QImage across = runNode(type, dot, {{"length", 10}, {"angle", 0}});
        QVERIFY(across.pixelColor(24, 20).red() > 0);  // along the row
        QCOMPARE(across.pixelColor(20, 24).red(), 0);  // not up or down
        QVERIFY(across.pixelColor(20, 20).red() < 255); // the dot's light is spread
        const QImage down = runNode(type, dot, {{"length", 10}, {"angle", 90}});
        QVERIFY(down.pixelColor(20, 24).red() > 0);
        QCOMPARE(down.pixelColor(24, 20).red(), 0);
    }

    void levelsMapTheRange()
    {
        const QString type = QStringLiteral("levels");
        auto gray = [](int v) {
            QImage image(6, 6, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor(v, v, v));
            return image;
        };
        QCOMPARE(runNode(type, gray(100), {}), gray(100)); // untouched settings: as it came
        QCOMPARE(runNode(type, gray(40), {{"inBlack", 50}}).pixelColor(2, 2).red(), 0);
        QCOMPARE(runNode(type, gray(220), {{"inWhite", 200}}).pixelColor(2, 2).red(), 255);
        QVERIFY(runNode(type, gray(100), {{"gamma", 200}}).pixelColor(2, 2).red() > 100); // gamma over 1 lightens
        QCOMPARE(runNode(type, gray(255), {{"outWhite", 128}}).pixelColor(2, 2).red(), 128);
        QCOMPARE(runNode(type, gray(0), {{"outBlack", 60}}).pixelColor(2, 2).red(), 60);
        QImage half(6, 6, QImage::Format_ARGB32_Premultiplied); // the alpha is left alone
        half.fill(QColor(200, 200, 200, 128));
        QCOMPARE(runNode(type, half, {{"inBlack", 10}}).pixelColor(2, 2).alpha(), 128);
    }

    void vignetteDarkensTheCornersOnly()
    {
        const QString type = QStringLiteral("vignette");
        QImage white(41, 41, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        QCOMPARE(runNode(type, white, {{"amount", 0}}), white);
        const QImage dark = runNode(type, white, {{"amount", 100}, {"size", 40}, {"softness", 30}});
        QCOMPARE(dark.pixelColor(20, 20), QColor(Qt::white)); // the center is not touched
        QVERIFY(dark.pixelColor(0, 0).red() < 40);             // the corner is nearly black
        QVERIFY(dark.pixelColor(10, 10).red() > dark.pixelColor(0, 0).red()); // gentler on the way in
        QVERIFY(dark.pixelColor(10, 10).red() < 255);
        // The color can be another one, and a see-through pixel stays so.
        const QImage tinted = runNode(type, white, {{"amount", 100}, {"size", 40}, {"tint", QColor(255, 0, 0)}});
        QVERIFY(tinted.pixelColor(0, 0).red() > 200 && tinted.pixelColor(0, 0).green() < 60);
        QImage clear(41, 41, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QCOMPARE(runNode(type, clear, {{"amount", 100}, {"size", 0}}).pixelColor(0, 0).alpha(), 0);
    }

    void dropShadowPutsTheShapeBehind()
    {
        const QString type = QStringLiteral("dropshadow");
        QImage dot(41, 41, QImage::Format_ARGB32_Premultiplied);
        dot.fill(Qt::transparent);
        for (int y = 15; y < 20; ++y)
            for (int x = 15; x < 20; ++x)
                dot.setPixelColor(x, y, Qt::red);
        QCOMPARE(runNode(type, dot, {{"opacity", 0}}), dot); // no shadow: as it came
        const QImage shadowed = runNode(type, dot, {{"x", 10}, {"y", 10}, {"blur", 0}, {"opacity", 100}});
        QCOMPARE(shadowed.pixelColor(17, 17), QColor(Qt::red));     // the picture stays on top
        QCOMPARE(shadowed.pixelColor(27, 27), QColor(Qt::black));   // its shadow, moved by the offset
        QCOMPARE(shadowed.pixelColor(35, 35).alpha(), 0);           // and nothing elsewhere
        const QImage soft = runNode(type, dot, {{"x", 10}, {"y", 10}, {"blur", 4}, {"opacity", 100},
                                                {"color", QColor(0, 0, 255)}});
        QVERIFY(soft.pixelColor(27, 27).blue() > 0 && soft.pixelColor(27, 27).red() == 0); // in its own color
        QVERIFY(soft.pixelColor(31, 27).alpha() > 0);              // the blur has spread it
    }

    void mergeWithAMaskShowsTheFrontOnlyThere()
    {
        Graph g;
        const NodeId back = g.addNode(QStringLiteral("frame"));
        const NodeId color = g.addNode(QStringLiteral("color"));
        const NodeId mask = g.addNode(QStringLiteral("mask"));
        const NodeId merge = g.addNode(QStringLiteral("merge"));
        const NodeId out = g.addNode(QStringLiteral("output"));
        g.setParam(color, QStringLiteral("color"), QColor(0, 0, 255, 255));
        g.setData(mask, maskShapesToJson({MaskShape::rectangle(QRectF(10, 10, 20, 20))}));
        g.connect(back, 0, merge, 0);
        g.connect(color, 0, merge, 1);
        g.connect(back, 0, mask, 0);
        g.connect(mask, 1, merge, 2); // the mask socket, not the image one
        g.connect(merge, 0, out, 0);
        QImage white(40, 40, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        Evaluator evaluator;
        EvalContext context;
        context.frame = white;
        const QImage result = evaluator.evaluate(g, context);
        QCOMPARE(result.pixelColor(20, 20), QColor(Qt::blue)); // inside: the front
        QCOMPARE(result.pixelColor(2, 2), QColor(Qt::white));   // outside: the back
        // Without the mask the front covers everything.
        g.disconnect(merge, 2);
        QCOMPARE(evaluator.evaluate(g, context).pixelColor(2, 2), QColor(Qt::blue));
    }

    void nodeDataIsStoredAndLoaded()
    {
        Graph g;
        const NodeId mask = g.addNode(QStringLiteral("mask"));
        const QString json = maskShapesToJson({MaskShape::rectangle(QRectF(1, 2, 3, 4))});
        g.setData(mask, json);
        Graph other;
        int heard = 0;
        other.addListener([&](const GraphChange &c) { heard += c.kind == GraphChange::Kind::Data; });
        other.load(g.toData());
        QCOMPARE(other.node(mask)->data, json);
        other.setData(mask, {});
        QCOMPARE(heard, 1);
        other.setData(mask, {}); // the same: nothing to hear
        QCOMPARE(heard, 1);
    }

    void choicesAndTogglesAreStoredAsNumbers()
    {
        Graph g;
        const NodeId aberration = g.addNode(QStringLiteral("chromatic"), {1, 2});
        const NodeId noise = g.addNode(QStringLiteral("sensornoise"), {3, 4});
        QCOMPARE(g.node(aberration)->params.value(QStringLiteral("mode")).toInt(), 0);
        g.setParam(aberration, QStringLiteral("mode"), 1);
        g.setParam(noise, QStringLiteral("animate"), 1);
        Graph other;
        other.load(g.toData());
        QCOMPARE(other.node(aberration)->params.value(QStringLiteral("mode")).toInt(), 1);
        QCOMPARE(other.node(noise)->params.value(QStringLiteral("animate")).toInt(), 1);
        // A stored choice out of range is held to the menu.
        mothion::doc::GraphData data = g.toData();
        data.nodes[0].params[0].second = 9;
        other.load(data);
        QCOMPARE(other.node(aberration)->params.value(QStringLiteral("mode")).toInt(), 1);
    }

    void evaluatorRunsTheNodes()
    {
        Graph g;
        const NodeId frame = g.addNode(QStringLiteral("frame"));
        const NodeId blur = g.addNode(QStringLiteral("blur"));
        const NodeId out = g.addNode(QStringLiteral("output"));
        Evaluator evaluator;
        const QImage input = frameWithDot();
        QVERIFY(evaluator.evaluate(g, {input}).isNull()); // nothing wired to Output
        g.connect(frame, 0, out, 0);
        QCOMPARE(evaluator.evaluate(g, {input}), input);
        g.connect(frame, 0, blur, 0);
        g.connect(blur, 0, out, 0);
        g.setParam(blur, QStringLiteral("radius"), 8);
        const QImage blurred = evaluator.evaluate(g, {input});
        QVERIFY(blurred != input);
        QVERIFY(qGray(blurred.pixel(20, 20)) > 200); // the dot has been blurred away
        g.setParam(blur, QStringLiteral("radius"), 0);
        QCOMPARE(evaluator.evaluate(g, {input}), input);
    }

    void evaluatorOnlyRedoesWhatChanged()
    {
        Graph g;
        const NodeId frame = g.addNode(QStringLiteral("frame"));
        const NodeId blur = g.addNode(QStringLiteral("blur"));
        const NodeId color = g.addNode(QStringLiteral("color"));
        const NodeId merge = g.addNode(QStringLiteral("merge"));
        const NodeId out = g.addNode(QStringLiteral("output"));
        g.connect(frame, 0, blur, 0);
        g.connect(blur, 0, merge, 0);
        g.connect(color, 0, merge, 1);
        g.connect(merge, 0, out, 0);
        Evaluator evaluator;
        const QImage input = frameWithDot();
        evaluator.evaluate(g, {input});
        QCOMPARE(evaluator.processedCount(), 5);
        evaluator.evaluate(g, {input}); // nothing changed
        QCOMPARE(evaluator.processedCount(), 0);
        g.setParam(color, QStringLiteral("opacity"), 1); // no such setting: ignored
        g.setParam(merge, QStringLiteral("opacity"), 50); // Merge and Output, not what is upstream
        evaluator.evaluate(g, {input});
        QCOMPARE(evaluator.processedCount(), 2);
        g.setParam(blur, QStringLiteral("radius"), 2); // Blur, Merge, Output; the Color stays
        evaluator.evaluate(g, {input});
        QCOMPARE(evaluator.processedCount(), 3);
        QImage other = input;
        other.setPixelColor(1, 1, Qt::red); // another frame: all that looks at it, and what follows
        evaluator.evaluate(g, {other});
        QCOMPARE(evaluator.processedCount(), 4); // Frame, Blur, Merge, Output
    }
};

QTEST_MAIN(TestCompositing)
#include "tst_compositing.moc"
