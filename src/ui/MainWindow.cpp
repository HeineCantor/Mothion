#include "ui/MainWindow.h"

#include "canvas/CanvasView.h"
#include "input/ProximityFilter.h"
#include "tools/ToolState.h"
#include "ui/DockLayout.h"
#include "ui/IconFactory.h"
#include "ui/PanelDock.h"
#include "ui/panels/ColorPanel.h"
#include "ui/panels/LayersPanel.h"
#include "ui/panels/PressureCurveEditor.h"
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
    , m_canvas(new canvas::CanvasView(m_toolState, this))
{
    setWindowTitle(QStringLiteral("Mothion"));
    resize(1400, 900);

    setCentralWidget(m_canvas);

    auto *properties = new ToolPropertiesPanel(m_toolState, this);
    WorkspacePanels panels;
    panels.tools = new ToolPalettePanel(m_toolState, this);
    panels.toolProperties = properties;
    panels.color = new ColorPanel(m_toolState, this);
    panels.layers = new LayersPanel(this);
    panels.timeline = new QLabel(tr("Timeline"), this);

    // Pen proximity only reaches the QApplication; the filter forwards it to the canvas.
    auto *proximity = new input::ProximityFilter(this);
    connect(proximity, &input::ProximityFilter::proximityChanged, m_canvas,
            &canvas::CanvasView::setPenProximity);
    // Live marker on the pressure curve while the tip touches.
    connect(m_canvas, &canvas::CanvasView::sampleReceived, properties->curveEditor(),
            [editor = properties->curveEditor()](const PenSample &s) {
                editor->showInputPressure(s.tipDown() ? s.pressure : -1.0f);
            });

    createCommandBar();
    m_dockLayout = new DockLayout(this, panels);
    createMenus();
    statusBar()->showMessage(tr("Pronto"));
    auto *viewInfo = new QLabel(this);
    statusBar()->addPermanentWidget(viewInfo);
    connect(m_canvas, &canvas::CanvasView::viewChanged, viewInfo, [viewInfo](double zoom, double rot) {
        viewInfo->setText(tr("Zoom %1%  ·  Rotazione %2°").arg(qRound(zoom * 100)).arg(qRound(rot)));
    });

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
    // Provisional shortcuts; real keymap comes with the UX design.
    auto addView = [this](const QString &text, const QList<QKeySequence> &keys, auto slot) {
        QAction *a = m_viewMenu->addAction(text, m_canvas, slot);
        a->setShortcuts(keys);
        return a;
    };
    addView(tr("Ingrandisci"), {QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)},
            &canvas::CanvasView::zoomIn);
    addView(tr("Riduci"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)}, &canvas::CanvasView::zoomOut);
    addView(tr("Adatta alla finestra"), {QKeySequence(Qt::CTRL | Qt::Key_0)}, &canvas::CanvasView::fitToView);
    m_viewMenu->addSeparator();
    addView(tr("Ruota a sinistra"), {QKeySequence(Qt::CTRL | Qt::Key_Comma)}, &canvas::CanvasView::rotateLeft);
    addView(tr("Ruota a destra"), {QKeySequence(Qt::CTRL | Qt::Key_Period)}, &canvas::CanvasView::rotateRight);
    addView(tr("Azzera rotazione"), {QKeySequence(Qt::CTRL | Qt::Key_Slash)}, &canvas::CanvasView::resetRotation);

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
