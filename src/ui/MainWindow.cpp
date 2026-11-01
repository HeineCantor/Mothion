#include "ui/MainWindow.h"

#include "canvas/CanvasView.h"
#include "document/DocumentController.h"
#include "input/ProximityFilter.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"
#include "tools/BrushLibrary.h"
#include "tools/ToolState.h"
#include "ui/DockLayout.h"
#include "ui/ExportDialog.h"
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
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QLabel>
#include <QMenuBar>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

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
    createCanvasArea();
    m_dockLayout = new DockLayout(this, panels);
    createMenus();
    statusBar()->showMessage(tr("Ready"));
    connect(m_canvas, &canvas::CanvasView::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
    auto *viewInfo = new QLabel(this);
    statusBar()->addPermanentWidget(viewInfo);
    connect(m_canvas, &canvas::CanvasView::viewChanged, viewInfo, [viewInfo](double zoom, double rot) {
        viewInfo->setText(tr("Zoom %1%  ·  Rotation %2°").arg(qRound(zoom * 100)).arg(qRound(rot)));
    });

    connect(m_document, &document::DocumentController::historyChanged, this, &MainWindow::updateTitle);
    updateTitle();

    m_dockLayout->restore();
}

void MainWindow::createCommandBar()
{
    auto make = [this](IconId id, const QString &text, QKeySequence::StandardKey key, auto slot) {
        auto *a = new QAction(icon(id), text, this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    m_fileActions << make(IconId::New, tr("New"), QKeySequence::New, &MainWindow::newDocument)
                  << make(IconId::Open, tr("Open…"), QKeySequence::Open, &MainWindow::open)
                  << make(IconId::Save, tr("Save"), QKeySequence::Save, &MainWindow::save);
    m_saveAsAction = new QAction(tr("Save as…"), this);
    // Not every platform has a standard Save As key (Windows has none).
    m_saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

    // Undo/redo run on the document's History; labels ("Undo stroke") and
    // enabled state follow DocumentController::historyChanged.
    QAction *undo = new QAction(icon(IconId::Undo), tr("Undo"), this);
    undo->setShortcuts(QKeySequence::Undo); // Ctrl+Z
    QAction *redo = new QAction(icon(IconId::Redo), tr("Redo"), this);
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

    auto *bar = new QToolBar(tr("Commands"), this);
    bar->setObjectName(QStringLiteral("toolbar.commands")); // needed by saveState()
    bar->setMovable(false);
    bar->setIconSize(QSize(20, 20));
    bar->addActions(m_fileActions);
    addToolBar(Qt::TopToolBarArea, bar);
}

void MainWindow::createCanvasArea()
{
    // View commands: on the canvas bar and in the View menu.
    // Provisional shortcuts; real keymap comes with the UX design.
    auto view = [this](IconId id, const QString &text, const QList<QKeySequence> &keys, auto slot) {
        auto *a = new QAction(icon(id), text, this);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, m_canvas, slot);
        return a;
    };
    m_zoomActions << view(IconId::ZoomOut, tr("Zoom out"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)},
                          &canvas::CanvasView::zoomOut)
                  << view(IconId::Zoom, tr("Zoom in"),
                          {QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)},
                          &canvas::CanvasView::zoomIn)
                  << view(IconId::ZoomFit, tr("Fit to window"), {QKeySequence(Qt::CTRL | Qt::Key_0)},
                          &canvas::CanvasView::fitToView)
                  << view(IconId::ZoomActual, tr("Actual size"), {QKeySequence(Qt::CTRL | Qt::Key_1)},
                          &canvas::CanvasView::actualSize);
    m_rotateActions << view(IconId::RotateLeft, tr("Rotate left"), {QKeySequence(Qt::CTRL | Qt::Key_Comma)},
                            &canvas::CanvasView::rotateLeft)
                    << view(IconId::RotateRight, tr("Rotate right"), {QKeySequence(Qt::CTRL | Qt::Key_Period)},
                            &canvas::CanvasView::rotateRight)
                    << view(IconId::RotateReset, tr("Reset rotation"), {QKeySequence(Qt::CTRL | Qt::Key_Slash)},
                            &canvas::CanvasView::resetRotation);

    // Clear: enabled only when the current drawing has pixels and may be changed.
    m_clearAction = new QAction(icon(IconId::Clear), tr("Clear layer"), this);
    m_clearAction->setToolTip(tr("Clear layer: erase the current layer's drawing on this frame"));
    connect(m_clearAction, &QAction::triggered, this, [this] {
        QString reason;
        if (!m_document->clearCurrentDrawing(&reason))
            statusBar()->showMessage(reason, 4000);
    });
    auto syncClear = [this] { m_clearAction->setEnabled(m_document->canClearCurrentDrawing()); };
    using document::DocumentController;
    connect(m_document, &DocumentController::structureChanged, this, syncClear);
    connect(m_document, &DocumentController::currentLayerChanged, this, syncClear);
    connect(m_document, &DocumentController::currentFrameChanged, this, syncClear);
    connect(m_document, &DocumentController::pixelsChanged, this, syncClear);
    connect(m_document, &DocumentController::historyChanged, this, syncClear);
    syncClear();

    // The bar sits right above the canvas, inside the central area (not a
    // movable QMainWindow toolbar): it belongs to the canvas, not the window.
    auto *bar = new QToolBar(tr("Canvas"), this);
    bar->setObjectName(QStringLiteral("toolbar.canvas"));
    bar->setIconSize(QSize(18, 18));
    bar->addActions(m_editActions);
    bar->addSeparator();
    bar->addAction(m_clearAction);
    bar->addSeparator();
    bar->addActions(m_zoomActions);
    bar->addSeparator();
    bar->addActions(m_rotateActions);

    auto *area = new QWidget(this);
    auto *layout = new QVBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_canvas, 1);
    setCentralWidget(area);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addActions(m_fileActions);
    file->addAction(m_saveAsAction);
    file->addSeparator();
    file->addAction(tr("Import image…"), this, &MainWindow::importImage);
    file->addAction(tr("Import brush (.sut)…"), this, &MainWindow::importBrush);
    file->addSeparator();
    file->addAction(tr("Export frames…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::exportFrames);
    file->addSeparator();
    QAction *quit = file->addAction(tr("Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions(m_editActions);
    edit->addSeparator();
    edit->addAction(m_clearAction);
    edit->addSeparator();
    edit->addAction(tr("Select all"), QKeySequence::SelectAll, m_document, &document::DocumentController::selectAll);
    edit->addAction(tr("Deselect"), QKeySequence(Qt::CTRL | Qt::Key_D), m_document,
                    &document::DocumentController::clearSelection);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addActions(m_zoomActions);
    m_viewMenu->addSeparator();
    m_viewMenu->addActions(m_rotateActions);
    m_viewMenu->addSeparator();
    QAction *debug = m_viewMenu->addAction(tr("Input debug overlay"));
    debug->setCheckable(true);
    debug->setShortcut(QKeySequence(Qt::Key_F12));
    debug->setShortcutContext(Qt::ApplicationShortcut); // also when a floating panel has focus
    connect(debug, &QAction::toggled, m_canvas, &canvas::CanvasView::setDebugOverlayVisible);

    QMenu *window = menuBar()->addMenu(tr("&Window"));
    for (PanelDock *dock : m_dockLayout->docks())
        window->addAction(dock->toggleViewAction());
    window->addSeparator();
    QMenu *theme = window->addMenu(tr("Theme"));
    auto *themeGroup = new QActionGroup(this);
    const std::pair<QString, ThemeChoice> themes[] = {
        {tr("System"), ThemeChoice::System},
        {tr("Light"), ThemeChoice::Light},
        {tr("Dark"), ThemeChoice::Dark},
    };
    for (const auto &[text, choice] : themes) {
        QAction *a = theme->addAction(text);
        a->setCheckable(true);
        a->setChecked(choice == themeChoice());
        themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [choice] { setThemeChoice(*qApp, choice); });
    }
    window->addSeparator();
    window->addAction(tr("Reset default layout"), m_dockLayout, &DockLayout::resetToDefault);
}

void MainWindow::importImage()
{
    QStringList patterns;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    const QString file = QFileDialog::getOpenFileName(this, tr("Import image"), QString(),
                                                      tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (file.isEmpty())
        return;
    QImageReader reader(file);
    reader.setAutoTransform(true); // photos: apply their EXIF orientation
    const QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Import image"),
                             tr("Cannot read %1: %2").arg(QFileInfo(file).fileName(), reader.errorString()));
        return;
    }
    m_document->importImage(image, QFileInfo(file).completeBaseName());
    statusBar()->showMessage(tr("Image imported: %1").arg(QFileInfo(file).fileName()), 4000);
}

void MainWindow::importBrush()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import brush"), QString(),
                                                            tr("Clip Studio Paint sub tools (*.sut)"));
    for (const QString &file : files) {
        QString error;
        QStringList warnings;
        if (!m_brushes->importSut(file, &error, &warnings)) {
            QMessageBox::warning(this, tr("Import brush"), error);
            continue;
        }
        if (warnings.isEmpty())
            statusBar()->showMessage(tr("Brush imported: %1").arg(QFileInfo(file).completeBaseName()), 4000);
        else
            QMessageBox::information(this, tr("Import brush"),
                                     tr("%1 imported. Not reproduced yet:\n• %2")
                                         .arg(QFileInfo(file).completeBaseName(),
                                              warnings.join(QStringLiteral("\n• "))));
    }
}

// --- project files ---------------------------------------------------------------

namespace {

const QString kProjectDirKey = QStringLiteral("paths/projects");

QString projectFilter()
{
    return MainWindow::tr("Mothion projects (*.%1)").arg(QLatin1String(io::kProjectSuffix));
}

QString projectDirectory()
{
    return QSettings().value(kProjectDirKey).toString();
}

void rememberProjectDirectory(const QString &file)
{
    QSettings().setValue(kProjectDirKey, QFileInfo(file).absolutePath());
}

} // namespace

void MainWindow::newDocument()
{
    if (!maybeSave())
        return;
    m_document->newDocument();
    m_filePath.clear();
    updateTitle();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Open project"), projectDirectory(), projectFilter());
    if (!file.isEmpty())
        openFile(file);
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    std::unique_ptr<doc::Document> loaded = io::loadProject(path, &error);
    if (!loaded) {
        QMessageBox::warning(this, tr("Open project"), error);
        return false;
    }
    m_document->replaceDocument(std::move(loaded));
    m_filePath = QFileInfo(path).absoluteFilePath();
    rememberProjectDirectory(m_filePath);
    updateTitle();
    statusBar()->showMessage(tr("Opened %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

bool MainWindow::save()
{
    return m_filePath.isEmpty() ? saveAs() : saveTo(m_filePath);
}

bool MainWindow::saveAs()
{
    const QString start = m_filePath.isEmpty()
        ? QDir(projectDirectory()).filePath(tr("Untitled") + QLatin1Char('.') + QLatin1String(io::kProjectSuffix))
        : m_filePath;
    QString file = QFileDialog::getSaveFileName(this, tr("Save project"), start, projectFilter());
    if (file.isEmpty())
        return false;
    if (QFileInfo(file).suffix() != QLatin1String(io::kProjectSuffix))
        file += QLatin1Char('.') + QLatin1String(io::kProjectSuffix);
    return saveTo(file);
}

bool MainWindow::saveTo(const QString &path)
{
    m_document->finishEdits(); // an open puppet warp goes into the file
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool saved = io::saveProject(m_document->document(), path, &error);
    QApplication::restoreOverrideCursor();
    if (!saved) {
        QMessageBox::critical(this, tr("Save project"), error);
        return false;
    }
    m_filePath = QFileInfo(path).absoluteFilePath();
    rememberProjectDirectory(m_filePath);
    m_document->markSaved();
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

bool MainWindow::maybeSave()
{
    m_document->finishEdits();
    if (!m_document->isModified())
        return true;
    const QString name = m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
                                             tr("Save the changes to %1 before closing it?").arg(name),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                             QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::updateTitle()
{
    const QString name = m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).completeBaseName();
    setWindowTitle(name + QStringLiteral("[*] — Mothion"));
    setWindowModified(m_document->isModified());
}

void MainWindow::exportFrames()
{
    m_document->finishEdits();
    const doc::Document &d = m_document->document();
    const QString name = m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).completeBaseName();
    ExportDialog dialog(m_document->currentFrame(), d.frameCount(), name, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    bool done = false;
    const QString file = dialog.path();
    if (!dialog.isSequence()) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        done = io::exportFrame(d, dialog.firstFrame(), file, dialog.options(), &error);
        QApplication::restoreOverrideCursor();
    } else {
        const int total = dialog.lastFrame() - dialog.firstFrame() + 1;
        QProgressDialog progress(tr("Exporting frames…"), tr("Cancel"), 0, total, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(500);
        done = io::exportSequence(d, dialog.firstFrame(), dialog.lastFrame(), file, dialog.options(),
                                  [&progress](int count, int) {
                                      progress.setValue(count);
                                      return !progress.wasCanceled();
                                  },
                                  &error);
        done = done && !progress.wasCanceled();
        if (progress.wasCanceled() && error.isEmpty()) {
            statusBar()->showMessage(tr("Export cancelled"), 4000);
            return;
        }
    }
    if (!done) {
        QMessageBox::critical(this, tr("Export frames"), error);
        return;
    }
    statusBar()->showMessage(dialog.isSequence()
                                 ? tr("Exported %1 frames to %2").arg(dialog.lastFrame() - dialog.firstFrame() + 1)
                                       .arg(QFileInfo(file).absolutePath())
                                 : tr("Exported %1").arg(QFileInfo(file).fileName()),
                             4000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    m_dockLayout->save();
    QMainWindow::closeEvent(event);
}

} // namespace mothion::ui
