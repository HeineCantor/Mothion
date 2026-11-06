// Tests for the UI layer: panel layouts, document controller actions.

#include "canvas/CanvasView.h"
#include "canvas/EffectBrushTools.h"
#include "canvas/FillTool.h"
#include "canvas/FrameRenderer.h"
#include "canvas/MoveTool.h"
#include "canvas/PerspectiveGizmo.h"
#include "canvas/PerspectiveSnap.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionTool.h"
#include "canvas/ShapeTool.h"
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
#include "ui/DiscordPresence.h"
#include "ui/MainWindow.h"
#include "ui/PanelDock.h"
#include "ui/SelectionBar.h"
#include "ui/PuppetBar.h"
#include "input/TabletInputAdapter.h"
#include "tools/Preferences.h"
#include "ui/ImportSequenceDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/ShapeBar.h"
#include "ui/Shortcuts.h"
#include "ui/TransformBar.h"
#include "ui/panels/BlendModes.h"
#include "ui/panels/BrushSizePanel.h"
#include "ui/panels/ColorWheelPanel.h"
#include "ui/panels/LayersPanel.h"
#include "ui/AdvancedToolSettings.h"
#include "ui/SplashScreen.h"
#include "ui/panels/ToolPalettePanel.h"
#include "ui/panels/SubToolPanel.h"
#include "ui/panels/ToolPropertiesPanel.h"
#include "ui/panels/timeline/TimelinePanel.h"
#include "ui/panels/timeline/TimelineView.h"

#include <numbers>
#include <set>
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
#include <QFileDialog>
#include <QDialogButtonBox>
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
#include <QSettings>
#include <QMenuBar>
#include <QPushButton>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <QDateTime>
#include <QSlider>
#include <QTest>
#include <QWheelEvent>
#include <QToolButton>

using namespace mothion;

class TestUi : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // keep the user's brushes out
        // And the user's settings (recent projects...): a folder of their own.
        QCoreApplication::setOrganizationName(QStringLiteral("MothionTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MothionTest"));
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    // Opening a project swaps the whole document under every panel and the
    // canvas (another size, other layers); the window then shows its name.
    void mainWindowOpensAProject()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shot 12.moth"));
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

    // Dragging the opacity slider shows the opacity on the canvas as it goes (for a layer and for a cel),
    // and the drag is one undo step.
    void opacitySliderPreviewsWhileDragged()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *panel = window.findChild<ui::LayersPanel *>();
        QVERIFY(panel);
        QSlider *slider = nullptr;
        for (QSlider *s : panel->findChildren<QSlider *>())
            if (s->orientation() == Qt::Horizontal && s->maximum() == 100)
                slider = s;
        QVERIFY(slider);
        const doc::Document &d = controller->document();
        controller->importImage(QImage(10, 10, QImage::Format_ARGB32), QStringLiteral("x"));
        const doc::CelId cel = controller->currentCel();
        auto layerOpacity = [&] {
            return d.cel(cel)->drawingAt(controller->currentFrame())->layers.front().properties.opacity;
        };
        const float before = layerOpacity();
        const bool couldUndo = controller->canUndo();
        const QString undoBefore = controller->undoText();

        slider->setSliderDown(true); // the handle grabbed
        slider->setSliderPosition(30); // moved with the button down
        QCOMPARE(layerOpacity(), 0.30f); // seen at once, not on release
        slider->setSliderPosition(60);
        QCOMPARE(layerOpacity(), 0.60f);
        QCOMPARE(controller->undoText(), undoBefore); // nothing recorded yet
        slider->setSliderDown(false);
        QCOMPARE(layerOpacity(), 0.60f);
        QVERIFY(controller->canUndo());
        QCOMPARE(controller->undoText(), QStringLiteral("Undo layer properties"));
        controller->undo(); // the whole drag at once
        QCOMPARE(layerOpacity(), before);
        QCOMPARE(controller->canUndo(), couldUndo);
    }

    // A cel's opacity can be previewed (shown on the frame, nothing recorded).
    void celPropertiesCanBePreviewed()
    {
        document::DocumentController controller;
        controller.importImage(QImage(10, 10, QImage::Format_ARGB32), QStringLiteral("x"));
        const doc::CelId id = controller.currentCel();
        doc::CelProperties shown = controller.document().cel(id)->properties();
        const float before = shown.opacity;
        const QString undoBefore = controller.undoText();
        QSignalSpy repaint(&controller, &document::DocumentController::pixelsChanged);
        shown.opacity = 0.25f;
        QVERIFY(controller.previewCelProperties(id, shown));
        QCOMPARE(controller.document().cel(id)->properties().opacity, 0.25f);
        QCOMPARE(repaint.count(), 1); // the frame is redrawn
        QCOMPARE(controller.undoText(), undoBefore);
        QVERIFY(!controller.previewCelProperties(doc::kNoId, shown));
        shown.opacity = before; // back, then the change as a command: one step
        controller.previewCelProperties(id, shown);
        shown.opacity = 0.25f;
        controller.execute(doc::makeSetCelProperties(controller.document(), id, shown));
        controller.undo();
        QCOMPARE(controller.document().cel(id)->properties().opacity, before);
    }

    // File > Open recent lists the projects opened or saved, the latest first; picking one opens it,
    // and one that is gone drops off the list.
    void openRecentListsLatestFirst()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto make = [&](const QString &name) {
            doc::Document project(64, 64);
            const QString path = dir.filePath(name + QStringLiteral(".") + QLatin1String(io::kProjectSuffix));
            QString error;
            [&] { QVERIFY2(io::saveProject(project, path, &error), qPrintable(error)); }();
            return path;
        };
        const QString a = make(QStringLiteral("alpha")), b = make(QStringLiteral("beta")),
                      c = make(QStringLiteral("gamma"));
        QSettings().remove(QStringLiteral("recent/projects"));

        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QMenu *recent = nullptr;
        for (QMenu *menu : window.findChildren<QMenu *>())
            if (menu->title() == QStringLiteral("Open recent"))
                recent = menu;
        QVERIFY(recent);
        auto names = [&] {
            emit recent->aboutToShow();
            QStringList out;
            for (QAction *action : recent->actions())
                if (!action->isSeparator() && action->isEnabled())
                    out << action->text();
            return out;
        };
        QVERIFY(window.openFile(a));
        QVERIFY(window.openFile(b));
        QVERIFY(window.openFile(c));
        QCOMPARE(names().mid(0, 3), (QStringList{QStringLiteral("gamma.moth"), QStringLiteral("beta.moth"),
                                                 QStringLiteral("alpha.moth")}));

        QVERIFY(window.openFile(a)); // opened again: back to the top
        QCOMPARE(names().first(), QStringLiteral("alpha.moth"));

        // Picking an entry opens it.
        QAction *beta = nullptr;
        for (QAction *action : recent->actions())
            if (action->text() == QStringLiteral("beta.moth"))
                beta = action;
        QVERIFY(beta);
        beta->trigger();
        QVERIFY(window.windowTitle().startsWith(QStringLiteral("beta")));
        QCOMPARE(names().first(), QStringLiteral("beta.moth"));

        // A file that is gone is no longer offered.
        QVERIFY(QFile::remove(c));
        QVERIFY(!names().contains(QStringLiteral("gamma.moth")));
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

    // The wrench sits under the panel, outside the scrolling: visible however
    // short the dock is.
    void wrenchStaysUnderTheScrollingPanel()
    {
        tools::ToolState state;
        auto *panel = new ui::ToolPropertiesPanel(&state);
        ui::PanelDock dock(QStringLiteral("Tool properties"), QStringLiteral("dock.test"), panel, nullptr,
                           panel->footer());
        auto *wrench = dock.findChild<QToolButton *>(QStringLiteral("advancedToolSettings"));
        auto *scroll = dock.findChild<QScrollArea *>();
        QVERIFY(wrench && scroll);
        QVERIFY(!scroll->isAncestorOf(wrench));

        dock.resize(260, 160);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(wrench->isVisible());
        QVERIFY(dock.rect().contains(QRect(wrench->mapTo(&dock, QPoint()), wrench->size())));

        state.setActiveTool(tools::ToolId::Puppet); // no brush options, no wrench
        QVERIFY(!wrench->isVisible());
    }

    // The wrench opens Advanced Tool Settings, whose correction options edit
    // ToolState and the active brush, and follow them back.
    void advancedSettingsEditTheBrush()
    {
        tools::ToolState state;
        ui::ToolPropertiesPanel panel(&state);
        auto *wrench = panel.findChild<QToolButton *>(QStringLiteral("advancedToolSettings"));
        QVERIFY(wrench);
        wrench->click();
        auto *dialog = panel.findChild<ui::AdvancedToolSettings *>();
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        auto box = [dialog](const QString &text) -> QCheckBox * {
            for (QCheckBox *b : dialog->findChildren<QCheckBox *>())
                if (b->text() == text)
                    return b;
            return nullptr;
        };
        QCheckBox *sharp = box(QStringLiteral("Sharp angles"));
        QCheckBox *post = box(QStringLiteral("Post correction"));
        QCheckBox *bezier = box(QStringLiteral("Bezier curve"));
        QVERIFY(sharp && post && bezier);

        sharp->click();
        QVERIFY(state.sharpCorners());
        QVERIFY(!bezier->isEnabled());
        post->click();
        QCOMPARE(state.brush(tools::ToolId::Brush)->style.revision, 10);
        QVERIFY(bezier->isEnabled());
        bezier->click();
        QVERIFY(state.brush(tools::ToolId::Brush)->style.revisionCurves);
        QVERIFY(!tools::roundBrush()->style.revisionCurves); // a copy was edited, not the shared brush

        state.setBrush(tools::ToolId::Brush, tools::roundBrush());
        QVERIFY(!post->isChecked());
        dialog->close();
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
    // X switches the selected color (they keep their places), [ and ] step the size through the presets.
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
        QCOMPARE(state.color(), QColor(Qt::white)); // the secondary is selected now...
        QCOMPARE(state.mainColor(), QColor(Qt::red)); // ...and nothing moved
        QCOMPARE(state.secondaryColor(), QColor(Qt::white));
        QCOMPARE(state.inactiveColor(), QColor(Qt::red));
        state.setColor(Qt::green); // editing changes the selected one only
        QCOMPARE(state.secondaryColor(), QColor(Qt::green));
        QCOMPARE(state.mainColor(), QColor(Qt::red));
        QTest::keyClick(&palette, Qt::Key_X);
        QCOMPARE(state.color(), QColor(Qt::red));

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

    // The move takes what the Layers panel has chosen: several layers, a
    // frame (all its layers), a cel (all the layers of all its frames); one
    // undo step, locked layers left as they are.
    void moveToolMovesTheChosenLayers()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 700);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        struct Made { doc::DrawingId drawing; doc::LayerId line, color; };
        std::vector<Made> frames;
        for (int f : {0, 1}) {
            doc::NewDrawing nd = doc::makeNewDrawing(d, cel, f, 1, "Line");
            controller.execute(std::move(nd.command));
            auto color = std::make_unique<doc::AddLayerCommand>(d, cel, nd.id, 1, doc::LayerProperties{"Color"});
            const doc::LayerId colorId = color->layerId();
            controller.execute(std::move(color));
            frames.push_back({nd.id, nd.layer, colorId});
            d.layer(cel, nd.id, nd.layer)->image.setPixel(100, 100, 0xff000000);
            d.layer(cel, nd.id, colorId)->image.setPixel(100, 100, 0xff000000);
        }
        QCoreApplication::processEvents();
        auto *tree = panel.findChild<QTreeWidget *>();
        auto moved = [&](const Made &m, doc::LayerId layer) {
            return d.layer(cel, m.drawing, layer)->image.pixel(110, 100) == doc::Pixel(0xff000000);
        };
        canvas::MoveTool tool(&controller, [](const QString &) {});
        auto drag = [&] {
            tool.press({0, 0}, Qt::NoModifier);
            tool.move({10, 0}, Qt::NoModifier);
            tool.release({10, 0}, Qt::NoModifier);
        };

        tree->setCurrentItem(panel.celItem(cel)); // the whole cel
        drag();
        for (const Made &m : frames)
            QVERIFY(moved(m, m.line) && moved(m, m.color));
        controller.undo(); // all at once
        QCoreApplication::processEvents();
        for (const Made &m : frames)
            QVERIFY(!moved(m, m.line) && !moved(m, m.color));

        tree->setCurrentItem(panel.frameItem(frames[1].drawing)); // one frame: both its layers
        drag();
        QVERIFY(moved(frames[1], frames[1].line) && moved(frames[1], frames[1].color));
        QVERIFY(!moved(frames[0], frames[0].line) && !moved(frames[0], frames[0].color));
        controller.undo();
        QCoreApplication::processEvents();

        // Two layers of a frame; one of them locked stays.
        tree->setCurrentItem(panel.layerItem(frames[1].line));
        panel.layerItem(frames[1].color)->setSelected(true);
        doc::LayerProperties locked = d.layer(cel, frames[1].drawing, frames[1].color)->properties;
        locked.locked = true;
        controller.execute(doc::makeSetLayerProperties(d, cel, frames[1].drawing, frames[1].color, locked));
        QCoreApplication::processEvents();
        QCOMPARE(controller.layerSelection().size(), std::size_t(2));
        drag();
        QVERIFY(moved(frames[1], frames[1].line));
        QVERIFY(!moved(frames[1], frames[1].color));
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
        QCOMPARE(tool.box(), QRect(900, 500, 60, 100)); // the selection's own box, not the pixels'

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
        tool.press({960, 600}, Qt::NoModifier);
        tool.release({1020, 700}, Qt::NoModifier);
        QCOMPARE(tool.transform().mapRect(QRectF(tool.box())), QRectF(900, 500, 120, 200));
        QCOMPARE(pixel(1000, 580), green);
        QVERIFY(tool.keyPress(Qt::Key_Escape)); // dropped
        QCOMPARE(qAlpha(pixel(1000, 580)), 0);
        QCOMPARE(pixel(930, 540), green);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        tool.activated(); // outside the box: a quarter turn around its center (930, 550)
        tool.press({1000, 550}, Qt::NoModifier);
        tool.release({930, 620}, Qt::NoModifier);
        const QPointF axis = tool.transform().map(QPointF(1, 0)) - tool.transform().map(QPointF(0, 0));
        QVERIFY(std::abs(axis.x()) < 1e-6 && std::abs(axis.y() - 1) < 1e-6);
        QCOMPARE(pixel(940, 555), green); // the bar now stands upright
        QCOMPARE(qAlpha(pixel(915, 540)), 0);
        tool.commit();
        QCOMPARE(controller.undoText(), QStringLiteral("Undo transform"));
    }

    // Just outside a corner of the box (not on its handle) a drag turns it, with a turning
    // arrow for the cursor; the handle itself scales, with arrows along its diagonal.
    void transformTurnsFromJustOutsideACorner()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.setViewZoom(1.0);
        tool.activated();
        QCOMPARE(tool.box(), QRect(910, 490, 100, 100));

        tool.hover({1010, 590}); // on the bottom-right handle
        QCOMPARE(tool.cursor().shape(), Qt::SizeFDiagCursor);
        tool.hover({960, 540}); // inside
        QCOMPARE(tool.cursor().shape(), Qt::SizeAllCursor);
        tool.hover({1020, 600}); // a little out of the corner
        QCOMPARE(tool.cursor().shape(), Qt::BitmapCursor); // the turning arrow
        tool.hover({1010, 490}); // the top-right handle: the other diagonal
        QCOMPARE(tool.cursor().shape(), Qt::SizeBDiagCursor);
        tool.hover({960, 590}); // the middle of the bottom side
        QCOMPARE(tool.cursor().shape(), Qt::SizeVerCursor);

        tool.press({1020, 600}, Qt::NoModifier); // 14 px out of the corner: turns
        tool.move({960, 650}, Qt::NoModifier);
        tool.release({960, 650}, Qt::NoModifier);
        const QTransform t = tool.transform();
        QCOMPARE(t.m11(), t.m22()); // a rotation: no scaling, no shear
        QVERIFY(std::abs(t.m12()) > 0.1);
        QVERIFY(std::abs(t.m11() * t.m11() + t.m12() * t.m12() - 1.0) < 1e-9);
        tool.cancel();
    }

    // The arrow next to + adds a perspective layer: a grid that cannot be
    // drawn on, and that the panel has no controls for (it is set on the canvas).
    void perspectiveLayers()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        QCoreApplication::processEvents();

        QToolButton *add = nullptr;
        for (QToolButton *b : panel.findChildren<QToolButton *>())
            if (b->toolTip() == QStringLiteral("New layer"))
                add = b;
        QVERIFY(add && add->menu());
        QStringList kinds;
        for (QAction *a : add->menu()->actions())
            kinds << a->text();
        QCOMPARE(kinds, (QStringList{QStringLiteral("Layer"), QStringLiteral("Perspective Layer")}));
        add->menu()->actions()[1]->trigger();
        QCoreApplication::processEvents();
        const doc::Drawing &drawing = *controller.currentDrawing();
        const doc::Layer *grid = drawing.layer(controller.currentLayer());
        QVERIFY(grid && grid->kind == doc::LayerKind::Perspective);
        QCOMPARE(grid->properties.name, std::string("Perspective 1"));
        QCOMPARE(grid->properties.perspective.originX, d.width() / 2.0); // fit to the document
        QCOMPARE(grid->properties.perspective.position, perspective::Vec3());
        QVERIFY(!panel.findChild<QSlider *>(QStringLiteral("perspectiveTilt"))); // no controls in the panel

        // Not drawn on, nor worked on with the selection tools.
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason));
        QVERIFY(reason.contains(QStringLiteral("perspective")));
        controller.selectAll();
        QVERIFY(!controller.eraseSelection(&reason));
        QVERIFY(reason.contains(QStringLiteral("perspective")));
    }

    // The handles of a perspective layer on the canvas, whatever the tool: the
    // middle square moves the plane, the arrows move it along its axes, the
    // rings turn it around them, the corner sizes the cells, the button opens
    // the settings. Each drag is live and one undo step.
    void transparentColorErasesWithTheBrush()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Brush, 24.0);
        auto stroke = [&](double y) {
            const QTransform toView = canvas.view().documentToView();
            const QPoint a = toView.map(QPointF(100, y)).toPoint(), b = toView.map(QPointF(300, y)).toPoint();
            QTest::mousePress(&canvas, Qt::LeftButton, {}, a);
            QTest::mouseMove(&canvas, (a + b) / 2);
            QTest::mouseMove(&canvas, b);
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, b);
        };
        auto alpha = [&](int x, int y) {
            return qAlpha(controller.currentDrawing()->layers.front().image.pixel(x, y));
        };
        stroke(200);
        QVERIFY(alpha(200, 200) > 0);

        // The panel's bar selects it; the wheel's colors (and the swap) select the main color back.
        ui::ColorWheelPanel panel(&state);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *swatches = panel.findChild<ui::ColorSwatchPair *>();
        QVERIFY(swatches);
        QVERIFY(!state.transparentSelected());
        QTest::mouseClick(swatches, Qt::LeftButton, {}, QPoint(swatches->width() / 2, swatches->height() - 4));
        QVERIFY(state.transparentSelected());

        // The brush now erases (its tip, its size), and stays erasing stroke after stroke.
        stroke(200);
        QCOMPARE(alpha(200, 200), 0);
        QCOMPARE(state.activeTool(), tools::ToolId::Brush);
        stroke(300);
        QVERIFY(controller.currentDrawing()->layers.front().image.pixel(200, 300) == 0);

        state.setColor(Qt::red); // choosing a color, even the same one
        QVERIFY(!state.transparentSelected());
        stroke(300);
        QVERIFY(alpha(200, 300) > 0);
        state.setTransparentSelected(true);
        state.switchColor();
        QVERIFY(!state.transparentSelected());
    }

    void perspectiveGizmo()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(1000, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        using Handle = canvas::PerspectiveGizmo::Handle;
        const canvas::PerspectiveGizmo &gizmo = canvas.perspectiveGizmo();
        QVERIFY(!gizmo.target()); // a raster layer is current
        QVERIFY(controller.addLayer(false, doc::LayerKind::Perspective));
        QVERIFY(gizmo.target());
        const doc::Drawing &drawing = *controller.currentDrawing();
        const doc::LayerId id = controller.currentLayer();
        auto grid = [&] { return drawing.layer(id)->properties.perspective; };
        auto at = [&](Handle h) {
            const auto p = gizmo.handlePoint(h, canvas.view());
            return p ? canvas.view().documentToView().map(*p) : QPointF(-1000, -1000);
        };
        for (Handle h : {Handle::Move, Handle::AxisX, Handle::AxisY, Handle::AxisZ, Handle::RingX, Handle::RingY,
                         Handle::RingZ, Handle::Scale, Handle::Menu}) {
            const auto p = gizmo.handlePoint(h, canvas.view());
            QVERIFY2(p, qPrintable(QStringLiteral("handle %1 shown").arg(int(h))));
            QCOMPARE(gizmo.handleAt(*p, canvas.view()), h);
        }
        auto drag = [&](QPointF from, QPointF by) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, from.toPoint());
            QTest::mouseMove(&canvas, (from + by * 0.5).toPoint());
            QTest::mouseMove(&canvas, (from + by).toPoint());
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, (from + by).toPoint());
        };
        auto near = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
        const double zoom = canvas.view().zoom();
        const QString undoBefore = controller.undoText();

        // The middle square: as far as the pointer went, on the sheet (the brush is the tool).
        QVERIFY(tools::ToolState::isPaintTool(state.activeTool()));
        drag(at(Handle::Move), QPointF(60, -30));
        QVERIFY(near(grid().position.x, 60.0 / zoom, 2.0 / zoom));
        QVERIFY(near(grid().position.y, -30.0 / zoom, 2.0 / zoom));
        QCOMPARE(grid().position.z, 0.0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo layer properties"));
        QVERIFY(drawing.layers.front().image.isEmpty()); // the brush drew nothing
        controller.undo();
        QCOMPARE(grid().position, perspective::Vec3());
        QCOMPARE(controller.undoText(), undoBefore);

        // The blue arrow: along the plane's normal, the orientation left alone.
        const perspective::Rotation orientation = grid().orientation;
        const QPointF blue = at(Handle::AxisZ) - at(Handle::Move);
        drag(at(Handle::AxisZ), blue / std::hypot(blue.x(), blue.y()) * 40.0);
        const perspective::Vec3 normal = perspective::axis(grid(), {0, 0, -1});
        QVERIFY(perspective::dot(grid().position, normal) > 1.0);
        const perspective::Vec3 rest = grid().position - normal * perspective::dot(grid().position, normal);
        QVERIFY(std::sqrt(perspective::dot(rest, rest)) < 1e-6); // only along it
        QCOMPARE(grid().orientation, orientation);
        controller.undo();

        // The blue ring: around the normal, which stays put; the grid's lines turn.
        const perspective::Vec3 xBefore = perspective::axis(grid(), {1, 0, 0});
        QPointF from = at(Handle::RingZ);
        drag(from, QPointF(25, 25));
        const perspective::Vec3 normalAfter = perspective::axis(grid(), {0, 0, -1});
        QVERIFY(perspective::dot(normalAfter, normal) > 1.0 - 1e-9);
        QVERIFY(perspective::dot(perspective::axis(grid(), {1, 0, 0}), xBefore) < 0.999);
        QCOMPARE(grid().position, perspective::Vec3());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo layer properties"));
        controller.undo();
        QCOMPARE(grid().orientation, orientation);

        // The red ring: around the grid's x axis, which stays put.
        drag(at(Handle::RingX), QPointF(0, 30));
        QVERIFY(perspective::dot(perspective::axis(grid(), {1, 0, 0}), xBefore) > 1.0 - 1e-9);
        QVERIFY(perspective::dot(perspective::axis(grid(), {0, 0, -1}), normal) < 0.999);
        controller.undo();

        // The corner: away from the middle, bigger cells.
        const double cell = grid().cell;
        const QPointF out = at(Handle::Scale) - at(Handle::Move);
        drag(at(Handle::Scale), out * 0.5);
        QVERIFY(grid().cell > cell * 1.3);
        controller.undo();
        QCOMPARE(grid().cell, cell);

        // The settings menu: each choice an undo step.
        QMenu *menu = const_cast<canvas::PerspectiveGizmo &>(gizmo).createMenu(&canvas);
        auto find = [](QMenu *m, const QString &text) -> QAction * {
            for (QAction *a : m->actions())
                if (a->text() == text)
                    return a;
            return nullptr;
        };
        QMenu *cells = find(menu, QStringLiteral("Cells"))->menu();
        QAction *four = nullptr;
        for (QAction *a : cells->actions())
            if (a->data().toInt() == 4)
                four = a;
        QVERIFY(four);
        four->trigger();
        QCOMPARE(grid().cells, 4);
        find(menu, QStringLiteral("Show horizon"))->toggle();
        QVERIFY(!grid().horizon);
        drag(at(Handle::RingY), QPointF(30, 0));
        QVERIFY(!(grid().orientation == orientation));
        find(menu, QStringLiteral("Reset rotation"))->trigger();
        QCOMPARE(grid().orientation, perspective::Grid().orientation);
        delete menu;

        // Locked: shown, but no handle takes the pointer.
        doc::LayerProperties locked = drawing.layer(id)->properties;
        locked.locked = true;
        controller.execute(doc::makeSetLayerProperties(d, controller.currentCel(), drawing.id, id, locked));
        const auto middle = gizmo.handlePoint(Handle::Move, canvas.view());
        QVERIFY(middle);
        QCOMPARE(gizmo.handleAt(*middle, canvas.view()), Handle::None);
        const perspective::Vec3 position = grid().position;
        drag(at(Handle::Move), QPointF(50, 0));
        QCOMPARE(grid().position, position);

        // Another layer current: no handles.
        controller.setCurrentLayer(drawing.layers.front().id);
        QVERIFY(!gizmo.target());
        QCOMPARE(gizmo.handleAt(*middle, canvas.view()), Handle::None);
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

    // The panel's merge button (and the right click menu with it, beside delete) fuses the
    // chosen layers into the lowest one, as one undo step.
    void layersMerge()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        QVERIFY(controller.addLayer(true));
        QVERIFY(controller.addLayer(true));
        const doc::CelId cel = controller.currentCel();
        const doc::Drawing &drawing = *controller.currentDrawing();
        QCOMPARE(drawing.layers.size(), std::size_t(3));
        const doc::LayerId bottom = drawing.layers[0].id, middle = drawing.layers[1].id, top = drawing.layers[2].id;
        auto layer = [&](doc::LayerId id) { return d.layer(cel, drawing.id, id); };
        layer(bottom)->image.setPixel(10, 10, 0xffff0000u);
        layer(middle)->image.setPixel(20, 20, 0xff00ff00u);
        layer(top)->image.setPixel(30, 30, 0xff0000ffu);
        QCoreApplication::processEvents();

        auto *tree = panel.findChild<QTreeWidget *>();
        QToolButton *merge = nullptr;
        for (QToolButton *b : panel.findChildren<QToolButton *>())
            if (b->toolTip() == QStringLiteral("Merge the selected layers"))
                merge = b;
        QVERIFY(tree && merge);
        auto rowOf = [&](doc::LayerId id) { return panel.layerItem(id); };
        QVERIFY(rowOf(top) && rowOf(middle));
        tree->setCurrentItem(rowOf(top));
        QCoreApplication::processEvents();
        QVERIFY(!merge->isEnabled()); // one layer: nothing to merge
        rowOf(middle)->setSelected(true);
        QCoreApplication::processEvents();
        QVERIFY(merge->isEnabled());

        QTest::mouseClick(merge, Qt::LeftButton);
        QCOMPARE(drawing.layers.size(), std::size_t(2));
        QCOMPARE(drawing.layers[0].id, bottom);
        QCOMPARE(drawing.layers[1].id, middle); // the lower of the two keeps the result
        QCOMPARE(layer(middle)->image.pixel(20, 20), doc::Pixel(0xff00ff00u));
        QCOMPARE(layer(middle)->image.pixel(30, 30), doc::Pixel(0xff0000ffu));
        QCOMPARE(layer(bottom)->image.pixel(10, 10), doc::Pixel(0xffff0000u)); // untouched
        QCOMPARE(controller.currentLayer(), middle);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo merge layers"));
        controller.undo();
        QCOMPARE(drawing.layers.size(), std::size_t(3));
        QCOMPARE(layer(top)->image.pixel(30, 30), doc::Pixel(0xff0000ffu));
        QCOMPARE(qAlpha(layer(middle)->image.pixel(30, 30)), 0);
    }

    // What a merge takes, and what it makes of the look of the layers.
    void mergeLayersKeepsTheLookAndSkipsWhatItCannotTake()
    {
        document::DocumentController controller;
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        QVERIFY(controller.addLayer(true));
        QVERIFY(controller.addLayer(true));
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::CelId cel = controller.currentCel();
        const doc::Drawing &drawing = *controller.currentDrawing();
        const doc::LayerId a = drawing.layers[0].id, b = drawing.layers[1].id, c = drawing.layers[2].id,
                           grid = drawing.layers[3].id;
        auto layer = [&](doc::LayerId id) { return d.layer(cel, drawing.id, id); };
        for (doc::LayerId id : {a, b})
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    layer(id)->image.setPixel(x, y, 0xffff0000u);
        layer(a)->properties.opacity = 0.5f;
        layer(a)->properties.blend = doc::BlendMode::Multiply;
        layer(c)->image.setPixel(50, 50, 0xff00ff00u);
        layer(c)->properties.locked = true;

        QString reason;
        QVERIFY(!controller.mergeLayers({a, grid}, &reason)); // a grid has no pixels to merge
        QVERIFY(!reason.isEmpty());
        QVERIFY(!controller.canMergeLayers({a, c})); // a locked one is not taken
        QVERIFY(controller.canMergeLayers({a, b, c, grid}));
        QVERIFY(controller.mergeLayers({a, b, c, grid}, &reason)); // a and b only
        QCOMPARE(drawing.layers.size(), std::size_t(3));
        QCOMPARE(layer(a)->properties.opacity, 1.0f); // the look is in the pixels now
        QCOMPARE(layer(a)->properties.blend, doc::BlendMode::Normal);
        QVERIFY(layer(c) && layer(grid));
        // a (half, multiplied over nothing) under b (opaque red): red.
        QCOMPARE(layer(a)->image.pixel(1, 1), doc::Pixel(0xffff0000u));
        QCOMPARE(layer(c)->image.pixel(50, 50), doc::Pixel(0xff00ff00u));
    }

    // Delete takes every selected layer row, whichever frames they are in; a frame keeps one layer.
    void deleteTakesSeveralLayers()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, cel, 0, 1, "Line").command);
        for (int i = 0; i < 3; ++i)
            QVERIFY(controller.addLayer(true));
        const doc::Drawing &first = *controller.currentDrawing();
        controller.execute(doc::makeNewDrawing(d, cel, 1, 1, "Line").command);
        controller.setCurrentFrame(1);
        for (int i = 0; i < 2; ++i)
            QVERIFY(controller.addLayer(true));
        const doc::Drawing &second = *controller.currentDrawing();
        QCOMPARE(first.layers.size(), std::size_t(4));
        QCOMPARE(second.layers.size(), std::size_t(3));
        QCoreApplication::processEvents();

        auto *tree = panel.findChild<QTreeWidget *>();
        QToolButton *del = nullptr;
        for (QToolButton *b : panel.findChildren<QToolButton *>())
            if (b->toolTip().startsWith(QStringLiteral("Delete")))
                del = b;
        QVERIFY(tree && del);
        // Two layers of the first frame, one of the second.
        const doc::LayerId keepSecond = second.layers[2].id;
        tree->setCurrentItem(panel.layerItem(first.layers[1].id));
        panel.layerItem(first.layers[3].id)->setSelected(true);
        panel.layerItem(second.layers[1].id)->setSelected(true);
        QCoreApplication::processEvents();
        QVERIFY(del->isEnabled());
        QTest::mouseClick(del, Qt::LeftButton);
        QCOMPARE(first.layers.size(), std::size_t(2));
        QCOMPARE(second.layers.size(), std::size_t(2));
        QVERIFY(second.layer(keepSecond));
        controller.undo(); // one step for all of them
        QCOMPARE(first.layers.size(), std::size_t(4));
        QCOMPARE(second.layers.size(), std::size_t(3));

        // All the layers of a frame chosen: the lowest stays.
        QCoreApplication::processEvents();
        const doc::LayerId lowest = second.layers[0].id;
        tree->setCurrentItem(panel.layerItem(second.layers[0].id));
        panel.layerItem(second.layers[1].id)->setSelected(true);
        panel.layerItem(second.layers[2].id)->setSelected(true);
        QCoreApplication::processEvents();
        QVERIFY(del->isEnabled());
        QTest::mouseClick(del, Qt::LeftButton);
        QCOMPARE(second.layers.size(), std::size_t(1));
        QCOMPARE(second.layers[0].id, lowest);
    }

    // Layers copied from the panel are pasted on top of another frame, each on its own or merged.
    void layersCopyAndPaste()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, cel, 0, 1, "Line").command);
        QVERIFY(controller.addLayer(true));
        const doc::Drawing &source = *controller.currentDrawing();
        const doc::LayerId a = source.layers[0].id, b = source.layers[1].id;
        d.layer(cel, source.id, a)->image.setPixel(10, 10, 0xffff0000u);
        d.layer(cel, source.id, b)->image.setPixel(20, 20, 0xff0000ffu);
        d.layer(cel, source.id, b)->properties.name = "Top";
        QCoreApplication::processEvents();
        QVERIFY(!controller.hasCopiedLayers());
        QString reason;
        QVERIFY(!controller.pasteLayers(false, {}, &reason)); // nothing copied yet
        QVERIFY(!reason.isEmpty());

        auto *tree = panel.findChild<QTreeWidget *>();
        tree->setCurrentItem(panel.layerItem(a));
        panel.layerItem(b)->setSelected(true);
        QCoreApplication::processEvents();
        QSignalSpy status(&panel, &ui::LayersPanel::statusMessage);
        panel.copyLayers();
        QVERIFY(controller.hasCopiedLayers());

        // To another frame: separate layers on top, in order, with their pixels, as one step.
        controller.execute(doc::makeNewDrawing(d, cel, 1, 1, "Line").command);
        controller.setCurrentFrame(1);
        const doc::Drawing &target = *controller.currentDrawing();
        QCOMPARE(target.layers.size(), std::size_t(1));
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.frameItem(target.id)); // the frame chosen, no layer: they go on top
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Frame);
        panel.pasteLayers(false);
        QCOMPARE(target.layers.size(), std::size_t(3));
        QCOMPARE(target.layers[1].image.pixel(10, 10), doc::Pixel(0xffff0000u));
        QCOMPARE(target.layers[2].image.pixel(20, 20), doc::Pixel(0xff0000ffu));
        QCOMPARE(target.layers[2].properties.name, std::string("Top"));
        QCOMPARE(controller.currentLayer(), target.layers[2].id);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo paste layers"));
        controller.undo();
        QCOMPARE(target.layers.size(), std::size_t(1));

        // Merged: one layer of what they show together.
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.frameItem(target.id));
        panel.pasteLayers(true);
        QCOMPARE(target.layers.size(), std::size_t(2));
        QCOMPARE(target.layers[1].image.pixel(10, 10), doc::Pixel(0xffff0000u));
        QCOMPARE(target.layers[1].image.pixel(20, 20), doc::Pixel(0xff0000ffu));

        // With layers of the frame selected, what is pasted takes their place, in either way.
        QCoreApplication::processEvents();
        controller.execute(doc::makeNewDrawing(d, cel, 3, 1, "Line").command);
        controller.setCurrentFrame(3);
        QVERIFY(controller.addLayer(true));
        QVERIFY(controller.addLayer(true));
        const doc::Drawing &over = *controller.currentDrawing();
        QCOMPARE(over.layers.size(), std::size_t(3));
        const doc::LayerId low = over.layers[0].id, mid = over.layers[1].id, high = over.layers[2].id;
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.layerItem(mid));
        panel.layerItem(high)->setSelected(true); // the middle and the top ones
        QCoreApplication::processEvents();
        panel.pasteLayers(false);
        QCOMPARE(over.layers.size(), std::size_t(3)); // low, then the two pasted
        QCOMPARE(over.layers[0].id, low);
        QVERIFY(!over.layer(mid) && !over.layer(high));
        QCOMPARE(over.layers[1].image.pixel(10, 10), doc::Pixel(0xffff0000u));
        QCOMPARE(over.layers[2].properties.name, std::string("Top"));
        controller.undo();
        QCOMPARE(over.layers.size(), std::size_t(3));
        QVERIFY(over.layer(mid) && over.layer(high));
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.layerItem(low));
        panel.layerItem(mid)->setSelected(true);
        QCoreApplication::processEvents();
        panel.pasteLayers(true); // merged, over the two lower ones
        QCOMPARE(over.layers.size(), std::size_t(2)); // the merged one, and the top one
        QVERIFY(!over.layer(low) && !over.layer(mid) && over.layer(high));
        QCOMPARE(over.layers[0].image.pixel(20, 20), doc::Pixel(0xff0000ffu));
        controller.undo();
        // Selecting the very layers that were copied does not overwrite them with themselves.
        controller.setCurrentFrame(0);
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.layerItem(a));
        panel.layerItem(b)->setSelected(true);
        QCoreApplication::processEvents();
        panel.pasteLayers(false);
        QCOMPARE(source.layers.size(), std::size_t(4));
        QVERIFY(source.layer(a) && source.layer(b));
        controller.undo();

        // Cut takes the layers and they can be pasted elsewhere.
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.layerItem(b));
        QCoreApplication::processEvents();
        panel.cutLayers();
        QCOMPARE(source.layers.size(), std::size_t(1)); // a stays: a frame keeps one layer
        QVERIFY(!source.layer(b));
        QVERIFY(controller.hasCopiedLayers());
        controller.setCurrentFrame(5);
        panel.pasteLayers(false);
        QCOMPARE(controller.currentDrawing()->layers.size(), std::size_t(1));
        QCOMPARE(controller.currentDrawing()->layers[0].image.pixel(20, 20), doc::Pixel(0xff0000ffu));
        controller.undo();

        // Where the cel shows nothing yet, a frame is made for them (without an empty layer).
        controller.setCurrentFrame(5);
        QVERIFY(!controller.currentDrawing());
        panel.pasteLayers(false);
        QVERIFY(controller.currentDrawing());
        QCOMPARE(controller.currentDrawing()->layers.size(), std::size_t(1)); // what the cut took
        QCOMPARE(controller.currentDrawing()->layers[0].image.pixel(20, 20), doc::Pixel(0xff0000ffu));
        controller.undo(); // frame and layers together
        QVERIFY(!controller.currentDrawing());
    }

    // Only the tools that have sub tools list any: Hand, Zoom, Move, Puppet, Eyedropper and
    // Transform show none (they once fell into the Fill's two).
    void toolsWithoutSubToolsListNone()
    {
        using tools::ToolId;
        for (ToolId t : {ToolId::Hand, ToolId::Zoom, ToolId::Move, ToolId::Puppet, ToolId::Eyedropper,
                         ToolId::Transform})
            QVERIFY(tools::builtInPresets(t).isEmpty());
        QCOMPARE(tools::builtInPresets(ToolId::Fill).size(), 3);
        QVERIFY(!tools::builtInPresets(ToolId::Brush).isEmpty());
        QVERIFY(!tools::builtInPresets(ToolId::Eraser).isEmpty());
        QCOMPARE(tools::builtInPresets(ToolId::RectSelect).size(), 2);
    }

    // The text of a selected row is in the highlighted text color (dark on the blue otherwise, in a light
    // theme), the others' in the normal one.
    void selectedLayerRowsKeepReadableText()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QString reason;
        const auto target = controller.beginPaint(true, &reason);
        QVERIFY(target);
        const doc::TiledImage before = *target->image;
        controller.endPaint(before);
        controller.addCel();
        ui::LayersPanel panel(&controller);
        panel.setToolState(&state);
        panel.resize(320, 420);
        panel.show();
        QTest::qWait(50);
        auto *tree = panel.findChild<QTreeWidget *>();
        QVERIFY(tree && tree->topLevelItemCount() >= 2);
        const QPalette pal = QApplication::palette();
        int selected = 0, others = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            QWidget *row = tree->itemWidget(item, 0);
            auto *text = row ? row->findChildren<QLabel *>().last() : nullptr;
            if (!text)
                continue;
            const QColor shown = text->palette().color(QPalette::WindowText);
            if (item->isSelected()) {
                ++selected;
                QCOMPARE(shown, pal.color(QPalette::HighlightedText));
            } else {
                ++others;
                QCOMPARE(shown, pal.color(QPalette::WindowText));
            }
        }
        QVERIFY(selected >= 1 && others >= 1);
    }

    // "Fill refer to other layers" finds the area in the union of the layers shown (the lines on another
    // layer stop it), "Fill" in the layer it fills; both fill in the current layer.
    void fillCanReferToOtherLayers()
    {
        tools::ToolState state;
        document::DocumentController controller;
        const auto presets = tools::builtInPresets(tools::ToolId::Fill);
        QCOMPARE(presets[1].name, QStringLiteral("Fill refer to other layers"));
        state.setColor(Qt::blue);
        state.setOpacity(tools::ToolId::Fill, 1.0);

        // A square of lines on one layer; then another layer, empty, is the one to fill.
        QString reason;
        const auto target = controller.beginPaint(true, &reason);
        QVERIFY(target);
        const doc::TiledImage before = *target->image;
        for (int i = 100; i <= 300; ++i)
            for (int t = 0; t < 3; ++t) {
                target->image->setPixel(i, 100 + t, 0xff000000);
                target->image->setPixel(i, 300 + t, 0xff000000);
                target->image->setPixel(100 + t, i, 0xff000000);
                target->image->setPixel(300 + t, i, 0xff000000);
            }
        controller.endPaint(before);
        const doc::LayerId lines = controller.currentLayer();
        QVERIFY(controller.addLayer(true));
        const doc::LayerId color = controller.currentLayer();
        QVERIFY(color != lines);

        canvas::FrameRenderer frame;
        canvas::FillTool tool(&controller, &state, [&]() -> const QImage & {
            frame.renderAll(controller.document(), controller.currentFrame());
            return frame.image();
        }, [](const QString &) {});
        auto pixel = [&](int x, int y) {
            for (const doc::Layer &l : controller.currentDrawing()->layers)
                if (l.id == color)
                    return l.image.pixel(x, y);
            return doc::Pixel(0);
        };

        tools::applyPreset(state, presets[1]);
        QVERIFY(state.fillSettings().allLayers);
        tool.press({200, 200}, Qt::NoModifier);
        QCOMPARE(pixel(200, 200), doc::Pixel(0xff0000ff)); // inside the lines of the other layer
        QCOMPARE(qAlpha(pixel(50, 50)), 0);                 // and not outside them
        controller.undo();

        tools::applyPreset(state, presets[0]);
        QVERIFY(!state.fillSettings().allLayers);
        tool.press({200, 200}, Qt::NoModifier);
        QCOMPARE(pixel(200, 200), doc::Pixel(0xff0000ff)); // this layer is empty: the area is all of it
        QCOMPARE(pixel(50, 50), doc::Pixel(0xff0000ff));
    }

    // The lasso fill sub tool: an outline drawn freehand, its inside filled
    // over what is there, within the selection, as one undo step.
    void lassoFillFillsTheOutline()
    {
        tools::ToolState state;
        document::DocumentController controller;
        const auto presets = tools::builtInPresets(tools::ToolId::Fill);
        QCOMPARE(presets.size(), 3);
        state.setOpacity(tools::ToolId::Fill, 0.5);
        tools::applyPreset(state, presets[2]);
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
        auto *play = window.findChild<QPushButton *>(QStringLiteral("autoplayPlay"));
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

    // "Go to frame" puts the timeline on the time slot the autoplay shows.
    void autoplayGoesToItsFrame()
    {
        document::DocumentController controller;
        for (int f = 0; f < 4; ++f) {
            QImage image(40, 40, QImage::Format_ARGB32);
            image.fill(Qt::red);
            controller.setCurrentFrame(f);
            QVERIFY(controller.pasteImage(image, QPoint(940, 520)));
        }
        controller.setCurrentFrame(0);
        ui::AutoplayWindow window(&controller);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QPushButton *>(QStringLiteral("autoplayPlay"))->setChecked(false);
        auto *slider = window.findChild<QSlider *>();
        slider->setValue(2);
        QCOMPARE(window.frame(), 2);
        QCOMPARE(controller.currentFrame(), 0); // the autoplay plays apart from the timeline
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Go to frame"))
                button->click();
        QCOMPARE(controller.currentFrame(), 2);
    }

    // Smudge drags the color under the pen along; Blur softens an edge; each is one undo step, and a
    // drag that is cancelled leaves the layer as it was.
    void blendToolSmudgesAndBlurs()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(200, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("red")); // x 860..1059, y 490..589
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::BlendTool tool(&controller, &state, [](const QString &) {});
        state.setSize(tools::ToolId::Blend, 60);
        state.setOpacity(tools::ToolId::Blend, 1.0);

        QCOMPARE(qAlpha(pixel(1075, 540)), 0);
        tool.press({1000, 540}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        for (int x = 1000; x <= 1120; x += 10)
            tool.move({double(x), 540}, Qt::NoModifier);
        tool.release({1120, 540}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QVERIFY(qAlpha(pixel(1075, 540)) > 60); // the red was pulled out past the edge
        QVERIFY(qRed(pixel(1075, 540)) > 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo blend"));
        controller.undo();
        QCOMPARE(qAlpha(pixel(1075, 540)), 0);

        // Blur: the hard edge at x = 1059 softens on both sides.
        tools::BlendSettings blend;
        blend.mode = tools::BlendSettings::Mode::Blur;
        state.setBlendSettings(blend);
        QCOMPARE(qAlpha(pixel(1058, 540)), 255);
        QCOMPARE(qAlpha(pixel(1060, 540)), 0);
        tool.press({1059, 520}, Qt::NoModifier);
        for (int y = 520; y <= 560; y += 5)
            tool.move({1059, double(y)}, Qt::NoModifier);
        tool.release({1059, 560}, Qt::NoModifier);
        QVERIFY(qAlpha(pixel(1058, 540)) < 255);
        QVERIFY(qAlpha(pixel(1060, 540)) > 0);
        controller.undo();

        // Cancelled: nothing changes and nothing is left to undo.
        const bool couldUndo = controller.canUndo();
        tool.press({1000, 540}, Qt::NoModifier);
        tool.move({1100, 540}, Qt::NoModifier);
        tool.cancel();
        QVERIFY(!tool.isActive());
        QCOMPARE(qAlpha(pixel(1075, 540)), 0);
        QCOMPARE(controller.canUndo(), couldUndo);
    }

    // Liquify pushes the pixels along with the pen, and expands what is under it; the selection protects the rest.
    void liquifyToolMovesPixels()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage bar(100, 10, QImage::Format_ARGB32);
        bar.fill(Qt::green);
        controller.importImage(bar, QStringLiteral("bar")); // x 910..1009, y 535..544
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::LiquifyTool tool(&controller, &state, [](const QString &) {});
        state.setSize(tools::ToolId::Liquify, 80);
        state.setOpacity(tools::ToolId::Liquify, 1.0);

        QCOMPARE(qAlpha(pixel(915, 540)), 255);
        tool.press({905, 540}, Qt::NoModifier);
        for (int x = 905; x <= 960; x += 5)
            tool.move({double(x), 540}, Qt::NoModifier);
        tool.release({960, 540}, Qt::NoModifier);
        QVERIFY(qAlpha(pixel(915, 540)) < 128); // the left end of the bar was swept to the right
        QCOMPARE(controller.undoText(), QStringLiteral("Undo liquify"));
        controller.undo();
        QCOMPARE(qAlpha(pixel(915, 540)), 255);

        // Held by a selection, what lies outside does not move.
        QPainterPath area;
        area.addRect(900, 500, 30, 80); // x 900..929
        controller.setSelection(area);
        tool.press({905, 540}, Qt::NoModifier);
        for (int x = 905; x <= 960; x += 5)
            tool.move({double(x), 540}, Qt::NoModifier);
        tool.release({960, 540}, Qt::NoModifier);
        QCOMPARE(qAlpha(pixel(950, 540)), 255);
        controller.clearSelection();

        // Expand: a small square swells under the pen (in a document of its own).
        document::DocumentController other;
        QImage dot(20, 20, QImage::Format_ARGB32);
        dot.fill(Qt::red);
        other.importImage(dot, QStringLiteral("dot")); // x 950..969, y 530..549
        other.clearSelection();
        const doc::Cel &otherCel = *other.document().cel(other.currentCel());
        auto dotPixel = [&](int x, int y) { return otherCel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::LiquifyTool expand(&other, &state, [](const QString &) {});
        tools::LiquifySettings liquify;
        liquify.mode = tools::LiquifySettings::Mode::Expand;
        state.setLiquifySettings(liquify);
        QCOMPARE(qAlpha(dotPixel(972, 540)), 0);
        expand.press({960, 540}, Qt::NoModifier);
        for (int i = 0; i < 6; ++i) {
            expand.move({980, 540}, Qt::NoModifier);
            expand.move({960, 540}, Qt::NoModifier);
        }
        expand.release({960, 540}, Qt::NoModifier);
        QVERIFY(qAlpha(dotPixel(972, 540)) > 0);
    }

    // The blend and liquify tools have their sub tools, and picking one sets the mode.
    void blendAndLiquifyHaveSubTools()
    {
        tools::ToolState state;
        const auto blend = tools::builtInPresets(tools::ToolId::Blend);
        QCOMPARE(blend.size(), 2);
        tools::applyPreset(state, blend[1]);
        QCOMPARE(state.blendSettings().mode, tools::BlendSettings::Mode::Blur);
        const auto liquify = tools::builtInPresets(tools::ToolId::Liquify);
        QCOMPARE(liquify.size(), 3);
        tools::applyPreset(state, liquify[2]);
        QCOMPARE(state.liquifySettings().mode, tools::LiquifySettings::Mode::Shrink);
        QCOMPARE(state.settings(tools::ToolId::Blend).opacity, 0.6); // a mode keeps size and strength
    }

    // The shape tool draws a rectangle, an ellipse or a line as one undo step; the selection holds it,
    // a click draws nothing.
    void shapeToolDrawsShapes()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto alphaAt = [&](int x, int y) {
            return cel.drawingAt(0) ? qAlpha(cel.drawingAt(0)->layers.front().image.pixel(x, y)) : 0;
        };
        canvas::ShapeTool tool(&controller, &state, [](const QString &) {});
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Shape, 4);

        // A click only.
        tool.press({100, 100}, Qt::NoModifier);
        tool.release({100, 100}, Qt::NoModifier);
        QVERIFY(!controller.canUndo());

        // An outlined rectangle: the edge is drawn, the inside is not.
        tool.press({100, 100}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        tool.move({200, 160}, Qt::NoModifier);
        tool.release({200, 160}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(alphaAt(150, 100), 255);
        QCOMPARE(alphaAt(100, 130), 255);
        QCOMPARE(alphaAt(150, 130), 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo shape"));
        controller.undo();
        QCOMPARE(alphaAt(150, 100), 0);

        // Filled, at half opacity, inside a selection that holds back the right part.
        tools::ShapeSettings shape = state.shapeSettings();
        shape.style = tools::ShapeSettings::Style::Filled;
        state.setShapeSettings(shape);
        state.setOpacity(tools::ToolId::Shape, 0.5);
        QPainterPath area;
        area.addRect(0, 0, 150, 1080);
        controller.setSelection(area);
        tool.press({100, 100}, Qt::NoModifier);
        tool.release({200, 160}, Qt::NoModifier);
        QVERIFY(qAbs(alphaAt(120, 130) - 128) <= 1);
        QCOMPARE(alphaAt(180, 130), 0);
        controller.undo();
        controller.clearSelection();

        // Shift makes a circle, Alt grows it from the center.
        shape.kind = tools::ShapeSettings::Kind::Ellipse;
        state.setShapeSettings(shape);
        state.setOpacity(tools::ToolId::Shape, 1.0);
        tool.press({300, 300}, Qt::AltModifier | Qt::ShiftModifier);
        tool.release({340, 310}, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(alphaAt(300, 300), 255); // the center
        QCOMPARE(alphaAt(300, 262), 255); // 40 up: a circle, not a 10 px high ellipse
        QCOMPARE(alphaAt(300, 350), 0);   // beyond the radius
    }

    // A polygon is made of clicks: the first point closes it, Enter or a double click finish it, Esc drops it.
    void shapeToolPolygonAndCurveTakeClicks()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto alphaAt = [&](int x, int y) {
            return cel.drawingAt(0) ? qAlpha(cel.drawingAt(0)->layers.front().image.pixel(x, y)) : 0;
        };
        canvas::ShapeTool tool(&controller, &state, [](const QString &) {});
        state.setSize(tools::ToolId::Shape, 4);
        tools::ShapeSettings shape;
        shape.kind = tools::ShapeSettings::Kind::Polygon;
        shape.style = tools::ShapeSettings::Style::Filled;
        state.setShapeSettings(shape);

        auto click = [&](double x, double y) {
            tool.press({x, y}, Qt::NoModifier);
            QVERIFY(!tool.isActive()); // no drag: the canvas does not hold the tool
            tool.release({x, y}, Qt::NoModifier);
        };
        click(100, 100);
        QVERIFY(tool.hasPendingEdit());
        click(200, 100);
        click(200, 200);
        QVERIFY(!controller.canUndo()); // nothing drawn yet
        click(100, 100);                // back on the first point: done
        QVERIFY(!tool.hasPendingEdit());
        QCOMPARE(alphaAt(180, 110), 255); // inside the triangle
        QCOMPARE(alphaAt(110, 190), 0);   // the other half of the square
        QCOMPARE(controller.undoText(), QStringLiteral("Undo shape"));
        controller.undo();

        // Esc drops it; two points are too few for a polygon.
        click(100, 100);
        click(200, 100);
        QVERIFY(tool.keyPress(Qt::Key_Escape));
        QVERIFY(!tool.hasPendingEdit());
        click(100, 100);
        click(200, 100);
        tool.commit();
        QVERIFY(!controller.canUndo());

        // A curve goes through its points and is finished by Enter; Backspace takes a point back.
        shape.kind = tools::ShapeSettings::Kind::Curve;
        state.setShapeSettings(shape);
        click(100, 300);
        click(150, 250);
        click(200, 300);
        click(250, 350);
        QVERIFY(tool.keyPress(Qt::Key_Backspace));
        QVERIFY(tool.keyPress(Qt::Key_Return));
        QCOMPARE(alphaAt(150, 250), 255);
        QCOMPARE(alphaAt(200, 300), 255);
        QCOMPARE(alphaAt(250, 350), 0); // the removed point
        QVERIFY(!tool.keyPress(Qt::Key_Return)); // nothing in progress
    }

    void shapeToolRoundedRectangleRoundsItsCorners()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        canvas::ShapeTool tool(&controller, &state, [](const QString &) {});
        tools::ShapeSettings shape;
        shape.kind = tools::ShapeSettings::Kind::RoundedRectangle;
        shape.style = tools::ShapeSettings::Style::Filled;
        shape.cornerRadius = 30;
        state.setShapeSettings(shape);
        tool.press({100, 100}, Qt::NoModifier);
        tool.release({300, 200}, Qt::NoModifier);
        const auto &image = cel.drawingAt(0)->layers.front().image;
        QCOMPARE(qAlpha(image.pixel(200, 150)), 255);
        QCOMPARE(qAlpha(image.pixel(101, 101)), 0); // the corner is cut off
        QCOMPARE(qAlpha(image.pixel(200, 101)), 255);
    }

    void shapeGeometryConstrains()
    {
        using Kind = tools::ShapeSettings::Kind;
        const QPointF a(10, 10);
        const auto square = canvas::shapeGeometry(Kind::Rectangle, a, {50, 30}, Qt::ShiftModifier);
        QCOMPARE(square.box, QRectF(10, 10, 40, 40));
        const auto centered = canvas::shapeGeometry(Kind::Rectangle, a, {20, 15}, Qt::AltModifier);
        QCOMPARE(centered.box, QRectF(0, 5, 20, 10));
        const auto flat = canvas::shapeGeometry(Kind::Line, a, {50, 14}, Qt::ShiftModifier); // ~5.7°: snaps to 0°
        QVERIFY(qAbs(flat.line.p2().y() - 10) < 1e-6);
        QVERIFY(qAbs(flat.line.length() - QLineF(a, QPointF(50, 14)).length()) < 1e-6);
    }

    void shapeHasSubTools()
    {
        tools::ToolState state;
        const auto presets = tools::builtInPresets(tools::ToolId::Shape);
        QCOMPARE(presets.size(), 6);
        tools::applyPreset(state, presets[3]);
        QCOMPARE(state.shapeSettings().kind, tools::ShapeSettings::Kind::Ellipse);
        tools::applyPreset(state, presets[5]);
        QCOMPARE(state.shapeSettings().kind, tools::ShapeSettings::Kind::Curve);
        QCOMPARE(state.settings(tools::ToolId::Shape).size, 4.0); // a kind keeps the thickness
    }

    // The snap picks the axis closest to the direction of the pen, once it has moved far enough, and
    // puts the pen on that axis's line through the start.
    void perspectiveSnapChoosesTheNearestAxis()
    {
        const perspective::Grid grid = perspective::defaultGrid(1920, 1080);
        const QPointF start(900, 500);
        const auto dirs = canvas::PerspectiveSnap::directions(grid, start);
        for (int axis = 0; axis < 3; ++axis) {
            if (!dirs[std::size_t(axis)])
                continue;
            const QPointF u = *dirs[std::size_t(axis)];
            const double a = 5 * std::numbers::pi / 180; // a little off the axis
            const QPointF off(u.x() * std::cos(a) - u.y() * std::sin(a), u.x() * std::sin(a) + u.y() * std::cos(a));
            canvas::PerspectiveSnap snap;
            snap.begin(grid, start, 10.0);
            QVERIFY(!snap.update(start + off * 5)); // not far enough: not decided yet
            QVERIFY(!snap.decided());
            QVERIFY(snap.update(start + off * 100));
            // (two axes can share a direction: the one found must be as close as this)
            const QPointF onLine = snap.snapped(start + off * 100);
            const QPointF along = *dirs[std::size_t(snap.axis())];
            const QPointF w = onLine - start;
            QVERIFY(std::abs(w.x() * along.y() - w.y() * along.x()) < 1e-6); // on the line through the start
            QVERIFY(std::hypot(w.x(), w.y()) > 90 && std::hypot(w.x(), w.y()) < 101);
            // Kept: turning the pen another way afterwards does not change the axis.
            const int chosen = snap.axis();
            snap.update(start + QPointF(-off.y(), off.x()) * 100);
            QCOMPARE(snap.axis(), chosen);
        }
    }

    // Shift held: a straight line from where the last stroke ended to the pen, drawn when the pen touches;
    // letting go of Shift drops it; until a touch nothing is drawn.
    void heldShiftPlacesAStraightLineFromTheLastPoint()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Brush, 10.0);
        auto alphaAt = [&](const QPointF &docPoint) {
            for (const doc::Layer &l : controller.currentDrawing()->layers)
                if (l.id == controller.currentLayer())
                    return qAlpha(l.image.pixel(int(docPoint.x()), int(docPoint.y())));
            return -1;
        };
        const QTransform toView = canvas.view().documentToView();
        auto view = [&](const QPointF &p) { return toView.map(p).toPoint(); };
        auto tap = [&](const QPointF &p) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, view(p));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, view(p));
        };
        const QPointF last(100, 100), target(300, 160);

        // Nothing drawn yet: Shift has no point to start from, the touch is an ordinary dot.
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QTest::mouseMove(&canvas, view(target));
        tap(last);
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        QVERIFY(alphaAt(last) > 0);
        QCOMPARE(alphaAt((last + target) / 2.0), 0);

        // Held: the line shows (draws nothing) until the touch places it, one undo step.
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QTest::mouseMove(&canvas, view(target));
        QCOMPARE(alphaAt((last + target) / 2.0), 0);
        tap(target);
        QVERIFY(alphaAt((last + target) / 2.0) > 0);
        QVERIFY(alphaAt(QPointF(target.x() - 2, target.y() - 0.7)) > 0);
        QCOMPARE(alphaAt(QPointF(200, 100)), 0); // only the line

        // Still held: the next line starts where this one ended.
        const QPointF third(300, 300);
        tap(third);
        QVERIFY(alphaAt(QPointF(300, 230)) > 0);
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        controller.undo();
        controller.undo(); // the two lines
        QCOMPARE(alphaAt((last + target) / 2.0), 0);
        QVERIFY(alphaAt(last) > 0);

        // Let go of Shift: the touch is an ordinary dot again.
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        tap(QPointF(400, 400));
        QCOMPARE(alphaAt(QPointF(250, 250)), 0);
        QVERIFY(alphaAt(QPointF(400, 400)) > 0);
    }

    // Through a fisheye the axis's line is a curve: the pen is put on it, and it starts where the stroke did.
    void perspectiveSnapFollowsTheCurveOfAFisheye()
    {
        perspective::Grid grid = perspective::defaultGrid(800, 600);
        grid.fisheye = 0.8;
        const QPointF start(300, 250);
        const auto dirs = canvas::PerspectiveSnap::directions(grid, start);
        QVERIFY(dirs[0] && dirs[1] && dirs[2]);
        canvas::PerspectiveSnap snap;
        snap.begin(grid, start, 6.0);
        QVERIFY(snap.update(start + *dirs[0] * 40.0));
        QCOMPARE(snap.axis(), 0);
        const QPointF origin = snap.snapped(start);
        QVERIFY(QLineF(origin, start).length() < 1e-6);
        // Whatever the pen does, the point stays in the plane of the start's ray and the axis.
        const perspective::Vec3 ray = perspective::unprojectDirection(grid, {start.x(), start.y()});
        const perspective::Vec3 along = perspective::axis(grid, {1, 0, 0});
        const perspective::Vec3 n = perspective::cross(ray, along);
        const double length = std::sqrt(perspective::dot(n, n));
        for (const QPointF pen : {QPointF(420, 330), QPointF(150, 100), QPointF(500, 200)}) {
            const QPointF on = snap.snapped(pen);
            const perspective::Vec3 d = perspective::unprojectDirection(grid, {on.x(), on.y()});
            QVERIFY(std::abs(perspective::dot(d, n)) / length < 1e-6);
        }
        QVERIFY(snap.guide(QPointF(420, 330)).size() > 2); // a curve, not two points
    }

    // A stroke made with the snap on follows an axis of the chosen perspective layer instead of the pen.
    void perspectiveSnapBendsTheStroke()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Brush, 12.0);
        auto stroke = [&](const QPointF &from, const QPointF &to) {
            const QTransform toView = canvas.view().documentToView();
            const QPoint a = toView.map(from).toPoint(), b = toView.map(to).toPoint();
            QTest::mousePress(&canvas, Qt::LeftButton, {}, a);
            for (int i = 1; i <= 8; ++i)
                QTest::mouseMove(&canvas, a + (b - a) * i / 8);
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, b);
        };
        stroke({100, 100}, {120, 100}); // makes the drawing, and its layer
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId gridLayer = controller.currentLayer();
        controller.setCurrentLayer(raster);
        QVERIFY(controller.perspectiveLayerById(gridLayer));
        const doc::Layer *drawn = nullptr;
        for (const doc::Layer &l : controller.currentDrawing()->layers)
            if (l.id == raster)
                drawn = &l;
        QVERIFY(drawn);
        auto alpha = [&](const QPointF &p) { return qAlpha(drawn->image.pixel(int(p.x()), int(p.y()))); };

        const perspective::Grid grid = controller.perspectiveLayerById(gridLayer)->grid;
        const QPointF start(900, 500);
        const auto dirs = canvas::PerspectiveSnap::directions(grid, start);
        QVERIFY(dirs[0]);
        const QPointF u = *dirs[0];
        const double a = 6 * std::numbers::pi / 180;
        const QPointF off(u.x() * std::cos(a) - u.y() * std::sin(a), u.x() * std::sin(a) + u.y() * std::cos(a));
        const QPointF end = start + off * 200;

        // Without snapping: along the pen.
        stroke(start, end);
        QVERIFY(alpha(start + off * 150) > 0);
        controller.undo();
        QCOMPARE(alpha(start + off * 150), 0);

        // With it: along the axis; where the pen went is left clear.
        state.setPerspectiveSnap(true);
        state.setPerspectiveSnapLayer(gridLayer);
        stroke(start, end);
        QVERIFY(alpha(start + u * 150) > 0);
        QCOMPARE(alpha(start + off * 150), 0); // 16 px off the axis
        QVERIFY(alpha(start + u * 5) > 0);     // and from the start
    }

    // "Snap to" in the Layers panel: on a perspective layer it makes that layer the reference of the
    // pen's snapping, shown by a sight on its row; pressed again it lets go.
    void layersPanelSnapsToAPerspectiveLayer()
    {
        tools::ToolState state;
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.setToolState(&state);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("x"));
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId grid = controller.currentLayer();
        QCoreApplication::processEvents();
        auto *snapTo = panel.findChild<QToolButton *>(QStringLiteral("snapTo"));
        QVERIFY(snapTo);
        auto sight = [&](doc::LayerId layer) {
            auto *row = panel.layerItem(layer) ? panel.findChild<QTreeWidget *>()->itemWidget(panel.layerItem(layer), 0)
                                               : nullptr;
            auto *mark = row ? row->findChild<QLabel *>(QStringLiteral("snapMark")) : nullptr;
            return mark && !mark->isHidden();
        };

        QCOMPARE(controller.currentLayer(), grid);
        QVERIFY(snapTo->isEnabled());
        QVERIFY(!snapTo->isChecked());
        QVERIFY(!sight(grid));
        snapTo->click();
        QCOMPARE(state.perspectiveSnapLayer(), grid);
        QVERIFY(snapTo->isChecked());
        QVERIFY(sight(grid));

        controller.setCurrentLayer(raster); // a layer that cannot be snapped to: the button rests
        QCoreApplication::processEvents();
        QVERIFY(!snapTo->isEnabled());
        QVERIFY(sight(grid)); // the sight stays on the reference

        controller.setCurrentLayer(grid);
        QCoreApplication::processEvents();
        QVERIFY(snapTo->isChecked());
        snapTo->click(); // pressed again
        QCOMPARE(state.perspectiveSnapLayer(), doc::kNoId);
        QVERIFY(!sight(grid));

        // At most one layer is snapped to: snapping to another lets go of the first.
        snapTo->click();
        QVERIFY(sight(grid));
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId second = controller.currentLayer();
        QVERIFY(second != grid);
        QCoreApplication::processEvents();
        QVERIFY(!snapTo->isChecked()); // not this one
        QVERIFY(sight(grid) && !sight(second));
        snapTo->click();
        QCOMPARE(state.perspectiveSnapLayer(), second);
        QVERIFY(sight(second));
        QVERIFY(!sight(grid));
        controller.setCurrentLayer(grid);
        QCoreApplication::processEvents();
        QVERIFY(!snapTo->isChecked());
        snapTo->click();
        QCOMPARE(state.perspectiveSnapLayer(), grid);
        QVERIFY(sight(grid) && !sight(second));
    }

    // Lines of the grid partly behind the eye are cut at the near limit; the cut point used to round to just
    // behind it and the whole line was lost (seen with a 120° lens).
    void perspectiveGridKeepsLinesCutByTheNearLimit()
    {
        for (double tilt : {45.0, 60.0, 75.0})
            for (double turn = 0; turn < 360; turn += 15) {
                perspective::Grid grid = perspective::defaultGrid(1920, 1080);
                grid.fov = 120.0;
                grid.orientation = perspective::fromAngles(turn, tilt, 0.0);
                const auto segments = perspective::gridSegments(grid);
                // A line is kept when any part of it is in front of the eye: count them the slow way.
                int expected = 0;
                const int n = grid.cells;
                const double f = perspective::focalLength(grid), extent = n * grid.cell;
                auto in_front = [&](double x0, double y0, double x1, double y1) {
                    for (int s = 0; s <= 2000; ++s) {
                        const double t = s / 2000.0;
                        if (perspective::project(grid, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
                            return true;
                    }
                    return false;
                };
                for (int i = -n; i <= n; ++i) {
                    expected += in_front(i * grid.cell, -extent, i * grid.cell, extent);
                    expected += in_front(-extent, i * grid.cell, extent, i * grid.cell);
                }
                (void)f;
                QCOMPARE(int(segments.size()), expected);
            }
    }

    // The next axis, in order, and the stroke in progress drawn again on it: the A key. The guide (an outline
    // and a letter) is in the color of the axis the stroke is on.
    void perspectiveSnapAxisCanBeCycledMidStroke()
    {
        const perspective::Grid grid = perspective::defaultGrid(1920, 1080);
        canvas::PerspectiveSnap snap;
        snap.begin(grid, QPointF(900, 500), 10.0);
        QCOMPARE(snap.cycle(), 0); // undecided: the first
        QCOMPARE(snap.axis(), 0);
        QCOMPARE(snap.cycle(), 1);
        QCOMPARE(snap.cycle(), 2);
        QCOMPARE(snap.cycle(), 0);
        QVERIFY(canvas::PerspectiveSnap::axisColor(0) != canvas::PerspectiveSnap::axisColor(1));
        QCOMPARE(canvas::PerspectiveSnap::axisLetter(2), QChar('Z'));

        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::black);
        state.setSize(tools::ToolId::Brush, 12.0);
        auto toView = [&](const QPointF &p) { return canvas.view().documentToView().map(p).toPoint(); };
        auto drag = [&](const QPointF &from, const QPointF &to, int steps) {
            const QPoint a = toView(from), b = toView(to);
            for (int i = 1; i <= steps; ++i)
                QTest::mouseMove(&canvas, a + (b - a) * i / steps);
        };
        QTest::mousePress(&canvas, Qt::LeftButton, {}, toView({100, 100}));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, toView({120, 100}));
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId gridLayer = controller.currentLayer();
        controller.setCurrentLayer(raster);
        const perspective::Grid g = controller.perspectiveLayerById(gridLayer)->grid;
        state.setPerspectiveSnap(true);
        state.setPerspectiveSnapLayer(gridLayer);
        const doc::Layer *drawn = nullptr;
        for (const doc::Layer &l : controller.currentDrawing()->layers)
            if (l.id == raster)
                drawn = &l;
        QVERIFY(drawn);
        auto alpha = [&](const QPointF &p) { return qAlpha(drawn->image.pixel(int(p.x()), int(p.y()))); };
        // Pixels of the guide in the color of an axis (the grabbed canvas, the sign of the color is enough).
        auto guidePixels = [&](int axis) {
            const QImage shot = canvas.grab().toImage();
            const QColor c = canvas::PerspectiveSnap::axisColor(axis);
            int n = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor p = shot.pixelColor(x, y);
                    n += std::abs(p.red() - c.red()) + std::abs(p.green() - c.green()) + std::abs(p.blue() - c.blue()) < 40;
                }
            return n;
        };

        const QPointF start(900, 500);
        const auto dirs = canvas::PerspectiveSnap::directions(g, start);
        QVERIFY(dirs[0] && dirs[1]);
        const QPointF u0 = *dirs[0], u1 = *dirs[1];
        QVERIFY(std::abs(u0.x() * u1.y() - u0.y() * u1.x()) > 0.3); // the axes are well apart here
        const double a = 4 * std::numbers::pi / 180;
        const QPointF off(u0.x() * std::cos(a) - u0.y() * std::sin(a), u0.x() * std::sin(a) + u0.y() * std::cos(a));

        QTest::mousePress(&canvas, Qt::LeftButton, {}, toView(start));
        drag(start, start + off * 120, 8);
        QVERIFY(alpha(start + u0 * 60) > 0); // on the first axis
        QVERIFY(guidePixels(0) > 20);        // in its color
        QCOMPARE(guidePixels(1), 0);

        QTest::keyClick(&canvas, Qt::Key_A); // the next axis
        QCOMPARE(alpha(start + u0 * 60), 0);  // what was drawn on the first is gone
        QVERIFY(guidePixels(1) > 20);
        QCOMPARE(guidePixels(0), 0);
        drag(start + off * 120, start + off * 200, 4);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, toView(start + off * 200));
        // The stroke ends along the second axis, from the start (either way along it).
        bool inked = false;
        for (double t = -200; t <= 200; t += 5)
            inked = inked || alpha(start + u1 * t) > 0;
        QVERIFY(inked);
        QCOMPARE(alpha(start + u0 * 60), 0);
        QCOMPARE(guidePixels(1), 0); // the guide is gone with the stroke
    }

    // A perspective layer is found by its id, in any frame.
    void perspectiveLayersAreFoundById()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("x"));
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(!controller.perspectiveLayerById(doc::kNoId));
        QVERIFY(!controller.perspectiveLayerById(raster)); // a layer of pixels is not one
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId grid = controller.currentLayer();
        const auto found = controller.perspectiveLayerById(grid);
        QVERIFY(found);
        QCOMPARE(found->ref.layer, grid);
        controller.setCurrentFrame(5); // wherever the timeline is
        QVERIFY(controller.perspectiveLayerById(grid));
        QVERIFY(!controller.perspectiveLayerById(99999));
    }

    // The tool buttons are laid out in as many columns as the width allows, two to begin with.
    void toolPaletteReflowsWithItsWidth()
    {
        tools::ToolState state;
        ui::ToolPalettePanel palette(&state);
        auto columns = [&palette] {
            std::set<int> xs;
            for (auto *b : palette.findChildren<QToolButton *>())
                xs.insert(b->x());
            return int(xs.size());
        };
        palette.resize(palette.sizeHint());
        palette.show();
        QVERIFY(QTest::qWaitForWindowExposed(&palette));
        QCOMPARE(columns(), 2); // by default
        palette.resize(40, 600);
        QCOMPARE(columns(), 1);
        palette.resize(140, 600);
        QVERIFY(columns() >= 3);
        // Narrower, the buttons need more height; the panel asks for it (it scrolls in its dock).
        palette.resize(40, 100);
        const int tall = palette.minimumHeight();
        palette.resize(140, 100);
        QVERIFY(palette.minimumHeight() < tall);
        for (auto *b : palette.findChildren<QToolButton *>())
            QVERIFY(b->geometry().right() < palette.width()); // none spills over the side
    }

    // The Rich Presence speaks Discord's socket protocol: a handshake with the application id, then
    // SET_ACTIVITY with the frame and the start time, again when the frame changes (not too often); a
    // ping is answered; and it keeps looking for Discord when it is not there yet.
    void discordPresenceTalksToDiscord()
    {
        const QString name = QStringLiteral("mothion-test-discord-%1").arg(QCoreApplication::applicationPid());
        QLocalServer::removeServer(name);
        document::DocumentController controller;
        ui::DiscordPresence presence(&controller, QStringLiteral("123456789"));
        presence.setCandidatePaths({QStringLiteral("mothion-test-nobody-here"), name});
        presence.setMinimumUpdateInterval(60);
        presence.setRetryInterval(40);
        presence.start();
        QVERIFY(!presence.isConnected()); // Discord is not running yet

        // Discord starts later.
        QLocalServer server;
        QVERIFY(server.listen(name));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        QLocalSocket *client = server.nextPendingConnection();
        QByteArray received;
        struct Message
        {
            quint32 opcode;
            QJsonObject json;
            QByteArray raw;
        };
        auto next = [&]() -> Message {
            auto have = [&] {
                client->waitForReadyRead(0);
                received += client->readAll();
                return received.size() >= 8 && quint32(received.size()) >= 8 + qFromLittleEndian<quint32>(received.constData() + 4);
            };
            if (!QTest::qWaitFor(have, 3000))
                return {quint32(-1), {}, {}};
            const quint32 opcode = qFromLittleEndian<quint32>(received.constData());
            const quint32 length = qFromLittleEndian<quint32>(received.constData() + 4);
            const QByteArray raw = received.mid(8, int(length));
            received.remove(0, 8 + int(length));
            return {opcode, QJsonDocument::fromJson(raw).object(), raw};
        };
        auto send = [&](quint32 opcode, const QByteArray &json) {
            QByteArray message(8, '\0');
            qToLittleEndian<quint32>(opcode, message.data());
            qToLittleEndian<quint32>(quint32(json.size()), message.data() + 4);
            client->write(message + json);
            client->flush();
        };

        const Message hello = next();
        QCOMPARE(hello.opcode, quint32(0));
        QCOMPARE(hello.json.value(QStringLiteral("client_id")).toString(), QStringLiteral("123456789"));
        QCOMPARE(hello.json.value(QStringLiteral("v")).toInt(), 1);
        QVERIFY(!presence.isConnected()); // not until Discord says it is ready
        send(1, R"({"cmd":"DISPATCH","evt":"READY","data":{}})");
        QTRY_VERIFY(presence.isConnected());

        const Message first = next();
        QCOMPARE(first.opcode, quint32(1));
        QCOMPARE(first.json.value(QStringLiteral("cmd")).toString(), QStringLiteral("SET_ACTIVITY"));
        QVERIFY(!first.json.value(QStringLiteral("nonce")).toString().isEmpty());
        const QJsonObject args = first.json.value(QStringLiteral("args")).toObject();
        QCOMPARE(args.value(QStringLiteral("pid")).toInteger(), qint64(QCoreApplication::applicationPid()));
        const QJsonObject activity = args.value(QStringLiteral("activity")).toObject();
        QVERIFY(activity.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("Frame 1 / ")));
        const qint64 start = activity.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger();
        QVERIFY(std::abs(start - QDateTime::currentSecsSinceEpoch()) < 30);

        // The frame changes: sent again (the timer keeps the original start).
        controller.setCurrentFrame(4);
        controller.setCurrentFrame(5);
        const Message second = next();
        const QJsonObject again = second.json.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
        QVERIFY(again.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("Frame 6 / ")));
        QCOMPARE(again.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger(), start);

        // Ping: the same payload comes back as pong.
        send(3, QByteArrayLiteral("hello"));
        const Message pong = next();
        QCOMPARE(pong.opcode, quint32(4));
        QCOMPARE(pong.raw, QByteArrayLiteral("hello"));

        // Discord goes away: it is not connected, and finds it again when it is back.
        client->disconnectFromServer();
        QTRY_VERIFY(!presence.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        QVERIFY(server.nextPendingConnection());
    }

    // Without an application id the presence does nothing at all.
    void discordPresenceNeedsAnApplicationId()
    {
        document::DocumentController controller;
        ui::DiscordPresence presence(&controller, QString());
        presence.setCandidatePaths({QStringLiteral("mothion-test-nobody-here")});
        presence.start();
        controller.setCurrentFrame(3);
        QVERIFY(!presence.isConnected());
    }

    // "Flip" shows the animation mirrored left to right, in the window only.
    void autoplayFlipsHorizontally()
    {
        document::DocumentController controller;
        QImage image(200, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("red"));
        controller.clearSelection();
        // The red is moved to the left of the sheet: nothing is symmetric.
        QImage bar(100, 100, QImage::Format_ARGB32);
        bar.fill(Qt::blue);
        controller.setCurrentFrame(0);
        QVERIFY(controller.pasteImage(bar, QPoint(100, 450)));
        controller.clearSelection();

        ui::AutoplayWindow window(&controller);
        window.resize(720, 500);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QPushButton *>(QStringLiteral("autoplayPlay"))->setChecked(false);
        QPushButton *flip = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Flip"))
                flip = button;
        QVERIFY(flip && flip->isCheckable());

        // Where the blue is, seen from the window: left of the middle, then right of it.
        auto blueSide = [&] {
            const QImage shot = window.grab().toImage();
            long left = 0, right = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c.blue() > 200 && c.red() < 60 && c.green() < 60)
                        (x < shot.width() / 2 ? left : right) += 1;
                }
            return std::pair{left, right};
        };
        const auto before = blueSide();
        QVERIFY(before.first > 100 && before.second == 0);
        flip->click();
        const auto flipped = blueSide();
        QVERIFY(flipped.second > 100 && flipped.first == 0);
        QCOMPARE(controller.document().cel(controller.currentCel())->drawingAt(0)->layers.front().image.pixel(150, 500),
                 doc::Pixel(0xff0000ff)); // the drawing is as it was
        flip->click();
        QCOMPARE(blueSide(), before);
    }

    // The loop can be narrowed to a range of frames with the two handles under the slider.
    void autoplayLoopsOnlyARange()
    {
        document::DocumentController controller;
        for (int f = 0; f < 6; ++f) {
            QImage image(40, 40, QImage::Format_ARGB32);
            image.fill(QColor::fromHsv(f * 50, 255, 255));
            controller.setCurrentFrame(f);
            QVERIFY(controller.pasteImage(image, QPoint(940, 520)));
        }
        controller.clearSelection();
        ui::AutoplayWindow window(&controller);
        window.resize(720, 500);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *play = window.findChild<QPushButton *>(QStringLiteral("autoplayPlay"));
        auto *speed = window.findChild<QComboBox *>();
        speed->setCurrentIndex(speed->findData(60.0));
        QCOMPARE(window.frameCount(), 6);
        QCOMPARE(window.loopStart(), 0);
        QCOMPARE(window.loopEnd(), 5);

        window.setLoopRange(2, 4);
        QCOMPARE(window.loopStart(), 2);
        QCOMPARE(window.loopEnd(), 4);
        QTRY_VERIFY(window.frame() >= 2 && window.frame() <= 4); // from wherever it was, into the range
        std::set<int> seen;
        for (int i = 0; i < 40; ++i) {
            QTest::qWait(20);
            seen.insert(window.frame());
        }
        QVERIFY(seen.count(2) && seen.count(3) && seen.count(4)); // the range is played, and only it
        QVERIFY(!seen.count(0) && !seen.count(1) && !seen.count(5));

        // The handles: a drag of the right one, from its place to the left.
        play->setChecked(false);
        window.setLoopRange(0, 5);
        auto *bar = window.findChildren<QWidget *>().back();
        for (QWidget *w : window.findChildren<QWidget *>())
            if (w->height() == 16 && w->cursor().shape() == Qt::SizeHorCursor)
                bar = w;
        QCOMPARE(bar->cursor().shape(), Qt::SizeHorCursor);
        const int inset = 8;
        auto xOf = [&](int frame) { return int(inset + frame * (bar->width() - 2.0 * inset) / 5); };
        QTest::mousePress(bar, Qt::LeftButton, {}, QPoint(xOf(5), 8));
        QTest::mouseMove(bar, QPoint(xOf(3), 8));
        QTest::mouseRelease(bar, Qt::LeftButton, {}, QPoint(xOf(3), 8));
        QCOMPARE(window.loopEnd(), 3);
        QCOMPARE(window.loopStart(), 0);
        QTest::mousePress(bar, Qt::LeftButton, {}, QPoint(xOf(0), 8)); // the left one
        QTest::mouseMove(bar, QPoint(xOf(2), 8));
        QTest::mouseRelease(bar, Qt::LeftButton, {}, QPoint(xOf(2), 8));
        QCOMPARE(window.loopStart(), 2);
        QTest::mousePress(bar, Qt::LeftButton, {}, QPoint(xOf(3), 8)); // never past the other
        QTest::mouseMove(bar, QPoint(xOf(0), 8));
        QTest::mouseRelease(bar, Qt::LeftButton, {}, QPoint(xOf(0), 8));
        QVERIFY(window.loopStart() <= window.loopEnd());
        QTest::mouseDClick(bar, Qt::LeftButton, {}, QPoint(xOf(2), 8)); // all again
        QCOMPARE(window.loopStart(), 0);
        QCOMPARE(window.loopEnd(), 5);

        // A range that reaches the end follows it as the animation grows.
        window.setLoopRange(1, 5);
        QImage more(40, 40, QImage::Format_ARGB32);
        more.fill(Qt::yellow);
        controller.setCurrentFrame(6);
        QVERIFY(controller.pasteImage(more, QPoint(940, 520)));
        QTRY_COMPARE(window.loopEnd(), 6);
        QCOMPARE(window.loopStart(), 1);
    }

    // , and . step the timeline one time slot back and on, from the canvas or
    // a panel, Shift+, and Shift+. one frame of the cel; + and - change the
    // current frame's hold, but in the puppet's radius mode they stay the
    // radius's on the canvas.
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

        // Shift+, and Shift+. go to the cel's previous and next frame, over
        // holds and empty slots: a at 2, b at 3; nothing after b.
        controller->setCurrentFrame(0);
        QTest::keyClick(canvas, Qt::Key_Period, Qt::ShiftModifier);
        QCOMPARE(controller->currentFrame(), 2);
        QTest::keyClick(canvas, Qt::Key_Period, Qt::ShiftModifier);
        QCOMPARE(controller->currentFrame(), 3);
        QTest::keyClick(canvas, Qt::Key_Period, Qt::ShiftModifier); // none after it
        QCOMPARE(controller->currentFrame(), 3);
        controller->setCurrentFrame(9); // past them, on an empty slot
        QTest::keyClick(canvas, Qt::Key_Comma, Qt::ShiftModifier);
        QCOMPARE(controller->currentFrame(), 3);
        QTest::keyClick(canvas, Qt::Key_Comma, Qt::ShiftModifier);
        QCOMPARE(controller->currentFrame(), 2);
        QTest::keyClick(canvas, Qt::Key_Comma, Qt::ShiftModifier); // none before it
        QCOMPARE(controller->currentFrame(), 2);
        // And from the buttons of the timeline's bar.
        auto *nextFrame = window.findChild<QToolButton *>(QStringLiteral("nextFrame"));
        QVERIFY(nextFrame && window.findChild<QToolButton *>(QStringLiteral("previousFrame")));
        nextFrame->click();
        QCOMPARE(controller->currentFrame(), 3);
    }

    // Split frame cuts a hold in two independent frames; new frame and
    // insert inbetween move the cel's frames from a point one place on and
    // put a new one there.
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

        // New frame puts one at the point, Insert inbetween right after it;
        // either way the ones after move on and the new one is current.
        controller.setCurrentFrame(2);
        controller.insertFrame(cel, 2, false);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo new frame"));
        const doc::DrawingId made = t.drawingAt(2);
        QVERIFY(made != a.id && made != split.id);
        QCOMPARE(t.drawingAt(3), split.id); // moved on
        QCOMPARE(t.lastFrame(), 5);
        QCOMPARE(controller.currentFrame(), 2);
        QCOMPARE(controller.currentDrawingId(), made);
        controller.insertFrame(cel, 0, true);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo insert inbetween"));
        QCOMPARE(t.drawingAt(0), a.id);
        const doc::DrawingId after = t.drawingAt(1);
        QVERIFY(after != a.id);
        QCOMPARE(t.drawingAt(2), a.id); // the rest of the hold, cut around it
        QCOMPARE(t.drawingAt(3), made);
        QCOMPARE(controller.currentFrame(), 1);
        QCOMPARE(controller.currentDrawingId(), after);
    }

    // Frames selected in the Layers panel, of any cel, go together.
    void layersPanelDeletesSeveralFrames()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 600);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        std::vector<doc::DrawingId> ids;
        for (int f : {0, 1, 2}) {
            doc::NewDrawing nd = doc::makeNewDrawing(d, cel, f, 1, "Line");
            ids.push_back(nd.id);
            controller.execute(std::move(nd.command));
        }
        QCoreApplication::processEvents();
        auto *tree = panel.findChild<QTreeWidget *>();
        auto click = [&](doc::DrawingId id, Qt::KeyboardModifiers mods) {
            const QPoint at = tree->visualItemRect(panel.frameItem(id)).center();
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, mods, at);
        };
        click(ids[0], {});
        click(ids[2], Qt::ControlModifier);
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Frame);
        QToolButton *remove = nullptr;
        for (QToolButton *b : panel.findChildren<QToolButton *>())
            if (b->toolTip() == QStringLiteral("Delete the selected frames"))
                remove = b;
        QVERIFY(remove && remove->isEnabled());
        remove->click();
        QCoreApplication::processEvents();
        QCOMPARE(d.cel(cel)->drawings().size(), std::size_t(1));
        QVERIFY(d.cel(cel)->drawing(ids[1]));
        QCOMPARE(d.cel(cel)->timing().drawingAt(0), doc::kNoId);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo delete frame"));
        controller.undo(); // one step
        QCOMPARE(d.cel(cel)->drawings().size(), std::size_t(3));
        QCOMPARE(d.cel(cel)->timing().drawingAt(2), ids[2]);

        QCoreApplication::processEvents();
        click(ids[1], {});
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Delete); // the key does the same
        QCoreApplication::processEvents();
        QCOMPARE(d.cel(cel)->drawings().size(), std::size_t(2));
        QVERIFY(!d.cel(cel)->drawing(ids[1]));
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

    // Blocks on the timeline are selected with clicks (Ctrl adds, Shift a
    // range of the row) and dragged along the time together, in one step,
    // only onto free frames; copied, they are pasted as they are or as copies.
    void timelineDragsTheSelectedFrames()
    {
        document::DocumentController controller;
        doc::Document &d = controller.documentForCommands();
        const doc::CelId bottom = controller.currentCel();
        controller.addCel();
        const doc::CelId top = controller.currentCel();
        doc::NewDrawing a = doc::makeNewDrawing(d, bottom, 0, 2, "Line");
        controller.execute(std::move(a.command));
        doc::NewDrawing b = doc::makeNewDrawing(d, bottom, 2, 1, "Line");
        controller.execute(std::move(b.command));
        doc::NewDrawing c = doc::makeNewDrawing(d, bottom, 6, 1, "Line");
        controller.execute(std::move(c.command));
        doc::NewDrawing t = doc::makeNewDrawing(d, top, 1, 1, "Line");
        controller.execute(std::move(t.command));

        ui::TimelineView view(&controller);
        view.resize(800, 200);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        ui::TimelineMetrics m;
        auto cell = [&](int row, int frame) { // rows top first
            return QPoint(m.frameX(frame) + m.frameWidth / 2, m.rowY(row) + m.rowHeight / 2);
        };
        auto *vp = view.viewport();
        QTest::mouseClick(vp, Qt::LeftButton, {}, cell(1, 0));
        QCOMPARE(view.selectedFrames(), (std::vector<doc::ExposureRef>{{bottom, 0}}));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, cell(1, 6)); // the row's blocks up to it
        QCOMPARE(view.selectedFrames().size(), std::size_t(3));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, cell(1, 2)); // b out
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, cell(0, 1)); // the top cel's in
        QCOMPARE(view.selectedFrames().size(), std::size_t(3));

        // Dragged three frames on: a, c and the top one move, b stays.
        QTest::mousePress(vp, Qt::LeftButton, {}, cell(1, 0));
        QTest::mouseMove(vp, cell(1, 2));
        QTest::mouseMove(vp, cell(1, 3));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, cell(1, 3));
        const doc::Timing &low = d.cel(bottom)->timing(), &high = d.cel(top)->timing();
        QCOMPARE(low.drawingAt(3), a.id);
        QCOMPARE(low.exposureAt(3)->length, 2);
        QCOMPARE(low.drawingAt(2), b.id);
        QCOMPARE(low.drawingAt(0), doc::kNoId);
        QCOMPARE(low.drawingAt(9), c.id);
        QCOMPARE(high.drawingAt(4), t.id);
        QCOMPARE(controller.currentFrame(), 1); // the cursor stays: where the last click put it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo move frames"));
        QCOMPARE(view.selectedFrames().size(), std::size_t(3)); // where they went
        QVERIFY(std::find(view.selectedFrames().begin(), view.selectedFrames().end(),
                          doc::ExposureRef{bottom, 3}) != view.selectedFrames().end());

        controller.undo(); // all at once
        QCOMPARE(low.drawingAt(0), a.id);
        QCOMPARE(low.drawingAt(6), c.id);
        QCOMPARE(high.drawingAt(1), t.id);

        // A click on an empty cell clears the selection; one on a selected
        // block among several, without dragging, keeps it alone.
        QTest::mouseClick(vp, Qt::LeftButton, {}, cell(1, 0));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, cell(1, 6));
        QTest::mouseClick(vp, Qt::LeftButton, {}, cell(1, 6));
        QCOMPARE(view.selectedFrames(), (std::vector<doc::ExposureRef>{{bottom, 6}}));
        QTest::mouseClick(vp, Qt::LeftButton, {}, cell(1, 12));
        QVERIFY(view.selectedFrames().empty());

        // Onto another block: nothing moves.
        QTest::mouseClick(vp, Qt::LeftButton, {}, cell(1, 6));
        QTest::mousePress(vp, Qt::LeftButton, {}, cell(1, 6));
        QTest::mouseMove(vp, cell(1, 4));
        QTest::mouseMove(vp, cell(1, 2)); // onto b
        QTest::mouseRelease(vp, Qt::LeftButton, {}, cell(1, 2));
        QCOMPARE(low.drawingAt(6), c.id);
        QCOMPARE(low.drawingAt(2), b.id);

        // Copied, then pasted at the cursor: the same drawing, then a copy.
        view.setFocus();
        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        controller.setCurrentFrame(10);
        QTest::keyClick(&view, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(low.drawingAt(10), c.id);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo paste frames"));
        QCOMPARE(view.selectedFrames(), (std::vector<doc::ExposureRef>{{bottom, 10}}));
        controller.setCurrentFrame(12);
        QTest::keyClick(&view, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(low.drawingAt(12) != doc::kNoId && low.drawingAt(12) != c.id);
        const std::size_t drawings = d.cel(bottom)->drawings().size();
        controller.setCurrentFrame(12); // no room: nothing
        QTest::keyClick(&view, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(d.cel(bottom)->drawings().size(), drawings);
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

    // A polygon in progress has a bar with OK and Cancel under its points; OK waits for enough points.
    void shapeBarFinishesOrDrops()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *bar = window.findChild<ui::ShapeBar *>();
        QVERIFY(bar);
        controller->clearSelection();
        state->setActiveTool(tools::ToolId::Shape);
        tools::ShapeSettings shape;
        shape.kind = tools::ShapeSettings::Kind::Polygon;
        shape.style = tools::ShapeSettings::Style::Filled;
        state->setShapeSettings(shape);
        QVERIFY(!bar->isVisible());

        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto click = [&](QPointF doc) {
            QTest::mouseClick(canvas, Qt::LeftButton, {}, at(doc));
            QCoreApplication::processEvents();
        };
        auto *ok = bar->findChild<QToolButton *>(QStringLiteral("ok"));
        auto *cancel = bar->findChild<QToolButton *>(QStringLiteral("cancel"));
        QVERIFY(ok && cancel);
        click({900, 500});
        QVERIFY(bar->isVisible());
        QVERIFY(!ok->isEnabled()); // one point
        click({1000, 500});
        click({1000, 600});
        QVERIFY(ok->isEnabled());
        const QRect points = canvas->view().documentToView().mapRect(canvas->shapeBounds()).toAlignedRect();
        QVERIFY(bar->geometry().top() >= points.bottom() || bar->geometry().bottom() <= points.top());

        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QVERIFY(!controller->canUndo());

        click({900, 500});
        click({1000, 500});
        click({1000, 600});
        QTest::mouseClick(ok, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QCOMPARE(controller->undoText(), QStringLiteral("Undo shape"));
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        QCOMPARE(qAlpha(cel.drawingAt(0)->layers.front().image.pixel(980, 510)), 255);
    }

    // While a transform is open a bar with OK and Cancel floats under its box (the selection's
    // bar gives way); OK applies it, Cancel drops it, either returns to the previous tool.
    void transformBarAppliesOrDrops()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *bar = window.findChild<ui::TransformBar *>();
        auto *selectionBar = window.findChild<ui::SelectionBar *>();
        QVERIFY(bar && selectionBar);
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        QPainterPath area;
        area.addRect(900, 480, 130, 130); // a selection larger than the pixels
        controller->setSelection(area);
        QVERIFY(selectionBar->isVisible());
        QVERIFY(!bar->isVisible());

        state->setActiveTool(tools::ToolId::Transform);
        QCoreApplication::processEvents();
        QCOMPARE(canvas->transformBounds(), QRectF(900, 480, 130, 130)); // the selection's box
        QVERIFY(bar->isVisible());
        QVERIFY(!selectionBar->isVisible());
        const QRect box = canvas->view().documentToView().mapRect(canvas->transformBounds()).toAlignedRect();
        QVERIFY(bar->geometry().top() >= box.bottom() || bar->geometry().bottom() <= box.top()); // not on the box
        auto *ok = bar->findChild<QToolButton *>(QStringLiteral("ok"));
        auto *cancel = bar->findChild<QToolButton *>(QStringLiteral("cancel"));
        QVERIFY(ok && cancel);
        QCOMPARE(ok->text(), QStringLiteral("OK"));
        QCOMPARE(cancel->text(), QStringLiteral("Cancel"));

        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, at(from));
            QTest::mouseMove(canvas, at(to));
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(to));
        };
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        const doc::TiledImage &pixels = cel.drawingAt(0)->layers.front().image;
        drag({965, 545}, {1065, 545}); // moved 100 to the right
        QCOMPARE(pixels.pixel(1060, 545), doc::Pixel(0xffff0000));
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QVERIFY(selectionBar->isVisible());
        QVERIFY(state->activeTool() != tools::ToolId::Transform);
        QCOMPARE(qAlpha(pixels.pixel(1060, 545)), 0); // dropped
        QCOMPARE(pixels.pixel(960, 545), doc::Pixel(0xffff0000));

        state->setActiveTool(tools::ToolId::Transform);
        QVERIFY(bar->isVisible());
        drag({965, 545}, {1065, 545});
        QTest::mouseClick(ok, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QVERIFY(state->activeTool() != tools::ToolId::Transform);
        QCOMPARE(pixels.pixel(1060, 545), doc::Pixel(0xffff0000)); // applied
        QCOMPARE(controller->undoText(), QStringLiteral("Undo transform"));
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

    // Transform and puppet warp take the layers chosen in the Layers panel
    // together: one box, one mesh around all of them, one undo step.
    void transformAndWarpTakeTheChosenLayers()
    {
        document::DocumentController controller;
        tools::ToolState state;
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller.importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        const doc::DrawingId drawing = controller.currentDrawingId();
        const doc::LayerId line = controller.currentLayer();
        // A second layer with a patch at the bar's right end only.
        auto color = std::make_unique<doc::AddLayerCommand>(d, cel, drawing, 1, doc::LayerProperties{"Color"});
        const doc::LayerId colorId = color->layerId();
        controller.execute(std::move(color));
        for (int y = 525; y < 555; ++y)
            for (int x = 1120; x < 1155; ++x)
                d.layer(cel, drawing, colorId)->image.setPixel(x, y, 0xffff0000);
        const doc::TiledImage &linePixels = d.layer(cel, drawing, line)->image;
        const doc::TiledImage &colorPixels = d.layer(cel, drawing, colorId)->image;
        controller.setLayerSelection({{cel, drawing, line}, {cel, drawing, colorId}});

        canvas::TransformTool transform(&controller, &state, [](const QString &) {}, [] {});
        transform.activated();
        QCOMPARE(transform.box(), QRect(760, 520, 400, 40)); // around both
        transform.press({900, 540}, Qt::NoModifier);
        transform.release({900, 640}, Qt::NoModifier); // 100 px down
        QVERIFY(transform.keyPress(Qt::Key_Return));
        QCOMPARE(qAlpha(linePixels.pixel(800, 540)), 0);
        QVERIFY(qAlpha(linePixels.pixel(800, 640)) > 0);
        QCOMPARE(qAlpha(colorPixels.pixel(1130, 540)), 0);
        QVERIFY(qAlpha(colorPixels.pixel(1130, 640)) > 0);
        controller.undo(); // both at once
        QVERIFY(qAlpha(linePixels.pixel(800, 540)) > 0);
        QVERIFY(qAlpha(colorPixels.pixel(1130, 540)) > 0);

        canvas::PuppetTool warp(&controller, &state, [](const QString &) {}, [] {});
        warp.press({780, 540}, Qt::NoModifier); // anchor at the left end
        warp.release({780, 540}, Qt::NoModifier);
        warp.press({1140, 540}, Qt::NoModifier); // the right end, down by 120 px
        for (int step = 1; step <= 10; ++step)
            warp.move({1140, 540.0 + 12 * step}, Qt::NoModifier);
        warp.release({1140, 660}, Qt::NoModifier);
        warp.commit();
        QVERIFY(qAlpha(linePixels.pixel(780, 540)) > 0); // the anchored end stays
        QCOMPARE(qAlpha(colorPixels.pixel(1140, 540)), 0); // the patch went along
        QVERIFY(qAlpha(colorPixels.pixel(1130, 655)) > 0);
        QVERIFY(qAlpha(linePixels.pixel(1130, 655)) > 0);
        controller.undo();
        QVERIFY(qAlpha(colorPixels.pixel(1140, 540)) > 0);
        QCOMPARE(qAlpha(linePixels.pixel(1130, 655)), 0);
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

    // The puppet warp has an OK / Cancel bar under the warped area once there is something to apply
    // (a pin placed); OK applies the warp, Cancel drops it, as Enter and Esc do.
    void puppetBarAppliesOrDrops()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *bar = window.findChild<ui::PuppetBar *>();
        QVERIFY(bar);
        QImage image(400, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        controller->importImage(image, QStringLiteral("bar")); // x 760..1159, y 520..559
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        const doc::Pixel ink = pixel(960, 540);

        state->setActiveTool(tools::ToolId::Puppet);
        QCoreApplication::processEvents();
        QVERIFY(!bar->isVisible()); // nothing to apply yet
        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto click = [&](QPointF p) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, at(p));
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(p));
        };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, at(from));
            QTest::mouseMove(canvas, at(to));
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(to));
        };
        click({780, 540}); // the first pin
        QVERIFY(bar->isVisible());
        const QRect shown = canvas->view().documentToView().mapRect(canvas->puppetBounds()).toAlignedRect();
        QVERIFY(bar->geometry().top() >= shown.bottom() || bar->geometry().bottom() <= shown.top());
        auto *ok = bar->findChild<QToolButton *>(QStringLiteral("ok"));
        auto *cancel = bar->findChild<QToolButton *>(QStringLiteral("cancel"));
        QVERIFY(ok && cancel);

        click({1140, 540});
        drag({1140, 540}, {1140, 640});
        QCOMPARE(qAlpha(pixel(1140, 540)), 0); // bent
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCoreApplication::processEvents();
        QCOMPARE(pixel(1140, 540), ink); // dropped
        QVERIFY(!bar->isVisible());

        click({780, 540});
        click({1140, 540});
        drag({1140, 540}, {1140, 640});
        QTest::mouseClick(ok, Qt::LeftButton);
        QCoreApplication::processEvents();
        QCOMPARE(qAlpha(pixel(1140, 540)), 0); // applied: stays bent
        QVERIFY(!bar->isVisible());
        QVERIFY(controller->canUndo());
    }

    // Ctrl+C and Ctrl+V copy the drawing's pixels from the timeline too (the focus often stays there after a
    // frame change): with no block selected Ctrl+C is the window's copy, and Ctrl+V after it pastes the pixels at the
    // current frame, not the blocks (nothing copied: an error sound).
    void pastePixelsWithTheTimelineFocused()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        auto *controller = window.findChild<document::DocumentController *>();
        auto *timeline = window.findChild<ui::TimelineView *>();
        QVERIFY(timeline);
        QImage image(60, 60, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("a")); // 930..989, 510..569
        const doc::CelId cel = controller->currentCel();
        controller->insertFrame(cel, 1, false);
        controller->setCurrentFrame(0);
        controller->selectAll();
        timeline->setFocus();
        QVERIFY(timeline->selectedFrames().empty());
        QTest::keyClick(timeline, Qt::Key_C, Qt::ControlModifier); // the window's copy: the pixels
        controller->clearSelection();
        controller->setCurrentFrame(1);
        QTest::keyClick(timeline, Qt::Key_V, Qt::ControlModifier);
        const doc::Cel *c = controller->document().cel(cel);
        const doc::Drawing *second = c->drawingAt(1);
        QVERIFY(second);
        QCOMPARE(qAlpha(second->layers.front().image.pixel(960, 540)), 255);
    }

    // The keyboard focus follows the pointer onto the canvas, the timeline and the layer rows, but not out of a
    // field being typed in.
    void focusFollowsThePointer()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *timeline = window.findChild<ui::TimelineView *>();
        auto enter = [](QWidget *w) {
            QEnterEvent event(QPointF(5, 5), QPointF(5, 5), w->mapToGlobal(QPointF(5, 5)));
            QApplication::sendEvent(w, &event);
        };
        timeline->setFocus();
        QVERIFY(timeline->hasFocus());
        enter(canvas);
        QVERIFY(canvas->hasFocus());
        enter(timeline);
        QVERIFY(timeline->hasFocus());

        QLineEdit field(&window); // a field being typed in keeps the focus
        field.show();
        field.setFocus();
        QVERIFY(field.hasFocus());
        enter(canvas);
        QVERIFY(field.hasFocus());
    }

    // The two colors keep their places: a click on one selects it (the wheel then edits it, the other is
    // what a two-color brush uses), nothing swaps.
    void colorSwatchesSelectInsteadOfSwapping()
    {
        tools::ToolState state;
        ui::ColorWheelPanel panel(&state);
        panel.show();
        auto *swatches = panel.findChild<ui::ColorSwatchPair *>();
        QVERIFY(swatches);
        state.setColor(Qt::red);
        state.setSecondaryColor(Qt::blue);
        QVERIFY(!state.secondarySelected());
        QTest::mouseClick(swatches, Qt::LeftButton, {}, QPoint(40, 40)); // the secondary, behind
        QVERIFY(state.secondarySelected());
        QCOMPARE(state.color(), QColor(Qt::blue));
        QCOMPARE(state.mainColor(), QColor(Qt::red));
        state.setColor(Qt::green); // the wheel's edit goes to the selected one
        QCOMPARE(state.secondaryColor(), QColor(Qt::green));
        QCOMPARE(state.mainColor(), QColor(Qt::red));
        QCOMPARE(state.inactiveColor(), QColor(Qt::red));
        QTest::mouseClick(swatches, Qt::LeftButton, {}, QPoint(8, 8)); // the main, in front
        QVERIFY(!state.secondarySelected());
        QCOMPARE(state.color(), QColor(Qt::red));
        QCOMPARE(state.secondaryColor(), QColor(Qt::green));
        QCOMPARE(state.inactiveColor(), QColor(Qt::green));
    }

    // With deferred rendering, the samples of a drag are drawn once per turn of the event loop at the latest
    // transform (the box follows at once), and the release draws what is pending.
    void transformDrawsOncePerTurnWhileDragging()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        int repaints = 0;
        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [&] { ++repaints; });
        tool.setDeferredRendering(true);
        tool.setViewZoom(1.0);
        tool.activated();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto alpha = [&](int x, int y) { return qAlpha(cel.drawingAt(0)->layers.front().image.pixel(x, y)); };
        QVERIFY(alpha(960, 540) > 0);
        QCOMPARE(alpha(1100, 540), 0);

        tool.press({960, 540}, Qt::NoModifier); // inside: moves
        tool.move({1000, 540}, Qt::NoModifier);
        tool.move({1050, 540}, Qt::NoModifier);
        tool.move({1100, 540}, Qt::NoModifier); // three samples, nothing drawn yet
        QCOMPARE(tool.transform().dx(), 140.0); // the box is already there
        QCOMPARE(alpha(1100, 540), 0);
        QCoreApplication::processEvents(); // the turn: once, at the last
        QVERIFY(alpha(1100, 540) > 0);
        QCOMPARE(alpha(960, 540), 0);

        tool.move({1200, 540}, Qt::NoModifier); // pending when the button goes up
        tool.release({1200, 540}, Qt::NoModifier);
        QVERIFY(alpha(1200, 540) > 0); // drawn at the release, not left for later
        QCOMPARE(alpha(1100, 540), 0);
        tool.cancel();
    }

    // The keys of the commands live in Shortcuts: defaults, the person's own on top (saved), conflicts, and what
    // is bound follows a change.
    void shortcutsAreRemappable()
    {
        ui::Shortcuts &keys = ui::Shortcuts::instance();
        keys.resetAll();
        const QString brush = QStringLiteral("tool.brush");
        QCOMPARE(keys.key(brush), QKeySequence(Qt::Key_B));
        QVERIFY(!keys.isCustom(brush));
        for (const ui::ShortcutCommand &c : keys.commands()) // every command has a name and a group
            QVERIFY(!c.text.isEmpty() && !c.category.isEmpty());
        QCOMPARE(keys.conflictFor(QKeySequence(Qt::Key_B), QStringLiteral("tool.eraser")), brush);
        QCOMPARE(keys.conflictFor(QKeySequence(Qt::Key_B), brush), QString()); // its own key is no conflict
        QVERIFY(keys.conflictFor(QKeySequence(Qt::Key_F9), brush).isEmpty());

        QAction action;
        keys.bind(&action, QStringLiteral("file.export"));
        QCOMPARE(action.shortcuts(), QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_E)});
        QSignalSpy changed(&keys, &ui::Shortcuts::changed);
        keys.setKeys(QStringLiteral("file.export"), {QKeySequence(Qt::CTRL | Qt::Key_F9), QKeySequence(Qt::Key_F9)});
        QCOMPARE(changed.count(), 1);
        QVERIFY(keys.isCustom(QStringLiteral("file.export")));
        QCOMPARE(action.shortcuts().size(), 2); // the bound action followed
        QCOMPARE(keys.conflictFor(QKeySequence(Qt::Key_F9), brush), QStringLiteral("file.export"));

        keys.setKeys(QStringLiteral("file.export"), {}); // no keys at all, kept
        QVERIFY(keys.isCustom(QStringLiteral("file.export")) && action.shortcuts().isEmpty());
        keys.setKeys(QStringLiteral("file.export"), keys.command(QStringLiteral("file.export"))->defaults);
        QVERIFY(!keys.isCustom(QStringLiteral("file.export"))); // the defaults again: nothing kept
        QCOMPARE(action.shortcuts().size(), 1);

        // A tool's key: the palette takes the new one.
        tools::ToolState state;
        ui::ToolPalettePanel palette(&state);
        palette.show();
        palette.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&palette));
        state.setActiveTool(tools::ToolId::Hand);
        QTest::keyClick(&palette, Qt::Key_B);
        QCOMPARE(state.activeTool(), tools::ToolId::Brush); // the default key works
        keys.setKeys(brush, {QKeySequence(Qt::Key_J)});
        state.setActiveTool(tools::ToolId::Hand);
        QTest::keyClick(&palette, Qt::Key_B);
        QCOMPARE(state.activeTool(), tools::ToolId::Hand); // not any more
        QTest::keyClick(&palette, Qt::Key_J);
        QCOMPARE(state.activeTool(), tools::ToolId::Brush);
        keys.resetAll();
    }

    // The preferences keep their values (clamped), say when they change, and go back to the defaults.
    void preferencesChangeAndReset()
    {
        tools::Preferences &prefs = tools::Preferences::instance();
        prefs.resetAll();
        QCOMPARE(prefs.zoomFactor(), 1.25);
        QCOMPARE(prefs.rotateStep(), 15);
        QVERIFY(prefs.discordEnabled() && prefs.brushOutline());
        QSignalSpy changed(&prefs, &tools::Preferences::changed);
        prefs.setZoomStepPercent(50);
        QCOMPARE(prefs.zoomFactor(), 1.5);
        prefs.setZoomStepPercent(50); // the same: nothing to say
        QCOMPARE(changed.count(), 1);
        prefs.setRotateStep(500);
        QCOMPARE(prefs.rotateStep(), 90); // clamped
        prefs.setRecentLimit(0);
        QCOMPARE(prefs.recentLimit(), 1);
        prefs.resetAll();
        QCOMPARE(prefs.zoomFactor(), 1.25);
        QCOMPARE(prefs.recentLimit(), 10);
    }

    // The Discord presence can be switched off (disconnected, quiet) and on again.
    void discordPresenceCanBeDisabled()
    {
        document::DocumentController controller;
        ui::DiscordPresence presence(&controller, QStringLiteral("123"));
        presence.setCandidatePaths({});
        QVERIFY(presence.isEnabled());
        presence.setEnabled(false);
        QVERIFY(!presence.isEnabled() && !presence.isConnected());
        presence.start(); // nothing while it is off
        presence.setEnabled(true);
        QVERIFY(presence.isEnabled());
    }

    // File > Settings (and nowhere else) opens the window; what is set in it applies at once.
    void settingsWindowAppliesAtOnce()
    {
        tools::Preferences &prefs = tools::Preferences::instance();
        prefs.resetAll();
        ui::MainWindow window;
        QAction *settings = nullptr;
        for (QAction *a : window.menuBar()->actions())
            if (a->menu() && a->menu()->title().contains(QStringLiteral("File")))
                for (QAction *item : a->menu()->actions())
                    if (item->text().startsWith(QStringLiteral("Settings")))
                        settings = item;
        QVERIFY(settings);
        QVERIFY(settings->shortcuts().isEmpty()); // no key: only from the menu

        ui::SettingsDialog dialog;
        auto *discord = dialog.findChild<QCheckBox *>(QStringLiteral("discord"));
        auto *zoom = dialog.findChild<QSpinBox *>(QStringLiteral("zoomStep"));
        auto *outline = dialog.findChild<QCheckBox *>(QStringLiteral("brushOutline"));
        auto *commands = dialog.findChild<QTreeWidget *>(QStringLiteral("commands"));
        QVERIFY(discord && zoom && outline && commands);
        QVERIFY(discord->isChecked());
        discord->setChecked(false);
        QVERIFY(!prefs.discordEnabled());
        zoom->setValue(40);
        QCOMPARE(prefs.zoomStepPercent(), 40);
        outline->setChecked(false);
        QVERIFY(!prefs.brushOutline());

        int rows = 0; // every command is listed, under its group
        for (int i = 0; i < commands->topLevelItemCount(); ++i)
            rows += commands->topLevelItem(i)->childCount();
        QCOMPARE(rows, int(ui::Shortcuts::instance().commands().size()));
        prefs.resetAll();
    }

    // A key given to one command is taken off the one that had it.
    void assigningAShortcutTakesItFromTheOther()
    {
        ui::Shortcuts &keys = ui::Shortcuts::instance();
        keys.resetAll();
        keys.assign(QStringLiteral("tool.eraser"), QKeySequence(Qt::Key_B)); // the brush's
        QCOMPARE(keys.key(QStringLiteral("tool.eraser")), QKeySequence(Qt::Key_B));
        QVERIFY(keys.keys(QStringLiteral("tool.brush")).isEmpty()); // lost it
        QVERIFY(keys.isCustom(QStringLiteral("tool.brush")));
        keys.resetAll();
        QCOMPARE(keys.key(QStringLiteral("tool.brush")), QKeySequence(Qt::Key_B));
        QCOMPARE(keys.key(QStringLiteral("tool.eraser")), QKeySequence(Qt::Key_E));
    }

    // The default keys of the standard commands are the usual ones, not the platform's dedicated keys for
    // them (a "New" or "Paste" key some keyboards have), which would only clutter the list of shortcuts.
    void standardShortcutsHaveNoDedicatedKeys()
    {
        const Qt::Key dedicated[] = {Qt::Key_New, Qt::Key_Open, Qt::Key_Save, Qt::Key_Undo, Qt::Key_Redo,
                                     Qt::Key_Cut, Qt::Key_Copy, Qt::Key_Paste, Qt::Key_Exit};
        for (const char *id : {"file.new", "file.open", "file.save", "edit.undo", "edit.redo", "edit.cut", "edit.copy",
                               "edit.paste", "file.quit"}) {
            const QList<QKeySequence> keys = ui::Shortcuts::instance().command(QLatin1String(id))->defaults;
            QVERIFY2(!keys.isEmpty(), id);
            for (const QKeySequence &k : keys)
                for (Qt::Key bad : dedicated)
                    QVERIFY2(k[0].key() != bad, id);
        }
    }

    // File > New follows the new-project preferences; the undo history keeps as many steps as chosen; a mouse
    // draws with the chosen pressure.
    void projectUndoAndMousePreferencesApply()
    {
        tools::Preferences &prefs = tools::Preferences::instance();
        prefs.resetAll();
        document::DocumentController controller;
        QCOMPARE(controller.document().width(), 1920);
        prefs.setNewSize(640, 360);
        prefs.setNewFrameRate(12);
        prefs.setNewFrames(30);
        prefs.setPaperColor(QColor(10, 20, 30));
        controller.newDocument();
        QCOMPARE(controller.document().width(), 640);
        QCOMPARE(controller.document().height(), 360);
        QCOMPARE(controller.document().frameCount(), 30);
        QCOMPARE(controller.document().frameRate().fps(), 12.0);
        QCOMPARE(QColor::fromRgba(controller.document().paperColor()), QColor(10, 20, 30));

        prefs.setUndoSteps(10);
        for (int i = 0; i < 14; ++i)
            controller.addCel();
        int undone = 0;
        while (controller.canUndo()) {
            controller.undo();
            ++undone;
        }
        QCOMPARE(undone, 10);

        prefs.setMousePressurePercent(50);
        const QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::LeftButton,
                                Qt::NoModifier);
        const auto sample = input::toPenSample(press);
        QVERIFY(sample);
        QCOMPARE(sample->pressure, 0.5f);
        prefs.resetAll();
    }

    // With a drawing tool and the outline on, the canvas shows no system pointer (the brush's outline is the
    // pointer); with the outline off, or another tool, it does.
    void paintToolsHideTheSystemPointer()
    {
        tools::Preferences &prefs = tools::Preferences::instance();
        prefs.resetAll();
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        QVERIFY(canvas.cursor().pixmap().hasAlphaChannel());
        QCOMPARE(qAlpha(canvas.cursor().pixmap().toImage().pixel(4, 4)), 0); // transparent
        state.setActiveTool(tools::ToolId::Eraser);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        state.setActiveTool(tools::ToolId::Fill);
        QVERIFY(canvas.cursor().shape() != Qt::BitmapCursor || !canvas.cursor().pixmap().isNull());
        // The zoom tool has a magnifying glass of its own (not the arrows of a horizontal resize).
        state.setActiveTool(tools::ToolId::Zoom);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        QVERIFY(!canvas.cursor().pixmap().isNull());
        QCOMPARE(canvas.cursor().hotSpot(), QPoint(13, 13));
        // Something else changed the cursor of the window (a dock separator, a panel): the pen or mouse seen on the
        // canvas puts the canvas' own back, without waiting for the next enter.
        QWindow *handle = canvas.window()->windowHandle();
        QVERIFY(handle);
        state.setActiveTool(tools::ToolId::Brush);
        handle->setCursor(Qt::SplitHCursor);
        QTest::mouseMove(&canvas, QPoint(50, 50));
        QCOMPARE(handle->cursor().shape(), Qt::BitmapCursor);
        handle->setCursor(Qt::ArrowCursor);
        QTest::mouseMove(&canvas, QPoint(60, 60));
        QCOMPARE(handle->cursor().shape(), Qt::BitmapCursor);
        prefs.setBrushOutline(false);
        state.setActiveTool(tools::ToolId::Brush);
        QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor); // the pointer is back
        prefs.setBrushOutline(true);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        prefs.resetAll();
    }

    // An image sequence from files comes in as one new cel with a drawing for each image, in name order (2
    // before 10), `hold` frames each from the start frame; the scene grows to fit; one undo step; a file that
    // is no image is skipped.
    void imageSequenceImportsAsOneCel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const char *name, QColor color) {
            QImage image(40, 30, QImage::Format_ARGB32);
            image.fill(color);
            const QString path = dir.filePath(QLatin1String(name));
            image.save(path); // read back below: a file that did not save would not be imported
            return path;
        };
        QStringList files;
        files << write("walk_10.png", Qt::blue) << write("walk_2.png", Qt::green) << write("walk_1.png", Qt::red);
        QFile bad(dir.filePath(QStringLiteral("walk_3.png"))); // named like an image, is not one
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();
        files << bad.fileName();

        ui::MainWindow window;
        auto *controller = window.findChild<document::DocumentController *>();
        const int cels = controller->document().celCount();
        QCOMPARE(controller->document().frameCount(), 120);
        QCOMPARE(window.importSequenceFiles(files, QStringLiteral("walk"), 118, 2), 3);
        QCOMPARE(controller->document().celCount(), cels + 1);
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        QCOMPARE(QString::fromStdString(cel.properties().name), QStringLiteral("walk"));
        QCOMPARE(cel.drawings().size(), std::size_t(3));
        auto colorAt = [&](int frame) {
            const doc::Drawing *d = cel.drawingAt(frame);
            return d ? QColor::fromRgba(d->layers.front().image.pixel(960, 540)) : QColor();
        };
        QCOMPARE(colorAt(118), QColor(Qt::red)); // walk_1, then walk_2, then walk_10 (not walk_10 first)
        QCOMPARE(colorAt(119), QColor(Qt::red)); // held for two frames
        QCOMPARE(colorAt(120), QColor(Qt::green));
        QCOMPARE(colorAt(122), QColor(Qt::blue));
        QVERIFY(!cel.drawingAt(124));
        QCOMPARE(controller->document().frameCount(), 124); // the scene grew to hold it
        QCOMPARE(controller->undoText(), QStringLiteral("Undo import image sequence"));
        controller->undo();
        QCOMPARE(controller->document().celCount(), cels);
    }

    // A folder's images, whole: the image files directly in it (not other files, not in subfolders).
    void sequenceFromAFolderTakesItsImages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(Qt::red);
        for (const char *name : {"a_1.png", "a_2.png", "b.jpg"})
            image.save(dir.filePath(QLatin1String(name)));
        QFile text(dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.close();
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("sub")));
        image.save(dir.filePath(QStringLiteral("sub/inner.png")));
        const QStringList files = ui::MainWindow::imageFilesIn(dir.path());
        QCOMPARE(files.size(), 3);
        for (const QString &f : files)
            QVERIFY(QFileInfo(f).isAbsolute() && !f.contains(QStringLiteral("sub")) && !f.endsWith(QStringLiteral(".txt")));
    }

    // The sequence's file dialog gets its "Use this folder" button next to Open: a non-native QFileDialog has
    // the button box it is added to.
    void nonNativeFileDialogHasAButtonBox()
    {
        QFileDialog dialog;
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setFileMode(QFileDialog::ExistingFiles);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QVERIFY(buttons->button(QDialogButtonBox::Open) || buttons->button(QDialogButtonBox::Save)
                || !buttons->buttons().isEmpty());
    }

    // The dialog says where the sequence ends and when the scene has to grow.
    void importSequenceDialogShowsTheEnd()
    {
        ui::ImportSequenceDialog dialog(10, 4, 24);
        auto *start = dialog.findChild<QSpinBox *>(QStringLiteral("start"));
        auto *hold = dialog.findChild<QSpinBox *>(QStringLiteral("hold"));
        auto *end = dialog.findChild<QLabel *>(QStringLiteral("end"));
        QVERIFY(start && hold && end);
        QCOMPARE(dialog.startFrame(), 4); // the current frame, counted from 1 in the box
        QCOMPARE(start->value(), 5);
        QCOMPARE(dialog.hold(), 1);
        QVERIFY(!end->text().contains(QStringLiteral("grows"))); // frames 5 to 14 fit in 24
        hold->setValue(3);
        QVERIFY(end->text().contains(QStringLiteral("grows from 24 to 34")));
    }

    // A transform of a whole cel (every frame's layers chosen): while it is dragged only the frame shown is
    // drawn, the others when it is applied, and all of it is one undo step.
    void transformOfAWholeCelDrawsTheOtherFramesWhenApplied()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QList<QImage> images;
        for (QColor color : {QColor(Qt::red), QColor(Qt::green), QColor(Qt::blue)}) {
            QImage image(100, 100, QImage::Format_ARGB32);
            image.fill(color);
            images << image;
        }
        QCOMPARE(controller.importImageSequence(images, QStringLiteral("seq"), 0, 1), 3); // 910..1009, 490..589
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        std::vector<document::DocumentController::LayerRef> refs;
        for (const auto &[id, drawing] : cel.drawings())
            for (const doc::Layer &layer : drawing.layers)
                refs.push_back({cel.id(), id, layer.id});
        controller.setLayerSelection(refs);
        controller.setCurrentFrame(0);
        auto alphaAt = [&](int frame, int x, int y) {
            const doc::Drawing *d = cel.drawingAt(frame);
            return d ? qAlpha(d->layers.front().image.pixel(x, y)) : -1;
        };

        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.setViewZoom(1.0);
        tool.activated();
        QVERIFY(tool.hasPendingEdit());
        tool.press({960, 540}, Qt::NoModifier); // inside the box: moves it
        tool.move({1160, 540}, Qt::NoModifier); // 200 px to the right
        tool.release({1160, 540}, Qt::NoModifier);
        QVERIFY(alphaAt(0, 1160, 540) > 0 && alphaAt(0, 960, 540) == 0); // the frame shown: moved
        QVERIFY(alphaAt(1, 960, 540) > 0 && alphaAt(1, 1160, 540) == 0); // the others: where they were
        QVERIFY(alphaAt(2, 960, 540) > 0);

        tool.commit(); // applied: every frame
        QVERIFY(alphaAt(1, 1160, 540) > 0 && alphaAt(1, 960, 540) == 0);
        QVERIFY(alphaAt(2, 1160, 540) > 0 && alphaAt(2, 960, 540) == 0);
        controller.undo(); // one step for all of it
        for (int frame : {0, 1, 2})
            QVERIFY(alphaAt(frame, 960, 540) > 0 && alphaAt(frame, 1160, 540) == 0);
    }

    // The audio track: the file is read into the project, offset / volume / mute are undo steps, it can be
    // taken away, and an unreadable file is refused with a reason.
    void audioTrackIsKeptInTheProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile song(dir.filePath(QStringLiteral("Theme Song.MP3")));
        QVERIFY(song.open(QIODevice::WriteOnly));
        song.write(QByteArray(5000, 'x'));
        song.close();
        document::DocumentController controller;
        QVERIFY(!controller.audioTrack());
        QString reason;
        QVERIFY(!controller.importAudio(dir.filePath(QStringLiteral("missing.mp3")), &reason));
        QVERIFY(!reason.isEmpty());

        QVERIFY(controller.importAudio(song.fileName(), &reason));
        QVERIFY(controller.audioTrack());
        QCOMPARE(QString::fromStdString(controller.audioTrack()->name), QStringLiteral("Theme Song"));
        QCOMPARE(QString::fromStdString(controller.audioTrack()->format), QStringLiteral("mp3"));
        QCOMPARE(controller.audioTrack()->bytes(), std::size_t(5000));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo audio"));
        QVERIFY(controller.isModified());

        controller.setAudioOffset(24);
        controller.setAudioVolume(0.5f);
        controller.setAudioMuted(true);
        controller.setAudioMuted(true); // the same: no step
        QCOMPARE(controller.audioTrack()->offset, 24);
        QCOMPARE(controller.audioTrack()->volume, 0.5f);
        QVERIFY(controller.audioTrack()->muted);
        controller.undo();
        QVERIFY(!controller.audioTrack()->muted);
        controller.undo();
        QCOMPARE(controller.audioTrack()->volume, 1.0f);
        controller.undo();
        QCOMPARE(controller.audioTrack()->offset, 0);
        controller.setAudioDuration(61.5); // a fact about the file: not a step
        QCOMPARE(controller.audioTrack()->duration, 61.5);
        controller.undo(); // the import itself
        QVERIFY(!controller.audioTrack());
        controller.redo();
        QVERIFY(controller.audioTrack());
        controller.removeAudio();
        QVERIFY(!controller.audioTrack());
        controller.undo();
        QVERIFY(controller.audioTrack()); // back with its sound
        QCOMPARE(controller.audioTrack()->bytes(), std::size_t(5000));
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
