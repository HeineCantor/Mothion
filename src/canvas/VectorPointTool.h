#pragma once

#include "canvas/CanvasTool.h"
#include "canvas/VectorPoints.h"
#include "core/document/Ids.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"
#include "document/DocumentController.h"

#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QString>
#include <QTimer>

#include <compare>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Correct line (as Clip Studio's Correct line > Control point): the control points of a vector layer's strokes (see
// VectorPoints.h), changed one by one. A click on a stroke chooses it (Shift+click chooses one more, or takes it away),
// and the anchors of the strokes chosen show on them (a round dot for a smooth one, a square for a corner); then, by
// the mode of the tool's properties (tools::VectorPointSettings):
//
//  - Move: an anchor dragged takes the line with it (the anchors chosen go together, on every stroke chosen:
//    Shift+click chooses those from the anchor clicked last to this one, Ctrl+click one more or takes it away, Ctrl+A
//    them all); a press on the line of a stroke chosen adds an anchor there, which the drag moves; Alt+click on an
//    anchor takes it away (the line is laid smooth again there, or cut short at an end). An end of a stroke dropped on
//    an end of another stroke chosen makes the two one (see mergedStrokes()); dropped on its own other end, it makes the
//    line a loop (see closedStroke()). The anchors chosen show their Bezier
//    handles (see anchorHandle()), and each run of them the handle of the anchor next to each of its ends, towards it.
//    A handle dragged bends the line on its side (the handle on the other side of a smooth anchor turns with it, unless
//    Alt is held); dropped on its anchor it goes into it, and the anchor is a corner (a handle in its anchor is not
//    shown). Alt+drag from an anchor pulls its handles out of it, opposite each other: smooth again (Alt+click without a
//    drag still takes it away);
//  - Width: an anchor dragged up makes the line wider round it, down thinner (the anchors chosen together);
//  - Corner: a click on an anchor makes it a corner, or smooth again;
//  - Cut: a click on the line of a stroke chosen cuts it in two there; a loop is opened there;
//  - Lasso: a drag draws a loop round anchors of the strokes chosen, which become the anchors chosen (Shift: they are
//    chosen too, Alt: they are no longer chosen), to be moved or widened together in the other modes. A click without a
//    drag chooses the anchor there (Shift, Alt alike), or, on no anchor, chooses a stroke as in the other modes. A stroke
//    not chosen yet that the loop takes in or crosses, even in part, is chosen too, with all its anchors (not with Alt).
//
// Each change is one undo step, the strokes and the layer's pixels together (drawn again from the strokes). So is each
// change of what is chosen, strokes and anchors (a "selection" step, see DocumentController::recordToolChoice()).
class VectorPointTool : public CanvasTool
{
public:
    VectorPointTool(document::DocumentController *document, tools::ToolState *toolState,
                    std::function<void(const QString &)> status);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_edit.has_value() || m_lasso.has_value() || m_pendingPull.has_value(); }
    bool hover(const QPointF &docPos) override;
    void setViewZoom(double zoom) override { m_zoom = zoom > 0 ? zoom : 1.0; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    QCursor cursor() const override;

    // An anchor of a stroke chosen: the stroke's index in the layer, and the anchor's place among its anchors.
    struct AnchorRef
    {
        int stroke = -1;
        int k = -1;
        auto operator<=>(const AnchorRef &) const = default;
    };

    // The strokes chosen (their indices in the current layer's strokes, from the bottom), for tests.
    std::vector<int> chosenStrokes() const;
    // The stroke clicked last among them, -1 for none; its anchors, those of them chosen, and where anchor `k` of it
    // is (document coordinates). For tests.
    int chosenStroke() const;
    std::vector<int> anchors() const;
    std::set<int> chosenAnchors() const;
    std::optional<QPointF> anchorPoint(int k) const;
    // The same for any stroke chosen.
    std::vector<int> anchorsOf(int stroke) const;
    std::optional<QPointF> anchorPoint(int stroke, int k) const;
    std::set<AnchorRef> chosenAnchorRefs() const;
    // The lasso being drawn (document coordinates), empty when none; for tests.
    std::vector<QPointF> lassoPoints() const { return m_lasso ? m_lasso->points : std::vector<QPointF>(); }
    // A Bezier handle shown (Move mode): anchor `k` of stroke `stroke`, towards the next anchor (`out`) or the one before.
    struct HandleRef
    {
        int stroke = -1;
        int k = -1;
        bool out = false;
        bool operator==(const HandleRef &) const = default;
    };
    std::vector<HandleRef> shownHandles() const;
    // Where a handle shown is (document coordinates); for tests.
    std::optional<QPointF> handlePoint(const HandleRef &handle) const;
    // Ctrl+A: every anchor of the strokes chosen; false if no stroke is chosen.
    bool chooseAllAnchors();

    // Sizes on screen, px.
    static constexpr double kGrab = 8.0;      // how near an anchor grabs it
    static constexpr double kLineGrab = 6.0;  // how near the line takes it
    static constexpr double kTolerance = 3.0; // how far from the path the anchors let the line go

private:
    struct Picked
    {
        int index = -1;
        doc::VectorStroke stroke; // as it is in the layer (as it is being edited, during a drag)
        std::vector<int> anchors;
    };
    struct Chosen
    {
        doc::CelId cel = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::LayerId layer = doc::kNoId;
        std::vector<Picked> strokes; // by their index, from the bottom
        std::set<AnchorRef> anchors;
        std::optional<AnchorRef> pivot; // the anchor clicked last: where a Shift+click's range starts
        int current = -1;               // the stroke clicked last

        Picked *find(int index);
        const Picked *find(int index) const;
    };
    // An end of a stroke: its index, and whether it is the last point (else the first).
    struct End
    {
        int stroke = -1;
        bool atEnd = false;
        bool operator==(const End &) const = default;
    };
    struct Edit
    {
        enum class Kind { Move, Width, Handle };
        // A stroke whose anchors the edit moves: as it was at the press (with the anchor added, if one was), and the
        // points a drag changes (from the anchor before the first chosen to the one after the last, and one more each
        // side), with what they covered as drawn last.
        struct Stroke
        {
            int index = -1;
            doc::VectorStroke start;
            std::vector<int> anchors;
            int from = 0, to = -1;
            QRectF changedShown;
        };
        Kind kind = Kind::Move;
        document::DocumentController::PaintTarget target;
        doc::TiledImage before;                  // the layer's pixels at the press
        std::vector<doc::VectorStroke> original; // its strokes at the press
        std::vector<doc::VectorStroke> strokes;  // as they are now
        std::vector<Stroke> edited;
        int lowest = 0, highest = -1; // the strokes edited lie between these
        QPointF press;
        bool changed = false;
        bool rangesKnown = false;
        QRectF pending; // what is to be drawn again at the next flush()
        // The layer's pixels from the strokes under the lowest edited, and from those over the highest, made where
        // the drag needed them (`cached`): while it goes on only the strokes between are drawn again. The strokes over
        // are laid over as one picture unless one of them erases (then they are drawn again each time).
        doc::TiledImage below, above;
        QRegion cached;
        bool aboveLaidOver = true;
        // A lone end dragged: the end of another stroke chosen it is on now (dropped there, the two are merged).
        std::optional<End> mergeFrom, mergeTo;
        // A handle dragged: which, and how far from the press it was.
        HandleRef handle;
        QPointF handleGrab;
        QPointF handleNow; // where it is dragged to (shown there: the curve fitted again finds it only about there)
        bool pull = false; // the handles pulled out of the anchor (Alt+drag from it), not one handle moved
        std::map<std::pair<int, bool>, QPointF> known; // the stroke's handles set before (see m_remembered)
        HandleChange change;                           // what the drag did to them, as it is now
    };

    // What is chosen, as an undo step keeps it.
    struct Choice
    {
        doc::CelId cel = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::LayerId layer = doc::kNoId;
        std::vector<int> strokes;
        std::set<AnchorRef> anchors;
        std::optional<AnchorRef> pivot;
        int current = -1;
        bool operator==(const Choice &) const = default;
    };
    Choice choice() const;
    void restore(const Choice &choice);
    // Records the change from `before` to what is chosen now as an undo step, unless nothing changed or a change of
    // the strokes made it (m_strokesChanged: undoing that puts the choice right).
    void recordChoice(const Choice &before);

    // Lasso mode: the loop as it is drawn, and the keys held at the press.
    struct Lasso
    {
        std::vector<QPointF> points;
        Qt::KeyboardModifiers modifiers;
    };
    // The loop closed: the anchors inside it chosen (or, a click, the anchor or stroke there).
    void finishLasso();
    // Where the strokes are chosen by a click on them (no anchor, no line of a stroke chosen there).
    void chooseStrokeAt(const QPointF &docPos, Qt::KeyboardModifiers modifiers);

    const doc::Layer *currentLayer() const;
    // The strokes chosen, put right with the layer: dropped when the layer is another, taken again (with new anchors)
    // when a stroke changed (an undo).
    void refresh() const;
    std::optional<AnchorRef> anchorAt(const QPointF &docPos) const;
    std::optional<HandleRef> handleAt(const QPointF &docPos) const;
    // The topmost stroke of the current layer that `docPos` is on, or -1.
    int strokeAt(const QPointF &docPos) const;
    // An end of a stroke chosen within reach of `docPos`, other than `except` (the end dragged); loops have none.
    std::optional<End> endAt(const QPointF &docPos, const End &except) const;
    // The stroke `index` the one chosen (`add`: one more chosen, or taken away if it was).
    void choose(int index, bool add = false);
    Picked makePicked(int index) const;
    // Begins an edit of the strokes `indices` (chosen); false if the layer cannot be changed now.
    bool beginEdit(Edit::Kind kind, const std::set<int> &indices);
    // The strokes chosen as the edit has them now.
    void syncPicked();
    void redraw(const std::vector<doc::VectorStroke> &strokes, const QRectF &area);
    // A drag: what is pending drawn again, the strokes edited between the pixels of the strokes under and over them.
    // Called once the events waiting are handled (the pen sends many more than can be drawn), and on the release.
    void flush();
    void cacheLayers(const QRect &region);
    // Records the edit (if it changed anything) as one undo step.
    void finishEdit();
    // A change of the stroke `index` made at once (an anchor taken away, a corner, a cut): `apply` changes the layer's
    // strokes, true if it did.
    void changeAtOnce(int index, const std::function<bool(std::vector<doc::VectorStroke> &)> &apply);
    // `from` (dragged) and `to` made one stroke, or, the two ends of one, a loop; as one undo step.
    void merge(const End &from, const End &to);

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    double m_zoom = 1.0;
    mutable std::optional<Chosen> m_chosen;
    std::optional<Edit> m_edit;
    std::optional<Lasso> m_lasso;
    // Alt pressed on an anchor in Move mode: a drag pulls its handles out, a release where it was takes it away.
    struct PendingPull
    {
        AnchorRef anchor;
        QPointF press;
    };
    std::optional<PendingPull> m_pendingPull;
    // The handles set by dragging them, for each stroke while it stays as that left it (a fit of the points finds them only
    // about where they were put): shown there, and what the next drag starts from. Keyed by anchor and side.
    struct Remembered
    {
        doc::VectorStroke stroke;
        std::vector<int> anchors;
        std::map<std::pair<int, bool>, QPointF> handles;
    };
    mutable std::map<int, Remembered> m_remembered;
    // Those of `picked`, if they are still its (empty if it changed since).
    std::map<std::pair<int, bool>, QPointF> rememberedOf(const Picked &picked) const;
    // Whether a handle shown is in its anchor (a corner made by dropping it there): then it is not shown.
    bool handleInAnchor(const HandleRef &handle) const;
    bool m_strokesChanged = false;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0); // see DocumentController::recordToolChoice()
    QTimer m_flushTimer;
    std::optional<AnchorRef> m_hoverAnchor;
    std::optional<HandleRef> m_hoverHandle;
    int m_hoverStroke = -1;
};

} // namespace mothion::canvas
