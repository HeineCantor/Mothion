#pragma once

#include <QMainWindow>

namespace mothion::ui {

// The single workspace window. It owns the shared state and the canvas,
// and delegates dock creation and persistence to DockLayout.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
};

} // namespace mothion::ui
