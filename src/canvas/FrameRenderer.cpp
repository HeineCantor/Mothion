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

// The layer a clipped layer is clipped to: the first raster layer below it that is not clipped itself. -1 if the layer is
// not clipped or there is none (it then shows as any other).
int clipBase(const doc::Drawing &drawing, std::size_t index)
{
    if (!drawing.layers[index].properties.clipped)
        return -1;
    for (std::size_t below = index; below-- > 0;) {
        const doc::Layer &layer = drawing.layers[below];
        if (layer.kind != doc::LayerKind::Perspective && !layer.properties.clipped)
            return int(below);
    }
    return -1;
}

// The layer as it shows over the tiles (its pixels through its mask), alone on transparency, in `bounds`.
QImage layerImage(const doc::Layer &layer, const QRect &bounds, const std::vector<doc::TileCoord> &tiles)
{
    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.translate(-bounds.topLeft());
    for (const doc::TileCoord &c : tiles)
        if (const doc::TilePtr tile = layer.image.tile(c))
            p.drawImage(tileRect(c).topLeft(), layerTile(layer, c, *tile));
    return image;
}

// Draws the layer (its tiles among `tiles`, or its grid) with its opacity and blend mode (clipped to `clipTo` when
// there is one). The painter paints on `target`, whose pixel (0, 0) is the
// document's `origin`; only `limit` (in target pixels) may change.
void drawLayer(QPainter &p, QImage &target, const QPoint &origin, const QRect &limit, const doc::Layer &layer,
               const std::vector<doc::TileCoord> &tiles, double opacity, doc::BlendMode blend,
               const doc::Layer *clipTo = nullptr)
{
    if (clipTo && layer.kind != doc::LayerKind::Perspective) {
        // Clipped: what the layer has over the pixels of the layer it is clipped to (their opacity), nothing else; then
        // that goes over the others with the layer's own opacity and blend mode.
        QRect bounds;
        for (const doc::TileCoord &c : tiles)
            bounds |= tileRect(c);
        if (bounds.isEmpty())
            return;
        QImage clipped = layerImage(layer, bounds, tiles);
        const QImage mask = layerImage(*clipTo, bounds, tiles);
        QPainter cp(&clipped);
        cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        cp.drawImage(0, 0, mask);
        cp.end();
        if (doc::needsSoftwareBlend(blend)) {
            blendImage(target, bounds.topLeft() - origin, limit, clipped, opacity, blend);
        } else {
            p.setOpacity(opacity);
            p.setCompositionMode(compositionFor(blend));
            p.drawImage(bounds.topLeft(), clipped);
        }
        return;
    }
    if (layer.kind != doc::LayerKind::Perspective && doc::needsSoftwareBlend(blend)) {
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
    for (std::size_t i = 0; i < drawing.layers.size(); ++i) {
        const doc::Layer &layer = drawing.layers[i];
        if (!shows(layer, guides))
            continue;
        const doc::Layer *base = nullptr; // what it is clipped to
        if (const int b = clipBase(drawing, i); b >= 0) {
            base = &drawing.layers[std::size_t(b)];
            if (!base->properties.visible)
                continue; // a hidden layer hides what is clipped to it
        }
        drawLayer(p, image, area.topLeft(), image.rect(), layer, tiles, layer.properties.opacity, layer.properties.blend,
                  base);
    }
    return image;
}

QRect visibleBounds(const doc::Drawing &drawing, bool guides)
{
    const QRect tiles = drawingBounds(drawing, guides);
    if (tiles.isEmpty())
        return {};
    const QImage image = composeDrawing(drawing, tiles, guides);
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom)).translated(tiles.topLeft());
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
        if (m_only != doc::kNoId && cel.id() != m_only)
            continue;
        if (cel.properties().trajectory)
            continue; // not drawn: it only moves what follows it
        const doc::CelProperties &props = cel.properties();
        const doc::Drawing *drawing = cel.drawingAt(frame);
        // What the animation does to the cel on this frame (nothing for a cel with no keys, or one shown as drawn).
        const bool animated = !cel.motion().isEmpty() && cel.id() != m_asDrawn;
        const doc::CelPose pose = animated ? cel.motion().poseAt(frame) : doc::CelPose{};
        // On top of its own pose, what the trajectory it follows does (shown as drawn, it is not moved by it either).
        const doc::Matrix2x3 follow = cel.id() != m_asDrawn ? doc.followMatrix(cel, frame) : doc::Matrix2x3{};
        const float opacity = props.opacity * float(pose.opacity);
        if (!props.visible || opacity <= 0.0f || !drawing)
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
        if ((animated && !pose.movesNothing()) || !follow.isIdentity()) {
            // Moved as one piece: what its layers show together, read through the pose into this area.
            const doc::Matrix2x3 m = pose.matrix(cel.motion().pivotX, cel.motion().pivotY).then(follow);
            const QTransform forward(m.a, m.b, m.c, m.d, m.e, m.f);
            bool invertible = false;
            const QTransform back = forward.inverted(&invertible);
            if (!invertible)
                continue;
            const QRect source = back.mapRect(QRectF(area)).toAlignedRect().adjusted(-3, -3, 3, 3)
                                 & drawingBounds(*drawing, m_guides);
            if (source.isEmpty())
                continue;
            const QImage content = composeDrawing(*drawing, source, m_guides);
            QImage moved;
            const QTransform toArea = forward * QTransform::fromTranslate(-area.x(), -area.y());
            if (m_filter == ResampleFilter::Bilinear || m_filter == ResampleFilter::Nearest) {
                moved = QImage(area.size(), QImage::Format_ARGB32_Premultiplied);
                moved.fill(Qt::transparent);
                QPainter mp(&moved);
                mp.setRenderHint(QPainter::SmoothPixmapTransform, m_filter != ResampleFilter::Nearest);
                mp.setTransform(toArea);
                mp.drawImage(source.topLeft(), content);
            } else {
                moved = resampleTransformed(content, QTransform::fromTranslate(source.x(), source.y()) * toArea,
                                            area.size(), m_filter);
            }
            if (doc::needsSoftwareBlend(props.blend)) {
                blendImage(m_image, area.topLeft(), area, moved, opacity, props.blend);
            } else {
                p.setOpacity(opacity);
                p.setCompositionMode(compositionFor(props.blend));
                p.drawImage(area.topLeft(), moved);
            }
            continue;
        }
        if (shown == 1 && only->properties.blend == doc::BlendMode::Normal && !only->properties.clipped) {
            // A lone normal layer over transparency is itself: no folder needed.
            drawLayer(p, m_image, QPoint(0, 0), area, *only, tiles, opacity * only->properties.opacity, props.blend);
            continue;
        }
        if (doc::needsSoftwareBlend(props.blend)) {
            blendImage(m_image, area.topLeft(), area, composeDrawing(*drawing, area, m_guides), opacity, props.blend);
            continue;
        }
        p.setOpacity(opacity);
        p.setCompositionMode(compositionFor(props.blend));
        p.drawImage(area.topLeft(), composeDrawing(*drawing, area, m_guides));
    }
}

} // namespace mothion::canvas
