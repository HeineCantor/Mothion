#pragma once

#include "io/Autosave.h"

#include <QElapsedTimer>
#include "io/BlenderFrameCache.h"
#include "io/BlenderLinkServer.h"

#include <QMainWindow>
#include <QPointer>

class QMenu;
class QLabel;
class QPushButton;

namespace mothion::canvas {
class CanvasView;
}
namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class BrushLibrary;
class ColorSetLibrary;
class ToolState;
}

class QTimer;

namespace mothion::audio {
class AudioPlayer;
}

namespace mothion::ui {

class AutoplayWindow;
class BlenderLiveLink;
class CompositingWindow;
class DockLayout;

// The single workspace window.
//
// It creates the shared ToolState, the canvas and the panels, wires them
// together through ToolState, and hands the panels to DockLayout, which
// decides where they go and persists the arrangement.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens a project (no unsaved-changes question); false if it could not be read.
    bool openFile(const QString &path);
    // Tells the user, once, that an earlier run crashed, and where the report and the copy of the project are.
    void checkPreviousCrash();
    // Offers back, one project at a time, the work that autosave copies of an earlier run hold: a copy is of no use
    // (and is taken away) if the project was saved after it was made.
    void checkRecovery();
    // Opens a copy kept by the autosave as a new, unsaved project (it never goes over the original). False if it cannot
    // be read.
    bool openBackupCopy(const io::autosave::Entry &entry);
    // Keeps a copy of the project now if it has unsaved work the last copy lacks (what the timer does, when the
    // user is not drawing). Returns whether one was written.
    bool autosaveNow();
    // Saves the project to `path` (and takes the autosave copies of it away).
    bool saveTo(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    // Undo/redo keys pressed in a number field go to the document, and the
    // pan keys go to the canvas from the panels (see the .cpp).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createCommandBar();
    void createCanvasArea(); // the canvas with its bar of canvas commands above it
    void createMenus();
    void newDocument();
    void open();
    void openRecent(const QString &path);
    void fillRecentMenu();
    bool save();
    bool saveAs();
    // Asks what to do with unsaved changes; false if the user cancels.
    bool maybeSave();
    void updateTitle();
    void applyAutosaveInterval();
    void autosaveTick();
    void discardAutosave(); // the project is saved or its changes given up: its copy is of no use
    void exportFrames();
    void exportVideo();
    // Makes the frames between two poses of the current cel (see DocumentController::tweenFrames()).
    void tweenFrames();
    // Asks for the scene's length, in frames.
    void sceneLength();
    void importImage();
    // An image sequence: one dialog to choose the images (several of them, or "Use this folder" for all of
    // the folder's), then asks where it starts and the frames per image, and imports it.
    void chooseSequence();
    void chooseBlenderScene();
    void importSequence(QStringList files, const QString &name);

public:
    // The images of `files`, in name order (numbers as numbers: 2 before 10), one new cel with a frame for each,
    // from `startFrame`, `hold` frames each. How many were read and imported.
    // `scaleUp`: images smaller than the page are made as big as it (a draft rendered small).
    int importSequenceFiles(QStringList files, const QString &name, int startFrame, int hold, bool scaleUp = false);
    // A scene of a connected Blender as a new cel, made in the background (see BlenderLiveLink): the cel is there at once and
    // fills in frame by frame, the one the timeline is on first, while the status bar tells how far it is. False if that
    // Blender is not connected or one is already being made.
    bool startBlenderImport(int sessionId, int startFrame);
    bool blenderImportRunning() const;
    // Brings the current cel up to date from its Blender scene, if it has one and its Blender is connected.
    void refreshFromBlender();
    io::BlenderLinkServer *blenderServer() const { return m_blenderServer; }
    BlenderLiveLink *blenderLive() const { return m_blenderLive; }
    // The image files directly in `folder`, with their paths.
    static QStringList imageFilesIn(const QString &folder);

private:
    // The clipboard: the selected pixels (as an image other apps take too,
    // with where they were for pasting back in place), and pasting them or
    // any image onto the current layer.
    void copyPixels(bool cut);
    void pastePixels();
    void importBrush();
    // The audio track: asks for a sound file and puts it in the project (a song under the animation).
    void importAudio();
    void openSettings(); // the Settings window
    void openBackup();

    tools::ToolState *m_toolState = nullptr;
    tools::BrushLibrary *m_brushes = nullptr;
    tools::ColorSetLibrary *m_colorSets = nullptr;
    // Declared before m_canvas: the canvas needs the controller at construction.
    document::DocumentController *m_document = nullptr;
    canvas::CanvasView *m_canvas = nullptr;
    DockLayout *m_dockLayout = nullptr;
    QMenu *m_viewMenu = nullptr;
    audio::AudioPlayer *m_audioPlayer = nullptr; // the audio track, heard with the frames and as they are stepped
    QPointer<AutoplayWindow> m_autoplay; // one at a time, made when first asked for
    io::BlenderLinkServer *m_blenderServer = nullptr; // the Blenders connected through the add-on (a child of the window)
    io::BlenderFrameCache m_blenderCache;             // the frames got from them
    BlenderLiveLink *m_blenderLive = nullptr;         // what brings their scenes in and keeps them up to date
    QPushButton *m_blenderCancel = nullptr;           // in the status bar while it works
    QLabel *m_blenderState = nullptr;                 // in the status bar: the Blenders connected
    QPointer<CompositingWindow> m_compositing; // the node editor, made when first asked for
    QList<QAction *> m_fileActions;
    QList<QAction *> m_editActions;
    QAction *m_saveAsAction = nullptr;
    QMenu *m_recentMenu = nullptr; // File > Open recent
    QList<QAction *> m_zoomActions;
    QList<QAction *> m_rotateActions;
    QAction *m_clearAction = nullptr;
    QString m_filePath; // empty until saved or opened
    QTimer *m_autosaveTimer = nullptr;
    bool m_autosaveDirty = false; // the project changed since the last copy
    int m_autosaveWaits = 0;      // times the copy was put off for the user's drawing
    QElapsedTimer m_lastInput;
};

} // namespace mothion::ui
