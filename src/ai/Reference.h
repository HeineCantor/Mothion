#pragma once

#include <QImage>
#include <QPainterPath>
#include <QPoint>
#include <QString>

namespace mothion::document {
class DocumentController;
}

namespace mothion::ai {

// What "Refine with AI" gives the workflow: the layers chosen in the Layers panel (the current one if none is), as
// the frame shows them, over the paper, and, if something is selected, only the selection's area (what is outside
// a selection that is not a rectangle goes white).
struct Reference
{
    QImage image;            // RGB32, `image.size()` is the area; null if there is a problem
    QPoint origin;           // where its top-left corner is on the page
    QPainterPath selection;  // the selection when the reference was taken (page coordinates; empty: none)
    int frame = 0;           // the frame the layers are on
    int layers = 0;          // how many layers went into it
    QString problem;         // why there is no reference, for the person

    bool valid() const { return problem.isEmpty() && !image.isNull(); }
};

// The reference for what is chosen now. The layers must all be on the frame the timeline is on (the Layers panel
// shows that frame's layers, so they are unless the frame moved on); what is hidden or is not a drawing layer is left
// out.
Reference makeReference(const document::DocumentController &controller);

} // namespace mothion::ai
