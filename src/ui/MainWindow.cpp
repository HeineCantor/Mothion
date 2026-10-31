#include "ui/MainWindow.h"

#include "tools/ToolState.h"
#include "ui/DockLayout.h"
#include "ui/IconFactory.h"
#include "ui/PanelDock.h"
#include "ui/panels/ColorPanel.h"
#include "ui/panels/LayersPanel.h"
#include "ui/panels/ToolPalettePanel.h"
#include "ui/panels/ToolPropertiesPanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

namespace mothion::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_toolState(new tools::ToolState(this))
{
    setWindowTitle(QStringLiteral("Mothion"));
    resize(1400, 900);

    setCentralWidget(new QLabel(tr("Canvas"), this));

    auto *properties = new ToolPropertiesPanel(m_toolState, this);
    WorkspacePanels panels;
    panels.tools = new ToolPalettePanel(m_toolState, this);
    panels.toolProperties = properties;
    panels.color = new ColorPanel(m_toolState, this);
    panels.layers = new LayersPanel(this);
    panels.timeline = new QLabel(tr("Timeline"), this);

    createCommandBar();
    m_dockLayout = new DockLayout(this, panels);
    createMenus();
    statusBar()->showMessage(tr("Pronto"));

    m_dockLayout->restore();
}

void MainWindow::createCommandBar()
{
    struct Command { IconId icon; QString text; QKeySequence key; };
    const Command fileCommands[] = {
        {IconId::New, tr("Nuovo"), QKeySequence::New},
        {IconId::Open, tr("Apri…"), QKeySequence::Open},
        {IconId::Save, tr("Salva"), QKeySequence::Save},
    };
    const Command editCommands[] = {
        {IconId::Undo, tr("Annulla"), QKeySequence::Undo},
        {IconId::Redo, tr("Ripeti"), QKeySequence::Redo},
    };

    auto make = [this](const Command &c) {
        auto *a = new QAction(icon(c.icon), c.text, this);
        a->setShortcut(c.key);
        connect(a, &QAction::triggered, this, [this, text = c.text] { notImplemented(text); });
        return a;
    };
    for (const Command &c : fileCommands)
        m_fileActions << make(c);
    for (const Command &c : editCommands)
        m_editActions << make(c);

    auto *bar = new QToolBar(tr("Comandi"), this);
    bar->setObjectName(QStringLiteral("toolbar.commands")); // needed by saveState()
    bar->setMovable(false);
    bar->setIconSize(QSize(20, 20));
    bar->addActions(m_fileActions);
    bar->addSeparator();
    bar->addActions(m_editActions);
    addToolBar(Qt::TopToolBarArea, bar);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addActions(m_fileActions);
    file->addSeparator();
    QAction *quit = file->addAction(tr("Esci"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *edit = menuBar()->addMenu(tr("&Modifica"));
    edit->addActions(m_editActions);

    m_viewMenu = menuBar()->addMenu(tr("&Vista"));

    QMenu *window = menuBar()->addMenu(tr("&Finestra"));
    for (PanelDock *dock : m_dockLayout->docks())
        window->addAction(dock->toggleViewAction());
    window->addSeparator();
    window->addAction(tr("Ripristina layout predefinito"), m_dockLayout, &DockLayout::resetToDefault);
}

void MainWindow::notImplemented(const QString &what)
{
    statusBar()->showMessage(tr("%1: non ancora implementato").arg(what), 3000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_dockLayout->save();
    QMainWindow::closeEvent(event);
}

} // namespace mothion::ui
