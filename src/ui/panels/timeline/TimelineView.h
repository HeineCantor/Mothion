#pragma once

#include "core/document/Timing.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"
#include "ui/panels/timeline/TimelineMetrics.h"

#include <QAbstractScrollArea>

#include <map>
#include <optional>
#include <vector>

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Exposure timeline (Harmony style) of the document:
// one row per cel (top cel first), frames horizontally, each exposure a
// block spanning its hold with the frame's (drawing's) name. A ruler on top
// shows frame numbers and the current-frame cursor.
//
// Interaction:
//  - click / drag in the ruler or cells: current frame (and cel, in cells);
//  - in a row's header: the eye and the lock show / hide and lock the cel,
//    a click elsewhere makes it current, a double-click renames it;
//  - click a block: selects it (Ctrl+click adds or takes away one, Shift+click
//    the blocks of the row from the last one clicked); click an empty cell:
//    none selected;
//  - drag a selected block: moves all the selected ones by as many frames,
//    one undo step on release; only onto free frames (where they would land
//    on another frame they are shown there striped, and the drop is refused);
//  - copy (right click, or Ctrl+C here) the selected blocks; paste them on
//    their cels from the current frame, keeping their distances, as the same
//    drawings (Ctrl+V) or as independent copies (Ctrl+Shift+V), onto free
//    frames only;
//  - drag the right edge of a block: change the hold length (ripple), one
//    undo step on release;
//  - right click: new frame, clear frame, split frame, insert inbetween,
//    insert/remove frames, delete frame;
//  - Left/Right: frame, Up/Down: cel.
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

    // The selected blocks (exposures), by cel and first frame.
    const std::vector<doc::ExposureRef> &selectedFrames() const { return m_selected; }

    // Copies the selected blocks; pastes them at the current frame (see
    // above). Paste returns false when there is no room or nothing copied.
    void copyFrames();
    bool pasteFrames(bool asCopies);
    bool canPasteFrames() const;

signals:
    void frameWidthChanged(int width);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override; // Ctrl+C / V here are the timeline's

private:
    // Hold-length drag in progress.
    struct Resize
    {
        int celIndex = -1;
        int start = 0;     // first frame of the exposure being resized
        int newLength = 1;
    };

    int rowCount() const;
    int celIndexForRow(int row) const; // rows are top-first, cels bottom-first
    int rowAt(int y) const;              // -1 outside the rows
    int frameAt(int x) const;
    // The eye and lock toggles in a row's header.
    QRect eyeRect(int row) const;
    QRect lockRect(int row) const;
    void renameCel(int celIndex);
    // Exposure whose right edge is under (x, row), as {cel index, start}.
    std::optional<Resize> edgeAt(const QPoint &pos) const;
    const doc::Timing &timingForPainting(int celIndex) const; // preview while resizing or moving
    bool isSelected(doc::CelId cel, int start) const;
    // Press on the block `ref` at `frame` (viewport x `x`): the selection,
    // then maybe a drag.
    void pressFrame(const doc::ExposureRef &ref, int frame, int x, Qt::KeyboardModifiers modifiers);
    void dragFrames(int x);
    void dropFrames();
    void pruneSelection(); // blocks gone (undo, other edits) leave the selection

    void onStructureChanged();
    void updateScrollBars();
    void ensureFrameVisible(int frame);
    void scrubTo(const QPoint &pos, bool selectCel);

    void paintCells(QPainter &p) const;
    void paintExposure(QPainter &p, const doc::Exposure &e, const QString &name, const QColor &color, int y,
                       bool selected) const;
    // A dragged block where it cannot be dropped: striped, over what is there.
    void paintBlockedExposure(QPainter &p, const doc::Exposure &e, int y) const;
    void paintRuler(QPainter &p) const;
    void paintHeaders(QPainter &p) const;
    void paintCursor(QPainter &p) const;
    static QColor colorForCel(int celIndex);

    document::DocumentController *m_document;
    TimelineMetrics m_metrics;
    bool m_scrubbing = false;
    std::optional<Resize> m_resize;
    doc::Timing m_previewTiming; // the resized cel's timing during a drag

    std::vector<doc::ExposureRef> m_selected;
    std::optional<doc::ExposureRef> m_anchor; // the last block clicked, for Shift
    // Blocks being dragged along the time.
    struct FrameDrag
    {
        int pressX = 0;
        int pressFrame = 0;
        bool moved = false;
        int offset = 0;              // frames, as far as the earliest can go
        doc::ExposureRef grabbed;
        bool selectOnlyIt = false;   // a click (no drag) on one of several selected: it alone
        bool blocked = false;        // some would land on another frame: no drop
    };
    std::optional<FrameDrag> m_frameDrag;
    std::map<int, doc::Timing> m_movePreview; // by cel index, while dragging
    std::vector<doc::ExposureCopy> m_copied;  // the blocks copied
};

} // namespace mothion::ui
