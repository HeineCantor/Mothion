// Entry point: input-related application attributes, command line,
// application identity (used by QSettings), theme, main window.

#include "input/TabletBackend.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>

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

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption wintab(QStringLiteral("wintab"),
                                    QStringLiteral("Windows: use the Wintab tablet API instead of Windows Ink."));
    parser.addOption(wintab);
    parser.process(app);

    if (parser.isSet(wintab) && !mothion::input::setWinTabEnabled(true))
        qWarning("Wintab is not available (not on Windows, disabled at build time, or no wintab32.dll); "
                 "using the default tablet backend.");

    mothion::ui::applySystemTheme(app);

    mothion::ui::MainWindow window;
    window.show();
    return app.exec();
}
