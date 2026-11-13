#include "core/document/commands/StrokeCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

AddVectorStrokeCommand::AddVectorStrokeCommand(CelId cel, DrawingId drawing, LayerId layer, VectorStroke stroke)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_layer(layer)
    , m_stroke(std::move(stroke))
{
}

ChangeSet AddVectorStrokeCommand::apply(Document &doc)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_layer))
        layer->strokes.push_back(m_stroke);
    return {}; // the pixels are the paint command's
}

ChangeSet AddVectorStrokeCommand::revert(Document &doc)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_layer); layer && !layer->strokes.empty())
        layer->strokes.pop_back();
    return {};
}

std::size_t AddVectorStrokeCommand::memoryBytes() const
{
    return sizeof(*this) + m_stroke.points.size() * sizeof(brush::BrushInput);
}

} // namespace mothion::doc
