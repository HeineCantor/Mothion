// Tests for document commands and the undo History.

#include "core/document/Document.h"
#include "core/document/History.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <QTest>

using namespace mothion::doc;

namespace {

constexpr Pixel kInk = 0xff202020;

// A document with one layer, returned with that layer's id.
std::pair<std::unique_ptr<Document>, LayerId> docWithLayer()
{
    auto doc = std::make_unique<Document>(1920, 1080);
    AddLayerCommand add(*doc, 0, {"Linee"});
    add.apply(*doc);
    return {std::move(doc), add.layerId()};
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
        auto [doc, layerId] = docWithLayer();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, layerId, 0);
        history.execute(*doc, std::move(nd.command));
        Drawing *drawing = doc->layer(layerId)->drawing(nd.id);
        QVERIFY(drawing);

        // A stroke: snapshot, paint live, capture.
        const TiledImage before = drawing->image;
        drawing->image.setPixel(10, 10, kInk);
        drawing->image.setPixel(200, 10, kInk);
        auto paint = PaintCommand::capture(layerId, nd.id, before, drawing->image);
        QVERIFY(paint);
        QCOMPARE(paint->swaps().size(), std::size_t(2)); // two tiles touched
        history.pushApplied(std::move(paint));

        ChangeSet undone = history.undo(*doc);
        QCOMPARE(undone.pixels.size(), std::size_t(1));
        QCOMPARE(undone.pixels[0].tiles.size(), std::size_t(2));
        QCOMPARE(drawing->image.pixel(10, 10), Pixel(0));
        QVERIFY(drawing->image.isEmpty());

        history.redo(*doc);
        QCOMPARE(drawing->image.pixel(200, 10), kInk);

        // Painting nothing gives no command.
        QVERIFY(!PaintCommand::capture(layerId, nd.id, drawing->image, drawing->image));
    }

    void paintOverExistingTileKeepsOldPixels()
    {
        auto [doc, layerId] = docWithLayer();
        NewDrawing nd = makeNewDrawing(*doc, layerId, 0);
        nd.command->apply(*doc);
        Drawing *drawing = doc->layer(layerId)->drawing(nd.id);
        drawing->image.setPixel(1, 1, kInk);

        const TiledImage before = drawing->image;
        drawing->image.setPixel(2, 2, kInk); // same tile as the existing pixel
        auto paint = PaintCommand::capture(layerId, nd.id, before, drawing->image);
        QVERIFY(paint);
        paint->revert(*doc);
        QCOMPARE(drawing->image.pixel(1, 1), kInk);
        QCOMPARE(drawing->image.pixel(2, 2), Pixel(0));
    }

    void newDrawingUndoRedoKeepsId()
    {
        auto [doc, layerId] = docWithLayer();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, layerId, 4, 3);
        history.execute(*doc, std::move(nd.command));
        Layer *layer = doc->layer(layerId);
        QCOMPARE(layer->drawingAt(5)->name, std::string("1"));
        QCOMPARE(layer->timing().drawingAt(3), kNoId);

        history.undo(*doc);
        QVERIFY(layer->drawings().empty());
        QVERIFY(layer->timing().isEmpty());

        history.redo(*doc);
        QCOMPARE(layer->timing().drawingAt(6), nd.id);
        QVERIFY(layer->drawing(nd.id));
    }

    void removeDrawingRestoresItsExposures()
    {
        auto [doc, layerId] = docWithLayer();
        History history;
        NewDrawing a = makeNewDrawing(*doc, layerId, 0, 2);
        history.execute(*doc, std::move(a.command));
        NewDrawing b = makeNewDrawing(*doc, layerId, 2, 2);
        history.execute(*doc, std::move(b.command));
        history.execute(*doc, makeSetExposure(*doc, layerId, 4, 2, a.id)); // cycle back to A
        Layer *layer = doc->layer(layerId);
        layer->drawing(a.id)->image.setPixel(0, 0, kInk);
        const Timing timingBefore = layer->timing();

        history.execute(*doc, std::make_unique<RemoveDrawingCommand>(layerId, a.id));
        QVERIFY(!layer->drawing(a.id));
        QVERIFY(!layer->timing().usesDrawing(a.id));

        history.undo(*doc);
        QVERIFY(layer->timing() == timingBefore);
        QCOMPARE(layer->drawing(a.id)->image.pixel(0, 0), kInk);
    }

    void timingFactories()
    {
        auto [doc, layerId] = docWithLayer();
        History history;
        NewDrawing nd = makeNewDrawing(*doc, layerId, 0, 2);
        history.execute(*doc, std::move(nd.command));
        Layer *layer = doc->layer(layerId);

        history.execute(*doc, makeResizeExposure(*doc, layerId, 0, 6));
        QCOMPARE(layer->timing().lastFrame(), 6);
        history.execute(*doc, makeInsertEmptyFrames(*doc, layerId, 3, 2));
        QCOMPARE(layer->timing().drawingAt(3), kNoId);
        history.execute(*doc, makeRemoveFrames(*doc, layerId, 3, 2));
        QCOMPARE(layer->timing().drawingAt(3), nd.id);

        // No-ops and invalid edits produce no command.
        QVERIFY(!makeResizeExposure(*doc, layerId, 0, 6));
        QVERIFY(!makeResizeExposure(*doc, layerId, 50, 2));
        QVERIFY(!makeSetExposure(*doc, layerId, 0, 2, nd.id));

        history.undo(*doc);
        history.undo(*doc);
        history.undo(*doc);
        QCOMPARE(layer->timing().lastFrame(), 2);
    }

    void layerCommands()
    {
        Document doc(800, 600);
        History history;
        auto addA = std::make_unique<AddLayerCommand>(doc, 0, LayerProperties{"A"});
        const LayerId a = addA->layerId();
        history.execute(doc, std::move(addA));
        auto addB = std::make_unique<AddLayerCommand>(doc, 1, LayerProperties{"B"});
        const LayerId b = addB->layerId();
        history.execute(doc, std::move(addB));
        QCOMPARE(doc.indexOf(b), 1);

        history.execute(doc, std::make_unique<MoveLayerCommand>(b, 0));
        QCOMPARE(doc.indexOf(b), 0);
        LayerProperties hidden = doc.layer(a)->properties();
        hidden.visible = false;
        hidden.opacity = 0.5f;
        history.execute(doc, makeSetLayerProperties(doc, a, hidden));
        QVERIFY(!doc.layer(a)->properties().visible);
        QVERIFY(!makeSetLayerProperties(doc, a, hidden));

        history.execute(doc, std::make_unique<RemoveLayerCommand>(a));
        QCOMPARE(doc.layerCount(), 1);

        history.undo(doc); // remove
        QCOMPARE(doc.indexOf(a), 1);
        QVERIFY(!doc.layer(a)->properties().visible);
        history.undo(doc); // properties
        QVERIFY(doc.layer(a)->properties().visible);
        history.undo(doc); // move
        QCOMPARE(doc.indexOf(b), 1);
        history.undo(doc); // add B
        history.undo(doc); // add A
        QCOMPARE(doc.layerCount(), 0);

        history.redo(doc);
        QCOMPARE(doc.layerAt(0).id(), a); // same id on redo
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
