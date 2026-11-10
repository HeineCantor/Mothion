#pragma once

#include "core/document/Command.h"
#include "core/document/Drawing.h"
#include "core/document/Timing.h"

#include <memory>
#include <optional>

namespace mothion::doc {

// Adds a drawing (with its layers, possibly empty) to a cel.
// The drawing's id must come from Document::allocateDrawingId().
class AddDrawingCommand : public Command
{
public:
    AddDrawingCommand(CelId cel, Drawing drawing);

    CommandKind kind() const override { return CommandKind::AddDrawing; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    DrawingId m_id;
    std::optional<Drawing> m_drawing; // held while not in the document
};

// Removes a drawing and clears the frames that showed it.
class RemoveDrawingCommand : public Command
{
public:
    RemoveDrawingCommand(CelId cel, DrawingId drawing);

    CommandKind kind() const override { return CommandKind::RemoveDrawing; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    DrawingId m_id;
    std::optional<Drawing> m_drawing; // held while removed
    Timing m_timingBefore;
};

// "New drawing here": a new drawing on `cel` with one empty layer, named
// with the cel's next free number and exposed on [frame, frame+length).
// Returns the command (not yet applied) and the new drawing's and layer's ids.
struct NewDrawing
{
    std::unique_ptr<Command> command;
    DrawingId id = kNoId;
    LayerId layer = kNoId;
};
NewDrawing makeNewDrawing(Document &doc, CelId cel, int frame, int length = 1, std::string layerName = "Layer 1");

// "Split frame": the hold showing a drawing at `frame` is cut there; from
// `frame` to the end of the hold a copy of the drawing is shown instead,
// independent of it (its layers with new ids, the pixels shared until one
// side is painted). No command when `frame` is empty or starts its hold.
NewDrawing makeSplitDrawing(Document &doc, CelId cel, int frame);

// "Split frame every N": the hold showing a drawing at `frame` is cut every `step` frames, counted from the start of the
// hold; each piece after the first shows a copy of the drawing of its own (as above). The last piece is shorter when the
// hold is not a multiple of `step`. No command when `frame` is empty or the hold is not longer than `step`.
std::unique_ptr<Command> makeSplitEvery(Document &doc, CelId cel, int frame, int step);

// A frame copied from the timeline: a drawing of a cel, shown for `length`
// frames from `start`, counted from the first copied one.
struct ExposureCopy
{
    CelId cel = kNoId;
    DrawingId drawing = kNoId;
    int start = 0;
    int length = 1;
};

// The copied frames shown again on their cels from `frame` on, keeping their
// distances: the same drawings (a change to one shows on both), or with
// `clone` independent copies of them (one per drawing copied). nullptr when
// some would land on a frame already shown, or nothing is left to paste.
std::unique_ptr<Command> makePasteExposures(Document &doc, const std::vector<ExposureCopy> &copies, int frame,
                                            bool clone);

// A drawing put in: the cel's frames from `frame` on move one place to the
// right (a hold crossing it is cut around the gap) and a new drawing with
// one empty layer is shown at `frame`. `kind` names the undo step.
NewDrawing makeInbetween(Document &doc, CelId cel, int frame, std::string layerName = "Layer 1",
                         CommandKind kind = CommandKind::InsertInbetween);

} // namespace mothion::doc
