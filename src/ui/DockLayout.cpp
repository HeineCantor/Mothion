#include "ui/DockLayout.h"

#include "ui/PanelDock.h"

#include <QMainWindow>
#include <QSettings>
#include <QTimer>

namespace mothion::ui {

namespace {

// Bump when panels are added/renamed so stale saved layouts are ignored.
constexpr int kLayoutVersion = 3;

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
    // Side columns run the full height (tall brush sliders); the timeline
    // sits under the canvas only.
    window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    window->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    m_tools = addDock(QStringLiteral("dock.tools"), tr("Tools"), panels.tools);
    m_subTools = addDock(QStringLiteral("dock.subTools"), tr("Sub tool"), panels.subTools);
    m_toolProperties = addDock(QStringLiteral("dock.toolProperties"), tr("Tool properties"),
                               panels.toolProperties, panels.toolPropertiesFooter);
    m_brushSliders = addDock(QStringLiteral("dock.brushSliders"), tr("Size and opacity"),
                             panels.brushSliders);
    m_brushSize = addDock(QStringLiteral("dock.brushSize"), tr("Brush size"), panels.brushSize);
    m_colorWheel = addDock(QStringLiteral("dock.colorWheel"), tr("Color wheel"), panels.colorWheel);
    m_colorSet = addDock(QStringLiteral("dock.colorSet"), tr("Color set"), panels.colorSet);
    m_navigator = addDock(QStringLiteral("dock.navigator"), tr("Navigator"), panels.navigator);
    m_layers = addDock(QStringLiteral("dock.layers"), tr("Layers"), panels.layers);
    m_timeline = addDock(QStringLiteral("dock.timeline"), tr("Timeline"), panels.timeline);

    applyDefault();
}

PanelDock *DockLayout::addDock(const QString &id, const QString &title, QWidget *content, QWidget *footer)
{
    auto *dock = new PanelDock(title, id, content, m_window, footer);
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

    // Left, three columns:  tools | sub tool+properties / color wheel / brush size+color set | sliders.
    // Order matters: the columns are split first, then only the middle one is
    // split vertically, so the tool palette and the sliders keep the full height.
    m_window->addDockWidget(Qt::LeftDockWidgetArea, m_tools);
    m_window->splitDockWidget(m_tools, m_subTools, Qt::Horizontal);
    m_window->splitDockWidget(m_subTools, m_brushSliders, Qt::Horizontal);
    m_window->splitDockWidget(m_subTools, m_colorWheel, Qt::Vertical);
    m_window->splitDockWidget(m_colorWheel, m_brushSize, Qt::Vertical);
    // The first dock of a tab group is its front tab.
    m_window->tabifyDockWidget(m_subTools, m_toolProperties);
    m_window->tabifyDockWidget(m_brushSize, m_colorSet);
    // Right: navigator on top, layers below.
    m_window->addDockWidget(Qt::RightDockWidgetArea, m_navigator);
    m_window->splitDockWidget(m_navigator, m_layers, Qt::Vertical);
    // Bottom (under the canvas): timeline.
    m_window->addDockWidget(Qt::BottomDockWidgetArea, m_timeline);

    for (PanelDock *dock : m_docks)
        dock->show();
    m_subTools->raise(); // front tab of the sub tool / properties group

    // resizeDocks() is ignored until the main window has laid itself out once,
    // so the default sizes are applied on the next event-loop turn.
    m_defaultSizesPending = true;
    QTimer::singleShot(0, this, [this] {
        if (!m_defaultSizesPending) // a saved layout was restored meanwhile
            return;
        m_defaultSizesPending = false;
        m_window->resizeDocks({m_tools, m_subTools, m_brushSliders, m_navigator}, {48, 240, 52, 280},
                              Qt::Horizontal);
        m_window->resizeDocks({m_subTools, m_colorWheel, m_brushSize}, {330, 270, 150}, Qt::Vertical);
        m_window->resizeDocks({m_navigator, m_layers, m_timeline}, {300, 400, 210}, Qt::Vertical);
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
