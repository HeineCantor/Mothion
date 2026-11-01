#include "ui/MainWindow.h"

#include "canvas/CanvasView.h"
#include "document/DocumentController.h"
#include "input/ProximityFilter.h"
#include "tools/BrushLibrary.h"
#include "tools/ToolState.h"
#include "ui/DockLayout.h"
#include "ui/IconFactory.h"
#include "ui/PanelDock.h"
#include "ui/Theme.h"
#include "ui/panels/BrushSlidersPanel.h"
#include "ui/panels/ColorSetPanel.h"
#include "ui/panels/ColorWheelPanel.h"
#include "ui/panels/LayersPanel.h"
#include "ui/panels/NavigatorPanel.h"
#include "ui/panels/PressureCurveEditor.h"
#include "ui/panels/SubToolPanel.h"
#include "ui/panels/ToolPalettePanel.h"
#include "ui/panels/ToolPropertiesPanel.h"
#include "ui/panels/timeline/TimelinePanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

namespace mothion::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_toolState(new tools::ToolState(this))
    , m_brushes(new tools::BrushLibrary(this))
    , m_document(new document::DocumentController(this))
    , m_canvas(new canvas::CanvasView(m_toolState, m_document, this))
{
    setWindowTitle(QStringLiteral("Mothion"));
    resize(1400, 900);

    setCentralWidget(m_canvas);

    auto *properties = new ToolPropertiesPanel(m_toolState, this);
    WorkspacePanels panels;
    panels.tools = new ToolPalettePanel(m_toolState, this);
    panels.subTools = new SubToolPanel(m_toolState, m_brushes, this);
    panels.toolProperties = properties;
    panels.brushSliders = new BrushSlidersPanel(m_toolState, this);
    panels.colorWheel = new ColorWheelPanel(m_toolState, this);
    panels.colorSet = new ColorSetPanel(m_toolState, this);
    panels.navigator = new NavigatorPanel(m_canvas, this);
    panels.layers = new LayersPanel(m_document, this);
    panels.timeline = new TimelinePanel(m_document, this);

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
    connect(m_canvas, &canvas::CanvasView::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
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

    // Undo/redo run on the document's History; labels ("Annulla tratto") and
    // enabled state follow DocumentController::historyChanged.
    QAction *undo = new QAction(icon(IconId::Undo), tr("Annulla"), this);
    undo->setShortcuts(QKeySequence::Undo); // Ctrl+Z
    QAction *redo = new QAction(icon(IconId::Redo), tr("Ripeti"), this);
    // Platform standard (Ctrl+Shift+Z on Linux, Ctrl+Y on Windows) plus both everywhere.
    QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)})
        if (!redoKeys.contains(k))
            redoKeys << k;
    redo->setShortcuts(redoKeys);
    connect(undo, &QAction::triggered, m_document, &document::DocumentController::undo);
    connect(redo, &QAction::triggered, m_document, &document::DocumentController::redo);
    auto syncHistory = [this, undo, redo] {
        undo->setEnabled(m_document->canUndo());
        undo->setText(m_document->undoText());
        redo->setEnabled(m_document->canRedo());
        redo->setText(m_document->redoText());
    };
    connect(m_document, &document::DocumentController::historyChanged, this, syncHistory);
    syncHistory();
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
    file->addAction(tr("Importa pennello (.sut)…"), this, &MainWindow::importBrush);
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
    QMenu *theme = window->addMenu(tr("Tema"));
    auto *themeGroup = new QActionGroup(this);
    const std::pair<QString, ThemeChoice> themes[] = {
        {tr("Sistema"), ThemeChoice::System},
        {tr("Chiaro"), ThemeChoice::Light},
        {tr("Scuro"), ThemeChoice::Dark},
    };
    for (const auto &[text, choice] : themes) {
        QAction *a = theme->addAction(text);
        a->setCheckable(true);
        a->setChecked(choice == themeChoice());
        themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [choice] { setThemeChoice(*qApp, choice); });
    }
    window->addSeparator();
    window->addAction(tr("Ripristina layout predefinito"), m_dockLayout, &DockLayout::resetToDefault);
}

void MainWindow::importBrush()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Importa pennello"), QString(),
                                                            tr("Sotto-strumenti Clip Studio Paint (*.sut)"));
    for (const QString &file : files) {
        QString error;
        QStringList warnings;
        if (!m_brushes->importSut(file, &error, &warnings)) {
            QMessageBox::warning(this, tr("Importa pennello"), error);
            continue;
        }
        if (warnings.isEmpty())
            statusBar()->showMessage(tr("Pennello importato: %1").arg(QFileInfo(file).completeBaseName()), 4000);
        else
            QMessageBox::information(this, tr("Importa pennello"),
                                     tr("%1 importato. Non ancora riprodotto:\n• %2")
                                         .arg(QFileInfo(file).completeBaseName(),
                                              warnings.join(QStringLiteral("\n• "))));
    }
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
