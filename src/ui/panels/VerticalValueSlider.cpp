#include "ui/panels/VerticalValueSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

namespace {
constexpr double kHandleRadius = 8.0;
constexpr double kMargin = 4.0;
} // namespace

VerticalValueSlider::VerticalValueSlider(double minimum, double maximum, Scale scale, QWidget *parent)
    : QWidget(parent)
    , m_min(minimum)
    , m_max(maximum)
    , m_scale(scale)
    , m_value(minimum)
    , m_formatter([](double v) { return QString::number(v, 'f', 0); })
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
}

void VerticalValueSlider::setFormatter(Formatter formatter)
{
    m_formatter = std::move(formatter);
    update();
}

void VerticalValueSlider::setValue(double value)
{
    m_value = std::clamp(value, m_min, m_max);
    update();
}

QRectF VerticalValueSlider::badgeRect() const
{
    const double d = width() - 2 * kMargin;
    return {kMargin, kMargin, d, d};
}

QRectF VerticalValueSlider::trackRect() const
{
    const QRectF badge = badgeRect();
    const double top = badge.bottom() + kMargin + kHandleRadius;
    const double bottom = height() - kMargin - kHandleRadius;
    return {width() / 2.0 - 2, top, 4, std::max(1.0, bottom - top)};
}

double VerticalValueSlider::positionOf(double value) const
{
    if (m_scale == Scale::Logarithmic)
        return std::log(value / m_min) / std::log(m_max / m_min);
    return (value - m_min) / (m_max - m_min);
}

double VerticalValueSlider::valueAt(double position) const
{
    position = std::clamp(position, 0.0, 1.0);
    if (m_scale == Scale::Logarithmic)
        return m_min * std::pow(m_max / m_min, position);
    return m_min + (m_max - m_min) * position;
}

void VerticalValueSlider::editTo(double value)
{
    value = std::clamp(value, m_min, m_max);
    if (value == m_value)
        return;
    m_value = value;
    update();
    emit valueEdited(value);
}

void VerticalValueSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    const bool enabled = isEnabled();

    // Value badge.
    const QRectF badge = badgeRect();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.dark());
    p.drawEllipse(badge);
    QFont f = font();
    f.setPointSizeF(std::max(6.5, f.pointSizeF() - 1.5));
    p.setFont(f);
    p.setPen(enabled ? pal.text().color() : pal.color(QPalette::Disabled, QPalette::Text));
    p.drawText(badge, Qt::AlignCenter, m_formatter(m_value));

    // Track: dim above the handle, accent below (the "amount").
    const QRectF track = trackRect();
    const double y = track.bottom() - positionOf(m_value) * track.height();
    p.setBrush(pal.base());
    p.drawRoundedRect(track, 2, 2);
    p.setBrush(enabled ? pal.highlight() : pal.mid());
    p.drawRoundedRect(QRectF(track.left(), y, track.width(), track.bottom() - y), 2, 2);

    // Handle.
    p.setBrush(enabled ? pal.light() : pal.mid());
    p.setPen(QPen(pal.dark().color(), 1));
    p.drawEllipse(QPointF(track.center().x(), y), kHandleRadius, kHandleRadius);
}

void VerticalValueSlider::mousePressEvent(QMouseEvent *event)
{
    mouseMoveEvent(event);
}

void VerticalValueSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QRectF track = trackRect();
    editTo(valueAt((track.bottom() - event->position().y()) / track.height()));
}

void VerticalValueSlider::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    editTo(valueAt(positionOf(m_value) + steps * 0.02));
    event->accept();
}

void VerticalValueSlider::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange)
        update();
    QWidget::changeEvent(event);
}

} // namespace mothion::ui
