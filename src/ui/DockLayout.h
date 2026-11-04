#pragma once

#include <QList>
#include <QObject>

class QMainWindow;
class QWidget;

namespace mothion::ui {

class PanelDock;

// The content widgets of the workspace, created by MainWindow.
struct WorkspacePanels
{
    QWidget *tools = nullptr;
    QWidget *subTools = nullptr;
    QWidget *toolProperties = nullptr;
    QWidget *toolPropertiesFooter = nullptr; // fixed under it, not scrolled
    QWidget *brushSliders = nullptr;
    QWidget *brushSize = nullptr;
    QWidget *colorWheel = nullptr;
    QWidget *colorSet = nullptr;
    QWidget *navigator = nullptr;
    QWidget *layers = nullptr;
    QWidget *timeline = nullptr;
};

// Owns the arrangement of the dock panels around the canvas:
//  - wraps each panel in a PanelDock,
//  - knows the default arrangement (the only place that does),
//  - saves / restores the user's arrangement through QSettings.
//
// When the real UX is designed, changes to the layout happen here.
class DockLayout : public QObject
{
    Q_OBJECT

public:
    DockLayout(QMainWindow *window, const WorkspacePanels &panels);

    const QList<PanelDock *> &docks() const { return m_docks; }

    // Restores geometry, dock state and collapsed panels. Returns false (and
    // leaves the default arrangement) if nothing valid was saved.
    bool restore();
    void save() const;

    // Puts every panel back where applyDefault() places it, expanded and visible.
    void resetToDefault();

private:
    PanelDock *addDock(const QString &id, const QString &title, QWidget *content, QWidget *footer = nullptr);
    void applyDefault();

    QMainWindow *m_window;
    QList<PanelDock *> m_docks;
    PanelDock *m_tools = nullptr;
    PanelDock *m_subTools = nullptr;
    PanelDock *m_toolProperties = nullptr;
    PanelDock *m_brushSliders = nullptr;
    PanelDock *m_brushSize = nullptr;
    PanelDock *m_colorWheel = nullptr;
    PanelDock *m_colorSet = nullptr;
    PanelDock *m_navigator = nullptr;
    PanelDock *m_layers = nullptr;
    PanelDock *m_timeline = nullptr;
    bool m_defaultSizesPending = false;
};

} // namespace mothion::ui
