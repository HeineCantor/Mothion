#include "ui/MainWindow.h"

#include "canvas/CanvasView.h"
#include "document/DocumentController.h"
#include "input/ProximityFilter.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"
#include "tools/BrushLibrary.h"
#include "tools/ToolState.h"
#include "ui/AutoplayWindow.h"
#include "ui/DockLayout.h"
#include "ui/ExportDialog.h"
#include "ui/IconFactory.h"
#include "ui/PanelDock.h"
#include "ui/SelectionBar.h"
#include "ui/TransformBar.h"
#include "ui/Theme.h"
#include "ui/panels/BrushSizePanel.h"
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

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDataStream>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

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
    panels.toolPropertiesFooter = properties->footer();
    panels.brushSliders = new BrushSlidersPanel(m_toolState, this);
    panels.brushSize = new BrushSizePanel(m_toolState, this);
    panels.colorWheel = new ColorWheelPanel(m_toolState, this);
    panels.colorSet = new ColorSetPanel(m_toolState, this);
    panels.navigator = new NavigatorPanel(m_canvas, this);
    panels.layers = new LayersPanel(m_document, this);
    panels.timeline = new TimelinePanel(m_document, m_toolState, this);

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
    connect(static_cast<LayersPanel *>(panels.layers), &LayersPanel::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
    auto *selectionBar = new SelectionBar(m_canvas, m_document, m_toolState);
    connect(selectionBar, &SelectionBar::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
    new TransformBar(m_canvas);
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
    // enabled state follow DocumentController::historyChanged. An edit left
    // open on the canvas (puppet warp, transform) goes back through its own
    // steps first.
    QAction *undo = new QAction(icon(IconId::Undo), tr("Undo"), this);
    undo->setShortcuts(QKeySequence::Undo); // Ctrl+Z
    QAction *redo = new QAction(icon(IconId::Redo), tr("Redo"), this);
    // Platform standard (Ctrl+Shift+Z on Linux, Ctrl+Y on Windows) plus both everywhere.
    QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)})
        if (!redoKeys.contains(k))
            redoKeys << k;
    redo->setShortcuts(redoKeys);
    // Also from floating panels, which are windows of their own.
    undo->setShortcutContext(Qt::ApplicationShortcut);
    redo->setShortcutContext(Qt::ApplicationShortcut);
    connect(undo, &QAction::triggered, this, [this] {
        if (!m_canvas->undoInEdit())
            m_document->undo();
    });
    connect(redo, &QAction::triggered, this, [this] {
        if (!m_canvas->redoInEdit())
            m_document->redo();
    });
    auto syncHistory = [this, undo, redo] {
        const bool inEditUndo = m_canvas->canUndoInEdit(), inEditRedo = m_canvas->canRedoInEdit();
        undo->setEnabled(inEditUndo || m_document->canUndo());
        undo->setText(inEditUndo ? tr("Undo step") : m_document->undoText());
        redo->setEnabled(inEditRedo || m_document->canRedo());
        redo->setText(inEditRedo ? tr("Redo step") : m_document->redoText());
    };
    connect(m_document, &document::DocumentController::historyChanged, this, syncHistory);
    connect(m_canvas, &canvas::CanvasView::editStepsChanged, this, syncHistory);
    syncHistory();
    m_editActions << undo << redo;
    qApp->installEventFilter(this);

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
        if (!m_document->clearCurrentLayer(&reason))
            statusBar()->showMessage(reason, 4000);
    });
    auto syncClear = [this] { m_clearAction->setEnabled(m_document->canClearCurrentLayer()); };
    using document::DocumentController;
    connect(m_document, &DocumentController::structureChanged, this, syncClear);
    connect(m_document, &DocumentController::currentCelChanged, this, syncClear);
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

    // Snapping the pen to a perspective layer's axes, on the right of the bar: the on/off button,
    // the layer it snaps to, and a sight to pick that layer on the canvas.
    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    auto *snapAction = new QAction(icon(IconId::Snap), tr("Snap to perspective"), this);
    snapAction->setCheckable(true);
    snapAction->setToolTip(tr("Snap strokes to an axis of the perspective layer: the one nearest the direction drawn"));
    bar->addAction(snapAction);
    auto *snapLayer = new QComboBox(bar);
    snapLayer->setObjectName(QStringLiteral("snapLayer"));
    snapLayer->setMinimumWidth(150);
    snapLayer->setToolTip(tr("The perspective layer to snap to"));
    bar->addWidget(snapLayer);
    auto *pickAction = new QAction(icon(IconId::Crosshair), tr("Pick perspective layer"), this);
    pickAction->setCheckable(true);
    pickAction->setToolTip(tr("Click on the canvas to pick the perspective layer to snap to"));
    bar->addAction(pickAction);

    connect(snapAction, &QAction::toggled, m_toolState, &tools::ToolState::setPerspectiveSnap);
    connect(pickAction, &QAction::toggled, m_canvas, &canvas::CanvasView::setSnapPicking);
    connect(m_canvas, &canvas::CanvasView::snapPickingChanged, pickAction, [pickAction](bool on) {
        const QSignalBlocker block(pickAction);
        pickAction->setChecked(on);
    });
    connect(snapLayer, &QComboBox::activated, this, [this, snapLayer](int index) {
        if (index >= 0 && snapLayer->itemData(index).isValid())
            m_toolState->setPerspectiveSnapLayer(snapLayer->itemData(index).toULongLong(), snapLayer->itemText(index));
    });
    // The list follows the frame: the perspective layers shown on it, the one in use selected.
    auto syncSnap = [this, snapAction, snapLayer] {
        {
            const QSignalBlocker block(snapAction);
            snapAction->setChecked(m_toolState->perspectiveSnap());
        }
        const QSignalBlocker block(snapLayer);
        snapLayer->clear();
        const auto layers = m_document->perspectiveLayers();
        for (const auto &layer : layers)
            snapLayer->addItem(layer.name, qulonglong(layer.ref.layer));
        snapLayer->setEnabled(!layers.empty());
        if (layers.empty()) {
            snapLayer->addItem(tr("No perspective layer"));
            return;
        }
        if (const auto used = m_document->perspectiveLayerFor(m_toolState->perspectiveSnapLayer(),
                                                              m_toolState->perspectiveSnapLayerName()))
            snapLayer->setCurrentIndex(snapLayer->findData(qulonglong(used->ref.layer)));
    };
    connect(m_toolState, &tools::ToolState::perspectiveSnapChanged, this, syncSnap);
    connect(m_document, &document::DocumentController::structureChanged, this, syncSnap);
    connect(m_document, &document::DocumentController::currentFrameChanged, this, syncSnap);
    connect(m_document, &document::DocumentController::documentReplaced, this, syncSnap);
    connect(m_document, &document::DocumentController::historyChanged, this, syncSnap);
    syncSnap();

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
    edit->addAction(tr("Invert selection"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), m_document,
                    &document::DocumentController::invertSelection);
    edit->addSeparator();
    edit->addAction(tr("Cut"), QKeySequence::Cut, this, [this] { copyPixels(true); });
    edit->addAction(tr("Copy"), QKeySequence::Copy, this, [this] { copyPixels(false); });
    edit->addAction(tr("Paste"), QKeySequence::Paste, this, &MainWindow::pastePixels);
    edit->addSeparator();
    edit->addAction(icon(IconId::Transform), tr("Transform"), QKeySequence(Qt::CTRL | Qt::Key_T), this,
                    [this] { m_toolState->setActiveTool(tools::ToolId::Transform); });

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

    QMenu *animation = menuBar()->addMenu(tr("&Animation"));
    animation->addAction(tr("Autoplay"), this, [this] {
        if (!m_autoplay) {
            m_autoplay = new AutoplayWindow(m_document, this);
            m_autoplay->setAttribute(Qt::WA_DeleteOnClose); // closed: made again, playing, next time
        }
        m_autoplay->show();
        m_autoplay->raise();
        m_autoplay->activateWindow();
    });
    animation->addSeparator();
    // One frame back or on along the timeline, from anywhere in the window
    // (text and number fields keep their keys); stops at the ends.
    auto step = [this](int by) {
        m_document->setCurrentFrame(
            std::clamp(m_document->currentFrame() + by, 0, m_document->document().frameCount() - 1));
    };
    animation->addAction(tr("Previous time slot"), QKeySequence(Qt::Key_Comma), this, [step] { step(-1); });
    animation->addAction(tr("Next time slot"), QKeySequence(Qt::Key_Period), this, [step] { step(+1); });
    // Over holds and empty slots, to where the current cel's frames start.
    animation->addAction(tr("Previous frame"), QKeySequence(Qt::SHIFT | Qt::Key_Comma), this,
                         [this] { m_document->stepToFrame(-1); });
    animation->addAction(tr("Next frame"), QKeySequence(Qt::SHIFT | Qt::Key_Period), this,
                         [this] { m_document->stepToFrame(+1); });
    animation->addSeparator();
    // The hold of the frame at the current point, one frame longer or shorter;
    // the frames after it move along.
    QAction *longer = animation->addAction(tr("Lengthen frame"), this, [this] { m_document->changeCurrentHold(+1); });
    longer->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::KeypadModifier | Qt::Key_Plus)});
    QAction *shorter = animation->addAction(tr("Shorten frame"), this, [this] { m_document->changeCurrentHold(-1); });
    shorter->setShortcuts({QKeySequence(Qt::Key_Minus), QKeySequence(Qt::KeypadModifier | Qt::Key_Minus)});

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

namespace {

// Our own copies also say where the pixels were, to paste them in place.
const QString kPixelsMime = QStringLiteral("application/x-mothion-pixels");

} // namespace

void MainWindow::copyPixels(bool cut)
{
    // With the Layers panel's rows in focus, Ctrl+C and Ctrl+X take layers.
    if (auto *layers = findChild<LayersPanel *>(); layers && layers->hasRowFocus()) {
        if (cut)
            layers->cutLayers();
        else
            layers->copyLayers();
        return;
    }
    QString reason;
    QPoint origin;
    const QImage image = m_document->copyPixels(&origin, &reason);
    if (image.isNull()) {
        statusBar()->showMessage(reason, 4000);
        return;
    }
    auto *mime = new QMimeData;
    mime->setImageData(image);
    QByteArray ours;
    QDataStream out(&ours, QIODevice::WriteOnly);
    out << origin << image;
    mime->setData(kPixelsMime, ours);
    QGuiApplication::clipboard()->setMimeData(mime);
    if (!cut)
        return;
    const bool erased = m_document->hasSelection() ? m_document->eraseSelection(&reason)
                                                   : m_document->clearCurrentLayer(&reason);
    if (!erased)
        statusBar()->showMessage(tr("Copied, not cut: %1").arg(reason), 4000);
}

void MainWindow::pastePixels()
{
    if (auto *layers = findChild<LayersPanel *>(); layers && layers->hasRowFocus() && m_document->hasCopiedLayers()) {
        layers->pasteLayers(false);
        return;
    }
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QImage image;
    std::optional<QPoint> origin;
    if (mime && mime->hasFormat(kPixelsMime)) {
        QDataStream in(mime->data(kPixelsMime));
        QPoint at;
        in >> at >> image;
        if (in.status() == QDataStream::Ok)
            origin = at;
    }
    if (image.isNull() && mime && mime->hasImage())
        image = qvariant_cast<QImage>(mime->imageData()); // from another app: centered
    QString reason;
    if (!m_document->pasteImage(image, origin, &reason))
        statusBar()->showMessage(reason.isEmpty() ? tr("Nothing to paste") : reason, 4000);
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

// Opening: projects saved under the old name too.
QString openProjectFilter()
{
    return MainWindow::tr("Mothion projects (*.%1 *.%2)")
        .arg(QLatin1String(io::kProjectSuffix), QLatin1String(io::kOldProjectSuffix));
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
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Open project"), projectDirectory(), openProjectFilter());
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

namespace {

// Where a typed space or tab belongs to the text, not to the canvas.
bool isTextEntry(const QWidget *widget)
{
    for (; widget; widget = widget->parentWidget())
        if (qobject_cast<const QLineEdit *>(widget) || qobject_cast<const QAbstractSpinBox *>(widget)
            || qobject_cast<const QTextEdit *>(widget) || qobject_cast<const QPlainTextEdit *>(widget))
            return true;
    return false;
}

// In this window or one of its panels (docked or floating), not in a dialog.
bool isPartOf(const QWidget *widget, const QWidget *window)
{
    for (; widget; widget = widget->parentWidget()) {
        if (widget == window)
            return true;
        // Windows of their own with their own keys (Space plays the autoplay).
        if (qobject_cast<const QDialog *>(widget) || qobject_cast<const AutoplayWindow *>(widget))
            return false;
    }
    return false;
}

} // namespace

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The pan keys (Space, Tab) are for the canvas wherever the focus is,
    // unless typing: after picking a brush from a list, say, holding Tab
    // must pan at the first touch, not move the focus around the panel.
    if (event->type() == QEvent::KeyPress && watched != m_canvas && !QApplication::activePopupWidget()) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        auto *widget = qobject_cast<QWidget *>(watched);
        if ((key == Qt::Key_Space || key == Qt::Key_Tab) && widget && isPartOf(widget, this) && !isTextEntry(widget)) {
            m_canvas->setFocus(Qt::OtherFocusReason);
            QApplication::sendEvent(m_canvas, event);
            return true;
        }
    }

    // A spin box claims Ctrl+Z for the text typed in it, so the document's
    // undo would not run while one has focus (after nudging a size, say).
    // Number fields have nothing worth undoing as text: leave them the keys.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *widget = qobject_cast<QWidget *>(watched);
        const bool inSpinBox = widget && (qobject_cast<QAbstractSpinBox *>(widget)
                                          || qobject_cast<QAbstractSpinBox *>(widget->parentWidget()));
        if (inSpinBox) {
            const QKeySequence key(static_cast<QKeyEvent *>(event)->keyCombination());
            for (const QAction *action : std::as_const(m_editActions))
                if (action->shortcuts().contains(key))
                    return true; // not accepted: the shortcut runs instead
        }
    }
    return QMainWindow::eventFilter(watched, event);
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
