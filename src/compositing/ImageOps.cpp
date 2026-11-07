#include "compositing/ImageOps.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

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

QImage directionalBlur(const QImage &source, double length, double angleDegrees)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int steps = std::min(160, int(std::lround(length)));
    if (steps < 1 || image.isNull())
        return image;
    const int w = image.width(), h = image.height();
    const double angle = angleDegrees * 3.14159265358979323846 / 180.0;
    const double dx = std::cos(angle) * length / (steps + 1), dy = std::sin(angle) * length / (steps + 1);
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int a = 0, r = 0, g = 0, b = 0;
            for (int k = -steps / 2; k <= steps - steps / 2; ++k) { // the line, centered on the pixel
                const int sx = std::clamp(int(std::lround(x + dx * k)), 0, w - 1);
                const int sy = std::clamp(int(std::lround(y + dy * k)), 0, h - 1);
                const QRgb c = reinterpret_cast<const QRgb *>(image.constScanLine(sy))[sx];
                a += qAlpha(c); r += qRed(c); g += qGreen(c); b += qBlue(c);
            }
            const int n = steps + 1;
            out[x] = qRgba((r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n, (a + n / 2) / n);
        }
    });
    return result;
}

QImage mapChannels(const QImage &source, const std::array<std::uint8_t, 256> &table)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = image.width(), h = image.height();
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = qAlpha(in[x]);
            if (a == 0) {
                out[x] = 0;
                continue;
            }
            const int r = std::min(255, qRed(in[x]) * 255 / a);
            const int g = std::min(255, qGreen(in[x]) * 255 / a);
            const int b = std::min(255, qBlue(in[x]) * 255 / a);
            out[x] = qRgba((table[std::size_t(r)] * a + 127) / 255, (table[std::size_t(g)] * a + 127) / 255,
                           (table[std::size_t(b)] * a + 127) / 255, a);
        }
    });
    return result;
}

} // namespace mothion::compositing
