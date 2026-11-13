#pragma once

#include "core/document/Command.h"
#include "core/document/VectorStroke.h"

namespace mothion::doc {

// Adds a stroke to the end of a vector layer's list; undo takes it off. The pixels the stroke painted are
// the matching PaintCommand's: both go in one step.
class AddVectorStrokeCommand : public Command
{
public:
    AddVectorStrokeCommand(CelId cel, DrawingId drawing, LayerId layer, VectorStroke stroke);

    CommandKind kind() const override { return CommandKind::Paint; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_layer;
    VectorStroke m_stroke;
};

} // namespace mothion::doc
