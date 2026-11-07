#include "canvas/FrameRenderer.h"

#include "canvas/BlendDraw.h"
#include "canvas/TileQt.h"
#include "core/document/Blend.h"

#include <QPainter>
#include <QPainterPath>

#include <QLineF>

#include <algorithm>
#include <optional>
#include <utility>

namespace mothion::canvas {

namespace {

bool shows(const doc::Layer &layer, bool guides)
{
    if (!layer.properties.visible || layer.properties.opacity <= 0.0f)
        return false;
    return layer.kind == doc::LayerKind::Perspective ? guides : !layer.image.isEmpty();
}

// A tile of a layer as it shows: through the layer's mask, where it has pixels. The mask is black and white: a pixel of
// it that is white (or not there) lets the layer through, a black one does not, a gray one lets that much through.
// Without a mask over the tile the tile's own memory is wrapped, not copied.
QImage layerTile(const doc::Layer &layer, const doc::TileCoord &coord, const doc::Tile &tile)
{
    const doc::TilePtr mask = layer.mask ? layer.mask->tile(coord) : nullptr;
    if (!mask)
        return tileImage(tile);
    QImage out(doc::kTileSize, doc::kTileSize, QImage::Format_ARGB32_Premultiplied);
    auto *dst = reinterpret_cast<doc::Pixel *>(out.bits());
    for (int i = 0; i < doc::kTilePixels; ++i) {
        const doc::Pixel s = tile.pixels[std::size_t(i)];
        const doc::Pixel m = mask->pixels[std::size_t(i)];
        // The mask's pixel is premultiplied: its gray is (r, g, b) as they are, and what is not covered is white.
        const unsigned gray = (77u * ((m >> 16) & 0xff) + 150u * ((m >> 8) & 0xff) + 29u * (m & 0xff)) >> 8;
        const unsigned show = std::min(255u, 255u - (m >> 24) + gray);
        dst[i] = (((((s >> 24) * show + 127) / 255) << 24) | ((((s >> 16) & 0xff) * show + 127) / 255) << 16 |
                  ((((s >> 8) & 0xff) * show + 127) / 255) << 8 | ((s & 0xff) * show + 127) / 255);
    }
    return out;
}

// The modes QPainter lacks: the layer's tiles go over `target` pixel by pixel, inside `limit` (the target's pixel
// (0, 0) is the document's `origin`). The painter on the target is not touched.
void blendLayer(QImage &target, const QPoint &origin, const QRect &limit, const doc::Layer &layer,
                const std::vector<doc::TileCoord> &tiles, double opacity, doc::BlendMode blend)
{
    for (const doc::TileCoord &c : tiles) {
        const doc::TilePtr tile = layer.image.tile(c);
        if (!tile)
            continue;
        const QPoint corner = tileRect(c).topLeft() - origin;
        const QRect r = QRect(corner, tileRect(c).size()) & limit & target.rect();
        if (r.isEmpty())
            continue;
        const QImage pixels = layerTile(layer, c, *tile);
        for (int y = r.top(); y <= r.bottom(); ++y) {
            auto *dst = reinterpret_cast<doc::Pixel *>(target.scanLine(y)) + r.left();
            const auto *src = reinterpret_cast<const doc::Pixel *>(pixels.constScanLine(y - corner.y())) +
                              (r.left() - corner.x());
            doc::blendSpan(dst, src, r.width(), blend, float(opacity));
        }
    }
}

// An image (premultiplied, at `at` in `target`) over the target with a software blend mode, inside `limit`.
void blendImage(QImage &target, const QPoint &at, const QRect &limit, const QImage &image, double opacity,
                doc::BlendMode blend)
{
    const QRect r = QRect(at, image.size()) & limit & target.rect();
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto *dst = reinterpret_cast<doc::Pixel *>(target.scanLine(y)) + r.left();
        const auto *src = reinterpret_cast<const doc::Pixel *>(image.constScanLine(y - at.y())) + (r.left() - at.x());
        doc::blendSpan(dst, src, r.width(), blend, float(opacity));
    }
}

// Draws the layer (its tiles among `tiles`, or its grid) with its opacity
// and blend mode. The painter paints on `target`, whose pixel (0, 0) is the
// document's `origin`; only `limit` (in target pixels) may change.
void drawLayer(QPainter &p, QImage &target, const QPoint &origin, const QRect &limit, const doc::Layer &layer,
               const std::vector<doc::TileCoord> &tiles, double opacity, doc::BlendMode blend)
{
    if (layer.kind == doc::LayerKind::Raster && doc::needsSoftwareBlend(blend)) {
        blendLayer(target, origin, limit, layer, tiles, opacity, blend);
        return;
    }
    p.setOpacity(opacity);
    p.setCompositionMode(compositionFor(blend));
    if (layer.kind == doc::LayerKind::Perspective) {
        paintPerspective(p, layer.properties.perspective);
        return;
    }
    for (const doc::TileCoord &c : tiles)
        if (const doc::TilePtr tile = layer.image.tile(c))
            p.drawImage(tileRect(c).topLeft(), layerTile(layer, c, *tile));
}

double lineWidth(const perspective::Grid &grid)
{
    return std::max(1.0, grid.frameWidth / 960.0); // 2 px on an HD sheet
}

// What the painter can still draw on, in its own coordinates: its clip, or
// else its whole device.
QRectF paintable(const QPainter &p)
{
    QRectF area;
    if (const QPaintDevice *device = p.device())
        area = p.transform().inverted().mapRect(QRectF(0, 0, device->width(), device->height()));
    if (p.hasClipping())
        area = area.isNull() ? p.clipBoundingRect() : area & p.clipBoundingRect();
    return area;
}

// The part of a→b inside `r` (Liang–Barsky), as the fractions [t0, t1] of
// it; nothing if it misses.
std::optional<std::pair<double, double>> clipped(const QPointF &a, const QPointF &b, const QRectF &r)
{
    double t0 = 0.0, t1 = 1.0;
    const QPointF d = b - a;
    const double p[4] = {-d.x(), d.x(), -d.y(), d.y()};
    const double q[4] = {a.x() - r.left(), r.right() - a.x(), a.y() - r.top(), r.bottom() - a.y()};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0)
                return std::nullopt;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0)
            t0 = std::max(t0, t);
        else
            t1 = std::min(t1, t);
        if (t0 > t1)
            return std::nullopt;
    }
    return std::pair{t0, t1};
}

} // namespace

void paintPerspective(QPainter &p, const perspective::Grid &grid)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor color = QColor::fromRgba(grid.color);
    const double width = lineWidth(grid);
    // Only the lines' parts that can show are drawn: an update of a few
    // pixels does not stroke the whole grid, nor dash the whole horizon.
    const QRectF area = paintable(p).adjusted(-4.0 * width, -4.0 * width, 4.0 * width, 4.0 * width);
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap));
    for (const perspective::Segment &s : perspective::gridSegments(grid)) {
        const QPointF a(s.a.x, s.a.y), b(s.b.x, s.b.y);
        if (const auto t = clipped(a, b, area))
            p.drawLine(a + (b - a) * t->first, a + (b - a) * t->second);
    }
    if (grid.horizon && grid.fisheye > 0.0) {
        // A curve: one path, so the dashes run on along it.
        QPainterPath path;
        std::optional<QPointF> end;
        for (const perspective::Segment &s : perspective::horizonSegments(grid, grid.frameWidth * 4.0)) {
            const QPointF a(s.a.x, s.a.y), b(s.b.x, s.b.y);
            if (!clipped(a, b, area)) {
                end.reset();
                continue;
            }
            if (!end || *end != a)
                path.moveTo(a);
            path.lineTo(b);
            end = b;
        }
        p.setPen(QPen(color, width * 1.5, Qt::DashLine, Qt::FlatCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    } else if (grid.horizon)
        if (const auto h = perspective::horizonLine(grid, grid.frameWidth * 4.0)) {
            const QPointF a(h->a.x, h->a.y), b(h->b.x, h->b.y);
            if (const auto t = clipped(a, b, area)) {
                QPen pen(color, width * 1.5, Qt::DashLine, Qt::FlatCap);
                // The dashes where they would be on the whole line (offset in pen widths).
                pen.setDashOffset(t->first * QLineF(a, b).length() / pen.widthF());
                p.setPen(pen);
                p.drawLine(a + (b - a) * t->first, a + (b - a) * t->second);
            }
        }
    p.restore();
}

QRect drawingBounds(const doc::Drawing &drawing, bool guides)
{
    QRect bounds;
    for (const doc::Layer &layer : drawing.layers) {
        if (!shows(layer, guides))
            continue;
        if (layer.kind == doc::LayerKind::Perspective) {
            // Its lines, within reason around the plane (the horizon has no end).
            const perspective::Grid &g = layer.properties.perspective;
            QRectF lines;
            for (const perspective::Segment &s : perspective::gridSegments(g))
                lines |= QRectF(QPointF(s.a.x, s.a.y), QPointF(s.b.x, s.b.y)).normalized();
            if (g.horizon)
                for (const perspective::Segment &h : perspective::horizonSegments(g, g.frameWidth * 4.0))
                    lines |= QRectF(QPointF(h.a.x, h.a.y), QPointF(h.b.x, h.b.y)).normalized();
            const double reach = g.frameWidth * 4.0, pad = lineWidth(g) * 2.0;
            const QRectF limit(g.originX - reach, g.originY - reach, 2.0 * reach, 2.0 * reach);
            bounds |= lines.adjusted(-pad, -pad, pad, pad).intersected(limit).toAlignedRect();
            continue;
        }
        for (const doc::TileCoord &c : layer.image.tileCoords())
            bounds |= tileRect(c);
    }
    return bounds;
}

QImage composeDrawing(const doc::Drawing &drawing, const QRect &area, bool guides)
{
    QImage image(area.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (area.isEmpty())
        return image;
    QPainter p(&image);
    p.translate(-area.topLeft());
    const std::vector<doc::TileCoord> tiles = tilesIn(area);
    for (const doc::Layer &layer : drawing.layers)
        if (shows(layer, guides))
            drawLayer(p, image, area.topLeft(), image.rect(), layer, tiles, layer.properties.opacity,
                      layer.properties.blend);
    return image;
}

void FrameRenderer::renderAll(const doc::Document &doc, int frame)
{
    const QSize size(doc.width(), doc.height());
    if (m_image.size() != size)
        m_image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    render(doc, frame, m_image.rect());
}

void FrameRenderer::render(const doc::Document &doc, int frame, const QRect &rect)
{
    const QRect area = rect & m_image.rect();
    if (area.isEmpty())
        return;

    QPainter p(&m_image);
    p.setClipRect(area);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(area, m_paper ? QColor::fromRgba(doc.paperColor()) : QColor(Qt::transparent));

    const std::vector<doc::TileCoord> tiles = tilesIn(area);
    for (int i = 0; i < doc.celCount(); ++i) {
        const doc::Cel &cel = doc.celAt(i);
        const doc::CelProperties &props = cel.properties();
        const doc::Drawing *drawing = cel.drawingAt(frame);
        if (!props.visible || props.opacity <= 0.0f || !drawing)
            continue;
        const doc::Layer *only = nullptr; // the one layer shown, if there is just one
        int shown = 0;
        for (const doc::Layer &layer : drawing->layers)
            if (shows(layer, m_guides)) {
                only = &layer;
                ++shown;
            }
        if (shown == 0)
            continue;
        if (shown == 1 && only->properties.blend == doc::BlendMode::Normal) {
            // A lone normal layer over transparency is itself: no folder needed.
            drawLayer(p, m_image, QPoint(0, 0), area, *only, tiles, props.opacity * only->properties.opacity,
                      props.blend);
            continue;
        }
        if (doc::needsSoftwareBlend(props.blend)) {
            blendImage(m_image, area.topLeft(), area, composeDrawing(*drawing, area, m_guides), props.opacity,
                       props.blend);
            continue;
        }
        p.setOpacity(props.opacity);
        p.setCompositionMode(compositionFor(props.blend));
        p.drawImage(area.topLeft(), composeDrawing(*drawing, area, m_guides));
    }
}

} // namespace mothion::canvas
