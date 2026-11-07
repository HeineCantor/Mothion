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

bool isRaster(const doc::Layer &layer)
{
    return layer.kind == doc::LayerKind::Raster;
}

puppet::RasterImage rasterOf(const doc::Layer *layer, const QRect &area)
{
    puppet::RasterImage image(area.x(), area.y(), area.width(), area.height());
    if (layer && !layer->image.isEmpty())
        layer->image.read(area.x(), area.y(), area.width(), area.height(), image.pixels.data(), area.width());
    return image;
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

    // The layers in pairs: by name first, then what is left in the order it is stacked.
    std::vector<int> a, b; // the raster layers, as indices
    for (std::size_t i = 0; i < t.m_from.layers.size(); ++i)
        if (isRaster(t.m_from.layers[i]))
            a.push_back(int(i));
    for (std::size_t i = 0; i < t.m_to.layers.size(); ++i)
        if (isRaster(t.m_to.layers[i]))
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
    return t;
}

doc::Layer DrawingTween::layerAt(int index, double t) const
{
    const Pair &pair = m_pairs[std::size_t(index)];
    const doc::Layer *from = pair.from >= 0 ? &m_from.layers[std::size_t(pair.from)] : nullptr;
    const doc::Layer *to = pair.to >= 0 ? &m_to.layers[std::size_t(pair.to)] : nullptr;
    doc::Layer layer;
    layer.properties = (from ? from : to)->properties;
    const puppet::RasterImage a = rasterOf(from, m_area);
    const puppet::RasterImage b = rasterOf(to, m_area);
    const puppet::RasterImage result = tween::morph(a, b, m_field, t);
    layer.image.write(m_area.x(), m_area.y(), m_area.width(), m_area.height(), result.pixels.data(), m_area.width());
    return layer;
}

} // namespace mothion::document
