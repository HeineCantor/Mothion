#include "canvas/RasterSurface.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

namespace {
constexpr auto kFormat = QImage::Format_ARGB32_Premultiplied; // fastest for QPainter
}

RasterSurface::RasterSurface(const QSize &size)
    : m_committed(size, kFormat)
    , m_strokeMask(size, kFormat)
    , m_composite(size, kFormat)
{
    m_committed.fill(Qt::transparent);
    m_strokeMask.fill(Qt::transparent);
    m_composite.fill(Qt::transparent);
}

void RasterSurface::beginStroke(const QColor &color, double opacity, bool erase)
{
    m_color = color;
    m_color.setAlpha(255); // stroke-level opacity is applied at composition
    m_opacity = opacity;
    m_erase = erase;
    m_inStroke = true;
    m_strokeBounds = {};
}

QRect RasterSurface::addDabs(std::span<const Dab> dabs)
{
    if (!m_inStroke || dabs.empty())
        return {};

    QRectF changed;
    {
        QPainter p(&m_strokeMask);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        for (const Dab &d : dabs) {
            const double r = std::max(d.radius, 0.5);
            p.drawEllipse(d.center, r, r);
            changed |= QRectF(d.center.x() - r, d.center.y() - r, 2 * r, 2 * r);
        }
    }
    const QRect dirty = changed.toAlignedRect().adjusted(-1, -1, 1, 1) & m_composite.rect();
    m_strokeBounds |= dirty;
    recompose(dirty);
    return dirty;
}

void RasterSurface::recompose(const QRect &rect)
{
    if (rect.isEmpty())
        return;
    QPainter p(&m_composite);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(rect.topLeft(), m_committed, rect);
    p.setCompositionMode(m_erase ? QPainter::CompositionMode_DestinationOut
                                 : QPainter::CompositionMode_SourceOver);
    p.setOpacity(m_opacity);
    p.drawImage(rect.topLeft(), m_strokeMask, rect);
}

std::optional<RasterEdit> RasterSurface::endStroke()
{
    if (!m_inStroke)
        return std::nullopt;
    m_inStroke = false;
    if (m_strokeBounds.isEmpty())
        return std::nullopt;

    // Only the stroke's bounding box is stored, not the whole image.
    const QRect r = m_strokeBounds;
    RasterEdit edit{{r.topLeft(), m_committed.copy(r)}, {r.topLeft(), m_composite.copy(r)}};

    // The composite already shows the final result: make it the committed state.
    QPainter commit(&m_committed);
    commit.setCompositionMode(QPainter::CompositionMode_Source);
    commit.drawImage(r.topLeft(), m_composite, r);
    QPainter clear(&m_strokeMask);
    clear.setCompositionMode(QPainter::CompositionMode_Clear);
    clear.fillRect(r, Qt::transparent);
    return edit;
}

void RasterSurface::cancelStroke()
{
    if (!m_inStroke)
        return;
    m_inStroke = false;
    if (m_strokeBounds.isEmpty())
        return;
    QPainter clear(&m_strokeMask);
    clear.setCompositionMode(QPainter::CompositionMode_Clear);
    clear.fillRect(m_strokeBounds, Qt::transparent);
    QPainter restore(&m_composite);
    restore.setCompositionMode(QPainter::CompositionMode_Source);
    restore.drawImage(m_strokeBounds.topLeft(), m_committed, m_strokeBounds);
}

QRect RasterSurface::applyPatch(const RasterPatch &patch)
{
    cancelStroke();
    for (QImage *target : {&m_committed, &m_composite}) {
        QPainter p(target);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(patch.pos, patch.pixels);
    }
    return QRect(patch.pos, patch.pixels.size());
}

} // namespace mothion::canvas
