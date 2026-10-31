#include "canvas/ViewTransform.h"

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

QTransform ViewTransform::documentToView() const
{
    // QTransform composes in reverse: the last call is applied to points first.
    QTransform t;
    t.translate(m_viewport.width() / 2 + m_pan.x(), m_viewport.height() / 2 + m_pan.y());
    t.rotate(m_rotation);
    t.scale(m_flipH ? -m_zoom : m_zoom, m_flipV ? -m_zoom : m_zoom);
    t.translate(-m_document.width() / 2, -m_document.height() / 2);
    return t;
}

void ViewTransform::keepAnchored(const QPointF &docPoint, const QPointF &viewAnchor)
{
    m_pan += viewAnchor - documentToView().map(docPoint);
}

void ViewTransform::zoomAround(const QPointF &viewAnchor, double factor)
{
    const QPointF doc = mapToDocument(viewAnchor);
    m_zoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    keepAnchored(doc, viewAnchor);
}

void ViewTransform::rotateAround(const QPointF &viewAnchor, double degrees)
{
    const QPointF doc = mapToDocument(viewAnchor);
    double r = std::remainder(m_rotation + degrees, 360.0); // -> [-180, 180]
    if (r <= -180.0)
        r += 360.0;
    m_rotation = r;
    keepAnchored(doc, viewAnchor);
}

void ViewTransform::setFlippedAround(const QPointF &viewAnchor, bool horizontal, bool vertical)
{
    const QPointF doc = mapToDocument(viewAnchor);
    m_flipH = horizontal;
    m_flipV = vertical;
    keepAnchored(doc, viewAnchor);
}

void ViewTransform::centerOn(const QPointF &docPoint)
{
    keepAnchored(docPoint, viewportCenter());
}

void ViewTransform::fitToView()
{
    constexpr double kMargin = 0.92;
    m_rotation = 0.0;
    m_pan = {};
    const double zx = m_viewport.width() / m_document.width();
    const double zy = m_viewport.height() / m_document.height();
    m_zoom = std::clamp(std::min(zx, zy) * kMargin, kMinZoom, kMaxZoom);
}

} // namespace mothion::canvas
