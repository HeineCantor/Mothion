#pragma once

#include "core/document/TiledImage.h"

#include <QImage>
#include <QRect>

#include <vector>

// Bridges between the Qt-free tile model and QPainter.

namespace mothion::canvas {

// Wraps tile memory as a QImage without copying (same pixel format).
// The image is only valid while the tile is alive and not replaced.
QImage tileImage(doc::Tile &tile);
QImage tileImage(const doc::Tile &tile); // read-only

// Document rectangle covered by a tile.
QRect tileRect(doc::TileCoord coord);

// Tiles intersecting a document rectangle.
std::vector<doc::TileCoord> tilesIn(const QRect &rect);

// Copies an area of a tiled image into a new QImage (for previews and tests).
QImage toQImage(const doc::TiledImage &image, const QRect &area);

} // namespace mothion::canvas
