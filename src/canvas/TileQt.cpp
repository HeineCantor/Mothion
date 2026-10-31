#include "canvas/TileQt.h"

namespace mothion::canvas {

using doc::kTileSize;

QImage tileImage(doc::Tile &tile)
{
    return QImage(reinterpret_cast<uchar *>(tile.pixels.data()), kTileSize, kTileSize,
                  kTileSize * int(sizeof(doc::Pixel)), QImage::Format_ARGB32_Premultiplied);
}

QImage tileImage(const doc::Tile &tile)
{
    // The const-uchar constructor makes Qt copy before any write: safe for shared tiles.
    return QImage(reinterpret_cast<const uchar *>(tile.pixels.data()), kTileSize, kTileSize,
                  kTileSize * int(sizeof(doc::Pixel)), QImage::Format_ARGB32_Premultiplied);
}

QRect tileRect(doc::TileCoord coord)
{
    return {coord.x * kTileSize, coord.y * kTileSize, kTileSize, kTileSize};
}

std::vector<doc::TileCoord> tilesIn(const QRect &rect)
{
    std::vector<doc::TileCoord> coords;
    if (rect.isEmpty())
        return coords;
    const doc::TileCoord first = doc::TiledImage::tileAt(rect.left(), rect.top());
    const doc::TileCoord last = doc::TiledImage::tileAt(rect.right(), rect.bottom());
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x)
            coords.push_back({x, y});
    return coords;
}

QImage toQImage(const doc::TiledImage &image, const QRect &area)
{
    QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
    image.read(area.x(), area.y(), area.width(), area.height(), reinterpret_cast<doc::Pixel *>(out.bits()),
               out.bytesPerLine() / int(sizeof(doc::Pixel)));
    return out;
}

} // namespace mothion::canvas
