// Tests for the UI layer: the panels: layers, brush size, color sets, tool properties.

#include "test_common.h"

class TestUiPanels : public QObject
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


    // Dragging the opacity slider shows the opacity on the canvas as it goes (for a layer and for a cel),
    // and the drag is one undo step.
    // The cel's opacity, moved with the slider, changes the picture on the canvas as it moves (the layer's did).
    void celOpacityPreviewRedrawsTheCanvas()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage red(40, 40, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("x"));
        canvas::CanvasView view(&state, &controller);
        const doc::CelId id = controller.currentCel();
        const QPoint inside(controller.document().width() / 2, controller.document().height() / 2); // the image is centered
        const QRgb opaque = view.documentImage().pixel(inside);
        QCOMPARE(qRed(opaque), 255);
        QCOMPARE(qGreen(opaque), 0); // red on the white paper
        doc::CelProperties shown = controller.document().cel(id)->properties();
        shown.opacity = 0.3f;
        QVERIFY(controller.previewCelProperties(id, shown));
        QVERIFY(qGreen(view.documentImage().pixel(inside)) > 100); // paler already, nothing recorded
        shown.opacity = 1.0f;
        QVERIFY(controller.previewCelProperties(id, shown));
        QCOMPARE(view.documentImage().pixel(inside), opaque);
    }


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


    // A cel chosen in the Layers panel (not one of its layers) cannot be drawn on where it has a frame; where it
    // has none, drawing makes the frame and chooses its layer. Moving whole layers is not affected.
    void aSelectedCelCannotBeDrawnOn()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *tree = panel.findChild<QTreeWidget *>();
        QVERIFY(tree);
        QImage red(20, 20, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("x")); // a cel with a frame, from 0 on
        QCoreApplication::processEvents();
        const doc::CelId cel = controller.currentCel();

        tree->setCurrentItem(panel.celItem(cel)); // the cel row
        QVERIFY(!controller.layerChosen());
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason)); // there is a frame: refused
        QVERIFY(!reason.isEmpty());
        QVERIFY(!controller.pasteImage(red, QPoint(10, 10), &reason));
        QVERIFY(!controller.canUndo() || controller.undoText() != QStringLiteral("Undo paste"));
        QVERIFY(controller.beginPaintLayers(&reason).size() == 1); // a move still works on it
        controller.cancelPaint();

        // The same for a frame's row: it is not a layer either.
        tree->setCurrentItem(panel.frameItem(controller.currentDrawingId()));
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Frame);
        QVERIFY(!controller.layerChosen());
        reason.clear();
        QVERIFY(!controller.beginPaint(true, &reason));
        QVERIFY(!reason.isEmpty());
        QVERIFY(controller.beginPaintLayers(&reason).size() == 1); // moving it still works
        controller.cancelPaint();

        // A frame where the cel has none: drawing makes it, and the layer is the selection now.
        controller.addCel(); // a new cel, with no frame yet
        QCoreApplication::processEvents();
        tree->setCurrentItem(panel.celItem(controller.currentCel()));
        QVERIFY(!controller.layerChosen());
        QVERIFY(!controller.currentDrawing()); // nothing there yet
        const auto target = controller.beginPaint(true, &reason);
        QVERIFY(target.has_value());
        controller.cancelPaint();
        QVERIFY(controller.layerChosen());
        QCoreApplication::processEvents();
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Layer);
        QVERIFY(controller.beginPaint(true, &reason)); // and it can be drawn on from now on
        controller.cancelPaint();

        tree->setCurrentItem(panel.layerItem(controller.currentLayer())); // a layer chosen: drawing works
        QVERIFY(controller.layerChosen());
        QVERIFY(controller.beginPaint(true, &reason));
        controller.cancelPaint();
    }


    // The Color set panel: the library's active set as swatches; a click sets the color, the "+" adds the current
    // color, a drag reorders, the built-in set can only be clicked.
    void colorSetPanelShowsAndEditsTheSets()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        tools::ToolState state;
        tools::ColorSetLibrary library(dir.path());
        ui::ColorSetPanel panel(&state, &library);
        panel.resize(260, 300);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        ui::ColorSwatchGrid *grid = panel.grid();

        // The built-in set: a click sets the main color; nothing can be added or dragged.
        QCOMPARE(grid->current()->id, tools::ColorSetLibrary::kDefaultId);
        QVERIFY(!panel.addButton()->isEnabled());
        const QColor third = library.set(tools::ColorSetLibrary::kDefaultId)->swatches[2].color;
        QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(2).center());
        QCOMPARE(state.color().rgba(), third.rgba());
        const auto defaultBefore = library.set(tools::ColorSetLibrary::kDefaultId)->swatches;
        QTest::mousePress(grid, Qt::LeftButton, {}, grid->cellRect(0).center());
        QTest::mouseMove(grid, grid->cellRect(5).center());
        QTest::mouseRelease(grid, Qt::LeftButton, {}, grid->cellRect(5).center());
        QCOMPARE(library.set(tools::ColorSetLibrary::kDefaultId)->swatches, defaultBefore);

        // Another set, made and chosen: empty at first, the list follows.
        const QString id = library.create(QStringLiteral("Mine"));
        library.setActive(id);
        QCOMPARE(panel.setList()->currentText(), QStringLiteral("Mine"));
        QCOMPARE(panel.setList()->count(), 2);
        QVERIFY(panel.addButton()->isEnabled());
        QCOMPARE(grid->swatchAt(QPoint(5, 5)), -1);

        // The "+" adds the current color, and the grid shows it at once.
        state.setColor(QColor(200, 30, 40));
        panel.addButton()->click();
        state.setColor(QColor(10, 220, 30));
        panel.addButton()->click();
        state.setColor(QColor(20, 30, 250));
        panel.addButton()->click();
        QCOMPARE(library.set(id)->swatches.size(), 3);
        QCOMPARE(grid->swatchAt(grid->cellRect(2).center()), 2);
        QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(0).center());
        QCOMPARE(state.color(), QColor(200, 30, 40));

        // Dragging a swatch to another place moves it; a click does not.
        QTest::mousePress(grid, Qt::LeftButton, {}, grid->cellRect(0).center());
        QTest::mouseMove(grid, grid->cellRect(1).center());
        QTest::mouseMove(grid, grid->cellRect(2).center());
        QTest::mouseRelease(grid, Qt::LeftButton, {}, grid->cellRect(2).center());
        QCOMPARE(library.set(id)->swatches[2].color, QColor(200, 30, 40));
        QCOMPARE(library.set(id)->swatches[0].color, QColor(10, 220, 30));

        // A swatch's own color picker: opened without blocking, and the color is the swatch's when it is accepted.
        const QColor first = library.set(id)->swatches[1].color;
        const QColor untouched = library.set(id)->swatches[2].color;
        grid->editColor(1);
        QColorDialog *picker = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QColorDialog *>(top))
                picker = d;
        QVERIFY(picker);
        QCOMPARE(picker->currentColor(), library.set(id)->swatches[1].color); // starts from the swatch's color
        picker->setCurrentColor(QColor(90, 80, 70, 200));
        QCOMPARE(library.set(id)->swatches[1].color, first); // not yet
        picker->accept();
        QCOMPARE(library.set(id)->swatches[1].color, QColor(90, 80, 70, 200));
        QCOMPARE(library.set(id)->swatches[2].color, untouched); // the others are untouched
        const auto dialogs = QApplication::topLevelWidgets().size();
        grid->editColor(99); // no such swatch: nothing opens
        QCOMPARE(QApplication::topLevelWidgets().size(), dialogs);

        // Changes made elsewhere (the library) show in the grid, and a set chosen from the list is the active one.
        library.setColor(id, 0, QColor(1, 2, 3));
        QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(0).center());
        QCOMPARE(state.color(), QColor(1, 2, 3));
        panel.setList()->setCurrentIndex(0);
        emit panel.setList()->activated(0);
        QCOMPARE(library.activeId(), tools::ColorSetLibrary::kDefaultId);
        QVERIFY(!panel.addButton()->isEnabled());
    }


    // The camera has a row at the top of the Layers panel: the Camera button adds and selects it, its eye turns it
    // on and off, Delete takes it away; while it is selected the cels cannot be drawn on.
    void cameraHasARowInTheLayersPanel()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *tree = panel.findChild<QTreeWidget *>();
        auto *button = panel.findChild<QToolButton *>(QStringLiteral("cameraButton"));
        QVERIFY(tree && button);
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("x"));
        QCoreApplication::processEvents();
        QVERIFY(!panel.cameraItem()); // none in the project

        button->click(); // made, and selected
        QVERIFY(controller.camera());
        QVERIFY(panel.cameraItem());
        QCOMPARE(tree->topLevelItem(0), panel.cameraItem()); // above the cels
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Camera);
        QVERIFY(controller.cameraSelected());
        QCOMPARE(tree->currentItem(), panel.cameraItem());
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason)); // not drawn on

        // Its eye: on and off.
        auto *eye = tree->itemWidget(panel.cameraItem(), 0)->findChild<QToolButton *>();
        QVERIFY(eye && eye->isChecked());
        eye->click();
        QVERIFY(!controller.camera()->visible);
        QVERIFY(panel.cameraItem()); // the row stays, off
        eye = tree->itemWidget(panel.cameraItem(), 0)->findChild<QToolButton *>();
        QVERIFY(!eye->isChecked());
        eye->click();
        QVERIFY(controller.camera()->visible);

        // Another row chosen: the camera is not selected, and a layer can be drawn on again.
        tree->setCurrentItem(panel.layerItem(controller.currentLayer()));
        QVERIFY(!controller.cameraSelected());
        QVERIFY(panel.focus() != ui::LayersPanel::Focus::Camera);
        QVERIFY(controller.beginPaint(true, &reason));
        controller.cancelPaint();

        // The button again selects it (it does not make another one); the bin takes it away.
        button->click();
        QCOMPARE(controller.camera()->keys().size(), size_t(1));
        QVERIFY(controller.cameraSelected());
        QToolButton *bin = nullptr;
        for (QToolButton *b : panel.findChildren<QToolButton *>())
            if (b->toolTip() == QStringLiteral("Delete the camera"))
                bin = b;
        QVERIFY(bin && bin->isEnabled());
        bin->click();
        QCoreApplication::processEvents();
        QVERIFY(!controller.camera());
        QVERIFY(!panel.cameraItem());
        QVERIFY(!controller.cameraSelected());
        QVERIFY(panel.focus() != ui::LayersPanel::Focus::Camera);
        // Undone, the camera is back, not selected.
        controller.undo();
        QVERIFY(panel.cameraItem());
        QVERIFY(!controller.cameraSelected());
    }

    // The effect brushes and the shape tool have a size too: the palette, the [ ] keys and the size slider work
    // on it, as on the brush (the opacity slider stays with the paint tools).
    void sizeControlsServeTheEffectBrushesAndShapes()
    {
        tools::ToolState state;
        ui::BrushSizePanel panel(&state);
        panel.resize(250, 400);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        ui::BrushSizeGrid *grid = panel.grid();
        const int index = int(ui::BrushSizeGrid::presets().indexOf(25));
        for (const tools::ToolId tool : {tools::ToolId::Liquify, tools::ToolId::Blend, tools::ToolId::Shape}) {
            state.setActiveTool(tool);
            const double brushBefore = state.settings(tools::ToolId::Brush).size;
            QTest::mouseClick(grid, Qt::LeftButton, {}, grid->cellRect(index).center());
            QCOMPARE(state.settings(tool).size, 25.0);
            QCOMPARE(state.settings(tools::ToolId::Brush).size, brushBefore); // the brush's own is not touched
        }
        ui::ToolPalettePanel palette(&state);
        palette.show();
        QVERIFY(QTest::qWaitForWindowExposed(&palette));
        state.setActiveTool(tools::ToolId::Liquify);
        const double before = state.settings(tools::ToolId::Liquify).size;
        QTest::keyClick(&palette, Qt::Key_BracketRight);
        QVERIFY(state.settings(tools::ToolId::Liquify).size > before);
        QTest::keyClick(&palette, Qt::Key_BracketLeft);
        QCOMPARE(state.settings(tools::ToolId::Liquify).size, before);
        state.setActiveTool(tools::ToolId::Move); // no size
        const double move = state.settings(tools::ToolId::Move).size;
        QTest::keyClick(&palette, Qt::Key_BracketRight);
        QCOMPARE(state.settings(tools::ToolId::Move).size, move);

        ui::BrushSlidersPanel sliders(&state);
        state.setActiveTool(tools::ToolId::Shape);
        const auto bars = sliders.findChildren<ui::VerticalValueSlider *>();
        QCOMPARE(bars.size(), 2);
        QVERIFY(bars[0]->isEnabled());  // size
        QVERIFY(!bars[1]->isEnabled()); // opacity
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

        // The cel's row: the buttons act on the cel; its blend mode and opacity are the timeline's, here they are
        // shown but cannot be changed.
        tree->setCurrentItem(panel.celItem(cel));
        QCOMPARE(panel.focus(), ui::LayersPanel::Focus::Cel);
        QVERIFY(!panel.findChild<QSpinBox *>()->isEnabled());
        QVERIFY(!panel.findChild<QComboBox *>()->isEnabled());
        QVERIFY(!panel.findChild<QSlider *>()->isEnabled());
        QCOMPARE(d.cel(cel)->properties().opacity, 1.0f);
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


    // A cel's row in the Layers panel is in the color of its blocks in the timeline, in whichever theme with enough
    // contrast under the text (deep under light text, bright under dark text).
    void celRowsHaveTheirColorWithContrast()
    {
        for (const bool light : {false, true}) {
            if (light)
                ui::applyLightTheme(*qApp);
            else
                ui::applyDarkTheme(*qApp);
            document::DocumentController controller;
            ui::LayersPanel panel(&controller);
            panel.resize(320, 800);
            panel.show();
            QVERIFY(QTest::qWaitForWindowExposed(&panel));
            auto *tree = panel.findChild<QTreeWidget *>();
            QImage red(20, 20, QImage::Format_ARGB32);
            red.fill(Qt::red);
            controller.importImage(red, QStringLiteral("x"));
            const doc::LayerId layer = controller.currentDrawing()->layers.front().id;
            controller.addCel();
            QCoreApplication::processEvents();
            tree->setCurrentItem(panel.layerItem(layer)); // not a cel row
            const doc::Document &d = controller.document();
            QVERIFY(d.celCount() >= 2);
            const QColor text = QApplication::palette().color(QPalette::WindowText);
            for (int i = 0; i < d.celCount(); ++i) {
                QTreeWidgetItem *item = panel.celItem(d.celAt(i).id());
                QVERIFY(item);
                QVERIFY(!item->isSelected());
                const QColor fill = item->background(0).color();
                QCOMPARE(fill, ui::LayersPanel::celRowColor(i, QApplication::palette()));
                QVERIFY2(qAbs(fill.lightness() - text.lightness()) > 120, light ? "light theme" : "dark theme");
            }
        }
        ui::applyDarkTheme(*qApp); // the default again
    }


    // The first stroke on an empty frame makes the frame, and the rows for it wait for the pen to lift (rebuilding the
    // panel in the middle of a stroke is a stall in a project of many frames).
    void theRowsWaitForTheEndOfTheStroke()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        QImage red(20, 20, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("x"));
        controller.addCel(); // a cel with no frame
        QCoreApplication::processEvents();
        const auto target = controller.beginPaint(true);
        QVERIFY(target);
        const doc::DrawingId made = controller.currentDrawingId();
        QVERIFY(made != doc::kNoId);
        const doc::TiledImage before = *target->image;
        target->image->setPixel(3, 3, 0xff000000);
        QCoreApplication::processEvents(); // the change is announced, the panel holds back
        QVERIFY(controller.isPainting());
        QVERIFY(!panel.frameItem(made));
        controller.endPaint(before);
        QCoreApplication::processEvents();
        QVERIFY(panel.frameItem(made)); // now there
    }


    // Showing, locking or changing a cel or a layer updates its row where it is: the rows are not made anew (that is
    // a noticeable moment in a project of hundreds of frames). A change of structure still makes them anew.
    void lookChangesUpdateTheRowsInPlace()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 600);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QImage red(20, 20, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("x"));
        QCoreApplication::processEvents();
        const doc::CelId cel = controller.currentCel();
        const doc::Drawing *drawing = controller.currentDrawing();
        const doc::LayerId layer = drawing->layers.front().id;
        QTreeWidgetItem *celRow = panel.celItem(cel);
        QTreeWidgetItem *layerRow = panel.layerItem(layer);
        QVERIFY(celRow && layerRow);
        auto eye = [&](QTreeWidgetItem *item) {
            return panel.findChild<QTreeWidget *>()->itemWidget(item, 0)->findChildren<QToolButton *>(QStringLiteral("LayerEye")).first();
        };
        QVERIFY(eye(layerRow)->isChecked());

        doc::LayerProperties props = drawing->layers.front().properties;
        props.visible = false;
        props.opacity = 0.5f;
        controller.execute(doc::makeSetLayerProperties(controller.document(), cel, drawing->id, layer, props));
        QCoreApplication::processEvents();
        QCOMPARE(panel.layerItem(layer), layerRow); // the same row
        QCOMPARE(panel.celItem(cel), celRow);
        QVERIFY(!eye(layerRow)->isChecked());
        auto *text = panel.findChild<QTreeWidget *>()->itemWidget(layerRow, 0)->findChild<QLabel *>(QStringLiteral("rowText"));
        QVERIFY(text->text().contains(QStringLiteral("50")));

        controller.undo(); // and back
        QCoreApplication::processEvents();
        QCOMPARE(panel.layerItem(layer), layerRow);
        QVERIFY(eye(layerRow)->isChecked());

        controller.addCel(); // a new cel: the rows are made anew
        QCoreApplication::processEvents();
        QVERIFY(panel.layerItem(layer) != layerRow);
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


    // The selection is an outline, not a fill: the text of a selected row is in the same color as the others'.
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
            (item->isSelected() ? selected : others)++;
            QCOMPARE(shown, pal.color(QPalette::WindowText));
        }
        QVERIFY(selected >= 1 && others >= 1);
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

};

QTEST_MAIN(TestUiPanels)
#include "tst_ui_panels.moc"
