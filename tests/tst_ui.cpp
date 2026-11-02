// Tests for the UI layer: panel layouts, document controller actions.

#include "canvas/CanvasView.h"
#include "canvas/FillTool.h"
#include "canvas/FrameRenderer.h"
#include "canvas/MoveTool.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionTool.h"
#include "canvas/TransformTool.h"
#include "canvas/ViewTransform.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"
#include "tools/BrushLibrary.h"
#include "tools/BrushPreset.h"
#include "tools/ToolState.h"
#include "ui/AutoplayWindow.h"
#include "ui/ExportDialog.h"
#include "ui/MainWindow.h"
#include "ui/PanelDock.h"
#include "ui/SelectionBar.h"
#include "ui/panels/BlendModes.h"
#include "ui/panels/BrushSizePanel.h"
#include "ui/panels/LayersPanel.h"
#include "ui/SplashScreen.h"
#include "ui/panels/ToolPalettePanel.h"
#include "ui/panels/SubToolPanel.h"
#include "ui/panels/ToolPropertiesPanel.h"
#include "ui/panels/timeline/TimelinePanel.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QCheckBox>
#include <QClipboard>
#include <QMimeData>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QPaintEvent>
#include <QPainter>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QMenu>
#include <QMenuBar>
#include <QSlider>
#include <QTest>
#include <QWheelEvent>
#include <QToolButton>

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
        saved.insertCel(0, std::make_unique<doc::Cel>(saved.allocateCelId(), doc::CelProperties{"BG"}));
        doc::Cel &ink =
            saved.insertCel(1, std::make_unique<doc::Cel>(saved.allocateCelId(), doc::CelProperties{"Ink"}));
        doc::Drawing &drawing = ink.addDrawing({saved.allocateDrawingId(), "1", {{saved.allocateLayerId(), {"Line"}, {}}}});
        drawing.layers.front().image.setPixel(5, 5, 0xff000000);
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

    // Rectangle and lasso are the sub tools of one selection tool: one button
    // (M) that selects the shape used last, the shapes in the sub tool list.
    void selectionIsOneToolWithTwoSubTools()
    {
        tools::ToolState state;
        tools::BrushLibrary library;
        ui::ToolPalettePanel palette(&state);
        ui::SubToolPanel subTools(&state, &library);
        palette.show();
        palette.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&palette));
        auto *group = palette.findChild<QButtonGroup *>();
        QVERIFY(group->button(int(tools::ToolId::RectSelect)));
        QVERIFY(!group->button(int(tools::ToolId::LassoSelect))); // no button of its own

        QTest::keyClick(&palette, Qt::Key_M);
        QCOMPARE(state.activeTool(), tools::ToolId::RectSelect);
        auto *list = subTools.findChild<QListWidget *>();
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->currentRow(), 0);
        list->itemClicked(list->item(1)); // the lasso sub tool
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);
        QCOMPARE(list->currentRow(), 1);
        QVERIFY(group->button(int(tools::ToolId::RectSelect))->isChecked()); // the same button

        QTest::keyClick(&palette, Qt::Key_B);
        QTest::keyClick(&palette, Qt::Key_M); // the shape used last
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);
        QTest::keyClick(&palette, Qt::Key_B);
        group->button(int(tools::ToolId::RectSelect))->click();
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);
        list->itemClicked(list->item(0));
        QCOMPARE(state.activeTool(), tools::ToolId::RectSelect);
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
        const doc::Cel &cel = *d.cel(controller.currentCel());
        const doc::DrawingId drawing = cel.timing().drawingAt(0);
        QVERIFY(controller.canClearCurrentLayer());

        QVERIFY(controller.clearCurrentLayer());
        QVERIFY(cel.drawing(drawing)->layers.front().image.isEmpty());
        QCOMPARE(cel.timing().drawingAt(0), drawing); // still exposed, just empty
        QVERIFY(!controller.canClearCurrentLayer());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo clear"));

        controller.undo();
        QVERIFY(!cel.drawing(drawing)->layers.front().image.isEmpty());
        controller.redo();
        QVERIFY(cel.drawing(drawing)->layers.front().image.isEmpty());

        QString reason;
        QVERIFY(!controller.clearCurrentLayer(&reason)); // nothing left
        QVERIFY(!reason.isEmpty());
    }

    void clearRespectsLockedAndHiddenLayers()
    {
        document::DocumentController controller;
        QImage red(10, 10, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("red"));
        const doc::CelId id = controller.currentCel();
        doc::CelProperties props = controller.document().cel(id)->properties();

        for (bool locked : {true, false}) {
            doc::CelProperties p = props;
            p.locked = locked;
            p.visible = locked; // first locked, then hidden
            controller.execute(doc::makeSetCelProperties(controller.document(), id, p));
            QString reason;
            QVERIFY(!controller.canClearCurrentLayer());
            QVERIFY(!controller.clearCurrentLayer(&reason));
            QVERIFY(reason.contains(locked ? QStringLiteral("locked") : QStringLiteral("hidden")));
        }
    }

    void clearOnAnEmptyFrameDoesNothing()
    {
        document::DocumentController controller; // one layer, no drawings
        QVERIFY(!controller.canClearCurrentLayer());
        QVERIFY(!controller.clearCurrentLayer());
        QVERIFY(!controller.canUndo());
    }

    // The layer tools work on is one of the drawing shown: going to another
    // drawing keeps the layer of the same name, else the top one; a drawing
    // made by painting on an empty frame names its layer after it.
    void currentLayerFollowsTheDrawings()
    {
        document::DocumentController controller;
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        auto drawingWith = [&](int frame, std::vector<std::string> names) {
            doc::NewDrawing nd = doc::makeNewDrawing(d, cel, frame, 1, names.front());
            const doc::DrawingId id = nd.id;
            controller.execute(std::move(nd.command));
            for (std::size_t i = 1; i < names.size(); ++i)
                controller.execute(std::make_unique<doc::AddLayerCommand>(d, cel, id, int(i),
                                                                          doc::LayerProperties{names[i]}));
            return d.cel(cel)->drawing(id);
        };
        const doc::Drawing *first = drawingWith(0, {"Line", "Color"});
        const doc::Drawing *second = drawingWith(1, {"Color", "Line", "Shadow"});
        QCOMPARE(controller.currentLayer(), first->layers[0].id); // the one it was made with, still there

        QSignalSpy changed(&controller, &document::DocumentController::currentLayerChanged);
        controller.setCurrentLayer(first->layers[1].id); // Color
        controller.setCurrentLayer(first->layers[0].id); // Line
        QCOMPARE(changed.count(), 2);
        controller.setCurrentFrame(1);
        QCOMPARE(controller.currentLayer(), second->layers[1].id); // Line again
        controller.setCurrentLayer(second->layers[2].id); // Shadow: not in the first drawing
        controller.setCurrentFrame(0);
        QCOMPARE(controller.currentLayer(), first->layers.back().id);
        controller.setCurrentFrame(5);
        QCOMPARE(controller.currentLayer(), doc::kNoId); // an empty frame
        QCOMPARE(controller.currentDrawing(), nullptr);

        const auto target = controller.beginPaint(true); // the last one worked on was Color
        QVERIFY(target);
        const doc::Drawing *made = controller.currentDrawing();
        QVERIFY(made);
        QCOMPARE(made->layers.size(), std::size_t(1));
        QCOMPARE(made->layers[0].properties.name, std::string("Color"));
        QCOMPARE(target->layer, made->layers[0].id);
        QCOMPARE(controller.currentLayer(), target->layer);
        controller.cancelPaint();
        QCOMPARE(controller.currentDrawing(), nullptr);

        // A locked layer is not painted.
        controller.setCurrentFrame(0);
        doc::LayerProperties p = first->layers.back().properties;
        p.locked = true;
        controller.execute(doc::makeSetLayerProperties(d, cel, first->id, first->layers.back().id, p));
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason));
        QVERIFY(reason.contains(QStringLiteral("locked")));
        QVERIFY(!controller.canClearCurrentLayer());
    }

    void importedImageGetsItsOwnLayer()
    {
        document::DocumentController controller; // 1920×1080, one layer
        const doc::Document &d = controller.document();
        QImage image(100, 50, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("photo"));

        QCOMPARE(d.celCount(), 2);
        const doc::Cel &cel = d.celAt(1); // above the first one
        QCOMPARE(cel.properties().name, std::string("photo"));
        QCOMPARE(controller.currentCel(), cel.id());
        const doc::DrawingId drawing = cel.timing().drawingAt(0);
        QVERIFY(drawing != doc::kNoId);
        QCOMPARE(cel.timing().drawingAt(d.frameCount() - 1), drawing); // held for the whole scene
        const doc::TiledImage &pixels = cel.drawing(drawing)->layers.front().image;
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xffff0000)); // centered
        QCOMPARE(pixels.pixel(909, 540), doc::Pixel(0));
        QCOMPARE(pixels.pixel(910, 515), doc::Pixel(0xffff0000));

        controller.undo(); // one step takes it all back
        QCOMPARE(d.celCount(), 1);
    }

    void moveToolMovesTheDrawingInOneStep()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("dot")); // 955..964, 535..544
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;

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
        QCOMPARE(controller.document().celCount(), 1);
        QVERIFY(messages.isEmpty());
    }

    void moveToolShiftKeepsOneAxisAndCancelRestores()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("dot"));
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;

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
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;

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
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;
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
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
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
        const doc::Cel &cel = controller.document().celAt(1);
        auto pixel = [&](int x, int y) { return cel.drawing(cel.timing().drawingAt(0))->layers.front().image.pixel(x, y); };
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

    // The panel shows each cel as a folder holding all its frames, and each
    // frame its layers; the one shown now is current, and on an empty point
    // no layer can be added. Choosing another frame goes to where it is shown.
    void layersPanelFollowsTheFrame()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 500);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *tree = panel.findChild<QTreeWidget *>();
        auto button = [&](const QString &tip) {
            for (QToolButton *b : panel.findChildren<QToolButton *>())
                if (b->toolTip() == tip)
                    return b;
            return static_cast<QToolButton *>(nullptr);
        };
        auto *add = button(QStringLiteral("New layer"));
        QVERIFY(add);
        const doc::CelId cel = controller.currentCel();
        QCOMPARE(tree->topLevelItemCount(), 2); // the cel and the paper
        QVERIFY(panel.celItem(cel));
        QCOMPARE(panel.celItem(cel)->childCount(), 0); // no frames yet
        QCOMPARE(tree->currentItem(), panel.celItem(cel));
        QVERIFY(!add->isEnabled());
        QVERIFY(panel.selectedLayers().isEmpty());

        doc::Document &d = controller.documentForCommands();
        doc::NewDrawing nd = doc::makeNewDrawing(d, cel, 0, 2, "Line");
        controller.execute(std::move(nd.command));
        QCoreApplication::processEvents(); // the queued rebuild
        QCOMPARE(panel.frameItem(nd.id)->childCount(), 1);
        QCOMPARE(tree->currentItem(), panel.layerItem(nd.layer));
        QVERIFY(add->isEnabled());
        add->click();
        QCoreApplication::processEvents();
        QCOMPARE(controller.currentDrawing()->layers.size(), std::size_t(2));
        QCOMPARE(controller.currentDrawing()->layers[1].properties.name, std::string("Layer 2"));
        QCOMPARE(panel.selectedLayers(), QList<doc::LayerId>({controller.currentLayer()}));

        controller.setCurrentFrame(1); // the same frame, held
        QCOMPARE(tree->currentItem(), panel.layerItem(controller.currentLayer()));
        controller.setCurrentFrame(2); // nothing there
        QCOMPARE(tree->currentItem(), panel.celItem(cel));
        QVERIFY(!add->isEnabled());

        // Every frame is listed, in timeline order; the one shown now in bold.
        doc::NewDrawing later = doc::makeNewDrawing(d, cel, 6, 1, "Line");
        controller.execute(std::move(later.command));
        QCoreApplication::processEvents();
        QCOMPARE(panel.celItem(cel)->childCount(), 2);
        QCOMPARE(panel.celItem(cel)->child(0), panel.frameItem(nd.id));
        QCOMPARE(panel.celItem(cel)->child(1), panel.frameItem(later.id));
        auto bold = [&](doc::DrawingId id) {
            QWidget *row = tree->itemWidget(panel.frameItem(id), 0);
            return row->findChild<QLabel *>(QStringLiteral("frameText"))->font().bold();
        };
        QVERIFY(!bold(nd.id) && !bold(later.id)); // at frame 3, nothing
        controller.setCurrentFrame(6);
        QVERIFY(!bold(nd.id) && bold(later.id));
        // A layer of a frame not shown here: to the first point it is shown at.
        tree->setCurrentItem(panel.layerItem(nd.layer));
        QCOMPARE(controller.currentFrame(), 0);
        QCOMPARE(controller.currentLayer(), nd.layer);
        QVERIFY(bold(nd.id) && !bold(later.id));

        // The frame's row: a new layer on top of it.
        tree->setCurrentItem(panel.frameItem(nd.id));
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Frame);
        controller.setCurrentLayer(controller.currentDrawing()->layers[0].id);
        button(QStringLiteral("New layer on top of the frame"))->click();
        QCoreApplication::processEvents();
        QCOMPARE(controller.currentDrawing()->layers.size(), std::size_t(3));
        QCOMPARE(controller.currentLayer(), controller.currentDrawing()->layers.back().id);
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Layer);

        // The cel's row: the header and the buttons act on the cel.
        tree->setCurrentItem(panel.celItem(cel));
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Cel);
        panel.findChild<QSpinBox *>()->setValue(30);
        QCOMPARE(d.cel(cel)->properties().opacity, 0.3f);
        QCOMPARE(controller.currentDrawing()->layers[0].properties.opacity, 1.0f);
        button(QStringLiteral("New cel"))->click();
        QCoreApplication::processEvents();
        QCOMPARE(d.celCount(), 2);
        const doc::CelId second = controller.currentCel();
        QCOMPARE(tree->currentItem(), panel.celItem(second));
        QCOMPARE(tree->indexOfTopLevelItem(panel.celItem(second)), 0); // the top one
        button(QStringLiteral("Move the cel down"))->click();
        QCoreApplication::processEvents();
        QCOMPARE(d.indexOf(second), 0);

        // A cel's row clicked makes it current; a layer's row of another cel too.
        tree->setCurrentItem(panel.layerItem(nd.layer));
        QCOMPARE(controller.currentCel(), cel);
        QCOMPARE(controller.currentLayer(), nd.layer);

        // Folders closed by hand stay closed through rebuilds.
        panel.celItem(cel)->setExpanded(false);
        controller.execute(doc::makeSetCelProperties(d, second, doc::CelProperties{"Renamed"}));
        QCoreApplication::processEvents();
        QVERIFY(!panel.celItem(cel)->isExpanded());
        QVERIFY(panel.celItem(second)->isExpanded());
    }

    // Ctrl+click adds a layer to the selection, Shift+click a range; the
    // header and the buttons then act on all of them, as one undo step.
    void layersSelectWithCtrlAndShift()
    {
        document::DocumentController controller;
        doc::Document &doc = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        doc::NewDrawing nd = doc::makeNewDrawing(doc, cel, 0, 1, "Layer 1");
        controller.execute(std::move(nd.command));
        for (int i = 2; i <= 5; ++i) // Layer 1..5, bottom to top
            controller.execute(std::make_unique<doc::AddLayerCommand>(
                doc, cel, nd.id, i - 1, doc::LayerProperties{QStringLiteral("Layer %1").arg(i).toStdString()}));
        ui::LayersPanel panel(&controller);
        panel.resize(300, 500);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QCoreApplication::processEvents(); // the queued rebuild
        auto *tree = panel.findChild<QTreeWidget *>();
        const doc::Drawing &d = *controller.currentDrawing();
        auto count = [&] { return int(d.layers.size()); };
        auto id = [&](int index) { return d.layers[std::size_t(index)].id; };
        auto click = [&](int index, Qt::KeyboardModifiers mods) { // by layer index; rows are top first
            const QRect r = tree->visualItemRect(panel.layerItem(id(index)));
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, mods, r.center());
        };

        click(1, {});
        QCOMPARE(controller.currentLayer(), id(1));
        QCOMPARE(panel.selectedLayers(), QList<doc::LayerId>({id(1)}));
        click(3, Qt::ControlModifier);
        QCOMPARE(controller.currentLayer(), id(3));
        QCOMPARE(panel.selectedLayers(), QList<doc::LayerId>({id(1), id(3)}));
        click(0, Qt::ShiftModifier); // from the anchor (3) down to 0
        QCOMPARE(panel.selectedLayers(), QList<doc::LayerId>({id(0), id(1), id(2), id(3)}));
        click(2, Qt::ControlModifier); // and 2 out again
        QCOMPARE(panel.selectedLayers(), QList<doc::LayerId>({id(0), id(1), id(3)}));

        // The drag preview: the three rows as they look, stacked.
        const QPixmap preview = panel.dragPreview();
        const int rowHeight = tree->visualItemRect(panel.layerItem(id(0))).height();
        QCOMPARE(preview.deviceIndependentSize().toSize().height(), 3 * rowHeight);
        QVERIFY(preview.toImage().pixelColor(preview.width() / 2, 2).alpha() < 255); // see-through

        panel.findChild<QSpinBox *>()->setValue(40); // opacity for all three
        for (int i : {0, 1, 3})
            QCOMPARE(d.layers[std::size_t(i)].properties.opacity, 0.4f);
        QCOMPARE(d.layers[2].properties.opacity, 1.0f);
        controller.undo();
        QCOMPARE(d.layers[0].properties.opacity, 1.0f);
        QCoreApplication::processEvents();

        const doc::LayerId a = id(0), b = id(1), c = id(3), e = id(4), mid = id(2);
        auto button = [&](const QString &tip) {
            for (QToolButton *b : panel.findChildren<QToolButton *>())
                if (b->toolTip() == tip)
                    return b;
            return static_cast<QToolButton *>(nullptr);
        };
        QTest::mouseClick(button(QStringLiteral("Move the selected layers up")), Qt::LeftButton);
        QCoreApplication::processEvents();
        QCOMPARE(d.indexOf(a), 1); // each one place up
        QCOMPARE(d.indexOf(b), 2);
        QCOMPARE(d.indexOf(c), 4);
        QCOMPARE(d.indexOf(mid), 0);
        QCOMPARE(d.indexOf(e), 3);
        QCOMPARE(panel.selectedLayers().size(), 3); // still selected after the rebuild

        // Dragged to the top (row 0): the three keep their order, above the rest.
        panel.moveSelectedToRow(0);
        QCoreApplication::processEvents();
        QCOMPARE(d.indexOf(c), 4);
        QCOMPARE(d.indexOf(b), 3);
        QCOMPARE(d.indexOf(a), 2);
        QCOMPARE(d.indexOf(e), 1);
        QCOMPARE(d.indexOf(mid), 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo move layer"));
        controller.undo(); // one step
        QCoreApplication::processEvents();
        QCOMPARE(d.indexOf(a), 1);
        QCOMPARE(d.indexOf(c), 4);
        panel.moveSelectedToRow(count()); // to the bottom
        QCoreApplication::processEvents();
        QCOMPARE(d.indexOf(a), 0);
        QCOMPARE(d.indexOf(b), 1);
        QCOMPARE(d.indexOf(c), 2);
        QCOMPARE(panel.selectedLayers().size(), 3);

        QTest::mouseClick(button(QStringLiteral("Delete the selected layers")), Qt::LeftButton);
        QCoreApplication::processEvents();
        QCOMPARE(count(), 2);
        controller.undo();
        QCOMPARE(count(), 5);
    }

    // The lasso fill sub tool: an outline drawn freehand, its inside filled
    // over what is there, within the selection, as one undo step.
    void lassoFillFillsTheOutline()
    {
        tools::ToolState state;
        document::DocumentController controller;
        const auto presets = tools::builtInPresets(tools::ToolId::Fill);
        QCOMPARE(presets.size(), 2);
        state.setOpacity(tools::ToolId::Fill, 0.5);
        tools::applyPreset(state, presets[1]);
        QCOMPARE(state.fillSettings().mode, tools::FillSettings::Mode::Lasso);
        QCOMPARE(state.settings(tools::ToolId::Fill).opacity, 0.5); // a fill sub tool keeps the rest
        state.setOpacity(tools::ToolId::Fill, 1.0);
        state.setColor(Qt::blue);

        canvas::FrameRenderer frame;
        canvas::FillTool tool(&controller, &state, [&]() -> const QImage & { return frame.image(); },
                              [](const QString &) {});
        tool.press({100, 100}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        tool.move({300, 100}, Qt::NoModifier);
        tool.move({300, 300}, Qt::NoModifier);
        tool.release({100, 300}, Qt::NoModifier); // a square, closed by the release
        QVERIFY(!tool.isActive());
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        QCOMPARE(pixel(200, 200), doc::Pixel(0xff0000ff));
        QCOMPARE(qAlpha(pixel(350, 200)), 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo fill"));

        QPainterPath left; // kept inside the selection
        left.addRect(400, 400, 50, 200);
        controller.setSelection(left);
        state.setColor(Qt::red);
        tool.press({420, 450}, Qt::NoModifier);
        tool.move({500, 450}, Qt::NoModifier);
        tool.release({500, 550}, Qt::NoModifier);
        QCOMPARE(pixel(440, 460), doc::Pixel(0xffff0000)); // in the triangle and the selection
        QCOMPARE(qAlpha(pixel(480, 460)), 0);               // in the triangle, out of the selection

        tool.press({600, 600}, Qt::NoModifier); // cancelled: nothing
        tool.move({700, 600}, Qt::NoModifier);
        tool.cancel();
        QVERIFY(!tool.isActive());
    }

    // Copy takes the selected pixels and where they were; paste puts them on
    // the current layer, in place, on this frame, selected: one undo step.
    void copyAndPasteSelectedPixels()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        QPainterPath left;
        left.addRect(900, 500, 60, 100); // the bar's left half
        controller.setSelection(left);

        QPoint origin;
        const QImage copied = controller.copyPixels(&origin);
        QCOMPARE(copied.size(), QSize(50, 10)); // cropped to what is drawn
        QCOMPARE(origin, QPoint(910, 535));

        doc::Document &d = controller.documentForCommands(); // another layer, another frame
        auto ink = std::make_unique<doc::AddCelCommand>(d, d.celCount(), doc::CelProperties{"ink"});
        const doc::CelId inkId = ink->celId();
        controller.execute(std::move(ink));
        controller.setCurrentCel(inkId);
        controller.setCurrentFrame(5);
        const int cels = d.celCount();
        QVERIFY(controller.pasteImage(copied, origin));
        QCOMPARE(d.celCount(), cels); // no new layer
        QCOMPARE(controller.currentCel(), inkId);
        const doc::Cel &target = *controller.document().cel(inkId);
        QVERIFY(target.drawingAt(5));
        QVERIFY(!target.drawingAt(4)); // on this frame only
        QCOMPARE(target.drawingAt(5)->layers.front().image.pixel(930, 540), doc::Pixel(0xff00ff00));
        QCOMPARE(qAlpha(target.drawingAt(5)->layers.front().image.pixel(990, 540)), 0);
        QCOMPARE(controller.selection().boundingRect(), QRectF(910, 535, 50, 10));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo paste"));
        controller.undo(); // all of it: the drawing it made, the selection
        QVERIFY(!target.drawingAt(5));
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        QVERIFY(controller.pasteImage(QImage(20, 20, QImage::Format_ARGB32_Premultiplied), std::nullopt));
        QCOMPARE(controller.selection().boundingRect(), QRectF(950, 530, 20, 20)); // no place: centered

        doc::CelProperties locked = target.properties(); // a layer that cannot take it
        locked.locked = true;
        controller.execute(doc::makeSetCelProperties(controller.document(), inkId, locked));
        QString reason;
        QVERIFY(!controller.pasteImage(copied, origin, &reason));
        QVERIFY(!reason.isEmpty());

        auto empty = std::make_unique<doc::AddCelCommand>(d, d.celCount(), doc::CelProperties{"empty"});
        const doc::CelId emptyId = empty->celId();
        controller.execute(std::move(empty));
        controller.setCurrentCel(emptyId); // nothing to copy there
        reason.clear();
        QVERIFY(controller.copyPixels(nullptr, &reason).isNull());
        QVERIFY(!reason.isEmpty());
    }

    // Ctrl+C / Ctrl+X / Ctrl+V go through the system clipboard.
    void clipboardCopyCutPaste()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller->importImage(image, QStringLiteral("bar"));
        const doc::CelId bar = controller->currentCel();
        QPainterPath left;
        left.addRect(900, 500, 60, 100);
        controller->setSelection(left);
        auto action = [&](QKeySequence::StandardKey key) {
            for (QAction *a : window.findChildren<QAction *>())
                if (a->shortcuts().contains(QKeySequence::keyBindings(key).value(0)))
                    return a;
            return static_cast<QAction *>(nullptr);
        };
        QVERIFY(action(QKeySequence::Copy) && action(QKeySequence::Cut) && action(QKeySequence::Paste));

        action(QKeySequence::Cut)->trigger();
        const doc::Cel &source = *controller->document().cel(bar);
        QCOMPARE(qAlpha(source.drawingAt(0)->layers.front().image.pixel(930, 540)), 0); // cut
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage()); // for other apps too
        action(QKeySequence::Paste)->trigger(); // back onto the same layer, in place
        QCOMPARE(controller->currentCel(), bar);
        QCOMPARE(source.drawingAt(0)->layers.front().image.pixel(930, 540), doc::Pixel(0xff00ff00));

        QImage other(10, 10, QImage::Format_ARGB32); // an image from another app: centered
        other.fill(Qt::blue);
        QGuiApplication::clipboard()->setImage(other);
        action(QKeySequence::Paste)->trigger();
        QCOMPARE(source.drawingAt(0)->layers.front().image.pixel(960, 540), doc::Pixel(0xff0000ff));
    }

    // Autoplay plays the frames in a loop, the slider following; Play stops
    // it, the slider picks a frame, the speed sets the pace.
    void autoplayLoopsTheAnimation()
    {
        document::DocumentController controller;
        const QColor colors[] = {Qt::red, Qt::green, Qt::blue}; // a three-frame animation
        for (int f = 0; f < 3; ++f) {
            QImage image(40, 40, QImage::Format_ARGB32);
            image.fill(colors[f]);
            controller.setCurrentFrame(f);
            QVERIFY(controller.pasteImage(image, QPoint(940, 520)));
        }
        controller.clearSelection();

        ui::AutoplayWindow window(&controller);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *play = window.findChild<QCheckBox *>();
        auto *slider = window.findChild<QSlider *>();
        auto *speed = window.findChild<QComboBox *>();
        QVERIFY(play && slider && speed);
        QVERIFY(play->isChecked());
        QVERIFY(window.isPlaying());
        QCOMPARE(window.frameCount(), 3); // to the last frame with something on it
        QCOMPARE(window.fps(), 24.0);     // the document's rate
        QCOMPARE(slider->maximum(), 2);

        const int start = window.frame();
        QTRY_VERIFY(window.frame() != start);
        QTRY_COMPARE(window.frame(), 0); // and round again
        QCOMPARE(slider->value(), window.frame());

        play->setChecked(false);
        QVERIFY(!window.isPlaying());
        slider->setValue(1); // a frame picked
        QCOMPARE(window.frame(), 1);
        QCOMPARE(window.image().pixelColor(960, 540), QColor(Qt::green));
        slider->setValue(2);
        QCOMPARE(window.image().pixelColor(960, 540), QColor(Qt::blue));
        QTest::qWait(100);
        QCOMPARE(window.frame(), 2); // stopped

        speed->setCurrentIndex(speed->findData(6.0));
        QCOMPARE(window.fps(), 6.0);
        QTest::keyClick(&window, Qt::Key_Space); // Space plays too
        QVERIFY(play->isChecked() && window.isPlaying());
        QElapsedTimer timer;
        timer.start();
        QTRY_COMPARE(window.frame(), 0); // the next frame, at 6 fps
        QVERIFY(timer.elapsed() > 120);

        QImage more(40, 40, QImage::Format_ARGB32); // a frame drawn while it plays: in the loop
        more.fill(Qt::yellow);
        controller.setCurrentFrame(3);
        QVERIFY(controller.pasteImage(more, QPoint(940, 520)));
        QTRY_COMPARE(window.frameCount(), 4);
    }

    // , and . step the timeline one frame back and on, from the canvas or a
    // panel; + and - change the current frame's hold, but in the puppet's
    // radius mode they stay the radius's on the canvas.
    void commaAndPeriodStepTheFrames()
    {
        ui::MainWindow window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *state = window.findChild<tools::ToolState *>();
        canvas->setFocus();
        QCOMPARE(controller->currentFrame(), 0);
        QTest::keyClick(canvas, Qt::Key_Period);
        QTest::keyClick(canvas, Qt::Key_Period);
        QCOMPARE(controller->currentFrame(), 2);
        QTest::keyClick(canvas, Qt::Key_Comma);
        QCOMPARE(controller->currentFrame(), 1);
        QTest::keyClick(canvas, Qt::Key_Comma);
        QTest::keyClick(canvas, Qt::Key_Comma); // stops at the start
        QCOMPARE(controller->currentFrame(), 0);
        auto *layers = window.findChild<ui::LayersPanel *>()->findChild<QTreeWidget *>();
        layers->setFocus(); // from a panel too
        QTest::keyClick(layers, Qt::Key_Period);
        QCOMPARE(controller->currentFrame(), 1);
        controller->setCurrentFrame(controller->document().frameCount() - 1);
        QTest::keyClick(layers, Qt::Key_Period); // and at the end
        QCOMPARE(controller->currentFrame(), controller->document().frameCount() - 1);

        controller->setCurrentFrame(3);
        tools::PuppetSettings settings = state->puppetSettings();
        settings.local = true;
        state->setPuppetSettings(settings);
        state->setActiveTool(tools::ToolId::Puppet);
        canvas->setFocus();
        const double radius = state->puppetSettings().radius;
        QTest::keyClick(canvas, Qt::Key_Minus);
        QVERIFY(state->puppetSettings().radius < radius); // the radius
        QCOMPARE(controller->currentFrame(), 3);          // not the frame
        QTest::keyClick(canvas, Qt::Key_Comma);
        QCOMPARE(controller->currentFrame(), 2); // , still steps

        // + and - lengthen and shorten the frame shown at the current point,
        // the ones after it moving along; the point stays on that frame.
        state->setActiveTool(tools::ToolId::Brush);
        doc::Document &d = controller->documentForCommands();
        const doc::CelId cel = controller->currentCel();
        doc::NewDrawing a = doc::makeNewDrawing(d, cel, 2, 2, "Line");
        controller->execute(std::move(a.command));
        doc::NewDrawing b = doc::makeNewDrawing(d, cel, 4, 1, "Line");
        controller->execute(std::move(b.command));
        controller->setCurrentFrame(3);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Plus);
        const doc::Timing &t = d.cel(cel)->timing();
        QCOMPARE(t.exposureAt(2)->length, 3);
        QCOMPARE(t.exposureAt(5)->drawing, b.id); // moved along
        QTest::keyClick(canvas, Qt::Key_Minus);
        QTest::keyClick(canvas, Qt::Key_Minus);
        QCOMPARE(t.exposureAt(2)->length, 1);
        QCOMPARE(t.exposureAt(3)->drawing, b.id);
        QCOMPARE(controller->currentFrame(), 2); // still on the shortened frame
        QCOMPARE(controller->undoText(), QStringLiteral("Undo timing change"));
        QTest::keyClick(canvas, Qt::Key_Minus); // one frame long already
        QCOMPARE(t.exposureAt(2)->length, 1);
        controller->setCurrentFrame(20); // nothing there
        QTest::keyClick(canvas, Qt::Key_Plus);
        QCOMPARE(t.lastFrame(), 4);
    }

    // Split frame cuts a hold in two independent frames; insert inbetween
    // moves the cel's frames from a point one place on and puts a new one there.
    void splitFrameAndInsertInbetween()
    {
        document::DocumentController controller;
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        doc::NewDrawing a = doc::makeNewDrawing(d, cel, 0, 4, "Line");
        controller.execute(std::move(a.command));
        d.layer(cel, a.id, a.layer)->image.setPixel(3, 3, 0xff000000);
        const doc::Timing &t = d.cel(cel)->timing();

        QVERIFY(!doc::makeSplitDrawing(d, cel, 0).command); // the start of the hold
        QVERIFY(!doc::makeSplitDrawing(d, cel, 6).command); // nothing there
        doc::NewDrawing split = doc::makeSplitDrawing(d, cel, 2);
        controller.execute(std::move(split.command));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo split frame"));
        QCOMPARE(t.exposureAt(0)->length, 2);
        QCOMPARE(t.exposureAt(0)->drawing, a.id);
        QCOMPARE(t.exposureAt(2)->drawing, split.id);
        QCOMPARE(t.exposureAt(2)->length, 2);
        const doc::Drawing *copy = d.cel(cel)->drawing(split.id);
        QCOMPARE(copy->name, std::string("2"));
        QCOMPARE(copy->layers.size(), std::size_t(1));
        QVERIFY(copy->layers[0].id != a.layer);
        QCOMPARE(copy->layers[0].image.pixel(3, 3), doc::Pixel(0xff000000));
        d.layer(cel, split.id, split.layer)->image.setPixel(3, 3, 0); // independent: one painted, not the other
        QCOMPARE(d.layer(cel, a.id, a.layer)->image.pixel(3, 3), doc::Pixel(0xff000000));

        doc::NewDrawing between = doc::makeInbetween(d, cel, 1, "Line");
        controller.execute(std::move(between.command));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo insert inbetween"));
        QCOMPARE(t.drawingAt(0), a.id);
        QCOMPARE(t.drawingAt(1), between.id);
        QCOMPARE(t.drawingAt(2), a.id); // the rest of the hold, moved on
        QCOMPARE(t.drawingAt(3), split.id);
        QCOMPARE(t.lastFrame(), 5);
        QCOMPARE(d.cel(cel)->drawing(between.id)->layers.size(), std::size_t(1));
        controller.undo();
        QCOMPARE(t.lastFrame(), 4);
        QCOMPARE(t.drawingAt(1), a.id);
    }

    // The onion skin shows the current layer's drawings of the frames around
    // this one, once each, tinted: the ones before in one color, after in another.
    void onionSkinShowsTheFramesAround()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage square(60, 60, QImage::Format_ARGB32);
        square.fill(Qt::black);
        for (int f : {0, 1, 2, 4}) { // drawings on 0, 1, 2 and 4, each further right
            controller.setCurrentFrame(f);
            QVERIFY(controller.pasteImage(square, QPoint(400 + f * 200, 500)));
        }
        controller.clearSelection();
        controller.setCurrentFrame(2);
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(800, 450);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QVERIFY(canvas.onionSkinFrames().isEmpty()); // off to begin with

        tools::OnionSkinSettings onion = state.onionSkin();
        onion.enabled = true;
        onion.opacity = 1.0;
        state.setOnionSkin(onion);
        QCOMPARE(canvas.onionSkinFrames(), QList<int>({-1, -2, 2})); // frame 3 is empty
        onion.before = 1;
        onion.after = 0;
        state.setOnionSkin(onion);
        QCOMPARE(canvas.onionSkinFrames(), QList<int>({-1}));

        // What the canvas shows where frame 1's square is (and nothing of frame 2 is).
        const QPoint ghost = canvas.view().documentToView().map(QPointF(630, 530)).toPoint();
        const QColor tinted = canvas.grab().toImage().pixelColor(ghost);
        QVERIFY(tinted.red() > 150 && tinted.blue() < 120); // the "before" red
        onion.enabled = false;
        state.setOnionSkin(onion);
        QCOMPARE(canvas.grab().toImage().pixelColor(ghost), QColor(Qt::white)); // just the paper
    }

    // The timeline's button turns it on; its settings edit the rest.
    void timelineOnionSkinControls()
    {
        tools::ToolState state;
        document::DocumentController controller;
        ui::TimelinePanel panel(&controller, &state);
        auto *button = panel.findChild<QToolButton *>(QStringLiteral("onionSkin"));
        QVERIFY(button && button->isCheckable() && button->menu());
        button->click();
        QVERIFY(state.onionSkin().enabled);
        auto *before = button->menu()->findChild<QSpinBox *>(QStringLiteral("onionBefore"));
        auto *after = button->menu()->findChild<QSpinBox *>(QStringLiteral("onionAfter"));
        QVERIFY(before && after);
        before->setValue(4);
        after->setValue(1);
        QCOMPARE(state.onionSkin().before, 4);
        QCOMPARE(state.onionSkin().after, 1);
        tools::OnionSkinSettings s = state.onionSkin(); // and back from the state
        s.enabled = false;
        s.after = 3;
        state.setOnionSkin(s);
        QVERIFY(!button->isChecked());
        QCOMPARE(after->value(), 3);
        QVERIFY(button->menu()->findChild<QWidget *>(QStringLiteral("onionOpacity")));
        QVERIFY(button->menu()->findChild<QToolButton *>(QStringLiteral("onionBeforeColor")));
    }

    // The cel bar adds, moves and deletes cels and sets the current one's
    // blend and opacity; the row headers show, hide and lock them.
    void timelineCelControls()
    {
        tools::ToolState state;
        document::DocumentController controller;
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(800, 300);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        const doc::Document &d = controller.document();
        const doc::CelId first = controller.currentCel();
        auto *add = panel.findChild<QToolButton *>(QStringLiteral("celAdd"));
        auto *remove = panel.findChild<QToolButton *>(QStringLiteral("celRemove"));
        auto *down = panel.findChild<QToolButton *>(QStringLiteral("celDown"));
        auto *blend = panel.findChild<QComboBox *>(QStringLiteral("celBlend"));
        auto *opacity = panel.findChild<QSpinBox *>(QStringLiteral("celOpacityValue"));
        QVERIFY(add && remove && down && blend && opacity);
        QVERIFY(!remove->isEnabled()); // the only cel

        add->click();
        QCOMPARE(d.celCount(), 2);
        const doc::CelId second = controller.currentCel();
        QCOMPARE(d.indexOf(second), 1);
        QCOMPARE(d.cel(second)->properties().name, std::string("Cel 2"));
        QVERIFY(remove->isEnabled());

        blend->setCurrentIndex(ui::blendModeIndex(doc::BlendMode::Multiply));
        emit blend->activated(blend->currentIndex());
        QCOMPARE(d.cel(second)->properties().blend, doc::BlendMode::Multiply);
        opacity->setValue(40);
        QCOMPARE(d.cel(second)->properties().opacity, 0.4f);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo cel properties"));
        controller.setCurrentCel(first);
        QCOMPARE(blend->currentIndex(), ui::blendModeIndex(doc::BlendMode::Normal)); // follows the current cel
        QCOMPARE(opacity->value(), 100);

        controller.setCurrentCel(second);
        down->click();
        QCOMPARE(d.indexOf(second), 0);
        QVERIFY(!down->isEnabled());

        // The header of the top row (now the first cel): eye, then lock.
        ui::TimelineMetrics m;
        auto *view = panel.view()->viewport();
        const int y = m.rowY(0) + m.rowHeight / 2;
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(20, y));
        QVERIFY(!d.cel(first)->properties().visible);
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(38, y));
        QVERIFY(d.cel(first)->properties().locked);
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(90, y)); // the name: made current
        QCOMPARE(controller.currentCel(), first);

        remove->click();
        QCOMPARE(d.celCount(), 1);
        QVERIFY(!remove->isEnabled());
        controller.undo();
        QCOMPARE(d.celCount(), 2);
    }

    void animationMenuOpensAutoplay()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QAction *autoplay = nullptr;
        for (QAction *menu : window.menuBar()->actions())
            if (menu->text().remove(QLatin1Char('&')) == QStringLiteral("Animation"))
                for (QAction *a : menu->menu()->actions())
                    if (a->text() == QStringLiteral("Autoplay"))
                        autoplay = a;
        QVERIFY(autoplay);
        autoplay->trigger();
        auto *player = window.findChild<ui::AutoplayWindow *>();
        QVERIFY(player);
        QTRY_VERIFY(player->isVisible());
        QVERIFY(player->isPlaying());
        autoplay->trigger(); // the same window again
        QCOMPARE(window.findChildren<ui::AutoplayWindow *>().size(), 1);
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
        const doc::CelId bar = controller.currentCel();
        auto pixel = [&](doc::CelId id, int x, int y) {
            const doc::Cel &cel = *controller.document().cel(id);
            const doc::Drawing *drawing = cel.drawingAt(0);
            return drawing ? drawing->layers.front().image.pixel(x, y) : doc::Pixel(0);
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

        // Cut and copy go to a new layer above, in the same drawing.
        const doc::Drawing &drawing = *controller.document().cel(bar)->drawingAt(0);
        const doc::LayerId source = controller.currentLayer();
        auto layerPixel = [&](doc::LayerId id, int x, int y) {
            const doc::Layer *layer = drawing.layer(id);
            return layer ? layer->image.pixel(x, y) : doc::Pixel(0);
        };
        const int cels = controller.document().celCount();
        QVERIFY(controller.copySelectionToLayer());
        QCOMPARE(controller.document().celCount(), cels);
        QCOMPARE(drawing.layers.size(), std::size_t(2));
        const doc::LayerId copy = controller.currentLayer();
        QCOMPARE(controller.currentCel(), bar);
        QCOMPARE(drawing.indexOf(copy), drawing.indexOf(source) + 1);
        QCOMPARE(drawing.layer(copy)->properties.name, std::string("Layer 1 (copy)"));
        QCOMPARE(layerPixel(copy, 930, 540), green);
        QCOMPARE(qAlpha(layerPixel(copy, 990, 540)), 0);
        QCOMPARE(layerPixel(source, 930, 540), green); // the source keeps it
        controller.undo();
        QCOMPARE(drawing.layers.size(), std::size_t(1));
        QCOMPARE(controller.currentLayer(), source);

        QVERIFY(controller.cutSelectionToLayer());
        const doc::LayerId cut = controller.currentLayer();
        QCOMPARE(layerPixel(cut, 930, 540), green);
        QCOMPARE(qAlpha(layerPixel(source, 930, 540)), 0); // the source loses it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo cut to layer"));
        controller.undo();
        QCOMPARE(drawing.layers.size(), std::size_t(1));
        QCOMPARE(layerPixel(source, 930, 540), green);

        QPainterPath empty; // nothing under the selection
        empty.addRect(100, 100, 50, 50);
        controller.setSelection(empty);
        QVERIFY(!controller.cutSelectionToLayer(&reason));
        QCOMPARE(drawing.layers.size(), std::size_t(1));
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

    // Radius mode: no pins, a drag bends only what lies around the pen; the
    // wheel over the canvas sizes the radius instead of zooming.
    void puppetRadiusModeBendsAroundThePen()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Cel &cel = controller.document().celAt(1);
        auto pixel = [&](int x, int y) { return cel.drawing(cel.timing().drawingAt(0))->layers.front().image.pixel(x, y); };
        const doc::Pixel ink = pixel(960, 540);
        tools::PuppetSettings settings = state.puppetSettings();
        settings.local = true;
        settings.radius = 60;
        settings.density = 2;
        state.setPuppetSettings(settings);

        canvas::PuppetTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.press({1130, 540}, Qt::NoModifier); // near the right end
        QVERIFY(tool.isActive());
        tool.move({1130, 600}, Qt::NoModifier);
        tool.release({1130, 640}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(pixel(1130, 640), ink);       // the end went down
        QCOMPARE(qAlpha(pixel(1130, 540)), 0); // from where it was
        QCOMPARE(pixel(800, 540), ink);        // far from it nothing moved
        QCOMPARE(pixel(1000, 540), ink);
        QVERIFY(tool.keyPress(Qt::Key_Return));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo stroke"));
        controller.undo();
        QCOMPARE(pixel(1130, 540), ink);

        canvas::CanvasView canvas(&state, &controller); // the wheel: radius, not zoom
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Puppet);
        const double zoom = canvas.view().zoom();
        QWheelEvent up(QPointF(200, 150), canvas.mapToGlobal(QPointF(200, 150)), {}, QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&canvas, &up);
        QVERIFY(state.puppetSettings().radius > 60);
        QCOMPARE(canvas.view().zoom(), zoom);
        QWheelEvent ctrlUp(QPointF(200, 150), canvas.mapToGlobal(QPointF(200, 150)), {}, QPoint(0, 120),
                           Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&canvas, &ctrlUp);
        QVERIFY(canvas.view().zoom() > zoom); // Ctrl+wheel still zooms

        // + and − too, held keys included.
        canvas.setFocus();
        double radius = state.puppetSettings().radius;
        QTest::keyClick(&canvas, Qt::Key_Plus);
        QVERIFY(state.puppetSettings().radius > radius);
        radius = state.puppetSettings().radius;
        QTest::keyClick(&canvas, Qt::Key_Minus);
        QTest::keyPress(&canvas, Qt::Key_Minus, Qt::NoModifier); // a repeat
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Minus, Qt::NoModifier, QStringLiteral("-"), true);
        QApplication::sendEvent(&canvas, &repeat);
        QTest::keyRelease(&canvas, Qt::Key_Minus);
        QVERIFY(state.puppetSettings().radius < radius / 1.3);
    }

    // Undo inside an open warp takes back one drag (or pin) at a time; back
    // at the start the drawing is as it was, and the warp is still open.
    void puppetUndoGoesThroughItsSteps()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;
        const doc::TiledImage original = pixels;
        auto snapshot = [&] {
            std::vector<doc::Pixel> out(500 * 300);
            pixels.read(710, 400, 500, 300, out.data(), 500);
            return out;
        };
        canvas::PuppetTool tool(&controller, &state, [](const QString &) {}, [] {});
        QVERIFY(!tool.canUndoStep());

        tool.press({780, 540}, Qt::NoModifier); // 1: an anchor pin
        tool.release({780, 540}, Qt::NoModifier);
        const auto afterPin = snapshot();
        tool.press({1140, 540}, Qt::NoModifier); // 2: a handle, dragged down
        tool.move({1140, 600}, Qt::NoModifier);
        tool.release({1140, 600}, Qt::NoModifier);
        const auto afterFirstDrag = snapshot();
        tool.press({1140, 600}, Qt::NoModifier); // 3: dragged further
        tool.move({1140, 660}, Qt::NoModifier);
        tool.release({1140, 660}, Qt::NoModifier);
        QVERIFY(snapshot() != afterFirstDrag);
        QCOMPARE(tool.pinCount(), std::size_t(2));

        QVERIFY(tool.undoStep());
        QVERIFY(snapshot() == afterFirstDrag);
        QVERIFY(tool.undoStep());
        QCOMPARE(tool.pinCount(), std::size_t(1)); // the handle pin is gone too
        QVERIFY(snapshot() == afterPin);
        QVERIFY(tool.undoStep());
        QCOMPARE(tool.pinCount(), std::size_t(0));
        QVERIFY(!tool.canUndoStep());
        QVERIFY(pixels.tileCoords() == original.tileCoords()); // exactly as it was
        QVERIFY(snapshot() == afterPin);                       // (the pin changed no pixel)
        QVERIFY(tool.hasPendingEdit());                        // still open

        QVERIFY(tool.redoStep());
        QVERIFY(tool.redoStep());
        QVERIFY(snapshot() == afterFirstDrag);
        tool.press({1140, 600}, Qt::NoModifier); // a new drag drops what was undone
        tool.move({1100, 620}, Qt::NoModifier);
        tool.release({1100, 620}, Qt::NoModifier);
        QVERIFY(!tool.canRedoStep());
        tool.commit();
        QCOMPARE(controller.undoText(), QStringLiteral("Undo stroke")); // the warp, once
        controller.undo();
        QCOMPARE(controller.undoText(), QStringLiteral("Undo import image"));

        tools::PuppetSettings settings = state.puppetSettings(); // radius mode: each drag a step
        settings.local = true;
        state.setPuppetSettings(settings);
        tool.press({1130, 540}, Qt::NoModifier);
        tool.move({1130, 600}, Qt::NoModifier);
        tool.release({1130, 600}, Qt::NoModifier);
        tool.press({800, 540}, Qt::NoModifier);
        tool.move({800, 480}, Qt::NoModifier);
        tool.release({800, 480}, Qt::NoModifier);
        QVERIFY(qAlpha(pixels.pixel(800, 480)) != 0); // the left end lifted
        QVERIFY(tool.undoStep());
        QCOMPARE(qAlpha(pixels.pixel(800, 480)), 0);   // back down
        QVERIFY(qAlpha(pixels.pixel(1130, 540)) == 0); // the first drag stays
        QVERIFY(tool.undoStep());
        QVERIFY(qAlpha(pixels.pixel(1130, 540)) != 0);
        QVERIFY(!tool.undoStep());
    }

    // The same for a transform; and Ctrl+Z in the window goes there first.
    void transformUndoGoesThroughItsDrags()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        state->setActiveTool(tools::ToolId::Transform);
        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, at(from));
            QTest::mouseMove(canvas, at(to));
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(to));
        };
        drag({960, 540}, {1060, 540});
        drag({1060, 540}, {1060, 640});
        QAction *undo = nullptr;
        for (QAction *a : window.findChildren<QAction *>())
            if (a->shortcuts().contains(QKeySequence::keyBindings(QKeySequence::Undo).value(0)))
                undo = a;
        QVERIFY(undo);
        QCOMPARE(undo->text(), QStringLiteral("Undo step"));
        undo->trigger();
        QCOMPARE(state->activeTool(), tools::ToolId::Transform); // still transforming
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        const doc::TiledImage &pixels = cel.drawingAt(0)->layers.front().image;
        QCOMPARE(pixels.pixel(1060, 540), doc::Pixel(0xffff0000)); // back after the first drag
        QCOMPARE(qAlpha(pixels.pixel(1060, 640)), 0);
        undo->trigger();
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xffff0000)); // as it was
        QCOMPARE(undo->text(), QStringLiteral("Undo import image"));
    }

    // Shift+click selects several pins, dragging one moves them all, Delete
    // removes them; in radius mode Shift+click picks several points.
    void puppetMovesSeveralPinsOrPoints()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Cel &cel = controller.document().celAt(1);
        auto pixel = [&](int x, int y) { return cel.drawing(cel.timing().drawingAt(0))->layers.front().image.pixel(x, y); };
        const doc::Pixel ink = pixel(960, 540);
        canvas::PuppetTool tool(&controller, &state, [](const QString &) {}, [] {});
        auto click = [&](QPointF at, Qt::KeyboardModifiers mods) {
            tool.press(at, mods);
            tool.release(at, mods);
        };

        click({960, 540}, {});                 // the middle: an anchor
        click({780, 540}, {});                 // the left end
        click({1140, 540}, Qt::ShiftModifier); // and the right one, both selected
        QCOMPARE(tool.pinCount(), std::size_t(3));
        QCOMPARE(tool.selectedPins(), std::vector<int>({1, 2}));
        click({960, 540}, Qt::ShiftModifier); // the anchor in, and out again
        click({960, 540}, Qt::ShiftModifier);
        QCOMPARE(tool.selectedPins(), std::vector<int>({1, 2}));

        tool.press({780, 540}, {}); // dragging one of them moves both
        tool.move({780, 580}, {});
        tool.release({780, 600}, {});
        QCOMPARE(pixel(780, 600), ink);
        QCOMPARE(pixel(1140, 600), ink);
        QCOMPARE(qAlpha(pixel(780, 540)), 0);
        QCOMPARE(pixel(960, 540), ink); // the anchor held
        QCOMPARE(tool.selectedPins().size(), std::size_t(2));

        QVERIFY(tool.keyPress(Qt::Key_Delete));
        QCOMPARE(tool.pinCount(), std::size_t(1));
        QVERIFY(tool.selectedPins().empty());
        tool.cancel();

        tools::PuppetSettings settings = state.puppetSettings();
        settings.local = true;
        settings.radius = 40;
        settings.density = 2;
        state.setPuppetSettings(settings);
        click({790, 540}, Qt::ShiftModifier); // two points picked
        click({1130, 540}, Qt::ShiftModifier);
        QCOMPARE(tool.pickedPointCount(), std::size_t(2));
        click({1130, 540}, Qt::ShiftModifier); // out, and in again
        QCOMPARE(tool.pickedPointCount(), std::size_t(1));
        click({1130, 540}, Qt::ShiftModifier);
        QCOMPARE(tool.pickedPointCount(), std::size_t(2));
        tool.press({1130, 540}, {}); // from one of them: both go
        tool.move({1130, 580}, {});
        tool.release({1130, 600}, {});
        QCOMPARE(pixel(1130, 600), ink);
        QCOMPARE(pixel(790, 600), ink);
        QCOMPARE(pixel(960, 540), ink); // the middle, out of both discs
        QCOMPARE(qAlpha(pixel(960, 600)), 0);
        QCOMPARE(tool.pickedPointCount(), std::size_t(2)); // still picked

        tool.press({960, 540}, {}); // elsewhere: just that point, the picks dropped
        QCOMPARE(tool.pickedPointCount(), std::size_t(0));
        tool.release({960, 540}, {});
    }

    // + during a radius drag widens what bends, at once.
    void puppetRadiusChangesWhileDragging()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Cel &cel = controller.document().celAt(1);
        auto pixel = [&](int x, int y) { return cel.drawing(cel.timing().drawingAt(0))->layers.front().image.pixel(x, y); };
        tools::PuppetSettings settings = state.puppetSettings();
        settings.local = true;
        settings.radius = 30;
        settings.density = 2;
        state.setPuppetSettings(settings);
        canvas::PuppetTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.press({1130, 540}, {});
        tool.move({1130, 600}, {});
        QVERIFY(qAlpha(pixel(1060, 525)) != 0); // 70 px away: untouched so far
        for (int i = 0; i < 8; ++i) // 30 × 1.15⁸ ≈ 92 px
            QVERIFY(tool.keyPress(Qt::Key_Plus));
        QVERIFY(tool.isActive()); // still dragging
        QCOMPARE(qAlpha(pixel(1060, 521)), 0); // now pulled down with the rest
        tool.release({1130, 600}, {});
    }

    // In radius mode the mesh is there as soon as the tool is chosen, so the
    // point a drag would take shows before the first drag.
    void puppetRadiusModeMeshesAtOnce()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar"));
        tools::PuppetSettings settings = state.puppetSettings();
        settings.local = true;
        state.setPuppetSettings(settings);
        canvas::PuppetTool tool(&controller, &state, [](const QString &) {}, [] {});
        QVERIFY(tool.hover({960, 540}));
        QVERIFY(tool.hasPendingEdit()); // meshed, nothing changed yet
        tool.commit();
        QCOMPARE(controller.undoText(), QStringLiteral("Undo import image")); // no step of its own

        settings.local = false; // pins: nothing until a click
        state.setPuppetSettings(settings);
        tool.activated();
        QVERIFY(!tool.hasPendingEdit());
    }

    void puppetWarpBendsTheDrawingInOneStep()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;
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
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;
        // 3840×1080 → 1920×540, centered vertically.
        QCOMPARE(qAlpha(pixels.pixel(0, 540)), 255);
        QCOMPARE(qAlpha(pixels.pixel(0, 260)), 0);
        QCOMPARE(qAlpha(pixels.pixel(1919, 280)), 255);
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
