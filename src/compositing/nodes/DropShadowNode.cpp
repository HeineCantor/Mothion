#include "compositing/nodes/DropShadowNode.h"

#include "compositing/ImageOps.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

QImage DropShadowNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int opacity = std::clamp(params.value(QStringLiteral("opacity")).toInt(), 0, 100);
    if (opacity == 0)
        return source;
    const QColor color = params.value(QStringLiteral("color")).value<QColor>();
    const int dx = int(std::lround(params.value(QStringLiteral("x")).toInt() * context.scale));
    const int dy = int(std::lround(params.value(QStringLiteral("y")).toInt() * context.scale));
    const int radius = int(std::lround(params.value(QStringLiteral("blur")).toInt() * context.scale));

    // The shape in the color: each pixel is the color as much as the picture is opaque there, times the opacity.
    QImage shadow(source.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(shadow.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const int a = qAlpha(in[x]) * opacity / 100;
            out[x] = qRgba(color.red() * a / 255, color.green() * a / 255, color.blue() * a / 255, a);
        }
    }
    shadow = boxBlur(shadow, radius);

    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.drawImage(dx, dy, shadow);
    p.drawImage(0, 0, source);
    return result;
}

} // namespace mothion::compositing
