#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/Ids.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"
#include "document/DocumentController.h"

#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Correct line (as Clip Studio's Correct line > Control point): the control points of a vector layer's strokes (see
// VectorPoints.h), changed one by one. A click on a stroke chooses it, and its anchors show on it (a round dot for a
// smooth one, a square for a corner); then, by the mode of the tool's properties (tools::VectorPointSettings):
//
//  - Move: an anchor dragged takes the line with it (the anchors chosen go together: Shift+click chooses more, or
//    takes one away); a press on the line between them adds an anchor there, which the drag moves; Alt+click on an
//    anchor takes it away (the line is laid smooth again there, or cut short at an end);
//  - Width: an anchor dragged up makes the line wider round it, down thinner (the anchors chosen together);
//  - Corner: a click on an anchor makes it a corner, or smooth again (the angle rounded);
//  - Cut: a click on the line cuts it in two there.
//
// Each change is one undo step, the strokes and the layer's pixels together (drawn again from the strokes).
class VectorPointTool : public CanvasTool
{
public:
    VectorPointTool(document::DocumentController *document, tools::ToolState *toolState,
                    std::function<void(const QString &)> status);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_edit.has_value(); }
    bool hover(const QPointF &docPos) override;
    void setViewZoom(double zoom) override { m_zoom = zoom > 0 ? zoom : 1.0; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    QCursor cursor() const override;

    // The stroke chosen (its index in the current layer's strokes) and its anchors, for tests; -1 / empty for none.
    int chosenStroke() const;
    std::vector<int> anchors() const;
    std::set<int> chosenAnchors() const;
    // Where anchor `k` of the stroke chosen is (document coordinates); for tests.
    std::optional<QPointF> anchorPoint(int k) const;

    // Sizes on screen, px.
    static constexpr double kGrab = 8.0;      // how near an anchor grabs it
    static constexpr double kLineGrab = 6.0;  // how near the line takes it
    static constexpr double kTolerance = 3.0; // how far from the path the anchors let the line go

private:
    struct Chosen
    {
        doc::CelId cel = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::LayerId layer = doc::kNoId;
        int index = -1;
        doc::VectorStroke stroke; // as it is in the layer
        std::vector<int> anchors;
        std::set<int> chosen; // positions in `anchors`
    };
    struct Edit
    {
        enum class Kind { Move, Width };
        Kind kind = Kind::Move;
        document::DocumentController::PaintTarget target;
        doc::TiledImage before;                  // the layer's pixels at the press
        std::vector<doc::VectorStroke> original; // its strokes at the press
        std::vector<doc::VectorStroke> strokes;  // as they are now
        doc::VectorStroke start;                 // the stroke chosen at the press (with the anchor added, if one was)
        std::vector<int> anchors;                // its anchors then
        QPointF press;
        QRectF shown; // what the stroke covers as drawn now
        bool changed = false;
        // A drag: the points it changes (from the anchor before the first chosen to the one after the last), what
        // they covered as drawn last, and what is to be drawn again at the next flush().
        int from = 0, to = -1;
        QRectF changedShown;
        QRectF pending;
        // The layer's pixels from the strokes under the one chosen, and from those over it, made where the drag
        // needed them (`cached`): while it goes on only the stroke chosen is drawn again, between the two. The strokes
        // over it are laid over as one picture unless one of them erases (then they are drawn again each time).
        doc::TiledImage below, above;
        QRegion cached;
        bool aboveLaidOver = true;
    };

    const doc::Layer *currentLayer() const;
    // The stroke chosen, put right with the layer: dropped when the layer is another, taken again (with new anchors)
    // when the stroke changed (an undo).
    void refresh() const;
    int anchorAt(const QPointF &docPos) const;
    // The topmost stroke of the current layer that `docPos` is on, or -1.
    int strokeAt(const QPointF &docPos) const;
    void choose(int index);
    // Begins an edit of the stroke chosen; false if the layer cannot be changed now.
    bool beginEdit(Edit::Kind kind);
    // The stroke chosen is `stroke` now (and the strokes `strokes`): the layer drawn again where it changed.
    void showEdit(std::vector<doc::VectorStroke> strokes, const QRectF &area);
    void redraw(const std::vector<doc::VectorStroke> &strokes, const QRectF &area);
    // A drag: what is pending drawn again, the stroke chosen between the pixels of the strokes under and over it.
    // Called once the events waiting are handled (the pen sends many more than can be drawn), and on the release.
    void flush();
    void cacheLayers(const QRect &region);
    // Records the edit (if it changed anything) as one undo step.
    void finishEdit();
    // A change made at once (an anchor taken away, a corner, a cut): `apply` changes the layer's strokes, true if it did.
    void changeAtOnce(const std::function<bool(std::vector<doc::VectorStroke> &)> &apply);

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    double m_zoom = 1.0;
    mutable std::optional<Chosen> m_chosen;
    std::optional<Edit> m_edit;
    QTimer m_flushTimer;
    int m_hoverAnchor = -1;
    int m_hoverStroke = -1;
};

} // namespace mothion::canvas
