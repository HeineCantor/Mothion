#include "canvas/DabStamper.h"

#include "core/brush/Hardness.h"

#include <QPainter>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

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

void DabStamper::setSecondaryColor(const QColor &color)
{
    m_secondary = color;
    m_secondary.setAlpha(255);
}

namespace {

QColor shifted(const QColor &c, const brush::DabColor &v)
{
    if (v.hue == 0.0 && v.saturation == 0.0 && v.value == 0.0)
        return c;
    float h = 0, s = 0, val = 0;
    c.getHsvF(&h, &s, &val);
    double hue = std::fmod(std::max(0.0, double(h)) * 360.0 + v.hue, 360.0);
    if (hue < 0.0)
        hue += 360.0;
    return QColor::fromHsvF(float(hue / 360.0), float(std::clamp(s + v.saturation, 0.0, 1.0)),
                            float(std::clamp(val + v.value, 0.0, 1.0)));
}

} // namespace

QColor DabStamper::colorFor(const brush::DabColor &v) const
{
    if (v.isPlain())
        return m_color;
    using brush::ColorTarget;
    const QColor main = v.target != ColorTarget::Secondary ? shifted(m_color, v) : m_color;
    const QColor secondary = v.target != ColorTarget::Main ? shifted(m_secondary, v) : m_secondary;
    const double t = std::clamp(v.subColor, 0.0, 1.0);
    auto mix = [t](float a, float b) { return float(a + (b - a) * t); };
    return QColor::fromRgbF(mix(main.redF(), secondary.redF()), mix(main.greenF(), secondary.greenF()),
                            mix(main.blueF(), secondary.blueF()));
}

QImage DabStamper::recolored(const QImage &stamp, const QColor &color) const
{
    QImage out(stamp.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(color);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, stamp);
    return out;
}

const QImage *DabStamper::stampFor(const brush::BrushDab &dab, double pixels) const
{
    if (dab.tip >= 0 && std::size_t(dab.tip) < m_tips.size())
        return &m_tips[std::size_t(dab.tip)].pick(pixels);
    if (m_hard || m_soft.levels.empty())
        return nullptr;
    return &m_soft.pick(pixels);
}

void DabStamper::draw(QPainter &painter, const brush::BrushDab &dab) const
{
    draw(painter, dab, colorFor(dab.color));
}

void DabStamper::draw(QPainter &painter, const brush::BrushDab &dab, const QColor &color) const
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
    if (const QImage *stamp = stampFor(dab, std::max(s.width(), s.height()))) {
        if (color.rgb() == m_color.rgb())
            painter.drawImage(rect, *stamp);
        else
            painter.drawImage(rect, recolored(*stamp, color));
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(color.rgb()));
        painter.drawEllipse(rect);
    }
    painter.restore();
}

void DabStamper::drawRibbon(QPainter &painter, const brush::BrushDab &from, const brush::BrushDab &to,
                            const QColor &color) const
{
    const double alpha = std::clamp((from.alpha + to.alpha) / 2.0, 0.0, 1.0);
    if (alpha <= 0.0)
        return;
    // Edges across the segment at each dab, as wide as the dab.
    const double length = std::hypot(to.x - from.x, to.y - from.y);
    if (length <= 1e-9)
        return;
    const double nx = -(to.y - from.y) / length, ny = (to.x - from.x) / length;
    auto edge = [&](const brush::BrushDab &d, double sign) {
        return QPointF(d.x + sign * nx * d.radius, d.y + sign * ny * d.radius);
    };
    // Same corner order as the tip's rect: its top edge lies across the stroke at `from`.
    QPolygonF quad;
    quad << edge(from, -1.0) << edge(from, 1.0) << edge(to, 1.0) << edge(to, -1.0);

    painter.save();
    painter.setOpacity(alpha);
    const QImage *stamp = stampFor(to, std::max(2.0 * to.radius, length));
    QTransform map;
    if (stamp && QTransform::quadToQuad(QPolygonF(QRectF(stamp->rect())), quad, map)) {
        // The tip's width runs across the stroke, its height along it.
        painter.setTransform(map, true);
        painter.drawImage(QPointF(0, 0), color.rgb() == m_color.rgb() ? *stamp : recolored(*stamp, color));
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(color.rgb()));
        painter.drawPolygon(quad);
    }
    painter.restore();
}

} // namespace mothion::canvas
