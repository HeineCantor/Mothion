#include "core/document/TiledImage.h"

#include <algorithm>

namespace mothion::doc {

namespace {

// Floor division / modulo, correct for negative coordinates.
int floorDiv(int a, int b) { return (a >= 0 ? a : a - b + 1) / b; }
int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }

} // namespace

bool Tile::isTransparent() const
{
    return std::all_of(pixels.begin(), pixels.end(), [](Pixel p) { return alphaOf(p) == 0; });
}

TileCoord TiledImage::tileAt(int x, int y)
{
    return {floorDiv(x, kTileSize), floorDiv(y, kTileSize)};
}

std::vector<TileCoord> TiledImage::tileCoords() const
{
    std::vector<TileCoord> coords;
    coords.reserve(m_tiles.size());
    for (const auto &entry : m_tiles)
        coords.push_back(entry.first);
    return coords;
}

TilePtr TiledImage::tile(TileCoord coord) const
{
    const auto it = m_tiles.find(coord);
    return it == m_tiles.end() ? nullptr : it->second;
}

void TiledImage::setTile(TileCoord coord, TilePtr tile)
{
    if (!tile || tile->isTransparent()) {
        m_tiles.erase(coord);
        return;
    }
    // Stored as non-const so this image can write it later; the shared
    // ownership (use_count > 1) still forces a clone before any write.
    m_tiles[coord] = std::const_pointer_cast<Tile>(std::move(tile));
}

Tile &TiledImage::writableTile(TileCoord coord)
{
    std::shared_ptr<Tile> &slot = m_tiles[coord];
    if (!slot)
        slot = std::make_shared<Tile>();
    else if (slot.use_count() > 1)
        slot = std::make_shared<Tile>(*slot); // copy-on-write
    return *slot;
}

Pixel TiledImage::pixel(int x, int y) const
{
    const auto it = m_tiles.find(tileAt(x, y));
    if (it == m_tiles.end())
        return 0;
    return it->second->row(floorMod(y, kTileSize))[floorMod(x, kTileSize)];
}

void TiledImage::setPixel(int x, int y, Pixel value)
{
    write(x, y, 1, 1, &value, 1);
}

void TiledImage::read(int x, int y, int width, int height, Pixel *dst, std::ptrdiff_t dstStride) const
{
    for (int ty = floorDiv(y, kTileSize); ty <= floorDiv(y + height - 1, kTileSize); ++ty) {
        for (int tx = floorDiv(x, kTileSize); tx <= floorDiv(x + width - 1, kTileSize); ++tx) {
            // Intersection of the request with this tile, in image coordinates.
            const int x0 = std::max(x, tx * kTileSize), x1 = std::min(x + width, (tx + 1) * kTileSize);
            const int y0 = std::max(y, ty * kTileSize), y1 = std::min(y + height, (ty + 1) * kTileSize);
            const auto it = m_tiles.find({tx, ty});
            for (int py = y0; py < y1; ++py) {
                Pixel *out = dst + (py - y) * dstStride + (x0 - x);
                if (it == m_tiles.end())
                    std::fill(out, out + (x1 - x0), Pixel(0));
                else
                    std::copy_n(it->second->row(py - ty * kTileSize) + (x0 - tx * kTileSize), x1 - x0, out);
            }
        }
    }
}

void TiledImage::write(int x, int y, int width, int height, const Pixel *src, std::ptrdiff_t srcStride)
{
    for (int ty = floorDiv(y, kTileSize); ty <= floorDiv(y + height - 1, kTileSize); ++ty) {
        for (int tx = floorDiv(x, kTileSize); tx <= floorDiv(x + width - 1, kTileSize); ++tx) {
            const int x0 = std::max(x, tx * kTileSize), x1 = std::min(x + width, (tx + 1) * kTileSize);
            const int y0 = std::max(y, ty * kTileSize), y1 = std::min(y + height, (ty + 1) * kTileSize);
            const TileCoord coord{tx, ty};

            if (!m_tiles.contains(coord)) {
                // Don't create a tile just to store transparency.
                bool anyVisible = false;
                for (int py = y0; py < y1 && !anyVisible; ++py) {
                    const Pixel *in = src + (py - y) * srcStride + (x0 - x);
                    anyVisible = std::any_of(in, in + (x1 - x0), [](Pixel p) { return alphaOf(p) != 0; });
                }
                if (!anyVisible)
                    continue;
            }

            Tile &t = writableTile(coord);
            for (int py = y0; py < y1; ++py)
                std::copy_n(src + (py - y) * srcStride + (x0 - x), x1 - x0,
                            t.row(py - ty * kTileSize) + (x0 - tx * kTileSize));
            if (t.isTransparent())
                m_tiles.erase(coord);
        }
    }
}

void TiledImage::removeTransparentTiles()
{
    std::erase_if(m_tiles, [](const auto &entry) { return entry.second->isTransparent(); });
}

TiledImage TiledImage::translated(int dx, int dy) const
{
    TiledImage out;
    if (dx % kTileSize == 0 && dy % kTileSize == 0) {
        for (const auto &[c, tile] : m_tiles)
            out.m_tiles.emplace(TileCoord{c.x + dx / kTileSize, c.y + dy / kTileSize}, tile);
        return out;
    }
    for (const auto &[c, tile] : m_tiles)
        out.write(c.x * kTileSize + dx, c.y * kTileSize + dy, kTileSize, kTileSize, tile->pixels.data(), kTileSize);
    return out;
}

} // namespace mothion::doc
