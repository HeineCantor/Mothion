#include "ui/MainWindow.h"

#include <QLabel>

namespace mothion::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Mothion"));
    setCentralWidget(new QLabel(tr("Canvas"), this));
    resize(1400, 900);
}

} // namespace mothion::ui
