#pragma once

#include <QImage>
#include <QPointer>
#include <QWidget>

class QSplitter;
class QTimer;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

class NodeGraphView;
class ImagePreview;

// The compositing panel, in its own window (maximized the first time): the
// result of the nodes above, the node editor below. For now the only input
// is the frame selected on the timeline.
class CompositingWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CompositingWindow(document::DocumentController *document, QWidget *parent = nullptr);

    NodeGraphView *graph() const { return m_graph; }
    ImagePreview *preview() const { return m_preview; }

    // Takes the frame again and runs the nodes, at once.
    void refresh();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void scheduleRefresh();
    void frame();

    QPointer<document::DocumentController> m_document;
    NodeGraphView *m_graph;
    ImagePreview *m_preview;
    QTimer *m_timer;
    QSplitter *m_split;
    QTimer *m_frameTimer; // frames the layout once the window has its size
    bool m_autoFrame = true;
};

} // namespace mothion::ui
