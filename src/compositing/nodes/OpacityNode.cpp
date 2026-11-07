#include "compositing/nodes/OpacityNode.h"

#include "core/Parallel.h"

#include <algorithm>

namespace mothion::compositing {

QImage OpacityNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int opacity = std::clamp(params.value(QStringLiteral("opacity")).toInt(), 0, 100);
    const bool invert = params.value(QStringLiteral("invert")).toInt() != 0;
    if (opacity == 100 && !invert)
        return source;

    const int w = source.width(), h = source.height();
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = qAlpha(in[x]);
            int r = qRed(in[x]), g = qGreen(in[x]), b = qBlue(in[x]);
            int alpha = a;
            if (invert) { // the color without the old alpha, under the new one
                const int na = 255 - a;
                r = a ? std::min(255, r * 255 / a) * na / 255 : 0;
                g = a ? std::min(255, g * 255 / a) * na / 255 : 0;
                b = a ? std::min(255, b * 255 / a) * na / 255 : 0;
                alpha = na;
            }
            // All four multiplied: the picture holds its color already multiplied by its alpha.
            out[x] = qRgba((r * opacity + 50) / 100, (g * opacity + 50) / 100, (b * opacity + 50) / 100,
                           (alpha * opacity + 50) / 100);
        }
    });
    return result;
}

} // namespace mothion::compositing
