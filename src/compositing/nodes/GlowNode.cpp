#include "compositing/nodes/GlowNode.h"

#include "core/Parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace mothion::compositing {

namespace {

// A picture of float RGB, in linear light, as many as the pixels.
struct Plane
{
    int w = 0, h = 0;
    std::vector<float> rgb;

    Plane() = default;
    Plane(int width, int height) : w(width), h(height), rgb(size_t(width) * size_t(height) * 3, 0.0f) {}
    float *at(int x, int y) { return rgb.data() + (size_t(y) * size_t(w) + size_t(x)) * 3; }
    const float *at(int x, int y) const { return rgb.data() + (size_t(y) * size_t(w) + size_t(x)) * 3; }
};

// sRGB bytes to linear light, and back (the way back through a table of 4096 steps).
const std::array<float, 256> &toLinearTable()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = float(i) / 255.0f;
            t[size_t(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

constexpr int kBackSteps = 4096;

const std::array<unsigned char, kBackSteps + 1> &fromLinearTable()
{
    static const std::array<unsigned char, kBackSteps + 1> table = [] {
        std::array<unsigned char, kBackSteps + 1> t{};
        for (int i = 0; i <= kBackSteps; ++i) {
            const float l = float(i) / kBackSteps;
            const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            t[size_t(i)] = static_cast<unsigned char>(std::clamp(int(std::lround(c * 255.0f)), 0, 255));
        }
        return t;
    }();
    return table;
}

unsigned char toByte(float linear)
{
    return fromLinearTable()[size_t(std::clamp(linear, 0.0f, 1.0f) * kBackSteps + 0.5f)];
}

// Half the size: each pixel is the average of the 2x2 that it covers.
Plane halve(const Plane &in)
{
    Plane out(std::max(1, (in.w + 1) / 2), std::max(1, (in.h + 1) / 2));
    parallelFor(out.h, [&](int y) {
        for (int x = 0; x < out.w; ++x) {
            const int x1 = std::min(x * 2 + 1, in.w - 1), y1 = std::min(y * 2 + 1, in.h - 1);
            const float *a = in.at(x * 2, y * 2), *b = in.at(x1, y * 2), *c = in.at(x * 2, y1), *d = in.at(x1, y1);
            float *o = out.at(x, y);
            for (int k = 0; k < 3; ++k)
                o[k] = (a[k] + b[k] + c[k] + d[k]) * 0.25f;
        }
    });
    return out;
}

// A 3x3 average, to take the squares out of what the halving leaves.
void soften(Plane &p)
{
    Plane temp(p.w, p.h);
    parallelFor(p.h, [&](int y) {
        for (int x = 0; x < p.w; ++x) {
            const float *l = p.at(std::max(x - 1, 0), y), *m = p.at(x, y), *r = p.at(std::min(x + 1, p.w - 1), y);
            float *o = temp.at(x, y);
            for (int k = 0; k < 3; ++k)
                o[k] = (l[k] + m[k] + r[k]) * (1.0f / 3.0f);
        }
    });
    parallelFor(p.h, [&](int y) {
        for (int x = 0; x < p.w; ++x) {
            const float *u = temp.at(x, std::max(y - 1, 0)), *m = temp.at(x, y), *d = temp.at(x, std::min(y + 1, p.h - 1));
            float *o = p.at(x, y);
            for (int k = 0; k < 3; ++k)
                o[k] = (u[k] + m[k] + d[k]) * (1.0f / 3.0f);
        }
    });
}

// The value of a plane at the place that pixel (x, y) of a picture `scale` times as big has, by bilinear
// interpolation; the edges go on.
void sampleInto(const Plane &p, int x, int y, int scale, float weight, float *sum)
{
    const float fx = std::clamp((float(x) + 0.5f) / float(scale) - 0.5f, 0.0f, float(p.w - 1));
    const float fy = std::clamp((float(y) + 0.5f) / float(scale) - 0.5f, 0.0f, float(p.h - 1));
    const int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, p.w - 1), y1 = std::min(y0 + 1, p.h - 1);
    const float tx = fx - float(x0), ty = fy - float(y0);
    const float *a = p.at(x0, y0), *b = p.at(x1, y0), *c = p.at(x0, y1), *d = p.at(x1, y1);
    for (int k = 0; k < 3; ++k)
        sum[k] += weight * ((a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty);
}

float smoothstep(float lo, float hi, float v)
{
    if (hi <= lo)
        return v >= hi ? 1.0f : 0.0f;
    const float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

QImage GlowNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const float threshold = float(params.value(QStringLiteral("threshold")).toInt()) / 100.0f;
    const float knee = 0.3f * float(params.value(QStringLiteral("softness")).toInt()) / 100.0f;
    const double reach = params.value(QStringLiteral("size")).toInt() * context.scale;
    const float intensity = float(params.value(QStringLiteral("intensity")).toInt()) / 100.0f;
    const QColor tint = params.value(QStringLiteral("tint")).value<QColor>();
    const bool ignorePaper = params.value(QStringLiteral("ignorePaper")).toInt() != 0;
    if (intensity <= 0.0f)
        return source;

    const int w = source.width(), h = source.height();
    const auto &linear = toLinearTable();
    const int paperR = qRed(context.paper), paperG = qGreen(context.paper), paperB = qBlue(context.paper);

    // 1. The light the picture gives: what is above the threshold, in linear light.
    Plane light(w, h);
    parallelFor(h, [&](int y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
            float gives = smoothstep(threshold - knee, threshold + knee, (0.2126f * float(r) + 0.7152f * float(g) + 0.0722f * float(b)) / 255.0f);
            if (ignorePaper) {
                // Exactly the paper gives nothing; a few levels away, all it has.
                const int apart = std::max({std::abs(r - paperR), std::abs(g - paperG), std::abs(b - paperB)});
                gives *= std::min(1.0f, float(apart) / 8.0f);
            }
            float *o = light.at(x, y);
            o[0] = linear[size_t(r)] * gives;
            o[1] = linear[size_t(g)] * gives;
            o[2] = linear[size_t(b)] * gives;
        }
    });

    // 2. The pyramid: each level half the one before and softer, up to the reach asked for (a level reaches about
    // twice as far as the one below it).
    const int levels = std::clamp(int(std::lround(std::log2(std::max(2.0, reach)))), 1, 8);
    std::vector<Plane> pyramid;
    pyramid.reserve(size_t(levels));
    const Plane *previous = &light;
    for (int i = 1; i <= levels; ++i) {
        pyramid.push_back(halve(*previous));
        soften(pyramid.back());
        previous = &pyramid.back();
    }
    // The coarser, the weaker; the weights add up to one, so Intensity 1 gives back as much light as it took.
    std::vector<float> weights(static_cast<size_t>(levels), 0.0f);
    float total = 0.0f;
    for (int i = 0; i < levels; ++i) {
        weights[size_t(i)] = std::pow(0.75f, float(i));
        total += weights[size_t(i)];
    }
    for (float &weight : weights)
        weight /= total;

    // 3. Add the glow to the picture, in linear light, and back to bytes.
    const float tintR = linear[size_t(tint.red())] * intensity, tintG = linear[size_t(tint.green())] * intensity,
                tintB = linear[size_t(tint.blue())] * intensity;
    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    parallelFor(h, [&](int y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            float glow[3] = {0, 0, 0};
            for (int i = 0; i < levels; ++i)
                sampleInto(pyramid[size_t(i)], x, y, 1 << (i + 1), weights[size_t(i)], glow);
            const QRgb px = row[x];
            const unsigned char r = toByte(linear[size_t(qRed(px))] + glow[0] * tintR);
            const unsigned char g = toByte(linear[size_t(qGreen(px))] + glow[1] * tintG);
            const unsigned char b = toByte(linear[size_t(qBlue(px))] + glow[2] * tintB);
            // Light on a see-through pixel makes it less so: no color may be above its alpha.
            const int alpha = std::max({qAlpha(px), int(r), int(g), int(b)});
            out[x] = qRgba(r, g, b, alpha);
        }
    });
    return result;
}

} // namespace mothion::compositing
