// Entry point: sets up the application identity (used by QSettings),
// applies the theme and shows the main window.

#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Mothion"));
    QApplication::setApplicationName(QStringLiteral("Mothion"));
    QApplication::setApplicationVersion(QStringLiteral(MOTHION_VERSION));

    mothion::ui::applyDarkTheme(app);

    mothion::ui::MainWindow window;
    window.show();
    return app.exec();
}
