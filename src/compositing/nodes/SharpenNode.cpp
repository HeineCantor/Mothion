#include "compositing/nodes/SharpenNode.h"

#include "compositing/ImageOps.h"
#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

QImage SharpenNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const float amount = float(params.value(QStringLiteral("amount")).toInt()) / 100.0f;
    const int radius = std::max(1, int(std::lround(params.value(QStringLiteral("radius")).toInt() * context.scale)));
    if (amount <= 0.0f)
        return source;
    const QImage blurred = boxBlur(source, radius);
    const int w = source.width(), h = source.height();
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        const QRgb *soft = reinterpret_cast<const QRgb *>(blurred.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = qAlpha(in[x]);
            // The detail (what the picture has over its blur) added back, kept between nothing and its alpha: the
            // picture is premultiplied, so a color may not pass its alpha.
            auto channel = [&](int c, int s) { return std::clamp(int(std::lround(float(c) + amount * float(c - s))), 0, a); };
            out[x] = qRgba(channel(qRed(in[x]), qRed(soft[x])), channel(qGreen(in[x]), qGreen(soft[x])),
                           channel(qBlue(in[x]), qBlue(soft[x])), a);
        }
    });
    return result;
}

} // namespace mothion::compositing
