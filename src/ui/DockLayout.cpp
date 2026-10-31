#include "ui/DockLayout.h"

#include "ui/PanelDock.h"

#include <QMainWindow>
#include <QSettings>
#include <QTimer>

namespace mothion::ui {

namespace {

// Bump when panels are added/renamed so stale saved layouts are ignored.
constexpr int kLayoutVersion = 1;

const char *kGeometryKey = "layout/geometry";
const char *kStateKey = "layout/state";
const char *kCollapsedKey = "layout/collapsed";

} // namespace

DockLayout::DockLayout(QMainWindow *window, const WorkspacePanels &panels)
    : QObject(window)
    , m_window(window)
{
    window->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks
                           | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    // The timeline spans the full width under the side panels (as in Harmony / CSP).
    window->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    window->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    window->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    m_tools = addDock(QStringLiteral("dock.tools"), tr("Strumenti"), panels.tools);
    m_toolProperties = addDock(QStringLiteral("dock.toolProperties"), tr("Proprietà strumento"),
                               panels.toolProperties);
    m_color = addDock(QStringLiteral("dock.color"), tr("Colore"), panels.color);
    m_layers = addDock(QStringLiteral("dock.layers"), tr("Livelli"), panels.layers);
    m_timeline = addDock(QStringLiteral("dock.timeline"), tr("Timeline"), panels.timeline);

    applyDefault();
}

PanelDock *DockLayout::addDock(const QString &id, const QString &title, QWidget *content)
{
    auto *dock = new PanelDock(title, id, content, m_window);
    m_docks.append(dock);
    return dock;
}

void DockLayout::applyDefault()
{
    for (PanelDock *dock : m_docks) {
        dock->setFloating(false);
        dock->setCollapsed(false);
        m_window->removeDockWidget(dock); // also un-tabifies
    }

    // Left: narrow tool palette, tool properties beside it.
    m_window->addDockWidget(Qt::LeftDockWidgetArea, m_tools);
    m_window->splitDockWidget(m_tools, m_toolProperties, Qt::Horizontal);
    // Right: color on top, layers below.
    m_window->addDockWidget(Qt::RightDockWidgetArea, m_color);
    m_window->splitDockWidget(m_color, m_layers, Qt::Vertical);
    // Bottom: timeline.
    m_window->addDockWidget(Qt::BottomDockWidgetArea, m_timeline);

    for (PanelDock *dock : m_docks)
        dock->show();

    // resizeDocks() is ignored until the main window has laid itself out once,
    // so the default sizes are applied on the next event-loop turn.
    m_defaultSizesPending = true;
    QTimer::singleShot(0, this, [this] {
        if (!m_defaultSizesPending) // a saved layout was restored meanwhile
            return;
        m_defaultSizesPending = false;
        m_window->resizeDocks({m_tools}, {48}, Qt::Horizontal);
        m_window->resizeDocks({m_toolProperties, m_color}, {260, 260}, Qt::Horizontal);
        m_window->resizeDocks({m_color, m_layers, m_timeline}, {260, 320, 220}, Qt::Vertical);
    });
}

bool DockLayout::restore()
{
    QSettings settings;
    const QByteArray geometry = settings.value(kGeometryKey).toByteArray();
    const QByteArray state = settings.value(kStateKey).toByteArray();
    if (geometry.isEmpty() || state.isEmpty())
        return false;

    m_window->restoreGeometry(geometry);
    if (!m_window->restoreState(state, kLayoutVersion))
        return false;
    m_defaultSizesPending = false;

    const QStringList collapsed = settings.value(kCollapsedKey).toStringList();
    for (PanelDock *dock : m_docks)
        dock->setCollapsed(collapsed.contains(dock->objectName()));
    return true;
}

void DockLayout::save() const
{
    QStringList collapsed;
    for (const PanelDock *dock : m_docks)
        if (dock->isCollapsed())
            collapsed << dock->objectName();

    QSettings settings;
    settings.setValue(kGeometryKey, m_window->saveGeometry());
    settings.setValue(kStateKey, m_window->saveState(kLayoutVersion));
    settings.setValue(kCollapsedKey, collapsed);
}

void DockLayout::resetToDefault()
{
    applyDefault();
}

} // namespace mothion::ui
