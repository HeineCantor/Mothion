#include "canvas/SelectionMask.h"

#include "canvas/TileQt.h"

#include <QPainter>

#include <vector>

namespace mothion::canvas {

namespace {

// Premultiplied pixel scaled by k / 255.
doc::Pixel scaled(doc::Pixel p, int k)
{
    if (k >= 255)
        return p;
    if (k <= 0)
        return 0;
    auto channel = [&](int shift) { return ((((p >> shift) & 0xFF) * std::uint32_t(k) + 127) / 255) << shift; };
    return channel(24) | channel(16) | channel(8) | channel(0);
}

doc::Pixel over(doc::Pixel dst, doc::Pixel src)
{
    const int a = int(src >> 24);
    if (a == 255)
        return src;
    if (a == 0)
        return dst;
    return src + scaled(dst, 255 - a);
}

} // namespace

SelectionMask SelectionMask::fromPath(const QPainterPath &area)
{
    SelectionMask mask;
    mask.bounds = area.boundingRect().toAlignedRect();
    if (mask.bounds.isEmpty())
        return mask;
    mask.coverage = QImage(mask.bounds.size(), QImage::Format_Alpha8);
    mask.coverage.fill(0);
    QPainter p(&mask.coverage);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-mask.bounds.topLeft());
    p.fillPath(area, Qt::black);
    return mask;
}

int SelectionMask::at(int x, int y) const
{
    if (!bounds.contains(x, y))
        return 0;
    return coverage.constScanLine(y - bounds.y())[x - bounds.x()];
}

void fillByMask(doc::TiledImage &image, const SelectionMask &mask, doc::Pixel color, bool clear)
{
    if (mask.isEmpty())
        return;
    const QRect r = mask.bounds;
    std::vector<doc::Pixel> pixels(std::size_t(r.width()) * std::size_t(r.height()));
    image.read(r.x(), r.y(), r.width(), r.height(), pixels.data(), r.width());
    const int alpha = int(color >> 24);
    for (int y = 0; y < r.height(); ++y) {
        const uchar *cov = mask.coverage.constScanLine(y);
        for (int x = 0; x < r.width(); ++x) {
            if (!cov[x])
                continue;
            doc::Pixel &p = pixels[std::size_t(y) * std::size_t(r.width()) + std::size_t(x)];
            p = clear ? scaled(p, 255 - (cov[x] * alpha + 127) / 255) : over(p, scaled(color, cov[x]));
        }
    }
    image.write(r.x(), r.y(), r.width(), r.height(), pixels.data(), r.width());
}

void splitByMask(const doc::TiledImage &image, const SelectionMask &mask, doc::TiledImage &inside,
                 doc::TiledImage &outside)
{
    inside = {};
    outside = image;
    if (mask.isEmpty())
        return;
    const QRect r = mask.bounds;
    std::vector<doc::Pixel> src(std::size_t(r.width()) * std::size_t(r.height()));
    image.read(r.x(), r.y(), r.width(), r.height(), src.data(), r.width());
    std::vector<doc::Pixel> in(src.size()), out(src.size());
    for (int y = 0; y < r.height(); ++y) {
        const uchar *cov = mask.coverage.constScanLine(y);
        for (int x = 0; x < r.width(); ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(r.width()) + std::size_t(x);
            in[i] = scaled(src[i], cov[x]);
            out[i] = scaled(src[i], 255 - cov[x]);
        }
    }
    inside.write(r.x(), r.y(), r.width(), r.height(), in.data(), r.width());
    outside.write(r.x(), r.y(), r.width(), r.height(), out.data(), r.width());
}

void compositeOver(doc::TiledImage &bottom, const doc::TiledImage &top)
{
    std::vector<doc::Pixel> under(doc::kTilePixels);
    for (const doc::TileCoord &c : top.tileCoords()) {
        const doc::TilePtr tile = top.tile(c);
        if (!bottom.tile(c)) { // nothing underneath: share the tile
            bottom.setTile(c, tile);
            continue;
        }
        const int x = c.x * doc::kTileSize, y = c.y * doc::kTileSize;
        bottom.read(x, y, doc::kTileSize, doc::kTileSize, under.data(), doc::kTileSize);
        for (int i = 0; i < doc::kTilePixels; ++i)
            under[std::size_t(i)] = over(under[std::size_t(i)], tile->pixels[std::size_t(i)]);
        bottom.write(x, y, doc::kTileSize, doc::kTileSize, under.data(), doc::kTileSize);
    }
}

} // namespace mothion::canvas
