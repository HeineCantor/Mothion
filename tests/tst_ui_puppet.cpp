// Tests for the UI layer: puppet warp.

#include "test_common.h"

class TestUiPuppet : public QObject
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

};

QTEST_MAIN(TestUiPuppet)
#include "tst_ui_puppet.moc"
