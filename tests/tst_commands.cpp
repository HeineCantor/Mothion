// Tests for document commands and the undo History.

#include "core/document/Document.h"
#include "core/document/History.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <QTest>

using namespace mothion::doc;

namespace {

constexpr Pixel kInk = 0xff202020;

// A document with one layer, returned with that layer's id.
std::pair<std::unique_ptr<Document>, CelId> docWithCel()
{
    auto doc = std::make_unique<Document>(1920, 1080);
    AddCelCommand add(*doc, 0, {"Linee"});
    add.apply(*doc);
    return {std::move(doc), add.celId()};
}

// A command whose memory footprint is set by the test.
class FatCommand : public Command
{
public:
    explicit FatCommand(std::size_t bytes) : m_bytes(bytes) {}
    CommandKind kind() const override { return CommandKind::ChangeTiming; }
    ChangeSet apply(Document &) override { return {}; }
    ChangeSet revert(Document &) override { return {}; }
    std::size_t memoryBytes() const override { return m_bytes; }

private:
    std::size_t m_bytes;
};

} // namespace

class TestCommands : public QObject
{
    Q_OBJECT

private slots:
    void paintCaptureUndoRedo()
    {
        auto [doc, celId] = docWithCel();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, celId, 0);
        history.execute(*doc, std::move(nd.command));
        Drawing *drawing = doc->cel(celId)->drawing(nd.id);
        QVERIFY(drawing);

        // A stroke: snapshot, paint live, capture.
        const TiledImage before = drawing->layers.front().image;
        drawing->layers.front().image.setPixel(10, 10, kInk);
        drawing->layers.front().image.setPixel(200, 10, kInk);
        auto paint = PaintCommand::capture(celId, nd.id, nd.layer, before, drawing->layers.front().image);
        QVERIFY(paint);
        QCOMPARE(paint->swaps().size(), std::size_t(2)); // two tiles touched
        history.pushApplied(std::move(paint));

        ChangeSet undone = history.undo(*doc);
        QCOMPARE(undone.pixels.size(), std::size_t(1));
        QCOMPARE(undone.pixels[0].tiles.size(), std::size_t(2));
        QCOMPARE(drawing->layers.front().image.pixel(10, 10), Pixel(0));
        QVERIFY(drawing->layers.front().image.isEmpty());

        history.redo(*doc);
        QCOMPARE(drawing->layers.front().image.pixel(200, 10), kInk);

        // Painting nothing gives no command.
        QVERIFY(!PaintCommand::capture(celId, nd.id, nd.layer, drawing->layers.front().image, drawing->layers.front().image));
    }

    void paintOverExistingTileKeepsOldPixels()
    {
        auto [doc, celId] = docWithCel();
        NewDrawing nd = makeNewDrawing(*doc, celId, 0);
        nd.command->apply(*doc);
        Drawing *drawing = doc->cel(celId)->drawing(nd.id);
        drawing->layers.front().image.setPixel(1, 1, kInk);

        const TiledImage before = drawing->layers.front().image;
        drawing->layers.front().image.setPixel(2, 2, kInk); // same tile as the existing pixel
        auto paint = PaintCommand::capture(celId, nd.id, nd.layer, before, drawing->layers.front().image);
        QVERIFY(paint);
        paint->revert(*doc);
        QCOMPARE(drawing->layers.front().image.pixel(1, 1), kInk);
        QCOMPARE(drawing->layers.front().image.pixel(2, 2), Pixel(0));
    }

    void newDrawingUndoRedoKeepsId()
    {
        auto [doc, celId] = docWithCel();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, celId, 4, 3);
        history.execute(*doc, std::move(nd.command));
        Cel *cel = doc->cel(celId);
        QCOMPARE(cel->drawingAt(5)->name, std::string("1"));
        QCOMPARE(cel->timing().drawingAt(3), kNoId);

        history.undo(*doc);
        QVERIFY(cel->drawings().empty());
        QVERIFY(cel->timing().isEmpty());

        history.redo(*doc);
        QCOMPARE(cel->timing().drawingAt(6), nd.id);
        QVERIFY(cel->drawing(nd.id));
    }

    // A new drawing has one layer; layers are added, moved, changed and
    // removed with their pixels, each undoable.
    void layerCommandsUndoRedo()
    {
        auto [doc, celId] = docWithCel();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, celId, 0, 1, "Line");
        history.execute(*doc, std::move(nd.command));
        Drawing *drawing = doc->cel(celId)->drawing(nd.id);
        QCOMPARE(drawing->layers.size(), std::size_t(1));
        QCOMPARE(drawing->layers[0].id, nd.layer);
        QCOMPARE(drawing->layers[0].properties.name, std::string("Line"));

        auto add = std::make_unique<AddLayerCommand>(*doc, celId, nd.id, 1, LayerProperties{"Color"});
        const LayerId color = add->layerId();
        history.execute(*doc, std::move(add));
        QCOMPARE(drawing->indexOf(color), 1);
        drawing->layer(color)->image.setPixel(3, 3, kInk);

        history.execute(*doc, std::make_unique<MoveLayerCommand>(celId, nd.id, color, 0));
        QCOMPARE(drawing->indexOf(color), 0);
        LayerProperties hidden = drawing->layer(color)->properties;
        hidden.visible = false;
        history.execute(*doc, makeSetLayerProperties(*doc, celId, nd.id, color, hidden));
        QVERIFY(!drawing->layer(color)->properties.visible);
        QVERIFY(!makeSetLayerProperties(*doc, celId, nd.id, color, hidden)); // no change

        history.execute(*doc, std::make_unique<RemoveLayerCommand>(celId, nd.id, color));
        QCOMPARE(drawing->layers.size(), std::size_t(1));
        history.undo(*doc);
        QCOMPARE(drawing->indexOf(color), 0);
        QCOMPARE(drawing->layer(color)->image.pixel(3, 3), kInk);
        history.undo(*doc);
        QVERIFY(drawing->layer(color)->properties.visible);
        history.undo(*doc);
        QCOMPARE(drawing->indexOf(color), 1);
        history.undo(*doc);
        QVERIFY(!drawing->layer(color));
        history.redo(*doc);
        QCOMPARE(drawing->indexOf(color), 1); // the same id again
    }

    void removeDrawingRestoresItsExposures()
    {
        auto [doc, celId] = docWithCel();
        History history;
        NewDrawing a = makeNewDrawing(*doc, celId, 0, 2);
        history.execute(*doc, std::move(a.command));
        NewDrawing b = makeNewDrawing(*doc, celId, 2, 2);
        history.execute(*doc, std::move(b.command));
        history.execute(*doc, makeSetExposure(*doc, celId, 4, 2, a.id)); // cycle back to A
        Cel *cel = doc->cel(celId);
        cel->drawing(a.id)->layers.front().image.setPixel(0, 0, kInk);
        const Timing timingBefore = cel->timing();

        history.execute(*doc, std::make_unique<RemoveDrawingCommand>(celId, a.id));
        QVERIFY(!cel->drawing(a.id));
        QVERIFY(!cel->timing().usesDrawing(a.id));

        history.undo(*doc);
        QVERIFY(cel->timing() == timingBefore);
        QCOMPARE(cel->drawing(a.id)->layers.front().image.pixel(0, 0), kInk);
    }

    void timingFactories()
    {
        auto [doc, celId] = docWithCel();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, celId, 0, 2);
        history.execute(*doc, std::move(nd.command));
        Cel *cel = doc->cel(celId);

        history.execute(*doc, makeResizeExposure(*doc, celId, 0, 6));
        QCOMPARE(cel->timing().lastFrame(), 6);
        history.execute(*doc, makeInsertEmptyFrames(*doc, celId, 3, 2));
        QCOMPARE(cel->timing().drawingAt(3), kNoId);
        history.execute(*doc, makeRemoveFrames(*doc, celId, 3, 2));
        QCOMPARE(cel->timing().drawingAt(3), nd.id);

        // No-ops and invalid edits produce no command.
        QVERIFY(!makeResizeExposure(*doc, celId, 0, 6));
        QVERIFY(!makeResizeExposure(*doc, celId, 50, 2));
        QVERIFY(!makeSetExposure(*doc, celId, 0, 2, nd.id));

        history.undo(*doc);
        history.undo(*doc);
        history.undo(*doc);
        QCOMPARE(cel->timing().lastFrame(), 2);
    }

    void layerCommands()
    {
        Document doc(800, 600);
        History history;
        auto addA = std::make_unique<AddCelCommand>(doc, 0, CelProperties{"A"});
        const CelId a = addA->celId();
        history.execute(doc, std::move(addA));
        auto addB = std::make_unique<AddCelCommand>(doc, 1, CelProperties{"B"});
        const CelId b = addB->celId();
        history.execute(doc, std::move(addB));
        QCOMPARE(doc.indexOf(b), 1);

        history.execute(doc, std::make_unique<MoveCelCommand>(b, 0));
        QCOMPARE(doc.indexOf(b), 0);
        CelProperties hidden = doc.cel(a)->properties();
        hidden.visible = false;
        hidden.opacity = 0.5f;
        history.execute(doc, makeSetCelProperties(doc, a, hidden));
        QVERIFY(!doc.cel(a)->properties().visible);
        QVERIFY(!makeSetCelProperties(doc, a, hidden));

        history.execute(doc, std::make_unique<RemoveCelCommand>(a));
        QCOMPARE(doc.celCount(), 1);

        history.undo(doc); // remove
        QCOMPARE(doc.indexOf(a), 1);
        QVERIFY(!doc.cel(a)->properties().visible);
        history.undo(doc); // properties
        QVERIFY(doc.cel(a)->properties().visible);
        history.undo(doc); // move
        QCOMPARE(doc.indexOf(b), 1);
        history.undo(doc); // add B
        history.undo(doc); // add A
        QCOMPARE(doc.celCount(), 0);

        history.redo(doc);
        QCOMPARE(doc.celAt(0).id(), a); // same id on redo
    }

    void historyDiscardsRedoBranch()
    {
        Document doc(10, 10);
        History history;
        history.pushApplied(std::make_unique<FatCommand>(1));
        history.pushApplied(std::make_unique<FatCommand>(1));
        history.undo(doc);
        QVERIFY(history.canRedo());
        history.pushApplied(std::make_unique<FatCommand>(1));
        QVERIFY(!history.canRedo());
        QCOMPARE(history.size(), std::size_t(2));
    }

    void historyStepAndMemoryLimits()
    {
        Document doc(10, 10);
        History steps({3, 1000});
        for (int i = 0; i < 5; ++i)
            steps.pushApplied(std::make_unique<FatCommand>(1));
        QCOMPARE(steps.size(), std::size_t(3));

        History memory({100, 1000});
        memory.pushApplied(std::make_unique<FatCommand>(400));
        memory.pushApplied(std::make_unique<FatCommand>(400));
        QCOMPARE(memory.size(), std::size_t(2));
        memory.pushApplied(std::make_unique<FatCommand>(400)); // 1200 > 1000: oldest goes
        QCOMPARE(memory.size(), std::size_t(2));
        QCOMPARE(memory.memoryBytes(), std::size_t(800));

        memory.pushApplied(std::make_unique<FatCommand>(5000)); // alone over the limit: kept
        QCOMPARE(memory.size(), std::size_t(1));
        QVERIFY(memory.canUndo());
    }

    void cleanMark()
    {
        Document doc(10, 10);
        History history;
        QVERIFY(history.isClean()); // new document
        history.pushApplied(std::make_unique<FatCommand>(1));
        QVERIFY(!history.isClean());
        history.markClean(); // saved
        history.pushApplied(std::make_unique<FatCommand>(1));
        QVERIFY(!history.isClean());
        history.undo(doc);
        QVERIFY(history.isClean()); // back to the saved state

        history.undo(doc);
        history.pushApplied(std::make_unique<FatCommand>(1)); // saved state is on the discarded branch
        QVERIFY(!history.isClean());
        history.undo(doc);
        QVERIFY(!history.isClean()); // and can never come back
    }
};

QTEST_APPLESS_MAIN(TestCommands)
#include "tst_commands.moc"
