#include "core/document/commands/PaintCommand.h"

#include "core/document/Document.h"

#include <set>

namespace mothion::doc {

std::unique_ptr<PaintCommand> PaintCommand::capture(CelId cel, DrawingId drawing, const TiledImage &before,
                                                    const TiledImage &after)
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
    return std::make_unique<PaintCommand>(cel, drawing, std::move(swaps));
}

PaintCommand::PaintCommand(CelId cel, DrawingId drawing, std::vector<TileSwap> swaps)
    : m_cel(cel)
    , m_drawing(drawing)
    , m_swaps(std::move(swaps))
{
}

ChangeSet PaintCommand::put(Document &doc, bool after)
{
    Cel *cel = doc.cel(m_cel);
    Drawing *drawing = cel ? cel->drawing(m_drawing) : nullptr;
    if (!drawing)
        return {};
    ChangeSet::Pixels changed{m_cel, m_drawing, {}};
    for (const TileSwap &s : m_swaps) {
        drawing->image.setTile(s.coord, after ? s.after : s.before);
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
