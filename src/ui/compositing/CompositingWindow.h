#pragma once

#include <QImage>
#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>

class QLabel;
class QSlider;
class QSplitter;
class QTimer;
class QToolButton;

#include "compositing/Evaluator.h"
#include "compositing/Graph.h"
#include "compositing/MaskData.h"

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

using compositing::MaskShape;

class NodeGraphView;
class ImagePreview;
class RangeSlider;

// The compositing panel, in its own window (maximized the first time): the
// result of the nodes above, the node editor below. For now the only input
// is the frame selected on the timeline.
class CompositingWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CompositingWindow(document::DocumentController *document, QWidget *parent = nullptr);
    ~CompositingWindow() override;

    NodeGraphView *graph() const { return m_graph; }
    compositing::Graph &network() { return m_network; }
    ImagePreview *preview() const { return m_preview; }
    // The Mask node whose areas are being drawn on the preview (0: none). That is the one node selected, if it is a Mask.
    compositing::NodeId editedMask() const { return m_editing; }

    // Puts the edits not yet in the document into it (one undo step), at once.
    void commit();

    // Takes the frame again and runs the nodes, at once.
    void refresh();

    // Plays the scene through the nodes at the document's frame rate, on a smaller picture; frames that cannot
    // be made in time are skipped, so the speed is right. Stops at the end of the window's life or on stop().
    void play();
    void stop();
    bool isPlaying() const { return m_playing; }
    QSlider *playbar() const { return m_playbar; }
    RangeSlider *rangeBar() const { return m_range; }
    int framesShown() const { return m_shown; } // counted since playback started

protected:
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void scheduleRefresh();
    void playTick();
    void syncBars(); // the sliders follow the scene's length and the current frame
    void updateLabels();
    void frame();
    void applyView();
    void keepView();
    void editMask(compositing::NodeId id);  // 0 or a node that is not a mask: back to showing the result
    void addMaskShape(const MaskShape &shapeInPreviewPixels);
    QList<MaskShape> maskShapes() const;
    void setMaskShapes(const QList<MaskShape> &shapes);
    void seed();           // the network a project starts with, until the user changes it
    void documentChanged(); // the document's network is not the one shown (undo, redo, another project)

    QPointer<document::DocumentController> m_document;
    compositing::Graph m_network; // the nodes; the editor and the evaluator both read it
    compositing::Evaluator m_evaluator;
    int m_listener = 0;
    NodeGraphView *m_graph;
    ImagePreview *m_preview;
    QTimer *m_timer;
    QSplitter *m_split;
    QTimer *m_viewTimer; // keeps where the user left the node view, a moment after it moved
    QTimer *m_frameTimer; // frames the layout once the window has its size
    QImage m_frame;           // what the Frame nodes hold: the frame, composed, as big as the preview needs
    double m_frameScale = 1.0; // its width over the page's
    bool m_frameDirty = true;
    compositing::NodeId m_editing = 0;
    QWidget *m_maskBar = nullptr; // the tools of the mask being edited, over the preview
    bool m_autoFrame = true;
    doc::GraphData m_committed; // what the document holds, as far as this window knows
    bool m_pending = false;     // edits not yet committed
    bool m_loading = false;     // the network is being replaced from the document: not an edit
    QTimer *m_commitTimer;      // commits a moment after the last edit
    QTimer *m_playTimer;
    QElapsedTimer m_playClock;
    QToolButton *m_playButton = nullptr;
    QLabel *m_playInfo = nullptr;
    QSlider *m_playbar = nullptr; // the current frame
    RangeSlider *m_range = nullptr; // the frames that play
    QLabel *m_frameLabel = nullptr;
    QLabel *m_rangeLabel = nullptr;
    bool m_playing = false;
    bool m_stepping = false; // the window itself is moving the current frame
    int m_playFrom = 0;
    int m_lastShown = -1;
    int m_shown = 0;
    qint64 m_infoAt = 0;
    int m_infoShown = 0;
};

} // namespace mothion::ui
