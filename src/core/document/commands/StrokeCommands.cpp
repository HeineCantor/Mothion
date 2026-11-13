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

SetVectorStrokesCommand::SetVectorStrokesCommand(CelId cel, DrawingId drawing, LayerId layer,
                                                 std::vector<VectorStroke> before, std::vector<VectorStroke> after)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_layer(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetVectorStrokesCommand::apply(Document &doc)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_layer))
        layer->strokes = m_after;
    return {};
}

ChangeSet SetVectorStrokesCommand::revert(Document &doc)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_layer))
        layer->strokes = m_before;
    return {};
}

std::size_t SetVectorStrokesCommand::memoryBytes() const
{
    std::size_t points = 0;
    for (const VectorStroke &s : m_before)
        points += s.points.size();
    for (const VectorStroke &s : m_after)
        points += s.points.size();
    return sizeof(*this) + points * sizeof(brush::BrushInput);
}

RasterizeLayerCommand::RasterizeLayerCommand(CelId cel, DrawingId drawing, LayerId layer,
                                             std::vector<VectorStroke> strokes)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_layer(layer)
    , m_strokes(std::move(strokes))
{
}

ChangeSet RasterizeLayerCommand::apply(Document &doc)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_layer)) {
        layer->kind = LayerKind::Raster;
        layer->strokes.clear();
    }
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

ChangeSet RasterizeLayerCommand::revert(Document &doc)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_layer)) {
        layer->kind = LayerKind::Vector;
        layer->strokes = m_strokes;
    }
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

std::size_t RasterizeLayerCommand::memoryBytes() const
{
    std::size_t points = 0;
    for (const VectorStroke &s : m_strokes)
        points += s.points.size();
    return sizeof(*this) + points * sizeof(brush::BrushInput);
}

} // namespace mothion::doc

