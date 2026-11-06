#include "compositing/nodes/SensorNoiseNode.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

namespace {

// 32 well-mixed bits from a place, a seed and a salt (splitmix64 on the three).
quint32 hash(quint32 x, quint32 y, quint32 seed, quint32 salt)
{
    quint64 z = (quint64(x) << 32 | y) + (quint64(seed) * 2 + 1) * 0x9e3779b97f4a7c15ULL + salt * 0xd1342543de82ef95ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return quint32((z ^ (z >> 31)) & 0xffffffffu);
}

// Noise of mean 0 and spread 1, near enough to a Gauss bell: four bytes of the hash, added.
float bell(quint32 bits)
{
    const int sum = int(bits & 255) + int((bits >> 8) & 255) + int((bits >> 16) & 255) + int(bits >> 24);
    return (float(sum) / 255.0f - 2.0f) * 1.7320508f; // the sum of 4 uniforms has a spread of 1/sqrt(3)
}

} // namespace

QImage SensorNoiseNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    QImage base = inputs.value(0);
    if (base.isNull()) {
        if (context.frame.isNull())
            return {};
        base = QImage(context.frame.size(), QImage::Format_ARGB32);
        base.fill(QColor(128, 128, 128));
    }
    // Straight (not premultiplied) colors: noise is added to the color, the alpha stays what it was.
    QImage image = base.convertToFormat(QImage::Format_ARGB32);
    const float luminance = float(params.value(QStringLiteral("luminance")).toInt()) / 100.0f * 60.0f; // in levels
    const float chroma = float(params.value(QStringLiteral("chroma")).toInt()) / 100.0f * 60.0f;
    const int grain = std::max(1, int(std::lround(params.value(QStringLiteral("size")).toInt() * context.scale)));
    const int chromaGrain = grain * 2; // color noise comes in coarser blotches
    quint32 seed = quint32(params.value(QStringLiteral("seed")).toInt());
    if (usesFrameIndex(params))
        seed += quint32(context.frameIndex) * 7919u;
    if (luminance <= 0.0f && chroma <= 0.0f)
        return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    parallelFor(image.height(), [&](int y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = row[x];
            if (qAlpha(px) == 0)
                continue;
            const float r = float(qRed(px)), g = float(qGreen(px)), b = float(qBlue(px));
            const float y709 = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
            const float shadows = 1.0f - 0.5f * y709; // the darker, the noisier

            const quint32 gx = quint32(x / grain), gy = quint32(y / grain);
            const float n = luminance > 0.0f ? bell(hash(gx, gy, seed, 1)) * luminance * shadows : 0.0f;
            float dr = n, dg = n, db = n;
            if (chroma > 0.0f) {
                const quint32 cx = quint32(x / chromaGrain), cy = quint32(y / chromaGrain);
                const float cb = bell(hash(cx, cy, seed, 2)) * chroma * shadows;
                const float cr = bell(hash(cx, cy, seed, 3)) * chroma * shadows;
                dr += 1.402f * cr; // YCbCr's chroma axes back to RGB
                dg -= 0.344f * cb + 0.714f * cr;
                db += 1.772f * cb;
            }
            row[x] = qRgba(std::clamp(int(std::lround(r + dr)), 0, 255), std::clamp(int(std::lround(g + dg)), 0, 255),
                           std::clamp(int(std::lround(b + db)), 0, 255), qAlpha(px));
        }
    });
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied); // like the other nodes' pictures
}

} // namespace mothion::compositing
