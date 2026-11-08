// Entry point: input-related application attributes, command line,
// application identity (used by QSettings), theme, splash, main window.

#include "diagnostics/CrashHandler.h"
#include "input/TabletBackend.h"
#include "ui/MainWindow.h"
#include "ui/SessionDialogs.h"
#include "ui/SplashScreen.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QIcon>
#include <QDebug>
#include <QScreen>
#include <QSplashScreen>
#include <QTimer>

#include <algorithm>

int main(int argc, char *argv[])
{
    // Keep every tablet sample. This is already Qt's default; it is stated
    // here because it matters: when true, Qt drops intermediate tablet moves
    // (on Windows it also skips the pointer history). Must be set before
    // the QApplication is created.
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Mothion"));
    QApplication::setApplicationName(QStringLiteral("Mothion"));
    QApplication::setApplicationVersion(QStringLiteral(MOTHION_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon/mothion.png")));
    mothion::diagnostics::install(QApplication::applicationVersion()); // the log, and what a crash leaves behind

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption wintab(QStringLiteral("wintab"),
                                    QStringLiteral("Windows: use the Wintab tablet API instead of Windows Ink."));
    parser.addOption(wintab);
    // To try a shared session with two Mothions on one computer (or two): one hosts, the other joins.
    const QCommandLineOption hostOption(QStringLiteral("host-session"),
                                        QStringLiteral("Share the project on this port as soon as it is open."),
                                        QStringLiteral("port"));
    const QCommandLineOption joinOption(QStringLiteral("join-session"),
                                        QStringLiteral("Join the shared session of a host (address:port)."),
                                        QStringLiteral("address:port"));
    const QCommandLineOption codeOption(QStringLiteral("session-code"),
                                        QStringLiteral("The invitation code of the session (a host makes one if none is given)."),
                                        QStringLiteral("code"));
    const QCommandLineOption nameOption(QStringLiteral("session-name"), QStringLiteral("Your name in a shared session."),
                                        QStringLiteral("name"));
    parser.addOptions({hostOption, joinOption, codeOption, nameOption});
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("A .moth project to open."),
                                 QStringLiteral("[project]"));
    parser.process(app);

    if (parser.isSet(wintab) && !mothion::input::setWinTabEnabled(true))
        qWarning("Wintab is not available (not on Windows, disabled at build time, or no wintab32.dll); "
                 "using the default tablet backend.");

    mothion::ui::applySavedTheme(app);

    // The splash stays up at least this long, so a fast start does not just flash it.
    constexpr int kSplashMinimumMs = 2500;
    QSplashScreen splash(mothion::ui::splashPixmap(QApplication::applicationVersion(),
                                                   QGuiApplication::primaryScreen()->devicePixelRatio()));
    splash.show();
    QElapsedTimer shown;
    shown.start();
    QApplication::processEvents();

    mothion::ui::MainWindow window;
    QTimer::singleShot(std::max<qint64>(0, kSplashMinimumMs - shown.elapsed()), &window, [&] {
        window.show();
        splash.finish(&window);
        window.checkPreviousCrash();
        if (!parser.positionalArguments().isEmpty())
            window.openFile(parser.positionalArguments().constFirst());
        window.checkRecovery();
        const QString name = parser.value(nameOption);
        if (parser.isSet(hostOption)) {
            const QString code = parser.isSet(codeOption) ? parser.value(codeOption) : mothion::ui::makeInvitationCode();
            const quint16 port = quint16(parser.value(hostOption).toUInt());
            QString error;
            if (window.startHosting(name.isEmpty() ? mothion::ui::defaultPersonName() : name,
                                    port ? port : mothion::ui::kDefaultSessionPort, code, &error))
                qInfo().noquote() << "Sharing the project: port" << window.sessionPort() << "code" << code;
            else
                qWarning().noquote() << "The session could not start:" << error;
        } else if (parser.isSet(joinOption)) {
            const QString target = parser.value(joinOption);
            const int colon = target.lastIndexOf(QLatin1Char(':'));
            const QString host = colon > 0 ? target.left(colon) : target;
            const quint16 port = colon > 0 ? quint16(target.mid(colon + 1).toUInt()) : quint16(0);
            window.joinSession(host, port ? port : mothion::ui::kDefaultSessionPort, parser.value(codeOption),
                               name.isEmpty() ? mothion::ui::defaultPersonName() : name);
        }
    });
    return app.exec();
}
