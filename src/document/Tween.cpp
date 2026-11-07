#include "document/Tween.h"

#include "canvas/FrameRenderer.h"
#include "core/puppet/WarpRaster.h"
#include "core/tween/Morph.h"

#include <QImage>
#include <QObject>

#include <algorithm>
#include <cmath>

namespace mothion::document {

double easedTime(TweenEase ease, double u)
{
    u = std::clamp(u, 0.0, 1.0);
    switch (ease) {
    case TweenEase::Linear: return u;
    case TweenEase::EaseIn: return u * u;
    case TweenEase::EaseOut: return u * (2.0 - u);
    case TweenEase::EaseInOut: return u * u * (3.0 - 2.0 * u);
    }
    return u;
}

namespace {

// The registration works on a picture about this big (the longer side), whatever the size of the drawings.
constexpr int kWorkSize = 220;
constexpr int kMargin = 24; // px around the drawings, room for what moves outward

// A layer that is part of the picture: pixels that are shown (not hidden, not see-through to nothing). Hidden layers
// are left out of the tween altogether: they are not paired, not moved, and the frames made do not have them.
bool isTweened(const doc::Layer &layer)
{
    return layer.kind == doc::LayerKind::Raster && layer.properties.visible && layer.properties.opacity > 0.0f
           && !layer.image.isEmpty();
}

puppet::RasterImage rasterOf(const doc::Layer *layer, const QRect &area)
{
    puppet::RasterImage image(area.x(), area.y(), area.width(), area.height());
    if (layer && !layer->image.isEmpty())
        layer->image.read(area.x(), area.y(), area.width(), area.height(), image.pixels.data(), area.width());
    return image;
}

// The pixels of a layer, where they are (tile bounds), nothing for none.
QRect boundsOf(const doc::Layer *layer)
{
    QRect bounds;
    if (layer)
        for (const doc::TileCoord &c : layer->image.tileCoords())
            bounds |= QRect(c.x * doc::kTileSize, c.y * doc::kTileSize, doc::kTileSize, doc::kTileSize);
    return bounds;
}

// A block of a field, as a field of its own.
tween::Field cropped(const tween::Field &field, const QRect &block)
{
    tween::Field out(block.width(), block.height());
    for (int y = 0; y < block.height(); ++y)
        for (int x = 0; x < block.width(); ++x) {
            const std::size_t from = std::size_t(block.y() + y) * std::size_t(field.width) + std::size_t(block.x() + x);
            const std::size_t to = std::size_t(y) * std::size_t(block.width()) + std::size_t(x);
            out.dx[to] = field.dx[from];
            out.dy[to] = field.dy[from];
        }
    return out;
}

// The picture made `factor` times smaller (the mean of each block of pixels, premultiplied as they are).
puppet::RasterImage shrunk(const puppet::RasterImage &image, int factor, int width, int height)
{
    puppet::RasterImage out(0, 0, width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            unsigned a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int j = y * factor; j < std::min(image.height, (y + 1) * factor); ++j)
                for (int i = x * factor; i < std::min(image.width, (x + 1) * factor); ++i) {
                    const std::uint32_t p = image.pixels[std::size_t(j) * std::size_t(image.width) + std::size_t(i)];
                    a += p >> 24;
                    r += (p >> 16) & 0xff;
                    g += (p >> 8) & 0xff;
                    b += p & 0xff;
                    ++n;
                }
            if (n)
                out.pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] =
                    ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    return out;
}

} // namespace

std::optional<DrawingTween> DrawingTween::make(const doc::Drawing &from, const doc::Drawing &to, QString *reason)
{
    const QRect bounds = canvas::drawingBounds(from, false) | canvas::drawingBounds(to, false);
    if (bounds.isEmpty()) {
        if (reason)
            *reason = QObject::tr("The two frames have nothing drawn on them");
        return std::nullopt;
    }
    DrawingTween t;
    t.m_from = from;
    t.m_to = to;
    t.m_area = bounds.adjusted(-kMargin, -kMargin, kMargin, kMargin);

    // The layers in pairs (the visible ones with pixels): by name first, then what is left in the order it is stacked.
    std::vector<int> a, b; // the raster layers, as indices
    for (std::size_t i = 0; i < t.m_from.layers.size(); ++i)
        if (isTweened(t.m_from.layers[i]))
            a.push_back(int(i));
    for (std::size_t i = 0; i < t.m_to.layers.size(); ++i)
        if (isTweened(t.m_to.layers[i]))
            b.push_back(int(i));
    auto nameA = [&](std::size_t i) -> const std::string & { return t.m_from.layers[std::size_t(a[i])].properties.name; };
    auto nameB = [&](std::size_t j) -> const std::string & { return t.m_to.layers[std::size_t(b[j])].properties.name; };
    std::vector<bool> aUsed(a.size(), false), bUsed(b.size(), false);
    std::vector<int> partnerOfA(a.size(), -1);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            if (!bUsed[j] && nameA(i) == nameB(j)) {
                partnerOfA[i] = int(j);
                bUsed[j] = true;
                break;
            }
    std::size_t nextB = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (partnerOfA[i] >= 0)
            continue;
        while (nextB < b.size() && bUsed[nextB])
            ++nextB;
        if (nextB < b.size()) {
            partnerOfA[i] = int(nextB);
            bUsed[nextB] = true;
        }
    }
    for (std::size_t i = 0; i < a.size(); ++i)
        t.m_pairs.push_back({a[i], partnerOfA[i] >= 0 ? b[std::size_t(partnerOfA[i])] : -1});
    for (std::size_t j = 0; j < b.size(); ++j)
        if (!bUsed[j])
            t.m_pairs.push_back({-1, b[j]}); // a layer that only the second frame has: it fades in

    // How much ink there is at each point of the two drawings (their pictures' alpha), seen small.
    const int factor = std::max(1, (std::max(t.m_area.width(), t.m_area.height()) + kWorkSize - 1) / kWorkSize);
    auto ink = [&](const doc::Drawing &d) {
        const QImage image = canvas::composeDrawing(d, t.m_area, false);
        tween::Image full(image.width(), image.height());
        for (int y = 0; y < image.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                full.at(x, y) = float(qAlpha(row[x])) / 255.0f;
        }
        return tween::downscaled(full, factor);
    };
    const tween::Field small = tween::registerImages(ink(t.m_from), ink(t.m_to));
    t.m_field = tween::resized(small, t.m_area.width(), t.m_area.height());
    double reach = 0.0;
    for (std::size_t i = 0; i < t.m_field.dx.size(); ++i)
        reach = std::max(reach, std::hypot(double(t.m_field.dx[i]), double(t.m_field.dy[i])));
    t.m_reach = int(std::ceil(reach));

    // Small copies of the layers and of the field, for the preview.
    constexpr int kPreviewSize = 400;
    t.m_previewScale = std::max(1, (std::max(t.m_area.width(), t.m_area.height()) + kPreviewSize - 1) / kPreviewSize);
    t.m_previewSize = QSize((t.m_area.width() + t.m_previewScale - 1) / t.m_previewScale,
                            (t.m_area.height() + t.m_previewScale - 1) / t.m_previewScale);
    t.m_previewField = tween::resized(small, t.m_previewSize.width(), t.m_previewSize.height());
    for (const Pair &pair : t.m_pairs) {
        const doc::Layer *first = pair.from >= 0 ? &t.m_from.layers[std::size_t(pair.from)] : nullptr;
        const doc::Layer *second = pair.to >= 0 ? &t.m_to.layers[std::size_t(pair.to)] : nullptr;
        t.m_small.push_back({shrunk(rasterOf(first, t.m_area), t.m_previewScale, t.m_previewSize.width(), t.m_previewSize.height()),
                             shrunk(rasterOf(second, t.m_area), t.m_previewScale, t.m_previewSize.width(), t.m_previewSize.height()),
                             (first ? first : second)->properties});
    }
    return t;
}

QImage DrawingTween::preview(double t) const
{
    doc::Drawing between;
    for (const SmallPair &pair : m_small) {
        doc::Layer layer;
        layer.properties = pair.look;
        const puppet::RasterImage r = tween::morph(pair.from, pair.to, m_previewField, t);
        layer.image.write(0, 0, r.width, r.height, r.pixels.data(), r.width);
        between.layers.push_back(std::move(layer));
    }
    return canvas::composeDrawing(between, QRect(QPoint(0, 0), m_previewSize), false);
}

QImage DrawingTween::picture(double t) const
{
    doc::Drawing between;
    for (int i = 0; i < layerCount(); ++i)
        between.layers.push_back(layerAt(i, t));
    return canvas::composeDrawing(between, m_area, false);
}

doc::Layer DrawingTween::layerAt(int index, double t) const
{
    const Pair &pair = m_pairs[std::size_t(index)];
    const doc::Layer *from = pair.from >= 0 ? &m_from.layers[std::size_t(pair.from)] : nullptr;
    const doc::Layer *to = pair.to >= 0 ? &m_to.layers[std::size_t(pair.to)] : nullptr;
    doc::Layer layer;
    layer.properties = (from ? from : to)->properties;
    // Only where the layer has pixels, and as far around them as anything moves: an empty layer costs nothing and a
    // sparse one little, whatever the size of the drawing.
    const QRect content = boundsOf(from) | boundsOf(to);
    if (content.isEmpty())
        return layer;
    const QRect block = content.adjusted(-m_reach - 4, -m_reach - 4, m_reach + 4, m_reach + 4) & m_area;
    const puppet::RasterImage a = rasterOf(from, block);
    const puppet::RasterImage b = rasterOf(to, block);
    const puppet::RasterImage result = tween::morph(a, b, cropped(m_field, block.translated(-m_area.topLeft())), t);
    layer.image.write(block.x(), block.y(), block.width(), block.height(), result.pixels.data(), block.width());
    return layer;
}

} // namespace mothion::document
