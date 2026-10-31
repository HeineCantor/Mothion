#pragma once

#include <QMainWindow>

class QMenu;
class QUndoStack;

namespace mothion::canvas {
class CanvasView;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::ui {

class DockLayout;

// The single workspace window.
//
// It creates the shared ToolState, the canvas and the panels, wires them
// together through ToolState, and hands the panels to DockLayout, which
// decides where they go and persists the arrangement.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createCommandBar();
    void createMenus();
    void notImplemented(const QString &what);

    tools::ToolState *m_toolState = nullptr;
    canvas::CanvasView *m_canvas = nullptr;
    QUndoStack *m_undoStack = nullptr;
    DockLayout *m_dockLayout = nullptr;
    QMenu *m_viewMenu = nullptr;
    QList<QAction *> m_fileActions;
    QList<QAction *> m_editActions;
};

} // namespace mothion::ui
