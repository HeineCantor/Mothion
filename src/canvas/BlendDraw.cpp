#include "canvas/BlendDraw.h"

#include "core/document/Blend.h"

#include <algorithm>

namespace mothion::canvas {

QPainter::CompositionMode compositionFor(doc::BlendMode blend)
{
    switch (blend) {
    case doc::BlendMode::Normal: return QPainter::CompositionMode_SourceOver;
    case doc::BlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case doc::BlendMode::Screen: return QPainter::CompositionMode_Screen;
    case doc::BlendMode::Overlay: return QPainter::CompositionMode_Overlay;
    case doc::BlendMode::Add: return QPainter::CompositionMode_Plus;
    case doc::BlendMode::Darken: return QPainter::CompositionMode_Darken;
    case doc::BlendMode::Lighten: return QPainter::CompositionMode_Lighten;
    case doc::BlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
    case doc::BlendMode::ColorBurn: return QPainter::CompositionMode_ColorBurn;
    case doc::BlendMode::HardLight: return QPainter::CompositionMode_HardLight;
    case doc::BlendMode::SoftLight: return QPainter::CompositionMode_SoftLight;
    case doc::BlendMode::Difference: return QPainter::CompositionMode_Difference;
    case doc::BlendMode::Exclusion: return QPainter::CompositionMode_Exclusion;
    default: break; // blended by hand: see needsSoftwareBlend
    }
    return QPainter::CompositionMode_SourceOver;
}

void blendImageOver(QImage &target, const QImage &image, double opacity, doc::BlendMode blend)
{
    if (image.isNull() || target.isNull())
        return;
    const QImage source = image.format() == QImage::Format_ARGB32_Premultiplied
                              ? image
                              : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (doc::needsSoftwareBlend(blend)) {
        const int w = std::min(target.width(), source.width());
        const int h = std::min(target.height(), source.height());
        for (int y = 0; y < h; ++y)
            doc::blendSpan(reinterpret_cast<doc::Pixel *>(target.scanLine(y)),
                           reinterpret_cast<const doc::Pixel *>(source.constScanLine(y)), w, blend, float(opacity));
        return;
    }
    QPainter p(&target);
    p.setOpacity(opacity);
    p.setCompositionMode(compositionFor(blend));
    p.drawImage(0, 0, source);
}

} // namespace mothion::canvas
