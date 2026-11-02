// Entry point: input-related application attributes, command line,
// application identity (used by QSettings), theme, splash, main window.

#include "input/TabletBackend.h"
#include "ui/MainWindow.h"
#include "ui/SplashScreen.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QIcon>
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

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption wintab(QStringLiteral("wintab"),
                                    QStringLiteral("Windows: use the Wintab tablet API instead of Windows Ink."));
    parser.addOption(wintab);
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
        if (!parser.positionalArguments().isEmpty())
            window.openFile(parser.positionalArguments().constFirst());
    });
    return app.exec();
}
