#include "canvas/Resample.h"

#include "core/Parallel.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <vector>

namespace mothion::canvas {

namespace {

constexpr float kPi = 3.14159265358979f;

// The weight of a pixel `t` pixels (of the kernel's own size) away from the point read.
float weight(ResampleFilter filter, float t)
{
    t = std::abs(t);
    switch (filter) {
    case ResampleFilter::Nearest:
        return t < 0.5f ? 1.0f : 0.0f;
    case ResampleFilter::Bilinear:
        return t < 1.0f ? 1.0f - t : 0.0f;
    case ResampleFilter::Bicubic: // Catmull-Rom: passes through the pixels
        if (t < 1.0f)
            return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
        return t < 2.0f ? -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f : 0.0f;
    case ResampleFilter::Lanczos3:
        if (t < 1e-6f)
            return 1.0f;
        return t < 3.0f ? 3.0f * std::sin(kPi * t) * std::sin(kPi * t / 3.0f) / (kPi * kPi * t * t) : 0.0f;
    }
    return 0.0f;
}

float radius(ResampleFilter filter)
{
    switch (filter) {
    case ResampleFilter::Nearest: return 0.5f;
    case ResampleFilter::Bilinear: return 1.0f;
    case ResampleFilter::Bicubic: return 2.0f;
    case ResampleFilter::Lanczos3: return 3.0f;
    }
    return 1.0f;
}

} // namespace

QString filterName(ResampleFilter filter)
{
    switch (filter) {
    case ResampleFilter::Nearest: return QObject::tr("Nearest neighbor");
    case ResampleFilter::Bilinear: return QObject::tr("Bilinear");
    case ResampleFilter::Bicubic: return QObject::tr("Bicubic");
    case ResampleFilter::Lanczos3: return QObject::tr("Lanczos3");
    }
    return {};
}

QImage resampleTransformed(const QImage &source, const QTransform &sourceToResult, const QSize &size,
                           ResampleFilter filter)
{
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    bool invertible = false;
    const QTransform back = sourceToResult.inverted(&invertible);
    if (!invertible || source.isNull() || size.isEmpty())
        return result;
    const QImage image = source.format() == QImage::Format_ARGB32_Premultiplied
                             ? source
                             : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = image.width(), h = image.height();
    // How many pixels of the result for one of the picture (a turn changes nothing in it).
    const float scale = float(std::sqrt(std::abs(sourceToResult.determinant())));
    // Made smaller, the kernel is stretched over the area each pixel covers; made bigger, it is as it is.
    const float stretch = std::clamp(1.0f / std::max(scale, 1e-3f), 1.0f, 8.0f);
    const int half = filter == ResampleFilter::Nearest ? 0 : int(std::ceil(radius(filter) * stretch));
    const int taps = filter == ResampleFilter::Nearest ? 1 : 2 * half;
    const float feather = std::max(scale, 1e-3f); // the edge fades over a pixel of the result

    parallelFor(size.height(), [&](int y) {
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        std::vector<float> wx(static_cast<std::size_t>(taps), 0.0f);
        std::vector<float> wy(static_cast<std::size_t>(taps), 0.0f);
        for (int x = 0; x < size.width(); ++x) {
            // The point of the picture this pixel of the result looks at (pixel centers are at whole numbers).
            const QPointF p = back.map(QPointF(x + 0.5, y + 0.5));
            const float u = float(p.x()), v = float(p.y());
            const float coverage = std::clamp(u * feather + 0.5f, 0.0f, 1.0f) * std::clamp((float(w) - u) * feather + 0.5f, 0.0f, 1.0f)
                                   * std::clamp(v * feather + 0.5f, 0.0f, 1.0f) * std::clamp((float(h) - v) * feather + 0.5f, 0.0f, 1.0f);
            if (coverage <= 0.0f)
                continue;
            const float sx = u - 0.5f, sy = v - 0.5f;
            float acc[4] = {0, 0, 0, 0};
            if (filter == ResampleFilter::Nearest) {
                const QRgb px = reinterpret_cast<const QRgb *>(
                    image.constScanLine(std::clamp(int(std::floor(v)), 0, h - 1)))[std::clamp(int(std::floor(u)), 0, w - 1)];
                acc[0] = float(qAlpha(px)); acc[1] = float(qRed(px)); acc[2] = float(qGreen(px)); acc[3] = float(qBlue(px));
            } else {
                const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                float sumX = 0, sumY = 0;
                for (int i = 0; i < taps; ++i) {
                    wx[std::size_t(i)] = weight(filter, (sx - float(x0 + i - (half - 1))) / stretch);
                    wy[std::size_t(i)] = weight(filter, (sy - float(y0 + i - (half - 1))) / stretch);
                    sumX += wx[std::size_t(i)];
                    sumY += wy[std::size_t(i)];
                }
                if (sumX <= 0 || sumY <= 0)
                    continue;
                for (int j = 0; j < taps; ++j) {
                    const QRgb *line = reinterpret_cast<const QRgb *>(
                        image.constScanLine(std::clamp(y0 + j - (half - 1), 0, h - 1)));
                    const float wj = wy[std::size_t(j)] / sumY;
                    if (wj == 0.0f)
                        continue;
                    for (int i = 0; i < taps; ++i) {
                        const float wt = wj * wx[std::size_t(i)] / sumX;
                        const QRgb px = line[std::clamp(x0 + i - (half - 1), 0, w - 1)];
                        acc[0] += wt * float(qAlpha(px));
                        acc[1] += wt * float(qRed(px));
                        acc[2] += wt * float(qGreen(px));
                        acc[3] += wt * float(qBlue(px));
                    }
                }
            }
            const int alpha = std::clamp(int(std::lround(acc[0] * coverage)), 0, 255);
            auto channel = [&](float c) { return std::clamp(int(std::lround(c * coverage)), 0, alpha); };
            out[x] = qRgba(channel(acc[1]), channel(acc[2]), channel(acc[3]), alpha);
        }
    });
    return result;
}

} // namespace mothion::canvas
