#include "canvas/FrameRenderer.h"
#include "core/document/Document.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"
#include "ui/panels/LayersPanel.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>

#include <QTemporaryDir>
#include <QtTest>

using namespace mothion;

namespace {

// The time layer `id` in every drawing of `cel`: whether each has it, and the same pixels.
bool inEveryDrawing(const doc::Cel &cel, doc::LayerId id)
{
    const doc::Layer *first = nullptr;
    for (const auto &[drawingId, drawing] : cel.drawings()) {
        const doc::Layer *layer = drawing.layer(id);
        if (!layer || !layer->time)
            return false;
        if (!first)
            first = layer;
        else if (layer->image.tileCoords() != first->image.tileCoords()
                 || layer->properties != first->properties)
            return false;
        for (const doc::TileCoord &c : layer->image.tileCoords())
            if (layer->image.tile(c) != first->image.tile(c))
                return false;
    }
    return first != nullptr;
}

void paintSquare(document::DocumentController &controller, int x0, int y0, int size, doc::Pixel colour = 0xff000000u)
{
    QString why;
    const auto target = controller.beginPaint(false, &why);
    QVERIFY2(target, qPrintable(why));
    const doc::TiledImage before = *target->image;
    for (int y = y0; y < y0 + size; ++y)
        for (int x = x0; x < x0 + size; ++x)
            target->image->setPixel(x, y, colour);
    controller.endPaint(before);
}

} // namespace

class TestTimeLayer : public QObject
{
    Q_OBJECT

private slots:
    // A time layer is in every frame of its cel, the same in all of them: made in one frame it is in the others, a frame
    // made later has it, what is drawn on it in one frame is in all; undo and redo go with it.
    void aTimeLayerIsTheSameInEveryFrame()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 4, "A").command);
        controller.execute(doc::makeNewDrawing(d, celId, 4, 4, "B").command);
        const doc::Cel &cel = *d.cel(celId);
        QCOMPARE(int(cel.drawings().size()), 2);

        controller.setCurrentFrame(0);
        const doc::LayerId time = controller.addTimeLayer();
        QVERIFY(time != doc::kNoId);
        QVERIFY(controller.isTimeLayer(celId, time));
        QCOMPARE(controller.currentLayer(), time);
        QVERIFY(inEveryDrawing(cel, time));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo new time layer"));

        // Drawn on in frame A: in B too.
        paintSquare(controller, 10, 10, 20);
        QVERIFY(inEveryDrawing(cel, time));
        const doc::Drawing *b = cel.drawingAt(5);
        QCOMPARE(b->layer(time)->image.pixel(15, 15), 0xff000000u);
        controller.undo();
        QVERIFY(inEveryDrawing(cel, time));
        QCOMPARE(b->layer(time)->image.pixel(15, 15), 0u);
        controller.redo();
        QCOMPARE(b->layer(time)->image.pixel(15, 15), 0xff000000u);

        // A frame made later has it, with what is drawn on it.
        controller.execute(doc::makeNewDrawing(d, celId, 8, 4, "C").command);
        const doc::Drawing *c = cel.drawingAt(9);
        QVERIFY(c && c->layer(time));
        QCOMPARE(c->layer(time)->image.pixel(15, 15), 0xff000000u);
        QVERIFY(inEveryDrawing(cel, time));

        // Renamed in one frame: in all.
        doc::LayerProperties renamed = c->layer(time)->properties;
        renamed.name = "Body";
        controller.execute(doc::makeSetLayerProperties(d, celId, c->id, time, renamed));
        QCOMPARE(cel.drawingAt(0)->layer(time)->properties.name, std::string("Body"));

        // Where it is in the stack is each frame's own: moved in one, the others stay.
        const doc::Drawing *a = cel.drawingAt(0);
        QCOMPARE(a->indexOf(time), 1); // over the frame's own layer
        controller.execute(std::make_unique<doc::MoveLayerCommand>(celId, a->id, time, 0));
        QCOMPARE(a->indexOf(time), 0);
        QCOMPARE(b->indexOf(time), 1);

        // Not merged away in a frame; gone only out of the cel, from every frame, one undo step.
        QVERIFY(!controller.canMergeLayers({a->layers[0].id, a->layers[1].id}));
        QVERIFY(controller.removeTimeLayer(celId, time));
        QVERIFY(!controller.isTimeLayer(celId, time));
        for (const auto &[id, drawing] : cel.drawings())
            QVERIFY(!drawing.layer(time));
        controller.undo();
        QVERIFY(inEveryDrawing(cel, time));
        QCOMPARE(a->indexOf(time), 0); // each where it was
        QCOMPARE(b->indexOf(time), 1);
        // Its making undone: out of the frames made since too.
        controller.undo(); // the move
        controller.undo(); // the name
        controller.undo(); // frame C
        controller.undo(); // the square
        controller.undo(); // the time layer
        QVERIFY(!controller.isTimeLayer(celId, time));
        for (const auto &[id, drawing] : cel.drawings())
            QVERIFY(!drawing.layer(time));
    }

    // A frame split or copied keeps the time layer (the same id), not a copy of its own.
    void aFrameCopiedKeepsTheTimeLayer()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 6, "A").command);
        controller.setCurrentFrame(0);
        const doc::LayerId time = controller.addTimeLayer();
        controller.execute(doc::makeSplitDrawing(d, celId, 3).command);
        const doc::Cel &cel = *d.cel(celId);
        QCOMPARE(int(cel.drawings().size()), 2);
        QVERIFY(inEveryDrawing(cel, time));
        for (const auto &[id, drawing] : cel.drawings())
            QCOMPARE(int(std::count_if(drawing.layers.begin(), drawing.layers.end(),
                                       [](const doc::Layer &l) { return l.time; })),
                     1);
    }

    // A new frame made while the time layer is the one worked on: the time layer once, and the frame's own layer named like
    // the frame's own (not like the time layer, which would look like a second one).
    void aNewFrameHasTheTimeLayerOnce()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 1, "A").command);
        controller.setCurrentFrame(0);
        const doc::LayerId time = controller.addTimeLayer();
        QCOMPARE(controller.currentLayer(), time);
        controller.insertFrame(celId, 0, true); // Insert inbetween
        controller.insertFrame(celId, 1, false); // New frame
        const doc::Cel &cel = *d.cel(celId);
        QCOMPARE(int(cel.drawings().size()), 3);
        const std::string timeName = cel.drawingAt(0)->layer(time)->properties.name;
        for (const auto &[id, drawing] : cel.drawings()) {
            QCOMPARE(drawing.layers.size(), std::size_t(2));
            int named = 0;
            for (const doc::Layer &l : drawing.layers)
                named += l.properties.name == timeName ? 1 : 0;
            QCOMPARE(named, 1);
            QVERIFY(drawing.layer(time));
        }
    }

    // Kept in the file: its pixels once, where it is in each frame, what it follows.
    void timeLayersSurviveTheFile()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 4, "A").command);
        controller.execute(doc::makeNewDrawing(d, celId, 4, 4, "B").command);
        controller.setCurrentFrame(0);
        const doc::LayerId time = controller.addTimeLayer(doc::LayerKind::Raster);
        paintSquare(controller, 10, 10, 20);
        const doc::Cel &cel = *d.cel(celId);
        controller.execute(std::make_unique<doc::MoveLayerCommand>(celId, cel.drawingAt(5)->id, time, 0));
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setTimeLayerParent(celId, time, trajectory));
        QCOMPARE(d.cel(celId)->timeLayer(time)->parent, trajectory);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("time.moth"));
        QString error;
        QVERIFY2(io::saveProject(d, path, &error), qPrintable(error));
        const std::unique_ptr<doc::Document> loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        const doc::Cel &back = *loaded->cel(celId);
        QCOMPARE(back.timeLayers().size(), std::size_t(1));
        QCOMPARE(back.timeLayers()[0].parent, trajectory);
        QVERIFY(back.drawingAt(0)->layer(time) && back.drawingAt(5)->layer(time));
        QCOMPARE(back.drawingAt(0)->indexOf(time), 1);
        QCOMPARE(back.drawingAt(5)->indexOf(time), 0);
        QCOMPARE(back.drawingAt(5)->layer(time)->image.pixel(15, 15), 0xff000000u);
        QVERIFY(back.drawingAt(5)->layer(time)->time);
        // Put in a controller (as a file opened is), what it is is known and its copies stay as one.
        document::DocumentController opened;
        opened.replaceDocument(io::loadProject(path, &error));
        QVERIFY(inEveryDrawing(*opened.document().cel(celId), time));
    }

    // A time layer that follows a trajectory of its own is moved by it, before what its cel does; the other layers of the
    // cel are not.
    void aTimeLayerFollowsATrajectoryOfItsOwn()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 12, "A").command);
        controller.setCurrentFrame(0);
        paintSquare(controller, 100, 100, 10, 0xffff0000u); // the frame's own layer: red
        const doc::LayerId time = controller.addTimeLayer();
        paintSquare(controller, 200, 100, 10, 0xff0000ffu); // the time layer: blue
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        doc::CelPose start, later;
        later.x = 50;
        QVERIFY(controller.setCelKey(trajectory, 0, start));
        QVERIFY(controller.setCelKey(trajectory, 10, later));
        QVERIFY(controller.setTimeLayerParent(celId, time, trajectory));

        canvas::FrameRenderer renderer;
        renderer.setPaperVisible(false);
        renderer.renderAll(d, 10);
        const QImage &image = renderer.image();
        QCOMPARE(image.pixel(105, 105), 0xffff0000u); // the cel's own layer stays
        QCOMPARE(qAlpha(image.pixel(205, 105)), 0);   // the time layer went
        QCOMPARE(image.pixel(255, 105), 0xff0000ffu); // 50 to the right
        renderer.renderAll(d, 0);
        QCOMPARE(renderer.image().pixel(205, 105), 0xff0000ffu);

        // The cel's own motion comes on top of it.
        doc::CelPose down;
        down.y = 30;
        QVERIFY(controller.setCelKey(celId, 0, down));
        renderer.renderAll(d, 10);
        QCOMPARE(renderer.image().pixel(105, 135), 0xffff0000u);
        QCOMPARE(renderer.image().pixel(255, 135), 0xff0000ffu);

        // Let go: where it was drawn.
        QVERIFY(controller.setTimeLayerParent(celId, time, doc::kNoId));
        renderer.renderAll(d, 10);
        QCOMPARE(renderer.image().pixel(205, 135), 0xff0000ffu);
        QVERIFY(!controller.setTimeLayerParent(celId, time, celId)); // not its own cel
    }

    // In the timeline a time layer is a row inside its cel, and under it the rows of what it follows of its own, with
    // that one's keys; in the Layers panel a row under its cel, and one in each frame; its button makes one.
    void aTimeLayerHasRowsInsideItsCel()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 4, "A").command);
        controller.execute(doc::makeNewDrawing(d, celId, 4, 4, "B").command);
        controller.setCurrentFrame(0);
        ui::LayersPanel panel(&controller);
        panel.resize(320, 600);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *button = panel.findChild<QToolButton *>(QStringLiteral("addTimeLayer"));
        QVERIFY(button && button->menu());
        controller.setLayerChosen(true);
        QCoreApplication::processEvents();
        QVERIFY(button->isEnabled());
        button->menu()->actions().at(1)->trigger(); // a time vector layer
        QCoreApplication::processEvents();
        const doc::Cel &cel = *d.cel(celId);
        QCOMPARE(cel.timeLayers().size(), std::size_t(1));
        const doc::LayerId time = cel.timeLayers()[0].id;
        QCOMPARE(cel.drawingAt(0)->layer(time)->kind, doc::LayerKind::Vector);
        // A row under the cel, and one in each of the two frames.
        auto *tree = panel.findChild<QTreeWidget *>();
        QVERIFY(tree);
        QCOMPARE(tree->findChildren<QWidget *>(QStringLiteral("timeIcon")).size(), 3);

        ui::TimelineView view(&controller);
        auto rows = [&] {
            QStringList seen;
            for (int r = 0; r < view.rowCount(); ++r)
                seen << (view.isTimeRow(r) ? QStringLiteral("time")
                         : view.isLinkRow(r) ? QStringLiteral("link")
                         : view.isMotionRow(r) ? QStringLiteral("keys")
                                               : QStringLiteral("cel"));
            return seen.join(QLatin1Char(' '));
        };
        QCOMPARE(rows(), QStringLiteral("cel time"));
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setCelKey(trajectory, 0, doc::CelPose{}));
        QVERIFY(controller.setTimeLayerParent(celId, time, trajectory));
        QCOMPARE(rows(), QStringLiteral("cel keys cel time link")); // the trajectory (with its keys), then the cel
        const int link = view.rowCount() - 1;
        QCOMPARE(view.timeLayerOfRow(link), time);
        QCOMPARE(view.celIndexForRow(link), d.indexOf(trajectory)); // the trajectory's keys, bound
        controller.setTimeLayerParent(celId, time, doc::kNoId);
        QCOMPARE(rows(), QStringLiteral("cel keys cel time"));
    }

    // Chosen by itself (its row in the timeline), the time layer alone is what the tools act on (a transform moves it, not
    // the whole cel), and the Layers panel shows its row chosen.
    void aTimeLayerChosenByItselfIsAllTheToolsTake()
    {
        document::DocumentController controller;
        controller.setSceneLength(12);
        doc::Document &d = controller.documentForCommands();
        const doc::CelId celId = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, celId, 0, 4, "A").command);
        controller.execute(doc::makeNewDrawing(d, celId, 4, 4, "B").command);
        controller.setCurrentFrame(5);
        const doc::LayerId time = controller.addTimeLayer(doc::LayerKind::Vector);
        ui::LayersPanel panel(&controller);
        panel.resize(320, 600);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        controller.chooseCel(celId); // the whole cel: every layer of every frame
        QCoreApplication::processEvents();
        QCOMPARE(int(controller.layerSelection().size()), 4);
        QVERIFY(!controller.timeLayerChosen());

        controller.chooseTimeLayer(celId, time);
        QCoreApplication::processEvents();
        QVERIFY(controller.timeLayerChosen());
        QCOMPARE(controller.currentLayer(), time);
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Layer);
        QVERIFY(controller.layerSelection().empty()); // (empty: the current layer alone)
        QString why;
        const auto targets = controller.beginPaintLayers(&why, true);
        QCOMPARE(int(targets.size()), 1);
        QCOMPARE(targets[0].layer, time);
        QCOMPARE(targets[0].drawing, d.cel(celId)->drawingAt(5)->id); // the copy shown, which the others follow
        controller.cancelPaint();

        ui::TimelineView view(&controller);
        int timeRow = -1;
        for (int r = 0; r < view.rowCount(); ++r)
            if (view.isTimeRow(r))
                timeRow = r;
        QVERIFY(timeRow >= 0);
    }
};

QTEST_MAIN(TestTimeLayer)
#include "tst_timelayer.moc"
