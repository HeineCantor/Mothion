#pragma once

#include "core/document/Command.h"
#include "core/document/Layer.h"

#include <memory>

namespace mothion::doc {

// Adds a new empty layer at `index` (0 = bottom). The id is allocated at
// construction, so redo recreates the same layer.
class AddLayerCommand : public Command
{
public:
    AddLayerCommand(Document &doc, int index, LayerProperties properties);

    LayerId layerId() const { return m_id; }

    CommandKind kind() const override { return CommandKind::AddLayer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    LayerId m_id;
    int m_index;
    std::unique_ptr<Layer> m_layer; // held while not in the document
};

// Removes a layer with all its drawings; undo puts it back at the same index.
class RemoveLayerCommand : public Command
{
public:
    explicit RemoveLayerCommand(LayerId layer);

    CommandKind kind() const override { return CommandKind::RemoveLayer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    LayerId m_id;
    int m_index = 0;
    std::unique_ptr<Layer> m_layer;
};

class MoveLayerCommand : public Command
{
public:
    MoveLayerCommand(LayerId layer, int newIndex);

    CommandKind kind() const override { return CommandKind::MoveLayer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    LayerId m_id;
    int m_newIndex;
    int m_oldIndex = 0;
};

class SetLayerPropertiesCommand : public Command
{
public:
    SetLayerPropertiesCommand(LayerId layer, LayerProperties before, LayerProperties after);

    CommandKind kind() const override { return CommandKind::ChangeLayerProperties; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    ChangeSet put(Document &doc, const LayerProperties &properties);

    LayerId m_id;
    LayerProperties m_before;
    LayerProperties m_after;
};

// nullptr if the properties are already `properties`.
std::unique_ptr<Command> makeSetLayerProperties(const Document &doc, LayerId layer, LayerProperties properties);

} // namespace mothion::doc
