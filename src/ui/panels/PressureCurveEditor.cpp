#include "ui/panels/PressureCurveEditor.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace mothion::ui {

namespace {
constexpr double kMargin = 8.0;
constexpr double kHandleRadius = 5.0;
} // namespace

PressureCurveEditor::PressureCurveEditor(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(120, 120);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setToolTip(tr("Curva di pressione: trascina i due punti di controllo"));
}

void PressureCurveEditor::setCurve(const PressureCurve &curve)
{
    m_curve = curve;
    update();
}

void PressureCurveEditor::showInputPressure(float pressure)
{
    m_inputPressure = pressure;
    update();
}

QRectF PressureCurveEditor::plotRect() const
{
    const double side = std::min(width(), height()) - 2 * kMargin;
    return {kMargin, kMargin, side, side};
}

QPointF PressureCurveEditor::toWidget(PressureCurve::Point p) const
{
    const QRectF r = plotRect();
    return {r.left() + p.x * r.width(), r.bottom() - p.y * r.height()};
}

PressureCurve::Point PressureCurveEditor::toCurve(QPointF w) const
{
    const QRectF r = plotRect();
    return {float((w.x() - r.left()) / r.width()), float((r.bottom() - w.y()) / r.height())};
}

void PressureCurveEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();
    const QPalette &pal = palette();

    p.fillRect(r, pal.base());
    p.setPen(QPen(pal.mid().color(), 1));
    for (int i = 1; i < 4; ++i) {
        const double f = i / 4.0;
        p.drawLine(QPointF(r.left() + f * r.width(), r.top()), QPointF(r.left() + f * r.width(), r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + f * r.height()), QPointF(r.right(), r.top() + f * r.height()));
    }
    p.setPen(QPen(pal.light().color(), 1, Qt::DashLine));
    p.drawLine(r.bottomLeft(), r.topRight());

    // The curve itself, drawn from the lookup table (what the brush really uses).
    QPainterPath path;
    constexpr int kSteps = 64;
    for (int i = 0; i <= kSteps; ++i) {
        const float x = float(i) / kSteps;
        const QPointF pt = toWidget({x, m_curve.map(x)});
        i == 0 ? path.moveTo(pt) : path.lineTo(pt);
    }
    p.setPen(QPen(pal.highlight().color(), 2));
    p.drawPath(path);

    // Control handles with their tangent lines.
    const QPointF c1 = toWidget(m_curve.control1());
    const QPointF c2 = toWidget(m_curve.control2());
    p.setPen(QPen(pal.text().color(), 1));
    p.drawLine(toWidget({0, 0}), c1);
    p.drawLine(toWidget({1, 1}), c2);
    p.setBrush(pal.window());
    p.drawEllipse(c1, kHandleRadius, kHandleRadius);
    p.drawEllipse(c2, kHandleRadius, kHandleRadius);

    if (m_inputPressure >= 0.0f) {
        const QPointF dot = toWidget({m_inputPressure, m_curve.map(m_inputPressure)});
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0xb0, 0x30));
        p.drawEllipse(dot, 4, 4);
    }
}

void PressureCurveEditor::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const double d1 = QLineF(pos, toWidget(m_curve.control1())).length();
    const double d2 = QLineF(pos, toWidget(m_curve.control2())).length();
    const double grab = kHandleRadius * 2.5;
    if (std::min(d1, d2) <= grab)
        m_dragHandle = d1 <= d2 ? 0 : 1;
}

void PressureCurveEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragHandle < 0)
        return;
    const PressureCurve::Point p = toCurve(event->position());
    m_curve = m_dragHandle == 0 ? PressureCurve(p, m_curve.control2())
                                : PressureCurve(m_curve.control1(), p);
    update();
    emit curveEdited(m_curve);
}

void PressureCurveEditor::mouseReleaseEvent(QMouseEvent *)
{
    m_dragHandle = -1;
}

} // namespace mothion::ui
