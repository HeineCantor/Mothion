#include "ui/panels/ColorWheel.h"

#include <QConicalGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::ui {

namespace {
constexpr double kRingWidthRatio = 0.14; // ring thickness relative to the radius
constexpr double kPi = std::numbers::pi;
} // namespace

ColorWheel::ColorWheel(QWidget *parent)
    : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    rebuildSquare();
}

QColor ColorWheel::color() const
{
    return QColor::fromHsvF(float(m_hue), float(m_sat), float(m_val));
}

void ColorWheel::setColor(const QColor &color)
{
    const QColor hsv = color.toHsv();
    if (hsv.hsvHueF() >= 0) // keep the old hue for greys
        m_hue = hsv.hsvHueF();
    m_sat = hsv.hsvSaturationF();
    m_val = hsv.valueF();
    rebuildSquare();
    update();
}

QPointF ColorWheel::center() const
{
    return {width() / 2.0, height() / 2.0};
}

double ColorWheel::outerRadius() const
{
    return std::min(width(), height()) / 2.0 - 2.0;
}

double ColorWheel::innerRadius() const
{
    return outerRadius() * (1.0 - kRingWidthRatio);
}

QRectF ColorWheel::squareRect() const
{
    // Largest square inside the ring, with a small gap.
    const double half = (innerRadius() - 4.0) / std::numbers::sqrt2;
    return {center().x() - half, center().y() - half, 2 * half, 2 * half};
}

void ColorWheel::rebuildSquare()
{
    const int side = std::max(1, int(squareRect().width()));
    m_square = QImage(side, side, QImage::Format_RGB32);
    for (int y = 0; y < side; ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_square.scanLine(y));
        const double v = 1.0 - double(y) / std::max(1, side - 1);
        for (int x = 0; x < side; ++x) {
            const double s = double(x) / std::max(1, side - 1);
            line[x] = QColor::fromHsvF(float(m_hue), float(s), float(v)).rgb();
        }
    }
}

void ColorWheel::resizeEvent(QResizeEvent *)
{
    rebuildSquare();
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = center();
    const double ro = outerRadius();
    const double ri = innerRadius();

    // Hue ring: conical gradient through the hues, clipped to an annulus.
    // Gradient position t (counter-clockwise from 3 o'clock) shows hue t.
    QConicalGradient hues(c, 0);
    for (int i = 0; i <= 6; ++i)
        hues.setColorAt(i / 6.0, QColor::fromHsvF(i == 6 ? 0.0f : float(i) / 6.0f, 1, 1));
    QPainterPath ring;
    ring.addEllipse(c, ro, ro);
    ring.addEllipse(c, ri, ri);
    p.fillPath(ring, hues);

    const QRectF sq = squareRect();
    p.drawImage(sq, m_square);

    // Markers: hue on the ring, S/V in the square.
    const double angle = m_hue * 2 * kPi; // conical gradient runs counter-clockwise from 3 o'clock
    const double rm = (ro + ri) / 2;
    const QPointF hueMark(c.x() + rm * std::cos(angle), c.y() - rm * std::sin(angle));
    const QPointF svMark(sq.left() + m_sat * sq.width(), sq.top() + (1.0 - m_val) * sq.height());
    p.setBrush(Qt::NoBrush);
    for (const QPointF &m : {hueMark, svMark}) {
        p.setPen(QPen(Qt::black, 2.5));
        p.drawEllipse(m, 5, 5);
        p.setPen(QPen(Qt::white, 1.2));
        p.drawEllipse(m, 5, 5);
    }
}

void ColorWheel::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const double d = QLineF(center(), pos).length();
    if (squareRect().contains(pos))
        m_drag = Drag::SatVal;
    else if (d >= innerRadius() - 2 && d <= outerRadius() + 2)
        m_drag = Drag::Hue;
    else
        return;
    editAt(pos);
}

void ColorWheel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag != Drag::None)
        editAt(event->position());
}

void ColorWheel::mouseReleaseEvent(QMouseEvent *)
{
    m_drag = Drag::None;
}

void ColorWheel::editAt(const QPointF &pos)
{
    if (m_drag == Drag::Hue) {
        const QPointF d = pos - center();
        double a = std::atan2(-d.y(), d.x()) / (2 * kPi);
        m_hue = a < 0 ? a + 1.0 : a;
        rebuildSquare();
    } else {
        const QRectF sq = squareRect();
        m_sat = std::clamp((pos.x() - sq.left()) / sq.width(), 0.0, 1.0);
        m_val = std::clamp(1.0 - (pos.y() - sq.top()) / sq.height(), 0.0, 1.0);
    }
    update();
    emit colorEdited(color());
}

} // namespace mothion::ui
