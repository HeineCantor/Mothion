#include "canvas/SymmetryGizmo.h"

#include "canvas/Symmetry.h"
#include "canvas/ViewTransform.h"
#include "tools/ToolState.h"

#include <QApplication>
#include <QLineF>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace mothion::canvas {

SymmetryGizmo::SymmetryGizmo(document::DocumentController *document, tools::ToolState *toolState)
    : m_document(document)
    , m_toolState(toolState)
{
}

bool SymmetryGizmo::shown() const { return m_toolState->symmetry().on; }

QPointF SymmetryGizmo::knob(const ViewTransform &view) const
{
    // Along the first axis, up at angle 0, as far on screen whatever the zoom and the view's turn.
    const double a = m_document->symmetryAngle() * std::numbers::pi / 180.0;
    const QPointF center = m_document->symmetryCenter();
    const QPointF way(std::sin(a), -std::cos(a));
    const QPointF c = view.documentToView().map(center);
    const QPointF along = view.documentToView().map(center + way) - c;
    const double length = std::hypot(along.x(), along.y());
    return length > 0 ? c + along * (kKnobAway / length) : c;
}

SymmetryGizmo::Handle SymmetryGizmo::handleAt(const QPointF &docPos, const ViewTransform &view) const
{
    if (!shown())
        return Handle::None;
    const QPointF at = view.documentToView().map(docPos);
    const QPointF center = view.documentToView().map(m_document->symmetryCenter());
    if (std::hypot(at.x() - center.x(), at.y() - center.y()) <= kGrab)
        return Handle::Center;
    if (symmetryTurns(m_toolState->symmetry().kind)) {
        const QPointF k = knob(view);
        if (std::hypot(at.x() - k.x(), at.y() - k.y()) <= kGrab)
            return Handle::Turn;
    }
    return Handle::None;
}

std::optional<QPointF> SymmetryGizmo::handlePoint(Handle handle, const ViewTransform &view) const
{
    switch (handle) {
    case Handle::Center: return m_document->symmetryCenter();
    case Handle::Turn: return view.mapToDocument(knob(view));
    case Handle::None: break;
    }
    return std::nullopt;
}

bool SymmetryGizmo::press(const QPointF &docPos, const ViewTransform &view, bool anywhere)
{
    if (!shown())
        return false;
    Handle handle = handleAt(docPos, view);
    if (handle == Handle::None && !anywhere)
        return false;
    const doc::Document::SymmetryAxes &axes = m_document->document().symmetryAxes();
    Drag d;
    d.center = m_document->symmetryCenter();
    d.angle = m_document->symmetryAngle();
    d.placed = axes.placed;
    d.press = docPos;
    if (handle == Handle::Center) {
        // A second click on the center soon after the first: back in the middle, upright.
        if (m_lastCenterClick.isValid() && m_lastCenterClick.elapsed() < QApplication::doubleClickInterval()) {
            m_lastCenterClick.invalidate();
            m_document->resetSymmetryAxes();
            return true;
        }
    } else if (handle == Handle::None) { // Ctrl+drag: the center comes to the pen
        handle = Handle::Center;
        m_document->setSymmetryAxes(docPos, d.angle);
        d.center = docPos;
    }
    d.grabbed = handle != Handle::None;
    d.handle = handle;
    m_drag = d;
    return true;
}

void SymmetryGizmo::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    const Drag &d = *m_drag;
    if (d.handle == Handle::Center) {
        m_document->setSymmetryAxes(d.center + (docPos - d.press), d.angle);
        return;
    }
    // The knob: the first axis points from the center to the pen (0 is up).
    const QPointF v = docPos - d.center;
    if (std::hypot(v.x(), v.y()) < 1e-6)
        return;
    double angle = std::atan2(v.x(), -v.y()) * 180.0 / std::numbers::pi;
    if (modifiers & Qt::ShiftModifier)
        angle = std::round(angle / 15.0) * 15.0;
    m_document->setSymmetryAxes(d.center, angle);
}

void SymmetryGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return;
    move(docPos, QApplication::keyboardModifiers());
    // A click on the center that did not move it: a second one soon after puts the axes back (see press()).
    const bool click = m_drag->handle == Handle::Center && m_drag->grabbed
                       && QLineF(docPos, m_drag->press).length() < 1e-9;
    if (click)
        m_lastCenterClick.start();
    else
        m_lastCenterClick.invalidate();
    m_drag.reset();
}

void SymmetryGizmo::cancel()
{
    if (!m_drag)
        return;
    const Drag d = *m_drag;
    m_drag.reset();
    if (d.placed)
        m_document->setSymmetryAxes(d.center, d.angle);
    else
        m_document->resetSymmetryAxes();
}

bool SymmetryGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const Handle h = handleAt(docPos, view);
    if (h == m_hovered)
        return false;
    m_hovered = h;
    return true;
}

void SymmetryGizmo::paint(QPainter &painter, const ViewTransform &view) const
{
    if (!shown())
        return;
    const QTransform toView = view.documentToView();
    const QSizeF page(m_document->document().width(), m_document->document().height());
    const double reach = 4.0 * std::hypot(page.width(), page.height()) + 4000.0 / view.zoom();
    const tools::SymmetrySettings settings = m_toolState->symmetry();
    const QPointF centerDoc = m_document->symmetryCenter();
    const QColor accent(0x2b, 0xb3, 0xa8);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    for (const QLineF &guide : symmetryGuides(settings, centerDoc, m_document->symmetryAngle(), page, reach)) {
        const QLineF line = toView.map(guide);
        painter.setPen(QPen(QColor(255, 255, 255, 150), 3.0));
        painter.drawLine(line);
        QPen dash(accent, 1.4, Qt::DashLine);
        dash.setDashPattern({6, 4});
        painter.setPen(dash);
        painter.drawLine(line);
    }
    const QPointF center = toView.map(centerDoc);
    const bool active = m_drag.has_value();
    if (symmetryTurns(settings.kind)) {
        const QPointF k = knob(view);
        painter.setPen(QPen(accent, 1.4));
        painter.drawLine(center, k);
        painter.setBrush(m_hovered == Handle::Turn || (active && m_drag->handle == Handle::Turn) ? accent : QColor(Qt::white));
        painter.drawEllipse(k, kKnob, kKnob);
    }
    painter.setPen(QPen(Qt::white, 2.0));
    painter.setBrush(accent);
    const double r = m_hovered == Handle::Center || (active && m_drag->handle == Handle::Center) ? kDot + 1.5 : kDot;
    painter.drawEllipse(center, r, r);
    painter.restore();
}

} // namespace mothion::canvas
