#pragma once

#include "core/document/Timing.h"
#include "ui/panels/timeline/TimelineMetrics.h"

#include <QAbstractScrollArea>

#include <optional>

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Exposure timeline (Harmony style) of the document:
// one row per layer (top layer first), frames horizontally, each exposure a
// block spanning its hold with the drawing name. A ruler on top shows frame
// numbers and the current-frame cursor.
//
// Interaction:
//  - click / drag in the ruler or cells: current frame (and layer, in cells);
//  - drag the right edge of a block: change the hold length (ripple), one
//    undo step on release;
//  - right click: new drawing, clear frame, insert/remove frames, delete drawing;
//  - Left/Right: frame, Up/Down: layer.
//
// Everything is custom-painted in one widget so the ruler, names and cells
// scroll together without synchronising several widgets.
class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TimelineView(document::DocumentController *document, QWidget *parent = nullptr);

    int frameWidth() const { return m_metrics.frameWidth; }
    void setFrameWidth(int width);

    // Frames shown: the scene length, extended past the last exposure.
    int displayedFrames() const;

signals:
    void frameWidthChanged(int width);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // Hold-length drag in progress.
    struct Resize
    {
        int layerIndex = -1;
        int start = 0;     // first frame of the exposure being resized
        int newLength = 1;
    };

    int rowCount() const;
    int layerIndexForRow(int row) const; // rows are top-first, layers bottom-first
    int rowAt(int y) const;              // -1 outside the rows
    int frameAt(int x) const;
    // Exposure whose right edge is under (x, row), as {layer index, start}.
    std::optional<Resize> edgeAt(const QPoint &pos) const;
    const doc::Timing &timingForPainting(int layerIndex) const; // preview while resizing

    void onStructureChanged();
    void updateScrollBars();
    void ensureFrameVisible(int frame);
    void scrubTo(const QPoint &pos, bool selectLayer);

    void paintCells(QPainter &p) const;
    void paintExposure(QPainter &p, const doc::Exposure &e, const QString &name, const QColor &color,
                       int y) const;
    void paintRuler(QPainter &p) const;
    void paintHeaders(QPainter &p) const;
    void paintCursor(QPainter &p) const;
    static QColor colorForLayer(int layerIndex);

    document::DocumentController *m_document;
    TimelineMetrics m_metrics;
    bool m_scrubbing = false;
    std::optional<Resize> m_resize;
    doc::Timing m_previewTiming; // the resized layer's timing during a drag
};

} // namespace mothion::ui
