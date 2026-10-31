#include "canvas/NavigationGesture.h"

#include "canvas/ViewTransform.h"

#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

double angleAround(const QPointF &center, const QPointF &p)
{
    return std::atan2(p.y() - center.y(), p.x() - center.x()) * 180.0 / std::numbers::pi;
}

} // namespace

void NavigationGesture::begin(Mode mode, const QPointF &viewPos, const ViewTransform &view)
{
    m_mode = mode;
    m_last = viewPos;
    m_anchor = viewPos;
    m_lastAngle = angleAround(view.viewportCenter(), viewPos);
}

void NavigationGesture::update(const QPointF &viewPos, ViewTransform &view)
{
    switch (m_mode) {
    case Mode::None:
        return;
    case Mode::Pan:
        view.panBy(viewPos - m_last);
        break;
    case Mode::Zoom: {
        // Drag right / up to zoom in, left / down to zoom out.
        const QPointF d = viewPos - m_last;
        view.zoomAround(m_anchor, std::exp((d.x() - d.y()) * 0.01));
        break;
    }
    case Mode::Rotate: {
        const double angle = angleAround(view.viewportCenter(), viewPos);
        view.rotateAround(view.viewportCenter(), std::remainder(angle - m_lastAngle, 360.0));
        m_lastAngle = angle;
        break;
    }
    }
    m_last = viewPos;
}

} // namespace mothion::canvas
