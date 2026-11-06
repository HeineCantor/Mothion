#include "ui/compositing/ImagePreview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

namespace {

constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 64.0;
constexpr int kMargin = 12;

} // namespace

ImagePreview::ImagePreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus); // for the keys
}

void ImagePreview::setImage(const QImage &image)
{
    // A picture of another size (another page) starts fitted; the same size, as at every frame, stays where it is.
    if (image.size() != m_image.size()) {
        m_zoom = 1.0;
        m_offset = {};
    }
    m_image = image;
    update();
}

double ImagePreview::fitScale() const
{
    if (m_image.isNull() || m_image.width() < 1 || m_image.height() < 1)
        return 1.0;
    const double w = std::max(1, width() - 2 * kMargin), h = std::max(1, height() - 2 * kMargin);
    return std::min(w / m_image.width(), h / m_image.height());
}

QPointF ImagePreview::imagePointAt(const QPointF &widgetPos) const
{
    const QPointF center(width() / 2.0, height() / 2.0);
    return QPointF(m_image.width() / 2.0, m_image.height() / 2.0) + (widgetPos - center - m_offset) / scale();
}

void ImagePreview::zoomAround(const QPointF &widgetPos, double zoom)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    const QPointF anchor = imagePointAt(widgetPos); // stays under the pointer
    m_zoom = zoom;
    const QPointF center(width() / 2.0, height() / 2.0);
    m_offset = widgetPos - center - (anchor - QPointF(m_image.width() / 2.0, m_image.height() / 2.0)) * scale();
    update();
}

void ImagePreview::fit()
{
    m_zoom = 1.0;
    m_offset = {};
    update();
}

void ImagePreview::actualSize()
{
    zoomAround(QPointF(width() / 2.0, height() / 2.0), 1.0 / fitScale());
}

void ImagePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor window = palette().color(QPalette::Window);
    p.fillRect(rect(), window.lightness() < 128 ? window.lighter(135) : window.darker(115));
    if (m_image.isNull()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Nothing connected to Output"));
        return;
    }
    const double s = scale();
    const QSizeF size(m_image.width() * s, m_image.height() * s);
    const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2) + m_offset, size);
    p.setRenderHint(QPainter::SmoothPixmapTransform, s < 2.0);
    p.drawImage(target, m_image);

    if (std::abs(m_zoom - 1.0) > 0.001) { // where it is not just fitted: how much it is shown
        const QString text = tr("%1%").arg(std::lround(s * 100));
        p.setPen(palette().color(QPalette::Text));
        p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignBottom, text);
    }
}

void ImagePreview::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull())
        return;
    const QPoint a = event->angleDelta();
    const int delta = a.y() != 0 ? a.y() : a.x();
    zoomAround(event->position(), m_zoom * std::pow(1.0015, delta));
    event->accept();
}

void ImagePreview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_dragging = true;
        m_dragFrom = event->pos();
        setCursor(Qt::ClosedHandCursor);
        setFocus(Qt::MouseFocusReason);
        event->accept();
    }
}

void ImagePreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_offset += QPointF(event->pos() - m_dragFrom);
    m_dragFrom = event->pos();
    update();
}

void ImagePreview::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        m_dragging = false;
        unsetCursor();
    }
}

void ImagePreview::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        fit();
}

void ImagePreview::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F || event->key() == Qt::Key_Home
        || (event->key() == Qt::Key_0 && event->modifiers() == Qt::ControlModifier)) {
        fit();
    } else if (event->key() == Qt::Key_1 && event->modifiers() == Qt::ControlModifier) {
        actualSize();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ImagePreview::enterEvent(QEnterEvent *event)
{
    setFocus(Qt::MouseFocusReason); // so the keys work with the pointer over it
    QWidget::enterEvent(event);
}

} // namespace mothion::ui
