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
        g.setPosition(a, {5, 5});                   // not computed anew: silent
        g.removeNode(a);                            // the wire, then the node
        using K = GraphChange::Kind;
        QCOMPARE(heard, (QList<K>{K::NodeAdded, K::NodeAdded, K::Links, K::Param, K::Links, K::NodeRemoved}));
        g.removeListener(token);
        g.addNode(QStringLiteral("blur"));
        QCOMPARE(heard.size(), 6);
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
