#include "ui/compositing/RangeSlider.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

namespace {
constexpr qreal kHandle = 12; // half the width of a handle's grip: the margin of the groove
}

RangeSlider::RangeSlider(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(26);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RangeSlider::setMaximum(int maximum)
{
    m_maximum = std::max(0, maximum);
    m_first = std::clamp(m_first, 0, m_maximum);
    m_last = std::clamp(m_last, m_first, m_maximum);
    update();
}

void RangeSlider::setRange(int first, int last)
{
    m_first = std::clamp(first, 0, m_maximum);
    m_last = std::clamp(last, m_first, m_maximum);
    update();
}

qreal RangeSlider::positionOf(int value) const
{
    const qreal span = std::max(1.0, width() - 2 * kHandle);
    return kHandle + (m_maximum > 0 ? span * value / m_maximum : 0.0);
}

int RangeSlider::valueAt(qreal x) const
{
    const qreal span = std::max(1.0, width() - 2 * kHandle);
    return std::clamp(int(std::lround((x - kHandle) / span * m_maximum)), 0, m_maximum);
}

void RangeSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    const qreal mid = height() / 2.0;
    const QRectF groove(kHandle, mid - 3, std::max(1.0, width() - 2 * kHandle), 6);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Mid));
    p.drawRoundedRect(groove, 3, 3);
    const qreal a = positionOf(m_first), b = positionOf(m_last);
    p.setBrush(pal.color(QPalette::Highlight));
    p.drawRoundedRect(QRectF(a, mid - 3, std::max(0.0, b - a), 6), 3, 3);
    p.setPen(QPen(pal.color(QPalette::Dark), 1));
    p.setBrush(pal.color(QPalette::Button).lighter(130));
    for (const qreal x : {a, b})
        p.drawRoundedRect(QRectF(x - 7, mid - 11, 14, 22), 4, 4);
}

void RangeSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const qreal x = event->position().x();
    // The nearer handle; when they sit together, the one that can move the way the pen goes.
    const qreal da = std::abs(x - positionOf(m_first)), db = std::abs(x - positionOf(m_last));
    m_dragging = da < db || (da == db && x < positionOf(m_first)) ? 1 : 2;
    mouseMoveEvent(event);
}

void RangeSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    const int value = valueAt(event->position().x());
    const int first = m_dragging == 1 ? std::min(value, m_last) : m_first;
    const int last = m_dragging == 2 ? std::max(value, m_first) : m_last;
    if (first == m_first && last == m_last)
        return;
    m_first = first;
    m_last = last;
    update();
    emit rangeChanged(m_first, m_last);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = 0;
}

} // namespace mothion::ui
