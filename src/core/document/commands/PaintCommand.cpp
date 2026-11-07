#include "core/document/commands/PaintCommand.h"

#include "core/document/Document.h"

#include <set>

namespace mothion::doc {

std::unique_ptr<PaintCommand> PaintCommand::capture(CelId cel, DrawingId drawing, LayerId layer,
                                                    const TiledImage &before, const TiledImage &after, bool mask)
{
    std::set<TileCoord> coords;
    for (const TileCoord &c : before.tileCoords())
        coords.insert(c);
    for (const TileCoord &c : after.tileCoords())
        coords.insert(c);

    std::vector<TileSwap> swaps;
    for (const TileCoord &c : coords) {
        TilePtr b = before.tile(c);
        TilePtr a = after.tile(c);
        if (b != a)
            swaps.push_back({c, std::move(b), std::move(a)});
    }
    if (swaps.empty())
        return nullptr;
    return std::make_unique<PaintCommand>(cel, drawing, layer, std::move(swaps), mask);
}

PaintCommand::PaintCommand(CelId cel, DrawingId drawing, LayerId layer, std::vector<TileSwap> swaps, bool mask)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_layer(layer)
    , m_swaps(std::move(swaps))
    , m_mask(mask)
{
}

ChangeSet PaintCommand::put(Document &doc, bool after)
{
    Layer *layer = doc.layer(m_cel, m_drawing, m_layer);
    if (!layer)
        return {};
    ChangeSet::Pixels changed{m_cel, m_drawing, m_layer, {}};
    TiledImage &image = m_mask ? (layer->mask ? *layer->mask : layer->mask.emplace()) : layer->image;
    for (const TileSwap &s : m_swaps) {
        image.setTile(s.coord, after ? s.after : s.before);
        changed.tiles.push_back(s.coord);
    }
    ChangeSet changes;
    changes.pixels.push_back(std::move(changed));
    return changes;
}

ChangeSet PaintCommand::apply(Document &doc)
{
    return put(doc, true);
}

ChangeSet PaintCommand::revert(Document &doc)
{
    return put(doc, false);
}

std::size_t PaintCommand::memoryBytes() const
{
    // Upper bound: the current side is also held by the document.
    std::size_t tiles = 0;
    for (const TileSwap &s : m_swaps)
        tiles += (s.before ? 1 : 0) + (s.after ? 1 : 0);
    return sizeof(*this) + tiles * sizeof(Tile);
}

} // namespace mothion::doc
