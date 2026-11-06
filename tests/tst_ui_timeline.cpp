// Tests for the UI layer: the timeline and the animation playback.

#include "test_common.h"

class TestUiTimeline : public QObject
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


    // "Flip" shows the animation mirrored left to right, in the window only.
    // The Autoplay shows the animation through the camera, when the project has one and it is on.
    void autoplayShowsTheCamera()
    {
        document::DocumentController controller;
        const int w = controller.document().width(), h = controller.document().height();
        QImage square(400, 400, QImage::Format_ARGB32);
        square.fill(Qt::blue);
        QVERIFY(controller.pasteImage(square, QPoint(w / 2 - 200, h / 2 - 200)));
        controller.clearSelection();

        ui::AutoplayWindow window(&controller);
        window.resize(720, 500);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QPushButton *>(QStringLiteral("autoplayPlay"))->setChecked(false);
        auto bluePixels = [&] {
            const QImage shot = window.grab().toImage();
            long n = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    n += (c.blue() > 200 && c.red() < 60 && c.green() < 60) ? 1 : 0;
                }
            return n;
        };
        const long plain = bluePixels();
        QVERIFY(plain > 1000);

        controller.setCameraKey(0, {w / 2.0, h / 2.0, 2, 0}); // twice as close: the square is four times the area
        QTRY_VERIFY(bluePixels() > 3 * plain);
        controller.setCameraVisible(false); // off: the sheet as it is
        QTRY_COMPARE(bluePixels(), plain);
        controller.setCameraVisible(true);
        QTRY_VERIFY(bluePixels() > 3 * plain);
        controller.removeCamera();
        QTRY_COMPARE(bluePixels(), plain);
    }

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


    // The Autoplay window plays the sound with the frames, and stops it when it stops.
    void autoplayPlaysTheSoundAtTheDocumentsRate()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4000, 'x'));
        file.close();
        document::DocumentController controller;
        QVERIFY(controller.importAudio(file.fileName()));
        audio::AudioPlayer player(&controller);
        ui::AutoplayWindow window(&controller, &player);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.isPlaying());
        QVERIFY(player.isPlaying()); // playing, at the document's rate (24 fps)
        auto *speed = window.findChild<QComboBox *>();
        QVERIFY(speed);
        int other = -1;
        for (int i = 0; i < speed->count(); ++i)
            if (std::abs(speed->itemData(i).toDouble() - 24.0) > 0.5)
                other = i;
        QVERIFY(other >= 0);
        const int own = speed->currentIndex();
        speed->setCurrentIndex(other);
        QVERIFY(window.isPlaying());
        QVERIFY(player.isPlaying()); // another speed: the sound goes along with it
        speed->setCurrentIndex(own);
        QVERIFY(player.isPlaying());
        QTest::keyClick(&window, Qt::Key_Space); // stop
        QVERIFY(!window.isPlaying());
        QVERIFY(!player.isPlaying());
    }


    // Slower or faster than the document's rate the sound goes slower or faster with the frames; the sound button
    // of the window silences it (for the session: the project's track is not touched) and brings it back.
    void autoplaySlowsTheSoundAndCanSilenceIt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4000, 'x'));
        file.close();
        document::DocumentController controller;
        QVERIFY(controller.importAudio(file.fileName()));
        audio::AudioPlayer player(&controller);
        ui::AutoplayWindow window(&controller, &player);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *speed = window.findChild<QComboBox *>();
        auto *sound = window.findChild<QPushButton *>(QStringLiteral("sound"));
        QVERIFY(speed && sound);
        QVERIFY(sound->isEnabled() && sound->isChecked());
        auto choose = [&](double fps) {
            for (int i = 0; i < speed->count(); ++i)
                if (std::abs(speed->itemData(i).toDouble() - fps) < 0.01)
                    speed->setCurrentIndex(i);
        };
        QVERIFY(player.isPlaying());
        QCOMPARE(player.rate(), 1.0);
        choose(12.0); // half the speed of the timeline's 24
        QVERIFY(player.isPlaying());
        QVERIFY(std::abs(player.rate() - 0.5) < 0.01);
        choose(48.0); // twice as fast: the sound too
        QVERIFY(player.isPlaying());
        QVERIFY(std::abs(player.rate() - 2.0) < 0.01);
        choose(24.0);
        QVERIFY(player.isPlaying());

        QTest::mouseClick(sound, Qt::LeftButton); // sound off here
        QVERIFY(!sound->isChecked() && player.isSilenced());
        QVERIFY(!player.isPlaying());
        QVERIFY(!controller.audioTrack()->muted); // the project is not changed
        QTest::mouseClick(sound, Qt::LeftButton);
        QVERIFY(sound->isChecked() && !player.isSilenced());
        QVERIFY(player.isPlaying());

        controller.removeAudio(); // no track: nothing to switch
        QVERIFY(!sound->isEnabled());
    }


    // The timeline's play button plays the animation on the canvas from the current frame, round the scene, with
    // the sound, and pauses it where it is; the frame moved by hand while it plays is where it plays on from.
    void timelinePlayButtonPlaysAndPauses()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4000, 'x'));
        file.close();
        tools::ToolState state;
        document::DocumentController controller;
        QVERIFY(controller.importAudio(file.fileName()));
        audio::AudioPlayer sound(&controller);
        ui::TimelinePanel panel(&controller, &state, &sound);
        panel.show();
        auto *play = panel.findChild<QToolButton *>(QStringLiteral("playPause"));
        QVERIFY(play);
        QVERIFY(!panel.player()->isPlaying() && !play->isChecked());

        controller.setCurrentFrame(10);
        QTest::mouseClick(play, Qt::LeftButton);
        QVERIFY(panel.player()->isPlaying() && play->isChecked());
        QVERIFY(sound.isPlaying()); // the sound goes with it
        QTRY_VERIFY(controller.currentFrame() > 12); // the frames move on, by the clock
        controller.setAudioMuted(true); // muted while it plays...
        QVERIFY(!sound.isPlaying());
        controller.setAudioMuted(false); // ...and back: the sound returns by itself with the next frames
        QTRY_VERIFY(sound.isPlaying());
        QVERIFY(panel.player()->isPlaying());
        QTest::mouseClick(play, Qt::LeftButton); // pause: where it is
        QVERIFY(!panel.player()->isPlaying() && !play->isChecked());
        QVERIFY(!sound.isPlaying());
        const int paused = controller.currentFrame();
        QTest::qWait(150);
        QCOMPARE(controller.currentFrame(), paused);

        // From the last frame of the scene: round to the first.
        controller.setCurrentFrame(controller.document().frameCount() - 1);
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_VERIFY(controller.currentFrame() < 20);
        // The frame moved by hand while it plays: it goes on from there.
        controller.setCurrentFrame(60);
        QTRY_VERIFY(controller.currentFrame() >= 60 && controller.currentFrame() < 80);
        panel.player()->stop();
    }


    // Space plays and pauses the timeline while the focus is in it (the pointer is over it); with the focus
    // anywhere else in the window it does not (over the canvas it is the key that pans).
    void spacePlaysAndPausesTheTimeline()
    {
        ui::Shortcuts::instance().resetAll();
        tools::ToolState state;
        document::DocumentController controller;
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *panel = new ui::TimelinePanel(&controller, &state, nullptr, &window);
        auto *elsewhere = new QPushButton(QStringLiteral("elsewhere"), &window);
        layout->addWidget(panel);
        layout->addWidget(elsewhere);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = panel->view();
        QCOMPARE(ui::Shortcuts::instance().key(QStringLiteral("anim.playPause")), QKeySequence(Qt::Key_Space));

        view->setFocus();
        QVERIFY(view->hasFocus());
        QTest::keyClick(view, Qt::Key_Space);
        QVERIFY(panel->player()->isPlaying());
        QTest::keyClick(view, Qt::Key_Space);
        QVERIFY(!panel->player()->isPlaying());

        elsewhere->setFocus();
        QVERIFY(elsewhere->hasFocus());
        QTest::keyClick(&window, Qt::Key_Space);
        QVERIFY(!panel->player()->isPlaying()); // not from outside the timeline
    }

};

QTEST_MAIN(TestUiTimeline)
#include "tst_ui_timeline.moc"
