#include "core/document/commands/DrawingCommands.h"

#include "core/document/Document.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <vector>

namespace mothion::doc {

namespace {

ChangeSet structural()
{
    ChangeSet c;
    c.structure = true;
    return c;
}

} // namespace

// --- AddDrawingCommand ------------------------------------------------------------

AddDrawingCommand::AddDrawingCommand(LayerId layer, Drawing drawing)
    : m_layer(layer)
    , m_id(drawing.id)
    , m_drawing(std::move(drawing))
{
}

ChangeSet AddDrawingCommand::apply(Document &doc)
{
    Layer *layer = doc.layer(m_layer);
    if (!layer || !m_drawing)
        return {};
    layer->addDrawing(std::move(*m_drawing));
    m_drawing.reset();
    return structural();
}

ChangeSet AddDrawingCommand::revert(Document &doc)
{
    if (Layer *layer = doc.layer(m_layer))
        m_drawing = layer->takeDrawing(m_id);
    return structural();
}

std::size_t AddDrawingCommand::memoryBytes() const
{
    return sizeof(*this) + (m_drawing ? m_drawing->image.memoryBytes() : 0);
}

// --- RemoveDrawingCommand ---------------------------------------------------------

RemoveDrawingCommand::RemoveDrawingCommand(LayerId layer, DrawingId drawing)
    : m_layer(layer)
    , m_id(drawing)
{
}

ChangeSet RemoveDrawingCommand::apply(Document &doc)
{
    Layer *layer = doc.layer(m_layer);
    if (!layer)
        return {};
    m_timingBefore = layer->timing();
    layer->timing().removeDrawing(m_id);
    m_drawing = layer->takeDrawing(m_id);
    return structural();
}

ChangeSet RemoveDrawingCommand::revert(Document &doc)
{
    Layer *layer = doc.layer(m_layer);
    if (!layer || !m_drawing)
        return {};
    layer->addDrawing(std::move(*m_drawing));
    m_drawing.reset();
    layer->timing() = m_timingBefore;
    return structural();
}

std::size_t RemoveDrawingCommand::memoryBytes() const
{
    return sizeof(*this) + (m_drawing ? m_drawing->image.memoryBytes() : 0);
}

// --- factory ----------------------------------------------------------------------

NewDrawing makeNewDrawing(Document &doc, LayerId layerId, int frame, int length)
{
    const Layer *layer = doc.layer(layerId);
    if (!layer)
        return {};
    const DrawingId id = doc.allocateDrawingId();
    Timing after = layer->timing();
    after.setExposure(frame, length, id);

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddDrawingCommand>(layerId, Drawing{id, layer->nextDrawingName(), {}}));
    steps.push_back(std::make_unique<SetTimingCommand>(layerId, layer->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::AddDrawing, std::move(steps)), id};
}

} // namespace mothion::doc
