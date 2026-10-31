#pragma once

#include "core/document/Command.h"
#include "core/document/Drawing.h"
#include "core/document/Timing.h"

#include <memory>
#include <optional>

namespace mothion::doc {

// Adds a drawing (with its image, possibly empty) to a layer.
// The drawing's id must come from Document::allocateDrawingId().
class AddDrawingCommand : public Command
{
public:
    AddDrawingCommand(LayerId layer, Drawing drawing);

    CommandKind kind() const override { return CommandKind::AddDrawing; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    LayerId m_layer;
    DrawingId m_id;
    std::optional<Drawing> m_drawing; // held while not in the document
};

// Removes a drawing and clears the frames that showed it.
class RemoveDrawingCommand : public Command
{
public:
    RemoveDrawingCommand(LayerId layer, DrawingId drawing);

    CommandKind kind() const override { return CommandKind::RemoveDrawing; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    LayerId m_layer;
    DrawingId m_id;
    std::optional<Drawing> m_drawing; // held while removed
    Timing m_timingBefore;
};

// "New drawing here": a new empty drawing on `layer`, named with the
// layer's next free number and exposed on [frame, frame+length).
// Returns the command (not yet applied) and the new drawing's id.
struct NewDrawing
{
    std::unique_ptr<Command> command;
    DrawingId id = kNoId;
};
NewDrawing makeNewDrawing(Document &doc, LayerId layer, int frame, int length = 1);

} // namespace mothion::doc
