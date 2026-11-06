#pragma once

#include <QMainWindow>
#include <QPointer>

class QMenu;

namespace mothion::canvas {
class CanvasView;
}
namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class BrushLibrary;
class ToolState;
}

namespace mothion::audio {
class AudioPlayer;
}

namespace mothion::ui {

class AutoplayWindow;
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
    bool saveTo(const QString &path);
    // Asks what to do with unsaved changes; false if the user cancels.
    bool maybeSave();
    void updateTitle();
    void exportFrames();
    void exportVideo();
    void importImage();
    // An image sequence: one dialog to choose the images (several of them, or "Use this folder" for all of
    // the folder's), then asks where it starts and the frames per image, and imports it.
    void chooseSequence();
    void importSequence(QStringList files, const QString &name);

public:
    // The images of `files`, in name order (numbers as numbers: 2 before 10), one new cel with a frame for each,
    // from `startFrame`, `hold` frames each. How many were read and imported.
    int importSequenceFiles(QStringList files, const QString &name, int startFrame, int hold);
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

    tools::ToolState *m_toolState = nullptr;
    tools::BrushLibrary *m_brushes = nullptr;
    // Declared before m_canvas: the canvas needs the controller at construction.
    document::DocumentController *m_document = nullptr;
    canvas::CanvasView *m_canvas = nullptr;
    DockLayout *m_dockLayout = nullptr;
    QMenu *m_viewMenu = nullptr;
    audio::AudioPlayer *m_audioPlayer = nullptr; // the audio track, heard with the frames and as they are stepped
    QPointer<AutoplayWindow> m_autoplay; // one at a time, made when first asked for
    QPointer<CompositingWindow> m_compositing; // the node editor, made when first asked for
    QList<QAction *> m_fileActions;
    QList<QAction *> m_editActions;
    QAction *m_saveAsAction = nullptr;
    QMenu *m_recentMenu = nullptr; // File > Open recent
    QList<QAction *> m_zoomActions;
    QList<QAction *> m_rotateActions;
    QAction *m_clearAction = nullptr;
    QString m_filePath; // empty until saved or opened
};

} // namespace mothion::ui
