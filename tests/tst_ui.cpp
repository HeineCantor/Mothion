// Tests for the UI layer: panel layouts, document controller actions.

#include "canvas/CanvasView.h"
#include "canvas/FillTool.h"
#include "canvas/FrameRenderer.h"
#include "canvas/MoveTool.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionTool.h"
#include "canvas/TransformTool.h"
#include "canvas/ViewTransform.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"
#include "tools/ToolState.h"
#include "ui/ExportDialog.h"
#include "ui/MainWindow.h"
#include "ui/PanelDock.h"
#include "ui/SelectionBar.h"
#include "ui/panels/BrushSizePanel.h"
#include "ui/SplashScreen.h"
#include "ui/panels/ToolPalettePanel.h"
#include "ui/panels/SubToolPanel.h"
#include "ui/panels/ToolPropertiesPanel.h"

#include <QCheckBox>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QPaintEvent>
#include <QPainter>
#include <QLineEdit>
#include <QListWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScrollArea>
#include <QSpinBox>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTabletEvent>
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

    // A number field with focus keeps Ctrl+Z for the document, not for its text.
    void undoWorksFromANumberField()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        QVERIFY(controller);
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller->importImage(image, QStringLiteral("pic"));
        QVERIFY(controller->canUndo());

        QSpinBox *spin = window.findChild<ui::ToolPropertiesPanel *>()->findChild<QSpinBox *>();
        QVERIFY(spin);
        window.activateWindow();
        spin->setFocus();
        QTRY_VERIFY(spin->hasFocus());
        QTest::keyClick(spin, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!controller->canUndo());
        QVERIFY(controller->canRedo());
        QTest::keyClick(spin, Qt::Key_Y, Qt::ControlModifier);
        QVERIFY(controller->canUndo());
    }

    // Tab held right after picking a brush from the list pans at the first
    // touch; in a number field it still moves to the next field.
    void panKeysReachTheCanvasFromPanels()
    {
        ui::MainWindow window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *canvas = window.findChild<canvas::CanvasView *>();
        QListWidget *brushes = window.findChild<ui::SubToolPanel *>()->findChild<QListWidget *>();
        QVERIFY(canvas && brushes);
        brushes->setFocus();
        QTRY_VERIFY(brushes->hasFocus());

        QTest::keyPress(brushes, Qt::Key_Tab);
        QVERIFY(canvas->hasFocus());
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(canvas, Qt::Key_Tab);
        QVERIFY(canvas->cursor().shape() != Qt::OpenHandCursor);

        QSpinBox *spin = window.findChild<ui::ToolPropertiesPanel *>()->findChild<QSpinBox *>();
        spin->setFocus();
        QTRY_VERIFY(spin->hasFocus());
        QTest::keyClick(spin, Qt::Key_Tab);
        QVERIFY(!canvas->hasFocus());
    }

    // A click on a size of the palette sets the active tool's size; the grid
    // wraps to the panel's width and scrolls when the panel is short.
    void brushSizePaletteSetsTheSize()
    {
        tools::ToolState state;
        ui::BrushSizePanel panel(&state);
        panel.resize(250, 120);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        ui::BrushSizeGrid *grid = panel.grid();
        QCOMPARE(grid->width(), grid->parentWidget()->width()); // the viewport: the width less the scroll bar
        QVERIFY(grid->height() > panel.height()); // the rest scrolls
        QCOMPARE(grid->cellRect(grid->width() / 40).top(), grid->cellRect(0).bottom() + 1); // 40 px cells

        state.setActiveTool(tools::ToolId::Brush);
        const int index = int(ui::BrushSizeGrid::presets().indexOf(25));
        QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(index).center());
        QCOMPARE(state.settings(tools::ToolId::Brush).size, 25.0);
        QCOMPARE(grid->presetAt(grid->cellRect(index).center()), index);

        // Only the chosen cell is highlighted, also among the big dots
        // (whose numbers have a backing drawn in the cell's color).
        const int big = int(ui::BrushSizeGrid::presets().indexOf(60));
        QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(big).center());
        const QImage image = grid->grab().toImage();
        const QRgb highlight = grid->palette().color(QPalette::Highlight).rgb();
        QCOMPARE(image.pixel(grid->cellRect(big).topLeft() + QPoint(2, 2)), highlight);
        QVERIFY(image.pixel(grid->cellRect(big + 1).topLeft() + QPoint(2, 2)) != highlight);

        state.setActiveTool(tools::ToolId::Move); // not a paint tool: no size to set
        QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(0).center());
        QCOMPARE(state.settings(tools::ToolId::Brush).size, 60.0);
    }

    // The palette is a dock of the workspace, the front tab of its group.
    void brushSizePaletteIsInTheWorkspace()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *dock = window.findChild<ui::PanelDock *>(QStringLiteral("dock.brushSize"));
        QVERIFY(dock);
        QTRY_VERIFY(!dock->visibleRegion().isEmpty());
        QVERIFY(window.findChild<ui::PanelDock *>(QStringLiteral("dock.colorSet"))->visibleRegion().isEmpty());
    }

    void tiltCheckboxFollowsToolState()
    {
        tools::ToolState state;
        ui::ToolPropertiesPanel panel(&state);
        QCheckBox *tilt = nullptr;
        for (QCheckBox *box : panel.findChildren<QCheckBox *>())
            if (box->text() == QStringLiteral("Use pen tilt"))
                tilt = box;
        QVERIFY(tilt);
        QVERIFY(tilt->isChecked());
        QVERIFY(state.usesTilt());

        tilt->click();
        QVERIFY(!state.usesTilt());
        state.setUseTilt(true);
        QVERIFY(tilt->isChecked());
    }

    // Shrinking the brush repaints where the old, larger outline was drawn,
    // so it leaves no ring behind on the canvas.
    void shrinkingTheBrushErasesTheOldOutline()
    {
        struct PaintSpy : QObject
        {
            QRegion painted;
            bool eventFilter(QObject *, QEvent *e) override
            {
                if (e->type() == QEvent::Paint)
                    painted += static_cast<QPaintEvent *>(e)->region();
                return false;
            }
        } spy;
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(600, 600);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.setZoom(1.0);
        const QPoint at(300, 300);
        state.setSize(tools::ToolId::Brush, 200); // outline radius 100 view px
        QTest::mouseMove(&canvas, at);
        QTest::qWait(50);

        canvas.installEventFilter(&spy);
        state.setSize(tools::ToolId::Brush, 10);
        QTRY_VERIFY(!spy.painted.isEmpty());
        // Points on the old ring, far outside the new 5 px outline.
        for (const QPoint &edge : {at + QPoint(100, 0), at - QPoint(100, 0), at + QPoint(0, 99), at - QPoint(70, 70)})
            QVERIFY2(spy.painted.contains(edge), qPrintable(QStringLiteral("(%1, %2) not repainted")
                                                                .arg(edge.x()).arg(edge.y())));
    }

    void toggleToolGoesBackToThePreviousOne()
    {
        tools::ToolState state; // starts on the brush
        state.toggleTool(tools::ToolId::Eraser);
        QCOMPARE(state.activeTool(), tools::ToolId::Eraser);
        state.toggleTool(tools::ToolId::Eraser);
        QCOMPARE(state.activeTool(), tools::ToolId::Brush);

        state.setActiveTool(tools::ToolId::Puppet);
        state.toggleTool(tools::ToolId::Eraser);
        state.toggleTool(tools::ToolId::Eraser);
        QCOMPARE(state.activeTool(), tools::ToolId::Puppet);
    }

    // A tool key held lasts as long as it is held; tapped, the tool stays.
    // X swaps the colors, [ and ] step the size through the presets.
    void toolKeysHoldSwapAndResize()
    {
        tools::ToolState state;
        ui::ToolPalettePanel palette(&state);
        palette.show();
        palette.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&palette));

        QTest::keyClick(&palette, Qt::Key_L); // tap
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);
        QTest::keyPress(&palette, Qt::Key_B); // hold
        QCOMPARE(state.activeTool(), tools::ToolId::Brush);
        QTest::qWait(ui::ToolPalettePanel::kHoldMs + 50);
        QTest::keyRelease(&palette, Qt::Key_B);
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);

        QTest::keyClick(&palette, Qt::Key_B);
        state.setColor(Qt::red);
        state.setSecondaryColor(Qt::white);
        QTest::keyClick(&palette, Qt::Key_X);
        QCOMPARE(state.color(), QColor(Qt::white));
        QCOMPARE(state.secondaryColor(), QColor(Qt::red));

        state.setSize(tools::ToolId::Brush, 9); // between presets 8 and 10
        QTest::keyClick(&palette, Qt::Key_BracketRight);
        QCOMPARE(state.settings(tools::ToolId::Brush).size, 10.0);
        QTest::keyClick(&palette, Qt::Key_BracketRight);
        QCOMPARE(state.settings(tools::ToolId::Brush).size, 12.0);
        QTest::keyClick(&palette, Qt::Key_BracketLeft);
        QTest::keyClick(&palette, Qt::Key_BracketLeft);
        QCOMPARE(state.settings(tools::ToolId::Brush).size, 8.0);
    }

    // E selects the eraser and, pressed again, the tool used before; a click
    // on the eraser button just keeps the eraser.
    void eraserShortcutTogglesBack()
    {
        tools::ToolState state;
        ui::ToolPalettePanel palette(&state);
        palette.show();
        palette.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&palette));
        state.setActiveTool(tools::ToolId::Move);

        QTest::keyClick(&palette, Qt::Key_E);
        QCOMPARE(state.activeTool(), tools::ToolId::Eraser);
        QTest::keyClick(&palette, Qt::Key_E);
        QCOMPARE(state.activeTool(), tools::ToolId::Move);

        QTest::keyClick(&palette, Qt::Key_E);
        QAbstractButton *eraser = palette.findChild<QButtonGroup *>()->button(int(tools::ToolId::Eraser));
        eraser->click();
        QCOMPARE(state.activeTool(), tools::ToolId::Eraser);

        QTest::keyClick(&palette, Qt::Key_B); // other tools do not toggle
        QTest::keyClick(&palette, Qt::Key_B);
        QCOMPARE(state.activeTool(), tools::ToolId::Brush);
    }

    // The splash is drawn sharp on HiDPI screens and finds the logo in the resources.
    void splashShowsTheLogo()
    {
        const QPixmap splash = ui::splashPixmap(QStringLiteral("1.2.3"), 2.0);
        QCOMPARE(splash.deviceIndependentSize().toSize(), QSize(600, 340));
        QCOMPARE(splash.size(), QSize(1200, 680));
        const QImage image = splash.toImage();
        const QColor background = image.pixelColor(4, image.height() / 2);
        QVERIFY(image.pixelColor(2 * (24 + 140), 2 * 170) != background); // the logo's body
    }

    // Holding Tab turns the pen into the hand, like Space; Tab stays on the
    // canvas instead of moving the focus to the next widget.
    void holdingTabPans()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QWidget window;
        auto *canvas = new canvas::CanvasView(&state, &controller, &window);
        auto *other = new QLineEdit(&window); // something Tab could move the focus to
        other->move(0, 400);
        window.resize(400, 450);
        canvas->resize(400, 380);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        const Qt::CursorShape drawing = canvas->cursor().shape();
        QVERIFY(drawing != Qt::OpenHandCursor);

        QTest::keyPress(canvas, Qt::Key_Tab);
        QVERIFY(canvas->hasFocus());
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(canvas, Qt::Key_Tab);
        QCOMPARE(canvas->cursor().shape(), drawing);

        // Released elsewhere: the hand does not stick.
        QTest::keyPress(canvas, Qt::Key_Tab);
        other->setFocus();
        QTRY_VERIFY(!canvas->hasFocus());
        QCOMPARE(canvas->cursor().shape(), drawing);
    }

    // After a click on a panel (say, a brush in the sub tool list), touching
    // the canvas with the pen brings the keys back to it.
    void penPressTakesTheFocus()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QWidget window;
        auto *canvas = new canvas::CanvasView(&state, &controller, &window);
        auto *panel = new QLineEdit(&window);
        panel->move(0, 400);
        window.resize(400, 450);
        canvas->resize(400, 380);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        panel->setFocus();
        QTRY_VERIFY(panel->hasFocus());

        static const QPointingDevice pen(QStringLiteral("test pen"), 1, QInputDevice::DeviceType::Stylus,
                                         QPointingDevice::PointerType::Pen,
                                         QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        const QPointF at(200, 190);
        QTabletEvent press(QEvent::TabletPress, &pen, at, canvas->mapToGlobal(at), 0.5, 0, 0, 0, 0, 0, Qt::NoModifier,
                           Qt::LeftButton, Qt::LeftButton);
        QApplication::sendEvent(canvas, &press);
        QTabletEvent release(QEvent::TabletRelease, &pen, at, canvas->mapToGlobal(at), 0, 0, 0, 0, 0, 0,
                             Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
        QApplication::sendEvent(canvas, &release);
        QTRY_VERIFY(canvas->hasFocus());

        QTest::keyPress(canvas, Qt::Key_Tab);
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(canvas, Qt::Key_Tab);
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

    // The selection follows the move while dragging, and the move is undone
    // with it: pixels and selection in one step.
    void moveCarriesTheSelectionLiveAndUndoesIt()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::Layer &layer = controller.document().layerAt(1);
        const doc::TiledImage &pixels = layer.drawing(layer.timing().drawingAt(0))->image;
        QPainterPath left;
        left.addRect(900, 500, 60, 100);
        controller.setSelection(left);

        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({0, 0}, Qt::NoModifier);
        tool.move({0, 50}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 550, 60, 100)); // live
        tool.release({0, 100}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo move"));

        controller.undo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));
        QCOMPARE(pixels.pixel(930, 540), doc::Pixel(0xff00ff00));
        controller.redo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100));
        QCOMPARE(pixels.pixel(930, 640), doc::Pixel(0xff00ff00));

        tool.press({0, 0}, Qt::NoModifier); // a cancelled move puts the selection back
        tool.move({30, 0}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(930, 600, 60, 100));
        tool.cancel();
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100));

        QPainterPath empty; // over transparent pixels only the selection moves: still undoable
        empty.addRect(100, 100, 50, 50);
        controller.setSelection(empty);
        tool.press({0, 0}, Qt::NoModifier);
        tool.release({20, 0}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(120, 100, 50, 50));
        controller.undo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(100, 100, 50, 50));
    }

    // While drawn the lasso is an open line; the release closes it.
    void lassoShowsNoClosingLineWhileDrawn()
    {
        document::DocumentController controller;
        canvas::SelectionTool lasso(&controller, canvas::SelectionTool::Shape::Lasso);
        lasso.press({10, 10}, Qt::NoModifier);
        lasso.move({90, 10}, Qt::NoModifier);
        lasso.move({90, 90}, Qt::NoModifier);

        const canvas::ViewTransform view;
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            lasso.paintOverlay(p, view);
        }
        const QPoint onEdge = view.documentToView().map(QPointF(50, 10)).toPoint();
        const QPoint onClosingLine = view.documentToView().map(QPointF(50, 50)).toPoint();
        QVERIFY(qAlpha(image.pixel(onEdge)) > 0);
        QCOMPARE(qAlpha(image.pixel(onClosingLine)), 0);

        lasso.release({90, 90}, Qt::NoModifier);
        QVERIFY(controller.selection().contains(QPointF(70, 30))); // closed: a triangle
    }

    // The eyedropper picks the color shown; Alt does the same with the brush,
    // without painting.
    void eyedropperPicksTheShownColor()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(200, 200, QImage::Format_ARGB32);
        image.fill(QColor(200, 40, 30));
        controller.importImage(image, QStringLiteral("red")); // centered: 860..1059, 440..639
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const QPoint onRed = canvas.view().documentToView().map(QPointF(960, 540)).toPoint();
        const QPoint onPaper = canvas.view().documentToView().map(QPointF(100, 100)).toPoint();

        state.setActiveTool(tools::ToolId::Eyedropper);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, onRed);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, onRed);
        QCOMPARE(state.color(), QColor(200, 40, 30));

        state.setActiveTool(tools::ToolId::Brush);
        state.setColor(Qt::blue);
        QTest::keyPress(&canvas, Qt::Key_Alt);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, onPaper);
        QTest::mouseMove(&canvas, onRed);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, onRed);
        QTest::keyRelease(&canvas, Qt::Key_Alt);
        QCOMPARE(state.color(), QColor(200, 40, 30)); // live, to where it was let go
        QCOMPARE(controller.undoText(), QStringLiteral("Undo import image")); // no stroke
    }

    // The bucket fills inside the lines, under their edge, within the
    // selection, as one undo step.
    void fillToolFillsInsideTheLines()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage ring(100, 100, QImage::Format_ARGB32);
        ring.fill(Qt::transparent);
        {
            QPainter p(&ring);
            p.setPen(QPen(Qt::black, 4));
            p.drawRect(10, 10, 80, 80);
        }
        controller.importImage(ring, QStringLiteral("lines")); // at 910..1009, 490..589
        const doc::Layer &layer = *controller.document().layer(controller.currentLayer());
        auto pixel = [&](int x, int y) { return layer.drawingAt(0)->image.pixel(x, y); };
        canvas::FrameRenderer frame;
        QStringList messages;
        canvas::FillTool tool(&controller, &state, [&]() -> const QImage & {
            frame.renderAll(controller.document(), controller.currentFrame());
            return frame.image();
        }, [&](const QString &text) { messages << text; });

        state.setColor(QColor(255, 0, 0));
        tool.press({960, 540}, Qt::NoModifier); // inside
        QCOMPARE(pixel(960, 540), doc::Pixel(0xffff0000));
        QCOMPARE(qAlpha(pixel(900, 540)), 0);       // outside stays empty
        QCOMPARE(pixel(920, 540), doc::Pixel(0xff000000)); // the line stays on top
        QCOMPARE(pixel(923, 540), doc::Pixel(0xffff0000)); // up to it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo fill"));
        controller.undo();
        QCOMPARE(qAlpha(pixel(960, 540)), 0);

        QPainterPath left; // kept inside the selection
        left.addRect(900, 480, 60, 120);
        controller.setSelection(left);
        tool.press({950, 540}, Qt::NoModifier);
        QCOMPARE(pixel(950, 540), doc::Pixel(0xffff0000));
        QCOMPARE(qAlpha(pixel(980, 540)), 0);
        tool.press({980, 540}, Qt::NoModifier); // outside it: refused
        QCOMPARE(qAlpha(pixel(980, 540)), 0);
        QVERIFY(!messages.isEmpty());
    }

    // The transform moves, scales and rotates the selected pixels with their
    // selection; Enter applies (one undo step), Esc drops it, either goes back.
    void transformMovesScalesAndRotatesTheSelection()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::Layer &layer = controller.document().layerAt(1);
        auto pixel = [&](int x, int y) { return layer.drawing(layer.timing().drawingAt(0))->image.pixel(x, y); };
        const doc::Pixel green = pixel(930, 540);
        QPainterPath left;
        left.addRect(900, 500, 60, 100); // the bar's left half
        controller.setSelection(left);
        state.setActiveTool(tools::ToolId::LassoSelect);
        state.setActiveTool(tools::ToolId::Transform);

        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.activated();
        QVERIFY(tool.hasPendingEdit());
        QCOMPARE(tool.box(), QRect(910, 535, 50, 10)); // tight around the pixels

        tool.press({930, 540}, Qt::NoModifier); // inside: moves
        tool.move({930, 590}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 550, 60, 100)); // live
        tool.release({930, 640}, Qt::NoModifier);
        QCOMPARE(pixel(930, 640), green);
        QCOMPARE(qAlpha(pixel(930, 540)), 0);
        QCOMPARE(pixel(990, 540), green); // outside the selection: stays
        QVERIFY(tool.keyPress(Qt::Key_Return));
        QVERIFY(!tool.hasPendingEdit());
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo transform"));
        controller.undo();
        QCOMPARE(pixel(930, 540), green);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        tool.activated(); // the bottom-right corner, twice as far from the top-left one
        tool.press({960, 545}, Qt::NoModifier);
        tool.release({1010, 555}, Qt::NoModifier);
        QCOMPARE(tool.transform().mapRect(QRectF(tool.box())), QRectF(910, 535, 100, 20));
        QCOMPARE(pixel(1000, 550), green);
        QVERIFY(tool.keyPress(Qt::Key_Escape)); // dropped
        QCOMPARE(qAlpha(pixel(1000, 550)), 0);
        QCOMPARE(pixel(930, 540), green);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        tool.activated(); // outside the box: a quarter turn around its center (935, 540)
        tool.press({1000, 540}, Qt::NoModifier);
        tool.release({935, 600}, Qt::NoModifier);
        const QPointF axis = tool.transform().map(QPointF(1, 0)) - tool.transform().map(QPointF(0, 0));
        QVERIFY(std::abs(axis.x()) < 1e-6 && std::abs(axis.y() - 1) < 1e-6);
        QCOMPARE(pixel(935, 560), green); // the bar now stands upright
        QCOMPARE(qAlpha(pixel(915, 540)), 0);
        tool.commit();
        QCOMPARE(controller.undoText(), QStringLiteral("Undo transform"));
    }

    // The ants march while there is a selection: the dashes swap in turns.
    void selectionAntsMarch()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::LassoSelect);

        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&canvas, QPoint(250, 100));
        QTest::mouseMove(&canvas, QPoint(250, 220));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(250, 220));
        QVERIFY(controller.hasSelection());
        const int phase = canvas.selectionAntsPhase();
        QTRY_VERIFY(canvas.selectionAntsPhase() != phase);
        QTRY_VERIFY(canvas.selectionAntsPhase() == phase);

        const canvas::ViewTransform view; // phase 1 is phase 0 with black and white swapped
        QImage still(100, 100, QImage::Format_ARGB32), swapped(100, 100, QImage::Format_ARGB32);
        QPainterPath line;
        line.moveTo(10, 50);
        line.lineTo(90, 50);
        for (auto [image, ph] : {std::pair{&still, 0}, std::pair{&swapped, 1}}) {
            image->fill(Qt::transparent);
            QPainter p(image);
            canvas::paintSelectionOutline(p, view, line, ph);
        }
        const int y = view.documentToView().map(QPointF(0, 50)).toPoint().y();
        int swaps = 0;
        for (int x = 0; x < 100; ++x)
            if (qAlpha(still.pixel(x, y)) && still.pixel(x, y) != swapped.pixel(x, y))
                ++swaps;
        QVERIFY(swaps > 0);
    }

    // Making, changing and clearing a selection are undo steps.
    // Erase, cut and copy work on what is selected, each as one undo step.
    void selectionCommandsWorkOnTheSelectedPixels()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::LayerId bar = controller.currentLayer();
        auto pixel = [&](doc::LayerId id, int x, int y) {
            const doc::Layer &layer = *controller.document().layer(id);
            const doc::Drawing *drawing = layer.drawingAt(0);
            return drawing ? drawing->image.pixel(x, y) : doc::Pixel(0);
        };
        const doc::Pixel green = pixel(bar, 930, 540);
        QString reason;
        QVERIFY(!controller.eraseSelection(&reason)); // nothing selected
        QVERIFY(!reason.isEmpty());

        QPainterPath left;
        left.addRect(900, 500, 60, 100); // the bar's left half
        controller.setSelection(left);
        QVERIFY(controller.eraseSelection());
        QCOMPARE(qAlpha(pixel(bar, 930, 540)), 0);
        QCOMPARE(pixel(bar, 990, 540), green);
        QVERIFY(controller.hasSelection());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo clear"));
        controller.undo();
        QCOMPARE(pixel(bar, 930, 540), green);

        const int layers = controller.document().layerCount();
        QVERIFY(controller.copySelectionToLayer());
        QCOMPARE(controller.document().layerCount(), layers + 1);
        const doc::LayerId copy = controller.currentLayer();
        QVERIFY(copy != bar);
        QCOMPARE(controller.document().indexOf(copy), controller.document().indexOf(bar) + 1);
        QCOMPARE(pixel(copy, 930, 540), green);
        QCOMPARE(qAlpha(pixel(copy, 990, 540)), 0);
        QCOMPARE(pixel(bar, 930, 540), green); // the source keeps it
        QVERIFY(controller.document().layer(copy)->drawingAt(controller.document().frameCount() - 1));
        controller.undo();
        QCOMPARE(controller.document().layerCount(), layers);

        controller.setCurrentLayer(bar);
        QVERIFY(controller.cutSelectionToLayer());
        const doc::LayerId cut = controller.currentLayer();
        QCOMPARE(pixel(cut, 930, 540), green);
        QCOMPARE(qAlpha(pixel(bar, 930, 540)), 0); // the source loses it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo cut to layer"));
        controller.undo();
        QCOMPARE(controller.document().layerCount(), layers);
        QCOMPARE(pixel(bar, 930, 540), green);

        QPainterPath empty; // nothing under the selection
        empty.addRect(100, 100, 50, 50);
        controller.setSelection(empty);
        QVERIFY(!controller.cutSelectionToLayer(&reason));
        QCOMPARE(controller.document().layerCount(), layers);
    }

    void invertSelectionSelectsTheRest()
    {
        document::DocumentController controller;
        controller.invertSelection(); // nothing selected: all
        QVERIFY(controller.selection().contains(QPointF(5, 5)));
        QPainterPath box;
        box.addRect(100, 100, 50, 50);
        controller.setSelection(box);
        controller.invertSelection();
        QVERIFY(!controller.selection().contains(QPointF(120, 120)));
        QVERIFY(controller.selection().contains(QPointF(5, 5)));
        controller.undo();
        QVERIFY(controller.selection().contains(QPointF(120, 120)));
    }

    // The bar shows with a selection, just under it, and its buttons act.
    void selectionBarFollowsTheSelection()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(600, 400);
        ui::SelectionBar bar(&canvas, &controller, &state);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QVERIFY(!bar.isVisible());

        QPainterPath box;
        box.addRect(800, 400, 300, 200);
        controller.setSelection(box);
        QVERIFY(bar.isVisible());
        const QRect area = canvas.view().documentToView().mapRect(box.boundingRect()).toAlignedRect();
        QVERIFY(bar.geometry().top() > area.bottom());
        QVERIFY(bar.geometry().top() - area.bottom() < 20);
        QVERIFY(canvas.rect().contains(bar.geometry()));

        canvas.zoomIn(); // follows the view
        const QRect zoomed = canvas.view().documentToView().mapRect(box.boundingRect()).toAlignedRect();
        QVERIFY(bar.geometry().top() > zoomed.bottom() || bar.geometry().bottom() < zoomed.top()
                || bar.geometry().bottom() == canvas.height() - 4);

        auto *invert = bar.findChild<QAbstractButton *>(QStringLiteral("invert"));
        QVERIFY(invert);
        // A pen on a button stays with the bar: the brush must not paint under it.
        state.setActiveTool(tools::ToolId::Brush);
        const QPointF at = QPointF(invert->rect().center());
        for (QEvent::Type type : {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}) {
            QTabletEvent pen(type, QPointingDevice::primaryPointingDevice(), at, invert->mapToGlobal(at), 0.8, 0, 0,
                             0, 0, 0, Qt::NoModifier, Qt::LeftButton,
                             type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(invert, &pen);
        }
        QCOMPARE(controller.undoText(), QStringLiteral("Undo selection")); // no stroke
        invert->click();
        QVERIFY(!controller.selection().contains(QPointF(900, 500)));
        bar.findChild<QAbstractButton *>(QStringLiteral("transform"))->click();
        QCOMPARE(state.activeTool(), tools::ToolId::Transform);
        state.setActiveTool(tools::ToolId::Brush);
        bar.findChild<QAbstractButton *>(QStringLiteral("deselect"))->click();
        QVERIFY(!controller.hasSelection());
        QVERIFY(!bar.isVisible());
    }

    void selectionChangesAreUndoable()
    {
        document::DocumentController controller;
        QPainterPath box;
        box.addRect(100, 100, 50, 50);
        controller.setSelection(box);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo selection"));
        controller.selectAll();
        controller.clearSelection();
        QVERIFY(!controller.hasSelection());

        controller.undo();
        QCOMPARE(controller.selection().boundingRect(),
                 QRectF(0, 0, controller.document().width(), controller.document().height()));
        controller.undo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(100, 100, 50, 50));
        controller.undo();
        QVERIFY(!controller.hasSelection());
        QVERIFY(!controller.canUndo());
        controller.redo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(100, 100, 50, 50));

        controller.setSelection(box); // the same selection again is no step
        controller.clearSelection();
        controller.undo();
        QVERIFY(controller.canUndo());
        controller.undo();
        QVERIFY(!controller.canUndo());
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
