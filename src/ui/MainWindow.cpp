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
#include "ui/panels/timeline/TimelinePanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoStack>

namespace mothion::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_toolState(new tools::ToolState(this))
    , m_canvas(new canvas::CanvasView(m_toolState, this))
    , m_undoStack(new QUndoStack(this))
{
    setWindowTitle(QStringLiteral("Mothion"));
    resize(1400, 900);

    setCentralWidget(m_canvas);
    // Each entry stores the stroke's bounding box twice; the limit caps memory.
    m_undoStack->setUndoLimit(100);
    m_canvas->setUndoStack(m_undoStack);

    auto *properties = new ToolPropertiesPanel(m_toolState, this);
    WorkspacePanels panels;
    panels.tools = new ToolPalettePanel(m_toolState, this);
    panels.toolProperties = properties;
    panels.color = new ColorPanel(m_toolState, this);
    panels.layers = new LayersPanel(this);
    panels.timeline = new TimelinePanel(this);

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

    auto make = [this](const Command &c) {
        auto *a = new QAction(icon(c.icon), c.text, this);
        a->setShortcut(c.key);
        connect(a, &QAction::triggered, this, [this, text = c.text] { notImplemented(text); });
        return a;
    };
    for (const Command &c : fileCommands)
        m_fileActions << make(c);

    // Undo/redo come from the stack, which keeps text ("Annulla tratto") and
    // enabled state in sync. Shortcuts must be set here: Qt 6 does not assign them.
    QAction *undo = m_undoStack->createUndoAction(this, tr("Annulla"));
    undo->setIcon(icon(IconId::Undo));
    undo->setShortcuts(QKeySequence::Undo); // Ctrl+Z
    QAction *redo = m_undoStack->createRedoAction(this, tr("Ripeti"));
    redo->setIcon(icon(IconId::Redo));
    // Platform standard (Ctrl+Shift+Z on Linux, Ctrl+Y on Windows) plus both everywhere.
    QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)})
        if (!redoKeys.contains(k))
            redoKeys << k;
    redo->setShortcuts(redoKeys);
    m_editActions << undo << redo;

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
    m_viewMenu->addSeparator();
    QAction *debug = m_viewMenu->addAction(tr("Overlay di debug input"));
    debug->setCheckable(true);
    debug->setShortcut(QKeySequence(Qt::Key_F12));
    debug->setShortcutContext(Qt::ApplicationShortcut); // also when a floating panel has focus
    connect(debug, &QAction::toggled, m_canvas, &canvas::CanvasView::setDebugOverlayVisible);

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
