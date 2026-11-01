#include "canvas/DabStamper.h"

#include "core/brush/Hardness.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

namespace {

constexpr int kSoftStampSize = 256;

// Soft round dab: coverage from the hardness profile, in the alpha channel.
QImage softCoverage(double hardness)
{
    QImage img(kSoftStampSize, kSoftStampSize, QImage::Format_ARGB32_Premultiplied);
    const double c = (kSoftStampSize - 1) / 2.0;
    for (int y = 0; y < kSoftStampSize; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < kSoftStampSize; ++x) {
            const double r = std::hypot(x - c, y - c) / (c + 0.5);
            const int a = int(std::lround(brush::hardnessCoverage(r, hardness) * 255.0));
            line[x] = qRgba(0, 0, 0, a);
        }
    }
    return img;
}

} // namespace

DabStamper::DabStamper(const QColor &color, double hardness, const QList<QImage> &tips, bool verticalThickness)
    : m_color(color)
    , m_hard(hardness >= 0.99999999)
    , m_verticalThickness(verticalThickness)
{
    m_color.setAlpha(255);
    if (tips.isEmpty() && !m_hard)
        m_soft = buildMipmap(softCoverage(hardness), m_color);
    m_tips.reserve(std::size_t(tips.size()));
    for (const QImage &tip : tips)
        m_tips.push_back(buildMipmap(tip, m_color));
}

DabStamper::Mipmap DabStamper::buildMipmap(const QImage &coverage, const QColor &color)
{
    // Stamp = stroke color where the coverage is.
    QImage stamp(coverage.size(), QImage::Format_ARGB32_Premultiplied);
    stamp.fill(color);
    {
        QPainter p(&stamp);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, coverage);
    }
    Mipmap m;
    m.levels.push_back(stamp);
    while (std::min(m.levels.back().width(), m.levels.back().height()) >= 8) {
        const QImage &last = m.levels.back();
        m.levels.push_back(last.scaled(last.width() / 2, last.height() / 2, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation));
    }
    return m;
}

const QImage &DabStamper::Mipmap::pick(double pixels) const
{
    // The smallest level still at least as large as the target.
    std::size_t i = 0;
    while (i + 1 < levels.size()
           && std::max(levels[i + 1].width(), levels[i + 1].height()) >= pixels)
        ++i;
    return levels[i];
}

QSizeF DabStamper::footprint(const brush::BrushDab &dab) const
{
    QSizeF size(2.0 * dab.radius, 2.0 * dab.radius);
    if (dab.tip >= 0 && std::size_t(dab.tip) < m_tips.size()) {
        // The tip's longer side spans the diameter.
        const QImage &tip = m_tips[std::size_t(dab.tip)].levels.front();
        const double k = 2.0 * dab.radius / std::max(tip.width(), tip.height());
        size = QSizeF(tip.width() * k, tip.height() * k);
    }
    if (m_verticalThickness)
        size.setWidth(size.width() * dab.thickness);
    else
        size.setHeight(size.height() * dab.thickness);
    return size;
}

QRectF DabStamper::bounds(const brush::BrushDab &dab) const
{
    const QSizeF s = footprint(dab);
    const double r = std::hypot(s.width(), s.height()) / 2.0 + 1.0;
    return QRectF(dab.x - r, dab.y - r, 2.0 * r, 2.0 * r);
}

void DabStamper::draw(QPainter &painter, const brush::BrushDab &dab) const
{
    if (dab.alpha <= 0.0)
        return;
    const QSizeF s = footprint(dab);
    const QRectF rect(-s.width() / 2.0, -s.height() / 2.0, s.width(), s.height());

    painter.save();
    painter.translate(dab.x, dab.y);
    painter.rotate(dab.angle);
    if (dab.mirror)
        painter.scale(-1.0, 1.0);
    painter.setOpacity(std::clamp(dab.alpha, 0.0, 1.0));
    if (dab.tip >= 0 && std::size_t(dab.tip) < m_tips.size()) {
        const Mipmap &tip = m_tips[std::size_t(dab.tip)];
        painter.drawImage(rect, tip.pick(std::max(s.width(), s.height())));
    } else if (m_hard || m_soft.levels.empty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawEllipse(rect);
    } else {
        painter.drawImage(rect, m_soft.pick(std::max(s.width(), s.height())));
    }
    painter.restore();
}

} // namespace mothion::canvas
