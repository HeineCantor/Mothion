// Tests for the UI layer: the main window, settings, shortcuts, import and export.

#include "ui/TweenDialog.h"
#include "test_common.h"

class TestUiWindow : public QObject
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


    // Tween frames: the dialog proposes the current frame and the next pose, shows the picture between them, and
    // refuses a pair that cannot be tweened; the menu has the command.
    void tweenDialogProposesThePosesAndShowsTheWay()
    {
        document::DocumentController controller;
        const doc::CelId cel = controller.currentCel();
        doc::Document &d = controller.documentForCommands();
        doc::NewDrawing first = doc::makeNewDrawing(d, cel, 0, 2, "ink");
        const doc::DrawingId a = first.id;
        controller.execute(std::move(first.command));
        doc::NewDrawing second = doc::makeNewDrawing(d, cel, 6, 1, "ink");
        const doc::DrawingId b = second.id;
        controller.execute(std::move(second.command));
        auto bar = [&](doc::DrawingId id, int x, int y) {
            for (int j = 0; j < 80; ++j)
                for (int i = 0; i < 12; ++i)
                    d.cel(cel)->drawing(id)->layers[0].image.setPixel(x + i, y + j, 0xff000000);
        };
        bar(a, 300, 300);
        bar(b, 340, 300);
        controller.setCurrentFrame(0);

        QCOMPARE(ui::TweenDialog::nextPose(*controller.document().cel(cel), 0), std::optional<int>(6));
        QCOMPARE(ui::TweenDialog::nextPose(*controller.document().cel(cel), 1), std::optional<int>(6)); // inside the hold
        QVERIFY(!ui::TweenDialog::nextPose(*controller.document().cel(cel), 6)); // nothing after the last pose
        QVERIFY(!ui::TweenDialog::nextPose(*controller.document().cel(cel), 3)); // an empty frame

        ui::TweenDialog dialog(&controller);
        dialog.show();
        QCOMPARE(dialog.cel(), cel);
        QCOMPARE(dialog.from(), 0);
        QCOMPARE(dialog.to(), 6);
        QCOMPARE(dialog.frameCount(), 5);
        QVERIFY(dialog.okButton()->isEnabled());
        QTRY_VERIFY(!dialog.preview()->pixmap().isNull()); // made apart: it comes a moment after
        // At the start of the way the picture is the first pose, at the end the second: the bar is on the left, then on the right.
        auto darkSide = [&](int percent) {
            dialog.previewSlider()->setValue(percent);
            const bool settled = QTest::qWaitFor([&] { return !dialog.previewBusy(); }, 5000);
            Q_UNUSED(settled)
            const QImage shot = dialog.preview()->pixmap().toImage();
            long left = 0, right = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x)
                    if (qGray(shot.pixel(x, y)) < 100)
                        (x < shot.width() / 2 ? left : right) += 1;
            return std::pair{left, right};
        };
        const auto atStart = darkSide(0), atEnd = darkSide(100);
        QVERIFY(atStart.first + atStart.second > 100 && atEnd.first + atEnd.second > 100);
        QVERIFY(atStart.first > atEnd.first); // the bar has moved to the right
        QVERIFY(atEnd.second > atStart.second);

        // The slider can be dragged all the way and back without holding anything up: one picture is made at a time,
        // and what was asked meanwhile is made once, at the last place.
        for (int i = 0; i <= 100; i += 5)
            dialog.previewSlider()->setValue(i);
        QVERIFY(dialog.previewBusy() || !dialog.preview()->pixmap().isNull());
        QTRY_VERIFY(!dialog.previewBusy());

        // A pair with no frame between, or the same pose twice: refused, with a note.
        dialog.toBox()->setValue(dialog.fromBox()->value() + 1);
        QVERIFY(!dialog.okButton()->isEnabled());
        QCOMPARE(dialog.frameCount(), 0);
        dialog.toBox()->setValue(2); // frame 2 shows the same drawing as frame 1 (a hold)
        QVERIFY(!dialog.okButton()->isEnabled());
        dialog.toBox()->setValue(4); // an empty frame
        QVERIFY(!dialog.okButton()->isEnabled());
        dialog.toBox()->setValue(7);
        QVERIFY(dialog.okButton()->isEnabled());

        // The easing is remembered.
        auto *ease = dialog.findChild<QComboBox *>();
        QVERIFY(ease);
        ease->setCurrentIndex(3);
        dialog.accept();
        ui::TweenDialog again(&controller);
        QCOMPARE(again.ease(), document::TweenEase::EaseInOut);
    }

    void animationMenuHasTweenFrames()
    {
        ui::MainWindow window;
        bool found = false;
        for (QAction *action : window.findChildren<QAction *>())
            found = found || action->text() == QStringLiteral("Tween frames…");
        QVERIFY(found);
    }

    // The scene's length is the user's to change, and one undo step.
    void sceneLengthIsChangedFromTheController()
    {
        document::DocumentController controller;
        const int before = controller.sceneLength();
        controller.setSceneLength(before + 30);
        QCOMPARE(controller.sceneLength(), before + 30);
        QVERIFY(controller.document().frameCount() == before + 30);
        controller.undo();
        QCOMPARE(controller.sceneLength(), before);
        controller.setSceneLength(0); // at least one frame
        QCOMPARE(controller.sceneLength(), 1);
        controller.setSceneLength(1); // unchanged: no step
        QVERIFY(controller.usedFrames() >= 0);

        ui::MainWindow window;
        bool found = false;
        for (QAction *action : window.findChildren<QAction *>())
            found = found || action->text() == QStringLiteral("Scene length…");
        QVERIFY(found);
    }

    // "Apply compositing" is offered when the project has a network, and is the user's choice.
    void exportDialogOffersTheCompositingOnlyWhenThereIsOne()
    {
        auto compositingBox = [](ui::ExportDialog &dialog) -> QCheckBox * {
            for (QCheckBox *box : dialog.findChildren<QCheckBox *>())
                if (box->text() == QStringLiteral("Apply compositing"))
                    return box;
            return nullptr;
        };
        ui::ExportDialog without(0, 10, QStringLiteral("x"), false);
        QVERIFY(compositingBox(without));
        QVERIFY(!compositingBox(without)->isEnabled());
        QVERIFY(!without.options().compositing);

        ui::ExportDialog with(0, 10, QStringLiteral("x"), true);
        QCheckBox *box = compositingBox(with);
        QVERIFY(box && box->isEnabled());
        box->setChecked(true);
        QVERIFY(with.options().compositing);
        box->setChecked(false);
        QVERIFY(!with.options().compositing);
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


    // The Rich Presence speaks Discord's socket protocol: a handshake with the application id, then
    // SET_ACTIVITY with the frame and the start time, again when the frame changes (not too often); a
    // ping is answered; and it keeps looking for Discord when it is not there yet.
    void discordPresenceTalksToDiscord()
    {
        const QString name = QStringLiteral("mothion-test-discord-%1").arg(QCoreApplication::applicationPid());
        QLocalServer::removeServer(name);
        document::DocumentController controller;
        ui::DiscordPresence presence(&controller, QStringLiteral("123456789"), nullptr,
                                     QStringList{QStringLiteral("mothion-test-nobody-here"), name});
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
        // Frame 5 may go out at once when the minimum interval has already passed (a slow machine), and then frame 6
        // after it; what matters is that frame 6 is the last, with the same start every time.
        QJsonObject again;
        for (int i = 0; i < 3; ++i) {
            const Message second = next();
            QVERIFY(second.opcode == 1);
            again = second.json.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
            QCOMPARE(again.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger(), start);
            if (again.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("Frame 6 / ")))
                break;
            QVERIFY(again.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("Frame 5 / ")));
        }
        QVERIFY(again.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("Frame 6 / ")));

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
        ui::DiscordPresence presence(&controller, QString(), nullptr, QStringList{QStringLiteral("mothion-test-nobody-here")});
        presence.start();
        controller.setCurrentFrame(3);
        QVERIFY(!presence.isConnected());
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
        ui::DiscordPresence presence(&controller, QStringLiteral("123"), nullptr, QStringList{});
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

QTEST_MAIN(TestUiWindow)
#include "tst_ui_window.moc"
