#include "compositing/nodes/BlurNode.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

namespace {

// Two passes of running sums, one along the rows and one down the columns.
QImage boxBlur(const QImage &source, int radius)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (radius < 1 || image.isNull())
        return image;
    const int w = image.width(), h = image.height();
    QImage temp(w, h, image.format());
    const int window = radius * 2 + 1;
    auto pass = [&](const QImage &in, QImage &out, bool horizontal) {
        const int lines = horizontal ? h : w;
        const int length = horizontal ? w : h;
        for (int line = 0; line < lines; ++line) {
            auto at = [&](int i) {
                i = std::clamp(i, 0, length - 1);
                return horizontal ? reinterpret_cast<const QRgb *>(in.constScanLine(line))[i]
                                  : reinterpret_cast<const QRgb *>(in.constScanLine(i))[line];
            };
            int sum[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const QRgb c = at(i);
                sum[0] += qAlpha(c); sum[1] += qRed(c); sum[2] += qGreen(c); sum[3] += qBlue(c);
            }
            for (int i = 0; i < length; ++i) {
                const QRgb px = qRgba(sum[1] / window, sum[2] / window, sum[3] / window, sum[0] / window);
                if (horizontal)
                    reinterpret_cast<QRgb *>(out.scanLine(line))[i] = px;
                else
                    reinterpret_cast<QRgb *>(out.scanLine(i))[line] = px;
                const QRgb add = at(i + radius + 1), drop = at(i - radius);
                sum[0] += qAlpha(add) - qAlpha(drop); sum[1] += qRed(add) - qRed(drop);
                sum[2] += qGreen(add) - qGreen(drop); sum[3] += qBlue(add) - qBlue(drop);
            }
        }
    };
    pass(image, temp, true);
    pass(temp, image, false);
    return image;
}

} // namespace

QImage BlurNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    const QImage &image = inputs.value(0);
    const int radius = static_cast<int>(std::lround(params.value(QStringLiteral("radius")).toInt() * context.scale));
    return image.isNull() ? QImage() : boxBlur(image, radius);
}

} // namespace mothion::compositing
