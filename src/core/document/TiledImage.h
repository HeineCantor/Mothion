#pragma once

#include "core/document/Tile.h"

#include <unordered_map>
#include <vector>

namespace mothion::doc {

// A sparse raster image made of 64×64 tiles. Missing tiles are fully
// transparent, so an image with a few lines costs a few tiles, whatever
// the canvas size. Coordinates are unbounded (may be negative); the
// document size is a clipping concern of whoever draws.
//
// Copy-on-write: copying a TiledImage only copies tile pointers. A tile is
// cloned before being written if anyone else (another image, a snapshot, an
// undo command) still holds it. That makes snapshots and undo cheap: a
// stroke's undo record is just "these tile pointers before, those after".
class TiledImage
{
public:
    using TileMap = std::unordered_map<TileCoord, std::shared_ptr<Tile>, TileCoordHash>;

    // Tile containing pixel (x, y); floor division, so negatives work.
    static TileCoord tileAt(int x, int y);

    bool isEmpty() const { return m_tiles.empty(); }
    std::size_t tileCount() const { return m_tiles.size(); }
    std::vector<TileCoord> tileCoords() const;

    // nullptr means "transparent".
    TilePtr tile(TileCoord coord) const;
    // Puts a tile in place (shared, not copied). nullptr or a transparent tile removes it.
    void setTile(TileCoord coord, TilePtr tile);

    // A tile that may be written: created if missing, cloned if shared.
    Tile &writableTile(TileCoord coord);

    Pixel pixel(int x, int y) const;
    void setPixel(int x, int y, Pixel value);

    // Rectangle copies to/from a caller buffer (stride in pixels).
    void read(int x, int y, int width, int height, Pixel *dst, std::ptrdiff_t dstStride) const;
    // Writing transparency never creates tiles, and tiles that end up fully
    // transparent are dropped.
    void write(int x, int y, int width, int height, const Pixel *src, std::ptrdiff_t srcStride);

    void removeTransparentTiles();

    // A copy moved by whole pixels. Moves by whole tiles share the tiles.
    TiledImage translated(int dx, int dy) const;

    // Bytes held by the tiles (counted fully even when shared with others).
    std::size_t memoryBytes() const { return m_tiles.size() * sizeof(Tile); }

private:
    TileMap m_tiles;
};

} // namespace mothion::doc
