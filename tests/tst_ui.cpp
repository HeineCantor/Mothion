// Tests for the UI layer: panel layouts, document controller actions.

#include "canvas/MoveTool.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionTool.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"
#include "tools/ToolState.h"
#include "ui/ExportDialog.h"
#include "ui/MainWindow.h"
#include "ui/PanelDock.h"
#include "ui/panels/ToolPropertiesPanel.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace mothion;

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); } // keep the user's brushes out

    // Opening a project swaps the whole document under every panel and the
    // canvas (another size, other layers); the window then shows its name.
    void mainWindowOpensAProject()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shot 12.mothion"));
        doc::Document saved(640, 360);
        saved.insertLayer(0, std::make_unique<doc::Layer>(saved.allocateLayerId(), doc::LayerProperties{"BG"}));
        doc::Layer &ink = saved.insertLayer(1, std::make_unique<doc::Layer>(saved.allocateLayerId(),
                                                                             doc::LayerProperties{"Ink"}));
        doc::Drawing &drawing = ink.addDrawing({saved.allocateDrawingId(), "1", {}});
        drawing.image.setPixel(5, 5, 0xff000000);
        ink.timing().setExposure(0, 12, drawing.id);
        QString error;
        QVERIFY2(io::saveProject(saved, path, &error), qPrintable(error));

        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openFile(path));
        QVERIFY(window.windowTitle().startsWith(QStringLiteral("shot 12")));
        QVERIFY(!window.isWindowModified());
        QTest::qWait(50); // let the panels repaint against the new document
    }

    // A dock shorter than its panel (short or highly scaled screen) scrolls
    // the panel instead of squeezing it under the next dock; with room to
    // spare the panel fills the dock and nothing scrolls.
    void shortDocksScrollTheirPanel()
    {
        tools::ToolState state;
        auto *panel = new ui::ToolPropertiesPanel(&state);
        ui::PanelDock dock(QStringLiteral("Tool properties"), QStringLiteral("dock.test"), panel);
        auto *scroll = qobject_cast<QScrollArea *>(dock.widget());
        QVERIFY(scroll);

        dock.resize(260, 160);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(panel->height() >= panel->minimumSizeHint().height()); // laid out in full, not squeezed

        dock.resize(260, 900);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QCOMPARE(panel->height(), scroll->viewport()->height()); // fills the dock
    }

    // The file name follows the chosen format; whatever the remembered
    // choices, the frames asked for stay within what the dialog offers.
    void exportDialogKeepsNameAndFormatInStep()
    {
        ui::ExportDialog dialog(4, 24, QStringLiteral("walk"));
        auto *format = dialog.findChild<QComboBox *>();
        QVERIFY(format);
        for (int i : {1, 0, 1}) {
            format->setCurrentIndex(i);
            QCOMPARE(io::formatForFile(dialog.path()), std::optional(dialog.options().format));
        }
        QCOMPARE(QFileInfo(dialog.path()).completeBaseName(), QStringLiteral("walk"));
        QVERIFY(dialog.firstFrame() >= 0 && dialog.firstFrame() <= dialog.lastFrame());
        QVERIFY(!dialog.options().transparent); // JPEG selected: never transparent
    }

    // Clear empties the drawing at the current layer and frame in one undo
    // step; the drawing and its exposures stay.
    void clearEmptiesTheCurrentDrawing()
    {
        document::DocumentController controller;
        QImage image(40, 30, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        controller.importImage(image, QStringLiteral("pic"));
        const doc::Document &d = controller.document();
        const doc::Layer &layer = *d.layer(controller.currentLayer());
        const doc::DrawingId drawing = layer.timing().drawingAt(0);
        QVERIFY(controller.canClearCurrentDrawing());

        QVERIFY(controller.clearCurrentDrawing());
        QVERIFY(layer.drawing(drawing)->image.isEmpty());
        QCOMPARE(layer.timing().drawingAt(0), drawing); // still exposed, just empty
        QVERIFY(!controller.canClearCurrentDrawing());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo clear"));

        controller.undo();
        QVERIFY(!layer.drawing(drawing)->image.isEmpty());
        controller.redo();
        QVERIFY(layer.drawing(drawing)->image.isEmpty());

        QString reason;
        QVERIFY(!controller.clearCurrentDrawing(&reason)); // nothing left
        QVERIFY(!reason.isEmpty());
    }

    void clearRespectsLockedAndHiddenLayers()
    {
        document::DocumentController controller;
        QImage red(10, 10, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("red"));
        const doc::LayerId id = controller.currentLayer();
        doc::LayerProperties props = controller.document().layer(id)->properties();

        for (bool locked : {true, false}) {
            doc::LayerProperties p = props;
            p.locked = locked;
            p.visible = locked; // first locked, then hidden
            controller.execute(doc::makeSetLayerProperties(controller.document(), id, p));
            QString reason;
            QVERIFY(!controller.canClearCurrentDrawing());
            QVERIFY(!controller.clearCurrentDrawing(&reason));
            QVERIFY(reason.contains(locked ? QStringLiteral("locked") : QStringLiteral("hidden")));
        }
    }

    void clearOnAnEmptyFrameDoesNothing()
    {
        document::DocumentController controller; // one layer, no drawings
        QVERIFY(!controller.canClearCurrentDrawing());
        QVERIFY(!controller.clearCurrentDrawing());
        QVERIFY(!controller.canUndo());
    }

    void importedImageGetsItsOwnLayer()
    {
        document::DocumentController controller; // 1920×1080, one layer
        const doc::Document &d = controller.document();
        QImage image(100, 50, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("photo"));

        QCOMPARE(d.layerCount(), 2);
        const doc::Layer &layer = d.layerAt(1); // above the first one
        QCOMPARE(layer.properties().name, std::string("photo"));
        QCOMPARE(controller.currentLayer(), layer.id());
        const doc::DrawingId drawing = layer.timing().drawingAt(0);
        QVERIFY(drawing != doc::kNoId);
        QCOMPARE(layer.timing().drawingAt(d.frameCount() - 1), drawing); // held for the whole scene
        const doc::TiledImage &pixels = layer.drawing(drawing)->image;
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xffff0000)); // centered
        QCOMPARE(pixels.pixel(909, 540), doc::Pixel(0));
        QCOMPARE(pixels.pixel(910, 515), doc::Pixel(0xffff0000));

        controller.undo(); // one step takes it all back
        QCOMPARE(d.layerCount(), 1);
    }

    void moveToolMovesTheDrawingInOneStep()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("dot")); // 955..964, 535..544
        const doc::Layer &layer = controller.document().layerAt(1);
        const doc::TiledImage &pixels = layer.drawing(layer.timing().drawingAt(0))->image;

        QStringList messages;
        canvas::MoveTool tool(&controller, [&](const QString &m) { messages << m; });
        tool.press({100, 100}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        tool.move({130.4, 110}, Qt::NoModifier);
        tool.move({150, 120.2}, Qt::NoModifier);
        tool.release({150, 120.2}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(qAlpha(pixels.pixel(960, 540)), 0);
        QCOMPARE(pixels.pixel(1010, 560), doc::Pixel(0xff00ff00)); // moved by (50, 20)

        controller.undo(); // the whole drag at once
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xff00ff00));
        controller.undo(); // then the import
        QCOMPARE(controller.document().layerCount(), 1);
        QVERIFY(messages.isEmpty());
    }

    void moveToolShiftKeepsOneAxisAndCancelRestores()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("dot"));
        const doc::Layer &layer = controller.document().layerAt(1);
        const doc::TiledImage &pixels = layer.drawing(layer.timing().drawingAt(0))->image;

        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({0, 0}, Qt::NoModifier);
        tool.move({40, 7}, Qt::ShiftModifier); // horizontal only
        QCOMPARE(pixels.pixel(1000, 540), doc::Pixel(0xff00ff00));
        tool.cancel();
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xff00ff00));
        QCOMPARE(qAlpha(pixels.pixel(1000, 540)), 0);
        QVERIFY(!controller.isPainting());
    }

    void moveToolRefusesEmptyFrames()
    {
        document::DocumentController controller; // Layer 1 has no drawing yet
        QStringList messages;
        canvas::MoveTool tool(&controller, [&](const QString &m) { messages << m; });
        tool.press({10, 10}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(messages.size(), 1);
    }

    void selectionToolsSetAndClearTheSelection()
    {
        document::DocumentController controller;
        canvas::SelectionTool rect(&controller, canvas::SelectionTool::Shape::Rectangle);
        rect.press({10.2, 20.4}, Qt::NoModifier);
        rect.move({50, 40}, Qt::NoModifier);
        rect.release({60.4, 70.6}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(10, 20, 50, 51)); // whole pixels

        rect.press({300, 300}, Qt::NoModifier); // a click deselects
        rect.release({300.5, 300}, Qt::NoModifier);
        QVERIFY(!controller.hasSelection());

        rect.press({-100, -100}, Qt::NoModifier); // clipped to the page
        rect.release({10, 10}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(0, 0, 10, 10));

        canvas::SelectionTool lasso(&controller, canvas::SelectionTool::Shape::Lasso);
        lasso.press({100, 100}, Qt::NoModifier);
        lasso.move({200, 100}, Qt::NoModifier);
        lasso.release({200, 200}, Qt::NoModifier); // triangle (100,100) (200,100) (200,200)
        QVERIFY(controller.selection().contains(QPointF(190, 110)));
        QVERIFY(!controller.selection().contains(QPointF(110, 190))); // the triangle's other half
    }

    void moveToolMovesOnlyTheSelection()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::Layer &layer = controller.document().layerAt(1);
        const doc::TiledImage &pixels = layer.drawing(layer.timing().drawingAt(0))->image;

        QPainterPath left;
        left.addRect(900, 500, 60, 100); // x 900..959: the bar's left half
        controller.setSelection(left);
        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({0, 0}, Qt::NoModifier);
        tool.release({0, 100}, Qt::NoModifier);

        QCOMPARE(pixels.pixel(930, 640), doc::Pixel(0xff00ff00)); // left half moved down
        QCOMPARE(qAlpha(pixels.pixel(930, 540)), 0);
        QCOMPARE(pixels.pixel(990, 540), doc::Pixel(0xff00ff00)); // right half stayed
        QCOMPARE(qAlpha(pixels.pixel(990, 640)), 0);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100)); // followed
    }

    void puppetWarpBendsTheDrawingInOneStep()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Layer &layer = controller.document().layerAt(1);
        const doc::TiledImage &pixels = layer.drawing(layer.timing().drawingAt(0))->image;
        const doc::Pixel ink = pixels.pixel(960, 540);

        canvas::PuppetTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.press({780, 540}, Qt::NoModifier); // anchor at the left end
        tool.release({780, 540}, Qt::NoModifier);
        QVERIFY(tool.hasPendingEdit());
        tool.press({1140, 540}, Qt::NoModifier); // handle at the right end
        QVERIFY(tool.isActive());
        QElapsedTimer timer;
        timer.start();
        for (int step = 1; step <= 10; ++step)
            tool.move({1140, 540.0 + 12 * step}, Qt::NoModifier); // lift it down by 120 px
        const qint64 perMove = timer.elapsed() / 10;
        tool.release({1140, 660}, Qt::NoModifier);
        qInfo("puppet drag update: %lld ms", perMove);

        QCOMPARE(pixels.pixel(780, 540), ink);           // the anchored end stays
        QCOMPARE(qAlpha(pixels.pixel(1140, 540)), 0);    // the free end left its place...
        QVERIFY(qAlpha(pixels.pixel(1130, 655)) > 0);     // ...for where it was dragged
        QVERIFY(!controller.canRedo());

        tool.commit();
        QVERIFY(!tool.hasPendingEdit());
        QVERIFY(!controller.isPainting());
        controller.undo(); // the whole warp at once
        QCOMPARE(pixels.pixel(1140, 540), ink);
        QCOMPARE(qAlpha(pixels.pixel(1130, 655)), 0);

        // Esc drops a warp in progress.
        tool.press({780, 540}, Qt::NoModifier);
        tool.release({780, 540}, Qt::NoModifier);
        tool.press({1140, 540}, Qt::NoModifier);
        tool.move({1140, 600}, Qt::NoModifier);
        tool.release({1140, 600}, Qt::NoModifier);
        QVERIFY(tool.keyPress(Qt::Key_Escape));
        QCOMPARE(pixels.pixel(1140, 540), ink);
        QVERIFY(!controller.isPainting());
    }

    void largeImagesAreScaledToFit()
    {
        document::DocumentController controller;
        QImage image(3840, 1080, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        controller.importImage(image, QStringLiteral("wide"));
        const doc::Layer &layer = controller.document().layerAt(1);
        const doc::TiledImage &pixels = layer.drawing(layer.timing().drawingAt(0))->image;
        // 3840×1080 → 1920×540, centered vertically.
        QCOMPARE(qAlpha(pixels.pixel(0, 540)), 255);
        QCOMPARE(qAlpha(pixels.pixel(0, 260)), 0);
        QCOMPARE(qAlpha(pixels.pixel(1919, 280)), 255);
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
