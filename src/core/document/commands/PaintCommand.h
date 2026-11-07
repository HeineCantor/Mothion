#pragma once

#include "core/document/Command.h"
#include "core/document/TiledImage.h"

#include <memory>
#include <vector>

namespace mothion::doc {

// Pixel change of one layer of a drawing, stored as tile pointers before and after.
//
// Usage for a stroke: copy the drawing's image before painting (a cheap
// snapshot that shares all tiles), paint into the drawing, then capture().
// Because the snapshot holds every tile, each tile the stroke touches is
// cloned on write, so "changed" is simply "different pointer".
class PaintCommand : public Command
{
public:
    struct TileSwap
    {
        TileCoord coord;
        TilePtr before; // nullptr = was transparent
        TilePtr after;  // nullptr = is transparent
    };

    // nullptr if `before` and `after` share every tile (nothing was painted).
    // `mask`: the pixels are those of the layer's mask, not of the layer.
    static std::unique_ptr<PaintCommand> capture(CelId cel, DrawingId drawing, LayerId layer,
                                                 const TiledImage &before, const TiledImage &after, bool mask = false);

    PaintCommand(CelId cel, DrawingId drawing, LayerId layer, std::vector<TileSwap> swaps, bool mask = false);

    CommandKind kind() const override { return CommandKind::Paint; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

    const std::vector<TileSwap> &swaps() const { return m_swaps; }

private:
    ChangeSet put(Document &doc, bool after);

    CelId m_cel;
    DrawingId m_drawing;
    LayerId m_layer;
    std::vector<TileSwap> m_swaps;
    bool m_mask;
};

} // namespace mothion::doc
