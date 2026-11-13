#include "compositing/nodes/LensDistortionNode.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

namespace {

enum class Kernel { Bilinear, Bicubic, Lanczos };

constexpr float kPi = 3.14159265358979f;

// The weight of a pixel `t` away (in pixels) from the point read.
float weight(Kernel kernel, float t)
{
    t = std::abs(t);
    switch (kernel) {
    case Kernel::Bilinear:
        return t < 1.0f ? 1.0f - t : 0.0f;
    case Kernel::Bicubic: // Catmull-Rom: passes through the pixels, no blur of its own
        if (t < 1.0f)
            return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
        return t < 2.0f ? -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f : 0.0f;
    case Kernel::Lanczos:
        if (t < 1e-6f)
            return 1.0f;
        return t < 3.0f ? 3.0f * std::sin(kPi * t) * std::sin(kPi * t / 3.0f) / (kPi * kPi * t * t) : 0.0f;
    }
    return 0.0f;
}

int radius(Kernel kernel) { return kernel == Kernel::Bilinear ? 1 : (kernel == Kernel::Bicubic ? 2 : 3); }

// The premultiplied color of the picture at (sx, sy) (pixel centers at whole numbers), already multiplied by how much
// of the picture is there: the edge fades over the width of a pixel, so the border of a bent picture is not a jagged
// line. Past the edge the pixels read are the edge's own.
void sample(const QImage &image, Kernel kernel, float sx, float sy, float out[4])
{
    const int w = image.width(), h = image.height();
    const float coverage = std::clamp(sx + 1.0f, 0.0f, 1.0f) * std::clamp(float(w) - sx, 0.0f, 1.0f)
                           * std::clamp(sy + 1.0f, 0.0f, 1.0f) * std::clamp(float(h) - sy, 0.0f, 1.0f);
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (coverage <= 0.0f)
        return;
    const int r = radius(kernel);
    const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
    const float fx = sx - float(x0), fy = sy - float(y0);
    float wx[6], wy[6];
    float sumX = 0, sumY = 0;
    for (int i = 0; i < 2 * r; ++i) {
        wx[i] = weight(kernel, fx - float(i - (r - 1)));
        wy[i] = weight(kernel, fy - float(i - (r - 1)));
        sumX += wx[i];
        sumY += wy[i];
    }
    float acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2 * r; ++j) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(std::clamp(y0 + j - (r - 1), 0, h - 1)));
        const float wj = wy[j] / sumY;
        for (int i = 0; i < 2 * r; ++i) {
            const QRgb px = line[std::clamp(x0 + i - (r - 1), 0, w - 1)];
            const float wt = wj * wx[i] / sumX;
            acc[0] += wt * float(qAlpha(px));
            acc[1] += wt * float(qRed(px));
            acc[2] += wt * float(qGreen(px));
            acc[3] += wt * float(qBlue(px));
        }
    }
    for (int c = 0; c < 4; ++c)
        out[c] = acc[c] * coverage;
}

} // namespace

QImage LensDistortionNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int amountPercent = params.value(QStringLiteral("amount")).toInt();
    const int zoomPercent = std::clamp(params.value(QStringLiteral("zoom")).toInt(), 50, 300);
    const bool autosize = params.value(QStringLiteral("autosize")).toBool();
    if (amountPercent == 0 && (autosize || zoomPercent == 100))
        return source;
    const Kernel kernel = Kernel(std::clamp(params.value(QStringLiteral("quality"), 1).toInt(), 0, 2));
    const int w = source.width(), h = source.height();
    const float a = float(amountPercent) / 100.0f;
    // Outwards the lens draws the edges in a lot; inwards, less (past a point the picture would fold over itself).
    const float k = a > 0 ? 1.2f * a : 0.3f * a;
    float zoom = float(zoomPercent) / 100.0f;
    const float fw = float(w), fh = float(h); // the picture's size, as the float it is used as
    const float cx = float(params.value(QStringLiteral("centerx"), 50).toInt()) / 100.0f * (fw - 1.0f);
    const float cy = float(params.value(QStringLiteral("centery"), 50).toInt()) / 100.0f * (fh - 1.0f);
    // r is 1 at the farthest corner from the center.
    const float reach = std::max(1.0f, std::hypot(std::max(cx, fw - 1.0f - cx), std::max(cy, fh - 1.0f - cy)));

    if (autosize) {
        // The smallest zoom with every point of the result coming from inside the picture. Along each ray from the
        // center the distance the lens takes it from only grows, so the edge of the frame is where it is greatest.
        // (Taken a half pixel past the frame's edge: a pixel's area is read, not only its center.)
        auto need = [&](float x, float y) {
            const float dx = x - cx, dy = y - cy;
            const float r = std::hypot(dx, dy) / reach;
            const float far = 1.0f + k * r * r;
            float z = 0.0f;
            if (dx > 0 && fw - 1.0f - cx > 0)
                z = std::max(z, dx * far / (fw - 1.0f - cx));
            else if (dx < 0 && cx > 0)
                z = std::max(z, -dx * far / cx);
            if (dy > 0 && fh - 1.0f - cy > 0)
                z = std::max(z, dy * far / (fh - 1.0f - cy));
            else if (dy < 0 && cy > 0)
                z = std::max(z, -dy * far / cy);
            return z;
        };
        float z = 0.0f;
        for (int x = 0; x < w; ++x)
            z = std::max({z, need(float(x), -0.5f), need(float(x), fh - 0.5f)});
        for (int y = 0; y < h; ++y)
            z = std::max({z, need(-0.5f, float(y)), need(fw - 0.5f, float(y))});
        z = std::max({z, need(-0.5f, -0.5f), need(fw - 0.5f, -0.5f), need(-0.5f, fh - 0.5f), need(fw - 0.5f, fh - 0.5f)});
        zoom = std::max(0.05f, z * 1.001f); // (a hair over, so a rounding does not show an edge)
    }

    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // Where the lens takes the point's neighborhood from: its distance to the center is stretched by this much
            // along the radius (the slope of the mapping) and across it (the mapping itself). More than a pixel of
            // the picture for one of the result, and the pixel's whole area is read, in a few points.
            const float dx0 = float(x) - cx, dy0 = float(y) - cy;
            const float r0 = std::hypot(dx0, dy0) / reach;
            const float stretch = std::max(1.0f + 3.0f * k * r0 * r0, 1.0f + k * r0 * r0) / zoom;
            const int n = kernel == Kernel::Bilinear ? 1 : std::clamp(int(std::ceil(stretch - 0.25f)), 1, 4);
            float acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    const float dx = dx0 + (float(i) + 0.5f) / float(n) - 0.5f, dy = dy0 + (float(j) + 0.5f) / float(n) - 0.5f;
                    const float r = std::hypot(dx, dy) / reach;
                    const float scale = (1.0f + k * r * r) / zoom;
                    float px[4];
                    sample(source, kernel, cx + dx * scale, cy + dy * scale, px);
                    for (int c = 0; c < 4; ++c)
                        acc[c] += px[c];
                }
            }
            const float inv = 1.0f / float(n * n);
            const int alpha = std::clamp(int(std::lround(acc[0] * inv)), 0, 255);
            // A kernel with a negative lobe can overshoot: no color past its alpha.
            out[x] = qRgba(std::min(alpha, std::max(0, int(std::lround(acc[1] * inv)))),
                           std::min(alpha, std::max(0, int(std::lround(acc[2] * inv)))),
                           std::min(alpha, std::max(0, int(std::lround(acc[3] * inv)))), alpha);
        }
    });
    return result;
}

} // namespace mothion::compositing
