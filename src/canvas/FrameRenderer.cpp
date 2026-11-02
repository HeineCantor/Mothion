#include "canvas/FrameRenderer.h"

#include "canvas/TileQt.h"

#include <QPainter>

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

bool shows(const doc::Layer &layer)
{
    return layer.properties.visible && layer.properties.opacity > 0.0f && !layer.image.isEmpty();
}

// Draws the layer's tiles among `tiles` with its opacity and blend mode.
void drawLayer(QPainter &p, const doc::Layer &layer, const std::vector<doc::TileCoord> &tiles, double opacity,
               doc::BlendMode blend)
{
    p.setOpacity(opacity);
    p.setCompositionMode(compositionFor(blend));
    for (const doc::TileCoord &c : tiles)
        if (const doc::TilePtr tile = layer.image.tile(c))
            p.drawImage(tileRect(c).topLeft(), tileImage(*tile));
}

} // namespace

QRect drawingBounds(const doc::Drawing &drawing)
{
    QRect bounds;
    for (const doc::Layer &layer : drawing.layers)
        if (shows(layer))
            for (const doc::TileCoord &c : layer.image.tileCoords())
                bounds |= tileRect(c);
    return bounds;
}

QImage composeDrawing(const doc::Drawing &drawing, const QRect &area)
{
    QImage image(area.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (area.isEmpty())
        return image;
    QPainter p(&image);
    p.translate(-area.topLeft());
    const std::vector<doc::TileCoord> tiles = tilesIn(area);
    for (const doc::Layer &layer : drawing.layers)
        if (shows(layer))
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
            if (shows(layer)) {
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
        p.drawImage(area.topLeft(), composeDrawing(*drawing, area));
    }
}

} // namespace mothion::canvas
