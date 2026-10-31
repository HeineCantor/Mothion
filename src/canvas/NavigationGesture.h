#pragma once

#include <QPointF>

namespace mothion::canvas {

class ViewTransform;

// A drag that changes the view instead of painting: pan, scrubby zoom or rotate.
// Fed with view-space positions (from pen or mouse alike), it updates a ViewTransform.
class NavigationGesture
{
public:
    enum class Mode { None, Pan, Zoom, Rotate };

    void begin(Mode mode, const QPointF &viewPos, const ViewTransform &view);
    void update(const QPointF &viewPos, ViewTransform &view);
    void end() { m_mode = Mode::None; }

    bool isActive() const { return m_mode != Mode::None; }
    Mode mode() const { return m_mode; }

private:
    Mode m_mode = Mode::None;
    QPointF m_last;      // previous position (pan, zoom)
    QPointF m_anchor;    // zoom anchor: where the drag started
    double m_lastAngle = 0.0; // rotate: angle of the pointer around the viewport center
};

} // namespace mothion::canvas
