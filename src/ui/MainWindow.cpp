#include "ui/MainWindow.h"

#include "canvas/CanvasView.h"
#include "diagnostics/CrashHandler.h"
#include "io/Autosave.h"
#include "document/DocumentController.h"
#include "input/ProximityFilter.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"
#include "tools/BrushLibrary.h"
#include "tools/ColorSetLibrary.h"
#include "tools/ToolState.h"
#include "audio/AudioPlayer.h"
#include "ui/AutoplayWindow.h"
#include "ui/compositing/CompositingWindow.h"
#include "tools/Preferences.h"
#include "core/Parallel.h"
#include "ui/DiscordPresence.h"
#include "ui/ImportSequenceDialog.h"
#include "ui/NewProjectDialog.h"
#include "ui/DockLayout.h"
#include "io/VideoExport.h"
#include "ui/ExportDialog.h"
#include "ui/TweenDialog.h"
#include "ui/VideoExportDialog.h"
#include "ui/IconFactory.h"
#include "ui/PanelDock.h"
#include "ui/SelectionBar.h"
#include "ui/PuppetBar.h"
#include "ui/ShapeBar.h"
#include "ai/ServerProcess.h"
#include "ui/RefineDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Shortcuts.h"
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
#include "ui/panels/timeline/TimelineView.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QTreeWidget>
#include <QCloseEvent>
#include <QDialog>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QThread>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace mothion::ui {

namespace {

// Keyboard focus follows the pointer over the canvas, the timeline and the layer rows: their keys (copy and paste,
// arrows, Delete...) act where the pointer is, not where the last click was. A field being typed in keeps it.
class FocusFollowsPointer : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Enter)
            return false;
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || !widget->isActiveWindow() || widget->hasFocus() || QApplication::activePopupWidget())
            return false;
        QWidget *current = QApplication::focusWidget();
        const bool typing = qobject_cast<QLineEdit *>(current) || qobject_cast<QAbstractSpinBox *>(current)
                            || qobject_cast<QTextEdit *>(current) || qobject_cast<QPlainTextEdit *>(current);
        if (!typing)
            widget->setFocus(Qt::MouseFocusReason);
        return false;
    }
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_toolState(new tools::ToolState(this))
    , m_brushes(new tools::BrushLibrary(this))
    , m_colorSets(new tools::ColorSetLibrary({}, this))
    , m_document(new document::DocumentController(this))
    , m_canvas(new canvas::CanvasView(m_toolState, m_document, this))
{
    m_audioPlayer = new audio::AudioPlayer(m_document, this);
    // If the program dies, a copy of the project is tried (the stroke being drawn is not in it).
    diagnostics::setEmergencySave([this](const QString &path) {
        return io::saveProject(m_document->document(), path, nullptr);
    });
    connect(this, &QObject::destroyed, [] { diagnostics::setEmergencySave({}); });
    setWindowTitle(QStringLiteral("Mothion"));
    resize(1400, 900);

    // A copy of the project with unsaved work, now and then (see autosaveTick()).
    m_autosaveTimer = new QTimer(this);
    m_lastInput.start();
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveTick);
    connect(m_document, &document::DocumentController::historyChanged, this, [this] { m_autosaveDirty = true; });
    connect(&tools::Preferences::instance(), &tools::Preferences::changed, this, &MainWindow::applyAutosaveInterval);
    applyAutosaveInterval();

    auto *properties = new ToolPropertiesPanel(m_toolState, this);
    WorkspacePanels panels;
    panels.tools = new ToolPalettePanel(m_toolState, this);
    panels.subTools = new SubToolPanel(m_toolState, m_brushes, this);
    panels.toolProperties = properties;
    panels.toolPropertiesFooter = properties->footer();
    panels.brushSliders = new BrushSlidersPanel(m_toolState, this);
    panels.brushSize = new BrushSizePanel(m_toolState, this);
    panels.colorWheel = new ColorWheelPanel(m_toolState, this);
    panels.colorSet = new ColorSetPanel(m_toolState, m_colorSets, this);
    panels.navigator = new NavigatorPanel(m_canvas, this);
    panels.layers = new LayersPanel(m_document, this);
    panels.timeline = new TimelinePanel(m_document, m_toolState, m_audioPlayer, this);

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
    {
        auto *follow = new FocusFollowsPointer(this);
        for (QWidget *w : {static_cast<QWidget *>(m_canvas), static_cast<QWidget *>(static_cast<TimelinePanel *>(panels.timeline)->view()),
                           static_cast<QWidget *>(panels.layers->findChild<QTreeWidget *>())})
            if (w)
                w->installEventFilter(follow);
    }
    m_dockLayout = new DockLayout(this, panels);
    createMenus();
    statusBar()->showMessage(tr("Ready"));
    connect(m_canvas, &canvas::CanvasView::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
    {
        TimelineView *timeline = static_cast<TimelinePanel *>(panels.timeline)->view();
        connect(timeline, &TimelineView::audioImportRequested, this, &MainWindow::importAudio);
        connect(timeline, &TimelineView::frameScrubbed, m_audioPlayer, &audio::AudioPlayer::scrub);
        connect(timeline, &TimelineView::audioFailed, this, [this](const QString &reason) {
            statusBar()->showMessage(tr("The audio cannot be read: %1").arg(reason), 8000);
        });
    }
    static_cast<LayersPanel *>(panels.layers)->setToolState(m_toolState);
    connect(static_cast<LayersPanel *>(panels.layers), &LayersPanel::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
    auto *selectionBar = new SelectionBar(m_canvas, m_document, m_toolState);
    connect(selectionBar, &SelectionBar::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
    new TransformBar(m_canvas);
    new ShapeBar(m_canvas);
    new PuppetBar(m_canvas);
    // Rich Presence: Discord shows "Mothion", the time and the frame (nothing without an application id).
    auto *discord = new DiscordPresence(m_document, DiscordPresence::defaultApplicationId(), this);
    connect(&tools::Preferences::instance(), &tools::Preferences::changed, discord,
            [discord] { discord->setEnabled(tools::Preferences::instance().discordEnabled()); });
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
    // The keys of every command come from Shortcuts, by id.
    Shortcuts &keymap = Shortcuts::instance();
    auto make = [this, &keymap](IconId id, const QString &text, const QString &command, auto slot) {
        auto *a = new QAction(icon(id), text, this);
        keymap.bind(a, command);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    m_fileActions << make(IconId::New, tr("New…"), QStringLiteral("file.new"), &MainWindow::newDocument)
                  << make(IconId::Open, tr("Open…"), QStringLiteral("file.open"), &MainWindow::open)
                  << make(IconId::Save, tr("Save"), QStringLiteral("file.save"), &MainWindow::save);
    m_saveAsAction = new QAction(tr("Save as…"), this);
    keymap.bind(m_saveAsAction, QStringLiteral("file.saveAs"));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

    // Undo/redo run on the document's History; labels ("Undo stroke") and
    // enabled state follow DocumentController::historyChanged. An edit left
    // open on the canvas (puppet warp, transform) goes back through its own
    // steps first.
    QAction *undo = new QAction(icon(IconId::Undo), tr("Undo"), this);
    QAction *redo = new QAction(icon(IconId::Redo), tr("Redo"), this);
    // Also from floating panels, which are windows of their own.
    keymap.bind(undo, QStringLiteral("edit.undo"), Qt::ApplicationShortcut);
    keymap.bind(redo, QStringLiteral("edit.redo"), Qt::ApplicationShortcut);
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
    auto view = [this](IconId id, const QString &text, const QString &command, auto slot) {
        auto *a = new QAction(icon(id), text, this);
        Shortcuts::instance().bind(a, command);
        connect(a, &QAction::triggered, m_canvas, slot);
        return a;
    };
    m_zoomActions << view(IconId::ZoomOut, tr("Zoom out"), QStringLiteral("view.zoomOut"), &canvas::CanvasView::zoomOut)
                  << view(IconId::Zoom, tr("Zoom in"), QStringLiteral("view.zoomIn"), &canvas::CanvasView::zoomIn)
                  << view(IconId::ZoomFit, tr("Fit to window"), QStringLiteral("view.fit"),
                          &canvas::CanvasView::fitToView)
                  << view(IconId::ZoomActual, tr("Actual size"), QStringLiteral("view.actualSize"),
                          &canvas::CanvasView::actualSize);
    m_rotateActions << view(IconId::RotateLeft, tr("Rotate left"), QStringLiteral("view.rotateLeft"),
                            &canvas::CanvasView::rotateLeft)
                    << view(IconId::RotateRight, tr("Rotate right"), QStringLiteral("view.rotateRight"),
                            &canvas::CanvasView::rotateRight)
                    << view(IconId::RotateReset, tr("Reset rotation"), QStringLiteral("view.rotateReset"),
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

    // Snapping the pen to the axes of a perspective layer, on the right of the bar: the on/off button.
    // The layer it snaps to is chosen in the Layers panel (its "Snap to" button).
    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    auto *snapAction = new QAction(icon(IconId::Snap), tr("Snap to perspective"), this);
    snapAction->setCheckable(true);
    snapAction->setToolTip(tr("Snap strokes to an axis of the perspective layer chosen in the Layers panel: "
                              "the one nearest the direction drawn"));
    bar->addAction(snapAction);
    connect(snapAction, &QAction::toggled, m_toolState, &tools::ToolState::setPerspectiveSnap);
    connect(m_toolState, &tools::ToolState::perspectiveSnapChanged, this, [this, snapAction] {
        const QSignalBlocker block(snapAction);
        snapAction->setChecked(m_toolState->perspectiveSnap());
    });

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
    m_recentMenu = new QMenu(tr("Open recent"), this);
    file->insertMenu(m_fileActions[2], m_recentMenu); // between Open and Save
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);
    file->addAction(m_saveAsAction);
    file->addSeparator();
    file->addAction(tr("Import image…"), this, &MainWindow::importImage);
    file->addAction(tr("Import image sequence…"), this, &MainWindow::chooseSequence);
    file->addAction(tr("Import audio…"), this, &MainWindow::importAudio);
    file->addAction(tr("Import brush (.sut)…"), this, &MainWindow::importBrush);
    file->addSeparator();
    Shortcuts &keymap = Shortcuts::instance();
    keymap.bind(file->addAction(tr("Export frames…"), this, &MainWindow::exportFrames), QStringLiteral("file.export"));
    file->addAction(tr("Export video…"), this, &MainWindow::exportVideo);
    file->addSeparator();
    file->addAction(tr("Settings…"), this, &MainWindow::openSettings); // only from here: no key
    file->addSeparator();
    keymap.bind(file->addAction(tr("Quit"), this, &QWidget::close), QStringLiteral("file.quit"));

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions(m_editActions);
    edit->addSeparator();
    edit->addAction(m_clearAction);
    edit->addSeparator();
    keymap.bind(edit->addAction(tr("Select all"), m_document, &document::DocumentController::selectAll),
                QStringLiteral("edit.selectAll"));
    keymap.bind(edit->addAction(tr("Deselect"), m_document, &document::DocumentController::clearSelection),
                QStringLiteral("edit.deselect"));
    keymap.bind(edit->addAction(tr("Invert selection"), m_document, &document::DocumentController::invertSelection),
                QStringLiteral("edit.invertSelection"));
    edit->addSeparator();
    keymap.bind(edit->addAction(tr("Cut"), this, [this] { copyPixels(true); }), QStringLiteral("edit.cut"));
    keymap.bind(edit->addAction(tr("Copy"), this, [this] { copyPixels(false); }), QStringLiteral("edit.copy"));
    keymap.bind(edit->addAction(tr("Paste"), this, &MainWindow::pastePixels), QStringLiteral("edit.paste"));
    edit->addSeparator();
    keymap.bind(edit->addAction(icon(IconId::Transform), tr("Transform"), this,
                                [this] { m_toolState->setActiveTool(tools::ToolId::Transform); }),
                QStringLiteral("edit.transform"));
    edit->addSeparator();
    edit->addAction(tr("Refine with AI…"), this, [this] {
        if (!m_refine) {
            if (!m_aiServer)
                m_aiServer = new ai::ServerProcess(this); // goes with the window, so does the server it started
            m_refine = new RefineDialog(m_document, m_aiServer, this); // stays open while one draws: not modal
        }
        m_refine->refreshReference();
        m_refine->show();
        m_refine->raise();
        m_refine->activateWindow();
    });

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addActions(m_zoomActions);
    m_viewMenu->addSeparator();
    m_viewMenu->addActions(m_rotateActions);
    m_viewMenu->addSeparator();
    QAction *debug = m_viewMenu->addAction(tr("Input debug overlay"));
    debug->setCheckable(true);
    keymap.bind(debug, QStringLiteral("view.debugOverlay"), Qt::ApplicationShortcut); // also from a floating panel
    connect(debug, &QAction::toggled, m_canvas, &canvas::CanvasView::setDebugOverlayVisible);

    QMenu *animation = menuBar()->addMenu(tr("&Animation"));
    animation->addAction(tr("Autoplay"), this, [this] {
        if (!m_autoplay) {
            m_autoplay = new AutoplayWindow(m_document, m_audioPlayer, this);
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
        m_audioPlayer->scrub(m_document->currentFrame()); // the sound of the slot stepped to
    };
    keymap.bind(animation->addAction(tr("Previous time slot"), this, [step] { step(-1); }),
                QStringLiteral("anim.previousSlot"));
    keymap.bind(animation->addAction(tr("Next time slot"), this, [step] { step(+1); }),
                QStringLiteral("anim.nextSlot"));
    // Over holds and empty slots, to where the current cel's frames start.
    keymap.bind(animation->addAction(tr("Previous frame"), this, [this] { m_document->stepToFrame(-1); }),
                QStringLiteral("anim.previousFrame"));
    keymap.bind(animation->addAction(tr("Next frame"), this, [this] { m_document->stepToFrame(+1); }),
                QStringLiteral("anim.nextFrame"));
    animation->addSeparator();
    // The hold of the frame at the current point, one frame longer or shorter;
    // the frames after it move along.
    animation->addAction(tr("Tween frames…"), this, &MainWindow::tweenFrames);
    animation->addAction(tr("Scene length…"), this, &MainWindow::sceneLength);
    animation->addSeparator();
    QAction *longer = animation->addAction(tr("Lengthen frame"), this, [this] { m_document->changeCurrentHold(+1); });
    keymap.bind(longer, QStringLiteral("anim.lengthen"));
    QAction *shorter = animation->addAction(tr("Shorten frame"), this, [this] { m_document->changeCurrentHold(-1); });
    keymap.bind(shorter, QStringLiteral("anim.shorten"));

    QMenu *window = menuBar()->addMenu(tr("&Window"));
    for (PanelDock *dock : m_dockLayout->docks())
        window->addAction(dock->toggleViewAction());
    window->addSeparator();
    window->addAction(tr("Compositing"), this, [this] {
        if (!m_compositing) {
            m_compositing = new CompositingWindow(m_document, this);
            m_compositing->showMaximized(); // full screen by default; it can be restored smaller
        } else {
            m_compositing->show();
        }
        m_compositing->refresh();
        m_compositing->raise();
        m_compositing->activateWindow();
    });
    window->addAction(tr("Reset default layout"), m_dockLayout, &DockLayout::resetToDefault);

    // For a bug report: where the log and the crash reports are.
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Open the logs folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(diagnostics::logDirectory()));
    });
    help->addAction(tr("Open the crash reports folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(diagnostics::crashDirectory()));
    });
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

namespace {

QStringList imagePatterns()
{
    QStringList patterns;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return patterns;
}

// A name for the cel of a sequence: the first file's, without the number that counts the frames
// ("walk_001" -> "walk").
QString sequenceName(const QString &firstFile)
{
    QString name = QFileInfo(firstFile).completeBaseName();
    while (!name.isEmpty() && (name.back().isDigit() || name.back() == QLatin1Char('_') || name.back() == QLatin1Char('-')
                               || name.back() == QLatin1Char('.') || name.back() == QLatin1Char(' ')))
        name.chop(1);
    return name.isEmpty() ? QFileInfo(firstFile).completeBaseName() : name;
}

// Name order with numbers as numbers (frame2 before frame10), without case. Written out rather than left to
// QCollator, whose numeric mode depends on the locale (in the "C" one it does nothing).
bool naturalLess(const QString &a, const QString &b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype ie = i, je = j;
            while (ie < a.size() && a[ie].isDigit())
                ++ie;
            while (je < b.size() && b[je].isDigit())
                ++je;
            QStringView x = QStringView(a).mid(i, ie - i), y = QStringView(b).mid(j, je - j);
            while (x.size() > 1 && x.front() == QLatin1Char('0'))
                x = x.mid(1);
            while (y.size() > 1 && y.front() == QLatin1Char('0'))
                y = y.mid(1);
            if (x.size() != y.size()) // more digits, a bigger number
                return x.size() < y.size();
            if (const int c = x.compare(y))
                return c < 0;
            i = ie;
            j = je;
        } else {
            const QChar x = a[i].toCaseFolded(), y = b[j].toCaseFolded();
            if (x != y)
                return x < y;
            ++i;
            ++j;
        }
    }
    return (a.size() - i) < (b.size() - j); // the shorter one first
}

} // namespace

int MainWindow::importSequenceFiles(QStringList files, const QString &name, int startFrame, int hold)
{
    // In name order, numbers as numbers (frame2 before frame10).
    std::stable_sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return naturalLess(QFileInfo(a).fileName(), QFileInfo(b).fileName());
    });
    // Reading and adding take a while for a long sequence: a progress bar (shown only if it does), which
    // keeps the window answering, and a Cancel while nothing has been added yet.
    const int total = int(files.size());
    QProgressDialog progress(tr("Reading images…"), tr("Cancel"), 0, 2 * total, this);
    progress.setWindowTitle(tr("Import image sequence"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    // Read on all the cores (decoding is the slow part); each image is also made ready for the document
    // there: in its pixel format, and scaled down to fit. This thread keeps the progress and the window.
    const int fitWidth = m_document->document().width(), fitHeight = m_document->document().height();
    std::vector<QImage> loaded;
    loaded.resize(std::size_t(total));
    std::atomic<int> done{0};
    std::atomic<bool> cancelled{false};
    std::thread readers([&] {
        parallelFor(total, [&](int i) {
            QImageReader reader(files[i]);
            reader.setAutoTransform(true);
            QImage image = reader.read();
            if (!image.isNull()) {
                image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (image.width() > fitWidth || image.height() > fitHeight)
                    image = image.scaled(fitWidth, fitHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            loaded[std::size_t(i)] = std::move(image);
            ++done;
        }, &cancelled);
    });
    while (done.load() < total && !cancelled.load()) {
        progress.setValue(done.load());
        if (progress.wasCanceled())
            cancelled = true;
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    readers.join();
    if (cancelled.load())
        return 0;
    QList<QImage> images;
    QStringList unreadable;
    for (int i = 0; i < total; ++i) {
        if (loaded[std::size_t(i)].isNull())
            unreadable << QFileInfo(files[i]).fileName();
        else
            images << loaded[std::size_t(i)];
    }
    progress.setLabelText(tr("Adding frames…"));
    progress.setCancelButton(nullptr); // the document is being changed: no way back from here
    progress.setValue(total);
    if (!unreadable.isEmpty())
        statusBar()->showMessage(tr("Skipped, cannot be read: %1").arg(unreadable.join(QStringLiteral(", "))), 6000);
    const int imported = m_document->importImageSequence(
        images, name.isEmpty() ? tr("Sequence") : name, startFrame, hold,
        [&progress, total](int added) { progress.setValue(total + added); });
    progress.setValue(2 * total);
    if (imported > 0 && unreadable.isEmpty())
        statusBar()->showMessage(tr("%n image(s) imported", nullptr, imported), 4000);
    return imported;
}

void MainWindow::importSequence(QStringList files, const QString &name)
{
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Import image sequence"), tr("There are no images to import."));
        return;
    }
    ImportSequenceDialog dialog(int(files.size()), m_document->currentFrame(), m_document->document().frameCount(),
                                this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (importSequenceFiles(files, name, dialog.startFrame(), dialog.hold()) == 0)
        QMessageBox::warning(this, tr("Import image sequence"), tr("None of the files could be read as an image."));
}

QStringList MainWindow::imageFilesIn(const QString &folder)
{
    QStringList files;
    for (const QFileInfo &info : QDir(folder).entryInfoList(imagePatterns(), QDir::Files | QDir::Readable))
        files << info.absoluteFilePath();
    return files;
}

void MainWindow::chooseSequence()
{
    // One dialog for both: select the images (Ctrl or Shift for several), or go into a folder and take it whole.
    QFileDialog dialog(this, tr("Import image sequence"));
    dialog.setOption(QFileDialog::DontUseNativeDialog); // the extra button is not possible in a native one
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilter(tr("Images (%1)").arg(imagePatterns().join(QLatin1Char(' '))));
    bool wholeFolder = false;
    if (auto *buttons = dialog.findChild<QDialogButtonBox *>()) {
        auto *folder = buttons->addButton(tr("Use this folder"), QDialogButtonBox::ActionRole);
        folder->setToolTip(tr("Import every image of the folder shown"));
        connect(folder, &QPushButton::clicked, &dialog, [&dialog, &wholeFolder] {
            wholeFolder = true;
            static_cast<QDialog &>(dialog).done(QDialog::Accepted);
        });
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (wholeFolder) {
        const QDir folder = dialog.directory();
        importSequence(imageFilesIn(folder.absolutePath()), folder.dirName());
        return;
    }
    const QStringList files = dialog.selectedFiles();
    if (!files.isEmpty())
        importSequence(files, sequenceName(files.first()));
}

void MainWindow::importAudio()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import audio"), QString(),
        tr("Audio (*.mp3 *.wav *.ogg *.oga *.flac *.m4a *.aac *.opus *.wma);;All files (*)"));
    if (file.isEmpty())
        return;
    QString reason;
    if (!m_document->importAudio(file, &reason)) {
        QMessageBox::warning(this, tr("Import audio"), reason);
        return;
    }
    statusBar()->showMessage(tr("Audio imported: %1").arg(QFileInfo(file).fileName()), 4000);
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

const QString kRecentFilesKey = QStringLiteral("recent/projects");

// Most recent first.
QStringList recentFiles()
{
    return QSettings().value(kRecentFilesKey).toStringList();
}

void setRecentFiles(const QStringList &files)
{
    QSettings().setValue(kRecentFilesKey, files);
}

// `path` goes to the top of the list (or is taken off it).
void rememberRecentFile(const QString &path)
{
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > tools::Preferences::instance().recentLimit())
        files.removeLast();
    setRecentFiles(files);
}

void forgetRecentFile(const QString &path)
{
    QStringList files = recentFiles();
    files.removeAll(path);
    setRecentFiles(files);
}

} // namespace

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::recentListCleared, this, [] { setRecentFiles({}); });
    dialog.exec();
}

void MainWindow::newDocument()
{
    NewProjectDialog dialog(this); // the size and the rest, asked before the work in progress is put away
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!maybeSave())
        return;
    discardAutosave();
    m_document->newDocument(dialog.pageWidth(), dialog.pageHeight(), dialog.frameRate(), dialog.frames(),
                            dialog.paper());
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

void MainWindow::openRecent(const QString &path)
{
    if (!maybeSave())
        return;
    if (!openFile(path))
        forgetRecentFile(path); // gone or unreadable: no use keeping it
}

void MainWindow::fillRecentMenu()
{
    m_recentMenu->clear();
    QStringList files = recentFiles();
    const qsizetype before = files.size();
    files.removeIf([](const QString &file) { return !QFileInfo::exists(file); });
    if (files.size() != before)
        setRecentFiles(files);
    while (files.size() > tools::Preferences::instance().recentLimit()) // the limit was lowered
        files.removeLast();
    for (const QString &file : std::as_const(files)) {
        QAction *action = m_recentMenu->addAction(QFileInfo(file).fileName());
        action->setToolTip(QDir::toNativeSeparators(file));
        action->setStatusTip(QDir::toNativeSeparators(file));
        connect(action, &QAction::triggered, this, [this, file] { openRecent(file); });
    }
    if (files.isEmpty()) {
        m_recentMenu->addAction(tr("No recent projects"))->setEnabled(false);
        return;
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear list"), this, [] { setRecentFiles({}); });
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    std::unique_ptr<doc::Document> loaded = io::loadProject(path, &error);
    if (!loaded) {
        QMessageBox::warning(this, tr("Open project"), error);
        return false;
    }
    discardAutosave(); // what the project was before
    m_document->replaceDocument(std::move(loaded));
    m_filePath = QFileInfo(path).absoluteFilePath();
    m_autosaveDirty = false;
    diagnostics::setProjectPath(m_filePath);
    diagnostics::note(QStringLiteral("Opened %1").arg(m_filePath));
    rememberProjectDirectory(m_filePath);
    rememberRecentFile(m_filePath);
    updateTitle();
    statusBar()->showMessage(tr("Opened %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

void MainWindow::applyAutosaveInterval()
{
    const int minutes = tools::Preferences::instance().autosaveMinutes();
    if (minutes <= 0) {
        m_autosaveTimer->stop();
        return;
    }
    m_autosaveTimer->start(minutes * 60 * 1000);
}

void MainWindow::autosaveTick()
{
    if (!m_autosaveDirty || !m_document->isModified())
        return;
    // Not in the middle of a stroke, nor while the pen or the keys are busy: a copy of a big project takes a moment.
    // After a minute of waiting it is made anyway.
    constexpr int kPauseMs = 1500, kMaxWaits = 60;
    if ((m_document->isPainting() || m_lastInput.elapsed() < kPauseMs) && m_autosaveWaits < kMaxWaits) {
        ++m_autosaveWaits;
        QTimer::singleShot(1000, this, &MainWindow::autosaveTick);
        return;
    }
    m_autosaveWaits = 0;
    autosaveNow();
}

bool MainWindow::autosaveNow()
{
    if (!m_autosaveDirty || !m_document->isModified() || m_document->isPainting())
        return false;
    QString error;
    if (!io::autosave::write(m_document->document(), m_filePath, &error)) {
        diagnostics::note(QStringLiteral("Autosave failed: %1").arg(error));
        return false;
    }
    m_autosaveDirty = false;
    diagnostics::note(QStringLiteral("Autosaved %1").arg(m_filePath.isEmpty() ? QStringLiteral("(untitled)") : m_filePath));
    return true;
}

// The copy of the project in the window, if the project is one that was worked on: a pristine untitled project
// leaves alone the copy of an earlier run, which may still be waiting to be recovered.
void MainWindow::discardAutosave()
{
    if (!m_filePath.isEmpty() || m_document->isModified())
        io::autosave::remove(m_filePath);
    m_autosaveDirty = false;
}

void MainWindow::checkRecovery()
{
    QList<io::autosave::Entry> entries = io::autosave::all();
    std::reverse(entries.begin(), entries.end()); // the latest first
    for (const io::autosave::Entry &entry : std::as_const(entries)) {
        if (io::autosave::isStale(entry)) {
            io::autosave::remove(entry.projectPath);
            continue;
        }
        const QString text = tr("Mothion found unsaved work on “%1”, kept at %2.\n\nRecover it? You can then save it "
                                "over the project, or elsewhere.")
                                 .arg(entry.name, QLocale().toString(entry.when, QLocale::ShortFormat));
        QMessageBox box(QMessageBox::Question, tr("Recover unsaved work"), text, QMessageBox::NoButton, this);
        QPushButton *recover = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
        QPushButton *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == discard) {
            io::autosave::remove(entry.projectPath);
        } else if (box.clickedButton() == recover) {
            QString error;
            std::unique_ptr<doc::Document> loaded = io::loadProject(entry.file, &error);
            if (!loaded) {
                QMessageBox::warning(this, tr("Recover unsaved work"), error);
                continue;
            }
            m_document->replaceDocument(std::move(loaded));
            m_filePath = entry.projectPath;
            diagnostics::setProjectPath(m_filePath);
            diagnostics::note(QStringLiteral("Recovered the autosave of %1").arg(entry.name));
            m_document->markUnsaved(); // it is not what the project's file holds
            m_autosaveDirty = true;    // the copy goes on being kept
            updateTitle();
            return; // one project fits the window; the others wait for the next start
        }
    }
}

void MainWindow::checkPreviousCrash()
{
    const diagnostics::PreviousCrash crash = diagnostics::takePreviousCrash();
    if (!crash.happened)
        return;
    QString text = (crash.hang ? tr("Mothion stopped responding the last time it ran, and was closed.")
                               : tr("Mothion closed unexpectedly the last time it ran."))
                   + tr("\n\nThe report is in:\n%1\n\nThe log is in:\n%2")
                       .arg(crash.report.isEmpty() ? diagnostics::crashDirectory() : crash.report, diagnostics::logDirectory());
    if (!crash.backup.isEmpty())
        text += tr("\n\nA copy of your project was saved:\n%1").arg(crash.backup);
    else if (!crash.hang)
        text += tr("\n\nNo copy of the project could be saved.");
    text += tr("\n\nIf you report the problem, please send the report and the log.");
    QMessageBox box(QMessageBox::Warning, crash.hang ? tr("Mothion stopped responding") : tr("Mothion crashed"), text, QMessageBox::Close, this);
    QPushButton *folder = box.addButton(tr("Open the folder"), QMessageBox::ActionRole);
    QPushButton *open = crash.backup.isEmpty() ? nullptr : box.addButton(tr("Open the copy"), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() == folder)
        QDesktopServices::openUrl(QUrl::fromLocalFile(diagnostics::crashDirectory()));
    else if (open && box.clickedButton() == open)
        openFile(crash.backup);
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
    io::autosave::remove(m_filePath); // the copy of what it was before (a project saved for the first time: untitled)
    m_filePath = QFileInfo(path).absoluteFilePath();
    io::autosave::remove(m_filePath);
    m_autosaveDirty = false;
    diagnostics::setProjectPath(m_filePath);
    diagnostics::note(QStringLiteral("Saved %1").arg(m_filePath));
    rememberProjectDirectory(m_filePath);
    rememberRecentFile(m_filePath);
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
    if (m_compositing)
        m_compositing->commit(); // edits made a moment ago are in the project
    m_document->finishEdits();
    const doc::Document &d = m_document->document();
    const QString name = m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).completeBaseName();
    ExportDialog dialog(m_document->currentFrame(), d.frameCount(), name, !d.compositing().nodes.empty(), this);
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

void MainWindow::sceneLength()
{
    const doc::Document &d = m_document->document();
    const double fps = d.frameRate().fps();
    bool ok = false;
    const int frames = QInputDialog::getInt(
        this, tr("Scene length"), tr("Frames (at %1 fps, %2 s now):").arg(fps, 0, 'g', 5).arg(d.frameCount() / fps, 0, 'f', 1),
        d.frameCount(), 1, 100000, 1, &ok);
    if (!ok || frames == d.frameCount())
        return;
    const int used = m_document->usedFrames();
    if (frames < used &&
        QMessageBox::question(this, tr("Scene length"),
                              tr("Some frames are drawn past frame %1. They stay in the project, but the scene, the "
                                 "playback and the exports end at frame %1.").arg(frames),
                              QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
        return;
    m_document->setSceneLength(frames);
    if (m_document->currentFrame() >= frames)
        m_document->setCurrentFrame(frames - 1);
    statusBar()->showMessage(tr("Scene length: %n frame(s)", "", frames), 6000);
}

void MainWindow::tweenFrames()
{
    m_document->finishEdits();
    TweenDialog dialog(m_document, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString reason;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int made = m_document->tweenFrames(dialog.cel(), dialog.from(), dialog.to(), dialog.ease(), &reason);
    QApplication::restoreOverrideCursor();
    if (made == 0) {
        QMessageBox::warning(this, tr("Tween frames"), reason.isEmpty() ? tr("No frames were made.") : reason);
        return;
    }
    statusBar()->showMessage(tr("%n frame(s) made between frames %1 and %2", "", made).arg(dialog.from() + 1).arg(dialog.to() + 1),
                             6000);
}

void MainWindow::exportVideo()
{
    if (m_compositing)
        m_compositing->commit(); // edits made a moment ago are in the project
    m_document->finishEdits();
    const doc::Document &d = m_document->document();
    const QString name = m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).completeBaseName();
    VideoExportDialog dialog(d.frameCount(), !d.compositing().nodes.empty(), name, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int total = dialog.lastFrame() - dialog.firstFrame() + 1;
    QProgressDialog progress(tr("Exporting video…"), tr("Cancel"), 0, total, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QString error;
    const bool done = io::exportVideo(d, dialog.firstFrame(), dialog.lastFrame(), dialog.path(), dialog.options(),
                                      [&progress](int count, int) {
                                          progress.setValue(count);
                                          return !progress.wasCanceled();
                                      },
                                      &error);
    if (progress.wasCanceled()) {
        statusBar()->showMessage(tr("Export cancelled"), 4000);
        return;
    }
    if (!done) {
        QMessageBox::critical(this, tr("Export video"), error);
        return;
    }
    statusBar()->showMessage(tr("Exported %1 frames to %2").arg(total).arg(QFileInfo(dialog.path()).fileName()), 6000);
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
    switch (event->type()) { // the user is working: an autosave waits for a pause
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::KeyPress:
        m_lastInput.restart();
        break;
    default:
        break;
    }

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
    discardAutosave(); // closed on purpose: nothing to recover
    m_dockLayout->save();
    QMainWindow::closeEvent(event);
}

} // namespace mothion::ui
