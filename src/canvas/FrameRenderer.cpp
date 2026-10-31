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

} // namespace

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
    p.fillRect(area, QColor::fromRgba(doc.paperColor()));

    const std::vector<doc::TileCoord> tiles = tilesIn(area);
    for (int i = 0; i < doc.layerCount(); ++i) {
        const doc::Layer &layer = doc.layerAt(i);
        const doc::LayerProperties &props = layer.properties();
        const doc::Drawing *drawing = layer.drawingAt(frame);
        if (!props.visible || props.opacity <= 0.0f || !drawing)
            continue;
        p.setOpacity(props.opacity);
        p.setCompositionMode(compositionFor(props.blend));
        for (const doc::TileCoord &c : tiles)
            if (const doc::TilePtr tile = drawing->image.tile(c))
                p.drawImage(tileRect(c).topLeft(), tileImage(*tile));
    }
}

} // namespace mothion::canvas
