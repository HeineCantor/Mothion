#include "compositing/nodes/ChromaticAberrationNode.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

namespace {

// The pixel at (x, y) of a premultiplied image, by bilinear interpolation of its four neighbours; the edge
// pixels go on outside the image. Returns the four channels as floats (a, r, g, b).
struct Sample
{
    float a, r, g, b;
};

Sample bilinear(const QImage &image, float x, float y)
{
    const int w = image.width(), h = image.height();
    x = std::clamp(x, 0.0f, float(w - 1));
    y = std::clamp(y, 0.0f, float(h - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    const QRgb *top = reinterpret_cast<const QRgb *>(image.constScanLine(y0));
    const QRgb *bottom = reinterpret_cast<const QRgb *>(image.constScanLine(y1));
    const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
    auto mix = [&](int (*channel)(QRgb)) {
        return w00 * float(channel(top[x0])) + w10 * float(channel(top[x1])) + w01 * float(channel(bottom[x0]))
               + w11 * float(channel(bottom[x1]));
    };
    return {mix(qAlpha), mix(qRed), mix(qGreen), mix(qBlue)};
}

} // namespace

QImage ChromaticAberrationNode::process(const QList<QImage> &inputs, const QVariantMap &params,
                                        const EvalContext &context) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const bool radial = params.value(QStringLiteral("mode")).toInt() == 0;
    const float amount = float(params.value(QStringLiteral("amount")).toInt() * context.scale);
    const float angle = float(params.value(QStringLiteral("angle")).toInt() * 3.14159265358979323846 / 180.0);
    if (amount <= 0.0f)
        return source;

    const int w = source.width(), h = source.height();
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    const float cx = float(w - 1) / 2, cy = float(h - 1) / 2;
    const float reach = std::max(1.0f, std::hypot(cx, cy)); // center to corner
    const float lx = amount * std::cos(angle), ly = amount * std::sin(angle);

    parallelFor(h, [&](int y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // The shift of the red channel; the blue one goes the other way.
            const float dx = radial ? amount * (float(x) - cx) / reach : lx;
            const float dy = radial ? amount * (float(y) - cy) / reach : ly;
            const Sample red = bilinear(source, float(x) + dx, float(y) + dy);
            const Sample blue = bilinear(source, float(x) - dx, float(y) - dy);
            const QRgb here = row[x];
            // The alpha is the largest of the three, so no channel is brighter than the alpha it sits in.
            const int alpha = std::max({int(std::lround(red.a)), qAlpha(here), int(std::lround(blue.a))});
            out[x] = qRgba(std::min(alpha, int(std::lround(red.r))), qGreen(here),
                           std::min(alpha, int(std::lround(blue.b))), alpha);
        }
    });
    return result;
}

} // namespace mothion::compositing
