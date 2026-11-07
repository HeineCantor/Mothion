#include "core/document/commands/LayerCommands.h"

#include "core/document/Document.h"

#include <algorithm>

namespace mothion::doc {

namespace {

ChangeSet structural()
{
    ChangeSet c;
    c.structure = true;
    return c;
}

Drawing *drawingOf(Document &doc, CelId celId, DrawingId drawing)
{
    Cel *cel = doc.cel(celId);
    return cel ? cel->drawing(drawing) : nullptr;
}

} // namespace

// --- AddLayerCommand ----------------------------------------------------------------

AddLayerCommand::AddLayerCommand(Document &doc, CelId cel, DrawingId drawing, int index,
                                 LayerProperties properties, TiledImage image, LayerKind kind)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_id(doc.allocateLayerId())
    , m_index(index)
    , m_layer(Layer{m_id, std::move(properties), std::move(image), kind})
{
}

ChangeSet AddLayerCommand::apply(Document &doc)
{
    Drawing *drawing = drawingOf(doc, m_cel, m_drawing);
    if (!drawing || !m_layer)
        return {};
    const int index = std::clamp(m_index, 0, int(drawing->layers.size()));
    drawing->layers.insert(drawing->layers.begin() + index, std::move(*m_layer));
    m_layer.reset();
    return structural();
}

ChangeSet AddLayerCommand::revert(Document &doc)
{
    Drawing *drawing = drawingOf(doc, m_cel, m_drawing);
    const int index = drawing ? drawing->indexOf(m_id) : -1;
    if (index < 0)
        return {};
    m_layer = std::move(drawing->layers[std::size_t(index)]);
    drawing->layers.erase(drawing->layers.begin() + index);
    return structural();
}

std::size_t AddLayerCommand::memoryBytes() const
{
    return sizeof(*this) + (m_layer ? m_layer->image.memoryBytes() : 0);
}

// --- RemoveLayerCommand -------------------------------------------------------------

RemoveLayerCommand::RemoveLayerCommand(CelId cel, DrawingId drawing, LayerId layer)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_id(layer)
{
}

ChangeSet RemoveLayerCommand::apply(Document &doc)
{
    Drawing *drawing = drawingOf(doc, m_cel, m_drawing);
    m_index = drawing ? drawing->indexOf(m_id) : -1;
    if (m_index < 0)
        return {};
    m_layer = std::move(drawing->layers[std::size_t(m_index)]);
    drawing->layers.erase(drawing->layers.begin() + m_index);
    return structural();
}

ChangeSet RemoveLayerCommand::revert(Document &doc)
{
    Drawing *drawing = drawingOf(doc, m_cel, m_drawing);
    if (!drawing || !m_layer)
        return {};
    const int index = std::clamp(m_index, 0, int(drawing->layers.size()));
    drawing->layers.insert(drawing->layers.begin() + index, std::move(*m_layer));
    m_layer.reset();
    return structural();
}

std::size_t RemoveLayerCommand::memoryBytes() const
{
    return sizeof(*this) + (m_layer ? m_layer->image.memoryBytes() : 0);
}

// --- MoveLayerCommand ---------------------------------------------------------------

MoveLayerCommand::MoveLayerCommand(CelId cel, DrawingId drawing, LayerId layer, int newIndex)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_id(layer)
    , m_newIndex(newIndex)
{
}

ChangeSet MoveLayerCommand::moveTo(Document &doc, int index)
{
    Drawing *drawing = drawingOf(doc, m_cel, m_drawing);
    const int from = drawing ? drawing->indexOf(m_id) : -1;
    if (from < 0)
        return {};
    Layer layer = std::move(drawing->layers[std::size_t(from)]);
    drawing->layers.erase(drawing->layers.begin() + from);
    index = std::clamp(index, 0, int(drawing->layers.size()));
    drawing->layers.insert(drawing->layers.begin() + index, std::move(layer));
    return structural();
}

ChangeSet MoveLayerCommand::apply(Document &doc)
{
    const Drawing *drawing = drawingOf(doc, m_cel, m_drawing);
    m_oldIndex = drawing ? drawing->indexOf(m_id) : 0;
    return moveTo(doc, m_newIndex);
}

ChangeSet MoveLayerCommand::revert(Document &doc)
{
    return moveTo(doc, m_oldIndex);
}

// --- SetLayerPropertiesCommand ------------------------------------------------------

SetLayerPropertiesCommand::SetLayerPropertiesCommand(CelId cel, DrawingId drawing, LayerId layer,
                                                     LayerProperties before, LayerProperties after)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_id(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetLayerPropertiesCommand::put(Document &doc, const LayerProperties &properties)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_id))
        layer->properties = properties;
    return structural();
}

ChangeSet SetLayerPropertiesCommand::apply(Document &doc)
{
    return put(doc, m_after);
}

ChangeSet SetLayerPropertiesCommand::revert(Document &doc)
{
    return put(doc, m_before);
}

std::unique_ptr<Command> makeSetLayerProperties(const Document &doc, CelId cel, DrawingId drawing, LayerId layerId,
                                                LayerProperties properties)
{
    const Layer *layer = doc.layer(cel, drawing, layerId);
    if (!layer || layer->properties == properties)
        return nullptr;
    return std::make_unique<SetLayerPropertiesCommand>(cel, drawing, layerId, layer->properties,
                                                       std::move(properties));
}

// --- SetLayerMaskCommand ------------------------------------------------------------

SetLayerMaskCommand::SetLayerMaskCommand(CelId cel, DrawingId drawing, LayerId layer, std::optional<TiledImage> before,
                                         std::optional<TiledImage> after)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_id(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetLayerMaskCommand::put(Document &doc, const std::optional<TiledImage> &mask)
{
    if (Layer *layer = doc.layer(m_cel, m_drawing, m_id))
        layer->mask = mask;
    ChangeSet changes = structural();
    changes.pixels.push_back({m_cel, m_drawing, m_id, {}}); // what the layer shows changes
    return changes;
}

ChangeSet SetLayerMaskCommand::apply(Document &doc)
{
    return put(doc, m_after);
}

ChangeSet SetLayerMaskCommand::revert(Document &doc)
{
    return put(doc, m_before);
}

std::size_t SetLayerMaskCommand::memoryBytes() const
{
    return sizeof(*this) + (m_before ? m_before->memoryBytes() : 0) + (m_after ? m_after->memoryBytes() : 0);
}

std::unique_ptr<Command> makeAddLayerMask(const Document &doc, CelId cel, DrawingId drawing, LayerId layerId)
{
    const Layer *layer = doc.layer(cel, drawing, layerId);
    if (!layer || layer->kind != LayerKind::Raster || layer->mask)
        return nullptr;
    return std::make_unique<SetLayerMaskCommand>(cel, drawing, layerId, std::nullopt, TiledImage());
}

std::unique_ptr<Command> makeRemoveLayerMask(const Document &doc, CelId cel, DrawingId drawing, LayerId layerId)
{
    const Layer *layer = doc.layer(cel, drawing, layerId);
    if (!layer || !layer->mask)
        return nullptr;
    return std::make_unique<SetLayerMaskCommand>(cel, drawing, layerId, layer->mask, std::nullopt);
}

} // namespace mothion::doc
