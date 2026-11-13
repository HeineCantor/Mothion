#include "compositing/nodes/VignetteNode.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

QImage VignetteNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const float amount = float(params.value(QStringLiteral("amount")).toInt()) / 100.0f;
    if (amount <= 0.0f)
        return source;
    const float inner = float(params.value(QStringLiteral("size")).toInt()) / 100.0f;
    const float outer = std::min(1.2f, inner + 0.05f + float(params.value(QStringLiteral("softness")).toInt()) / 100.0f * 0.9f);
    const QColor tint = params.value(QStringLiteral("tint")).value<QColor>();
    const int w = source.width(), h = source.height();
    const float cx = float(w - 1) / 2.0f, cy = float(h - 1) / 2.0f;
    const float reach = std::max(1.0f, std::hypot(cx, cy)); // center to corner

    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float d = std::hypot(float(x) - cx, float(y) - cy) / reach; // 0 at the center, 1 at the corners
            float t = std::clamp((d - inner) / (outer - inner), 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t) * amount; // smooth, and as much as asked
            const int a = qAlpha(in[x]);
            // Toward the color, as much as the pixel's alpha (a premultiplied pixel holds its color times it).
            auto mix = [&](int c, int target) { return int(std::lround(float(c) + (float(target * a) / 255.0f - float(c)) * t)); };
            out[x] = qRgba(mix(qRed(in[x]), tint.red()), mix(qGreen(in[x]), tint.green()),
                           mix(qBlue(in[x]), tint.blue()), a);
        }
    });
    return result;
}

} // namespace mothion::compositing
