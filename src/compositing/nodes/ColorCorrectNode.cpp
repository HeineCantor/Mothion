#include "compositing/nodes/ColorCorrectNode.h"

#include "core/Parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mothion::compositing {

QImage ColorCorrectNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int brightness = params.value(QStringLiteral("brightness")).toInt();
    const int contrast = params.value(QStringLiteral("contrast")).toInt();
    const int gammaPercent = params.value(QStringLiteral("gamma")).toInt();
    const int hue = params.value(QStringLiteral("hue")).toInt();
    const int saturation = params.value(QStringLiteral("saturation")).toInt();
    if (!brightness && !contrast && gammaPercent == 100 && !hue && !saturation)
        return source;

    // Hue and saturation together, as a matrix on the red, green and blue (the luminance weights are the usual ones).
    const double angle = hue * 3.14159265358979323846 / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    const double sat = 1.0 + saturation / 100.0;
    const double hueRotation[3][3] = {
        {0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715, 0.072 - c * 0.072 + s * 0.928},
        {0.213 - c * 0.213 + s * 0.143, 0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283},
        {0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715, 0.072 + c * 0.928 + s * 0.072}};
    const double saturate[3][3] = {{0.213 + 0.787 * sat, 0.715 - 0.715 * sat, 0.072 - 0.072 * sat},
                                   {0.213 - 0.213 * sat, 0.715 + 0.285 * sat, 0.072 - 0.072 * sat},
                                   {0.213 - 0.213 * sat, 0.715 - 0.715 * sat, 0.072 + 0.928 * sat}};
    float matrix[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            matrix[i][j] = float(saturate[i][0] * hueRotation[0][j] + saturate[i][1] * hueRotation[1][j] +
                                 saturate[i][2] * hueRotation[2][j]);
    const bool colors = hue || saturation;

    // Contrast around the middle gray, then brightness, then gamma: one table for the three.
    const double contrastFactor = 1.0 + contrast / 100.0; // 0 (flat) to 2
    const double gamma = std::max(0.1, gammaPercent / 100.0);
    std::array<std::uint8_t, 256> table{};
    for (int v = 0; v < 256; ++v) {
        double x = (v / 255.0 - 0.5) * contrastFactor + 0.5 + brightness / 100.0;
        x = std::pow(std::clamp(x, 0.0, 1.0), 1.0 / gamma);
        table[std::size_t(v)] = std::uint8_t(std::lround(std::clamp(x, 0.0, 1.0) * 255.0));
    }

    const int w = source.width(), h = source.height();
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = qAlpha(in[x]);
            if (a == 0) {
                out[x] = 0;
                continue;
            }
            // The color as it is without the alpha (the picture holds it multiplied by it).
            float r = std::min(255.0f, qRed(in[x]) * 255.0f / float(a));
            float g = std::min(255.0f, qGreen(in[x]) * 255.0f / float(a));
            float b = std::min(255.0f, qBlue(in[x]) * 255.0f / float(a));
            if (colors) {
                const float nr = matrix[0][0] * r + matrix[0][1] * g + matrix[0][2] * b;
                const float ng = matrix[1][0] * r + matrix[1][1] * g + matrix[1][2] * b;
                const float nb = matrix[2][0] * r + matrix[2][1] * g + matrix[2][2] * b;
                r = std::clamp(nr, 0.0f, 255.0f);
                g = std::clamp(ng, 0.0f, 255.0f);
                b = std::clamp(nb, 0.0f, 255.0f);
            }
            const int tr = table[std::size_t(std::lround(r))];
            const int tg = table[std::size_t(std::lround(g))];
            const int tb = table[std::size_t(std::lround(b))];
            out[x] = qRgba((tr * a + 127) / 255, (tg * a + 127) / 255, (tb * a + 127) / 255, a);
        }
    });
    return result;
}

} // namespace mothion::compositing
