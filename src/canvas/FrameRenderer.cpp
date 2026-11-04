#include "canvas/FrameRenderer.h"

#include "canvas/TileQt.h"

#include <QPainter>

#include <algorithm>

namespace mothion::canvas {

namespace {

QPainter::CompositionMode compositionFor(doc::BlendMode blend)
{
    switch (blend) {
    case doc::BlendMode::Normal: return QPainter::CompositionMode_SourceOver;
    case doc::BlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case doc::BlendMode::Screen: return QPainter::CompositionMode_Screen;
    case doc::BlendMode::Overlay: return QPainter::CompositionMode_Overlay;
    case doc::BlendMode::Add: return QPainter::CompositionMode_Plus;
    }
    return QPainter::CompositionMode_SourceOver;
}

bool shows(const doc::Layer &layer, bool guides)
{
    if (!layer.properties.visible || layer.properties.opacity <= 0.0f)
        return false;
    return layer.kind == doc::LayerKind::Perspective ? guides : !layer.image.isEmpty();
}

// Draws the layer (its tiles among `tiles`, or its grid) with its opacity
// and blend mode.
void drawLayer(QPainter &p, const doc::Layer &layer, const std::vector<doc::TileCoord> &tiles, double opacity,
               doc::BlendMode blend)
{
    p.setOpacity(opacity);
    p.setCompositionMode(compositionFor(blend));
    if (layer.kind == doc::LayerKind::Perspective) {
        paintPerspective(p, layer.properties.perspective);
        return;
    }
    for (const doc::TileCoord &c : tiles)
        if (const doc::TilePtr tile = layer.image.tile(c))
            p.drawImage(tileRect(c).topLeft(), tileImage(*tile));
}

double lineWidth(const perspective::Grid &grid)
{
    return std::max(1.0, grid.frameWidth / 960.0); // 2 px on an HD sheet
}

} // namespace

void paintPerspective(QPainter &p, const perspective::Grid &grid)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor color = QColor::fromRgba(grid.color);
    const double width = lineWidth(grid);
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap));
    for (const perspective::Segment &s : perspective::gridSegments(grid))
        p.drawLine(QPointF(s.a.x, s.a.y), QPointF(s.b.x, s.b.y));
    if (grid.horizon)
        if (const auto h = perspective::horizonLine(grid, grid.frameWidth * 4.0)) {
            p.setPen(QPen(color, width * 1.5, Qt::DashLine, Qt::FlatCap));
            p.drawLine(QPointF(h->a.x, h->a.y), QPointF(h->b.x, h->b.y));
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
                if (const auto h = perspective::horizonLine(g, g.frameWidth * 4.0))
                    lines |= QRectF(QPointF(h->a.x, h->a.y), QPointF(h->b.x, h->b.y)).normalized();
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
            drawLayer(p, layer, tiles, layer.properties.opacity, layer.properties.blend);
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
            drawLayer(p, *only, tiles, props.opacity * only->properties.opacity, props.blend);
            continue;
        }
        p.setOpacity(props.opacity);
        p.setCompositionMode(compositionFor(props.blend));
        p.drawImage(area.topLeft(), composeDrawing(*drawing, area, m_guides));
    }
}

} // namespace mothion::canvas
