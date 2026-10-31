#include "core/document/commands/LayerCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

namespace {

ChangeSet structural()
{
    ChangeSet c;
    c.structure = true;
    return c;
}

std::size_t layerBytes(const Layer *layer)
{
    std::size_t total = sizeof(Layer);
    if (layer)
        for (const auto &[id, d] : layer->drawings())
            total += d.image.memoryBytes();
    return total;
}

} // namespace

// --- AddLayerCommand ----------------------------------------------------------------

AddLayerCommand::AddLayerCommand(Document &doc, int index, LayerProperties properties)
    : m_id(doc.allocateLayerId())
    , m_index(index)
    , m_layer(std::make_unique<Layer>(m_id, std::move(properties)))
{
}

ChangeSet AddLayerCommand::apply(Document &doc)
{
    if (m_layer)
        doc.insertLayer(m_index, std::move(m_layer));
    return structural();
}

ChangeSet AddLayerCommand::revert(Document &doc)
{
    m_layer = doc.takeLayer(m_id);
    return structural();
}

std::size_t AddLayerCommand::memoryBytes() const
{
    return sizeof(*this) + layerBytes(m_layer.get());
}

// --- RemoveLayerCommand -------------------------------------------------------------

RemoveLayerCommand::RemoveLayerCommand(LayerId layer)
    : m_id(layer)
{
}

ChangeSet RemoveLayerCommand::apply(Document &doc)
{
    m_index = doc.indexOf(m_id);
    m_layer = doc.takeLayer(m_id);
    return structural();
}

ChangeSet RemoveLayerCommand::revert(Document &doc)
{
    if (m_layer)
        doc.insertLayer(m_index, std::move(m_layer));
    return structural();
}

std::size_t RemoveLayerCommand::memoryBytes() const
{
    return sizeof(*this) + layerBytes(m_layer.get());
}

// --- MoveLayerCommand ---------------------------------------------------------------

MoveLayerCommand::MoveLayerCommand(LayerId layer, int newIndex)
    : m_id(layer)
    , m_newIndex(newIndex)
{
}

ChangeSet MoveLayerCommand::apply(Document &doc)
{
    m_oldIndex = doc.indexOf(m_id);
    doc.moveLayer(m_id, m_newIndex);
    return structural();
}

ChangeSet MoveLayerCommand::revert(Document &doc)
{
    doc.moveLayer(m_id, m_oldIndex);
    return structural();
}

// --- SetLayerPropertiesCommand ------------------------------------------------------

SetLayerPropertiesCommand::SetLayerPropertiesCommand(LayerId layer, LayerProperties before,
                                                     LayerProperties after)
    : m_id(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetLayerPropertiesCommand::put(Document &doc, const LayerProperties &properties)
{
    if (Layer *layer = doc.layer(m_id))
        layer->setProperties(properties);
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

std::unique_ptr<Command> makeSetLayerProperties(const Document &doc, LayerId layerId, LayerProperties properties)
{
    const Layer *layer = doc.layer(layerId);
    if (!layer || layer->properties() == properties)
        return nullptr;
    return std::make_unique<SetLayerPropertiesCommand>(layerId, layer->properties(), std::move(properties));
}

} // namespace mothion::doc
