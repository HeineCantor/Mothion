#pragma once

#include "core/document/Command.h"
#include "core/document/Drawing.h"

#include <memory>
#include <optional>

namespace mothion::doc {

// Adds a layer of `kind` at `index` (0 = bottom) of a drawing, empty or with
// `image`.
// The id is allocated at construction, so redo recreates the same layer.
class AddLayerCommand : public Command
{
public:
    AddLayerCommand(Document &doc, CelId cel, DrawingId drawing, int index, LayerProperties properties,
                    TiledImage image = {}, LayerKind kind = LayerKind::Raster);

    LayerId layerId() const { return m_id; }

    CommandKind kind() const override { return CommandKind::AddLayer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_id;
    int m_index;
    std::optional<Layer> m_layer; // held while not in the document
};

// Removes a layer with its pixels; undo puts it back at the same index.
class RemoveLayerCommand : public Command
{
public:
    RemoveLayerCommand(CelId cel, DrawingId drawing, LayerId layer);

    CommandKind kind() const override { return CommandKind::RemoveLayer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_id;
    int m_index = 0;
    std::optional<Layer> m_layer;
};

class MoveLayerCommand : public Command
{
public:
    MoveLayerCommand(CelId cel, DrawingId drawing, LayerId layer, int newIndex);

    CommandKind kind() const override { return CommandKind::MoveLayer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    ChangeSet moveTo(Document &doc, int index);

    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_id;
    int m_newIndex;
    int m_oldIndex = 0;
};

class SetLayerPropertiesCommand : public Command
{
public:
    SetLayerPropertiesCommand(CelId cel, DrawingId drawing, LayerId layer, LayerProperties before,
                              LayerProperties after);

    CommandKind kind() const override { return CommandKind::ChangeLayerProperties; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    ChangeSet put(Document &doc, const LayerProperties &properties);

    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_id;
    LayerProperties m_before;
    LayerProperties m_after;
};

// nullptr if the properties are already `properties`.
// Gives a layer a mask (empty: everything shows), or takes it away; undo puts back what there was.
class SetLayerMaskCommand : public Command
{
public:
    SetLayerMaskCommand(CelId cel, DrawingId drawing, LayerId layer, std::optional<TiledImage> before,
                        std::optional<TiledImage> after);

    CommandKind kind() const override { return CommandKind::ChangeMask; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    ChangeSet put(Document &doc, const std::optional<TiledImage> &mask);

    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_id;
    std::optional<TiledImage> m_before;
    std::optional<TiledImage> m_after;
};

// Null when the layer is not there, is not a raster layer, or already has (no) mask.
std::unique_ptr<Command> makeAddLayerMask(const Document &doc, CelId cel, DrawingId drawing, LayerId layer);
std::unique_ptr<Command> makeRemoveLayerMask(const Document &doc, CelId cel, DrawingId drawing, LayerId layer);

std::unique_ptr<Command> makeSetLayerProperties(const Document &doc, CelId cel, DrawingId drawing, LayerId layer,
                                                LayerProperties properties);

} // namespace mothion::doc
