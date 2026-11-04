#pragma once

#include <QDockWidget>

class QLabel;
class QToolButton;

namespace mothion::ui {

// A QDockWidget with our own compact title bar: title, collapse, float, close.
//
// "Collapsed" hides the content and shrinks the dock to its title bar; it is
// our addition (QDockWidget has no such state), so DockLayout persists it
// separately from QMainWindow::saveState().
class PanelDock : public QDockWidget
{
    Q_OBJECT

public:
    // objectName must be unique and stable: QMainWindow::saveState() keys on it.
    // `footer`, if any, stays at the bottom, under the content, and does not
    // scroll with it.
    PanelDock(const QString &title, const QString &objectName, QWidget *content,
              QWidget *parent = nullptr, QWidget *footer = nullptr);

    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);

private:
    void updateButtons();

    QWidget *m_titleBar = nullptr;
    QToolButton *m_collapseButton = nullptr;
    bool m_collapsed = false;
};

} // namespace mothion::ui
