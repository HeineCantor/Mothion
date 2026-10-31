#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace mothion::doc {

// One pixel: premultiplied ARGB packed as 0xAARRGGBB in a native uint32.
// This is exactly QImage::Format_ARGB32_Premultiplied, so tiles can be shown
// without conversion; the core itself never includes Qt.
using Pixel = std::uint32_t;

inline constexpr int kTileSize = 64; // pixels per side
inline constexpr int kTilePixels = kTileSize * kTileSize;

inline constexpr std::uint8_t alphaOf(Pixel p) { return std::uint8_t(p >> 24); }

// A square block of pixels. Tiles are shared between images, undo history
// and snapshots through shared pointers; see TiledImage for the copy-on-write rule.
struct Tile
{
    std::array<Pixel, kTilePixels> pixels{}; // 16 KB, zero = transparent

    Pixel *row(int y) { return pixels.data() + y * kTileSize; }
    const Pixel *row(int y) const { return pixels.data() + y * kTileSize; }
    bool isTransparent() const;
};

using TilePtr = std::shared_ptr<const Tile>;

// Tile position in tile units: tile (1, 0) covers pixels x 64..127, y 0..63.
struct TileCoord
{
    int x = 0;
    int y = 0;
    auto operator<=>(const TileCoord &) const = default;
};

struct TileCoordHash
{
    std::size_t operator()(const TileCoord &c) const noexcept
    {
        return std::hash<std::uint64_t>{}((std::uint64_t(std::uint32_t(c.x)) << 32) | std::uint32_t(c.y));
    }
};

} // namespace mothion::doc
