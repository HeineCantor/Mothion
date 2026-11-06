#include "compositing/Evaluator.h"
#include "compositing/Graph.h"
#include "compositing/NodeType.h"

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
