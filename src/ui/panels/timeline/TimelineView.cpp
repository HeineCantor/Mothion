#include "ui/panels/timeline/TimelineView.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>

namespace mothion::ui {

namespace {

// Provisional colors, derived from the widget palette where possible.
const QColor kCursorColor(0xe0, 0x4a, 0x4a);
const QColor kSecondLine(0x60, 0x60, 0x68);

} // namespace

TimelineView::TimelineView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setMouseTracking(false);
}

void TimelineView::setTimeline(FakeTimeline timeline)
{
    m_timeline = std::move(timeline);
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, m_timeline.frameCount - 1));
    updateScrollBars();
    viewport()->update();
}

void TimelineView::setCurrentFrame(int frame)
{
    frame = std::clamp(frame, 0, std::max(0, m_timeline.frameCount - 1));
    if (frame == m_currentFrame)
        return;
    m_currentFrame = frame;
    ensureFrameVisible(frame);
    viewport()->update();
    emit currentFrameChanged(frame);
}

void TimelineView::setFrameWidth(int width)
{
    width = std::clamp(width, 6, 60);
    if (width == m_metrics.frameWidth)
        return;
    // Keep the current frame at the same screen position while zooming.
    const int cursorX = m_metrics.frameX(m_currentFrame);
    m_metrics.frameWidth = width;
    updateScrollBars();
    horizontalScrollBar()->setValue(m_metrics.headerWidth + m_currentFrame * width - cursorX);
    viewport()->update();
    emit frameWidthChanged(width);
}

// --- scrolling ---------------------------------------------------------------

void TimelineView::updateScrollBars()
{
    const QSize vp = viewport()->size();
    const int contentW = m_timeline.frameCount * m_metrics.frameWidth;
    const int contentH = int(m_timeline.tracks.size()) * m_metrics.rowHeight;
    const int visibleW = std::max(0, vp.width() - m_metrics.headerWidth);
    const int visibleH = std::max(0, vp.height() - m_metrics.rulerHeight);

    horizontalScrollBar()->setRange(0, std::max(0, contentW - visibleW));
    horizontalScrollBar()->setPageStep(visibleW);
    horizontalScrollBar()->setSingleStep(m_metrics.frameWidth);
    verticalScrollBar()->setRange(0, std::max(0, contentH - visibleH));
    verticalScrollBar()->setPageStep(visibleH);
    verticalScrollBar()->setSingleStep(m_metrics.rowHeight);
}

void TimelineView::ensureFrameVisible(int frame)
{
    const int left = m_metrics.headerWidth;
    const int right = viewport()->width();
    const int x = m_metrics.frameX(frame);
    if (x < left)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (left - x));
    else if (x + m_metrics.frameWidth > right)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (x + m_metrics.frameWidth - right));
}

void TimelineView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void TimelineView::scrollContentsBy(int, int)
{
    m_metrics.scrollX = horizontalScrollBar()->value();
    m_metrics.scrollY = verticalScrollBar()->value();
    viewport()->update();
}

// --- painting ----------------------------------------------------------------

void TimelineView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const QRect vp = viewport()->rect();
    p.fillRect(vp, palette().base());

    const TimelineMetrics &m = m_metrics;
    const QRect cells(m.headerWidth, m.rulerHeight, vp.width() - m.headerWidth, vp.height() - m.rulerHeight);
    const QRect ruler(m.headerWidth, 0, cells.width(), m.rulerHeight);
    const QRect headers(0, m.rulerHeight, m.headerWidth, cells.height());

    p.save();
    p.setClipRect(cells);
    paintCells(p);
    p.restore();

    p.save();
    p.setClipRect(ruler);
    paintRuler(p);
    p.restore();

    p.save();
    p.setClipRect(headers);
    paintHeaders(p);
    p.restore();

    // Corner and separators.
    p.fillRect(QRect(0, 0, m.headerWidth, m.rulerHeight), palette().window());
    p.setPen(palette().dark().color());
    p.drawLine(m.headerWidth - 1, 0, m.headerWidth - 1, vp.height());
    p.drawLine(0, m.rulerHeight - 1, vp.width(), m.rulerHeight - 1);

    p.save();
    p.setClipRect(QRect(m.headerWidth, 0, cells.width(), vp.height()));
    paintCursor(p);
    p.restore();
}

void TimelineView::paintCells(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const int bottom = viewport()->height();
    const int endX = m.frameX(m_timeline.frameCount);

    // Row backgrounds.
    for (int row = 0; row < int(m_timeline.tracks.size()); ++row) {
        const int y = m.rowY(row);
        p.fillRect(QRect(m.headerWidth, y, endX - m.headerWidth, m.rowHeight),
                   row % 2 ? palette().alternateBase() : palette().base());
    }
    // Current frame column.
    p.fillRect(QRect(m.frameX(m_currentFrame), m.rulerHeight, m.frameWidth, bottom),
               QColor(kCursorColor.red(), kCursorColor.green(), kCursorColor.blue(), 40));

    // Frame grid: thin line per frame, strong line every second.
    const int first = std::max(0, m.frameAt(m.headerWidth));
    const int last = std::min(m_timeline.frameCount, m.frameAt(viewport()->width()) + 1);
    for (int f = first; f <= last; ++f) {
        const bool second = m_timeline.fps > 0 && f % m_timeline.fps == 0;
        p.setPen(second ? kSecondLine : palette().mid().color());
        p.drawLine(m.frameX(f), m.rulerHeight, m.frameX(f), bottom);
    }

    // Exposures.
    for (int row = 0; row < int(m_timeline.tracks.size()); ++row) {
        const TimelineTrack &track = m_timeline.tracks[std::size_t(row)];
        for (const Exposure &e : track.exposures)
            if (e.start <= last && e.start + e.length >= first)
                paintExposure(p, e, track.color, m.rowY(row));
    }
}

void TimelineView::paintExposure(QPainter &p, const Exposure &e, const QColor &color, int y) const
{
    const TimelineMetrics &m = m_metrics;
    const QRect block(m.frameX(e.start) + 1, y + 2, e.length * m.frameWidth - 2, m.rowHeight - 4);

    p.fillRect(block, color.darker(170));
    p.fillRect(QRect(block.left(), block.top(), 3, block.height()), color); // exposure start mark
    p.setPen(color.darker(120));
    p.drawRect(block.adjusted(0, 0, -1, -1));

    // Drawing name at the start of the block; the rest of the hold shows a line.
    p.setPen(palette().text().color());
    const QRect textRect = block.adjusted(5, 0, -2, 0);
    const QString name = p.fontMetrics().elidedText(e.drawing, Qt::ElideRight, textRect.width());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, name);
    const int lineStart = textRect.left() + p.fontMetrics().horizontalAdvance(name) + 4;
    if (lineStart < block.right() - 4) {
        p.setPen(color);
        const int cy = block.center().y();
        p.drawLine(lineStart, cy, block.right() - 4, cy);
    }
}

void TimelineView::paintRuler(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    p.fillRect(QRect(m.headerWidth, 0, viewport()->width(), m.rulerHeight), palette().window());

    const int first = std::max(0, m.frameAt(m.headerWidth));
    const int last = std::min(m_timeline.frameCount - 1, m.frameAt(viewport()->width()));
    // Label every frame if there is room, otherwise every 5 / 10.
    const int labelEvery = m.frameWidth >= 22 ? 1 : (m.frameWidth >= 10 ? 5 : 10);
    for (int f = first; f <= last; ++f) {
        const int x = m.frameX(f);
        const int number = f + 1; // frames are shown 1-based, as animators count them
        const bool labelled = number == 1 || number % labelEvery == 0;
        p.setPen(palette().mid().color());
        p.drawLine(x, labelled ? m.rulerHeight - 10 : m.rulerHeight - 5, x, m.rulerHeight);
        if (labelled) {
            p.setPen(palette().text().color());
            p.drawText(QRect(x - 20, 0, m.frameWidth + 40, m.rulerHeight - 8), Qt::AlignCenter,
                       QString::number(number));
        }
    }
}

void TimelineView::paintHeaders(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    p.fillRect(QRect(0, m.rulerHeight, m.headerWidth, viewport()->height()), palette().window());
    for (int row = 0; row < int(m_timeline.tracks.size()); ++row) {
        const TimelineTrack &track = m_timeline.tracks[std::size_t(row)];
        const QRect r(0, m.rowY(row), m.headerWidth - 1, m.rowHeight);
        p.fillRect(QRect(r.left() + 4, r.top() + 6, 4, r.height() - 12), track.color);
        p.setPen(palette().text().color());
        p.drawText(r.adjusted(14, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, track.name);
        p.setPen(palette().mid().color());
        p.drawLine(r.bottomLeft(), r.bottomRight());
    }
}

void TimelineView::paintCursor(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const int x = m.frameX(m_currentFrame);
    // Frame number badge in the ruler, line through the cells.
    const QRect badge(x, 1, std::max(m.frameWidth, 26), m.rulerHeight - 3);
    p.fillRect(badge, kCursorColor);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, QString::number(m_currentFrame + 1));
    p.setPen(QPen(kCursorColor, 1));
    const int cx = x + m.frameWidth / 2;
    p.drawLine(cx, m.rulerHeight, cx, viewport()->height());
}

// --- interaction -------------------------------------------------------------

void TimelineView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().x() >= m_metrics.headerWidth) {
        m_scrubbing = true;
        setCurrentFrame(m_metrics.frameAt(int(event->position().x())));
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_scrubbing)
        setCurrentFrame(m_metrics.frameAt(int(event->position().x())));
}

void TimelineView::mouseReleaseEvent(QMouseEvent *)
{
    m_scrubbing = false;
}

void TimelineView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y() / 120;
        setFrameWidth(m_metrics.frameWidth + steps * 2);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event); // vertical; Shift = horizontal (Qt default)
}

void TimelineView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left: setCurrentFrame(m_currentFrame - 1); break;
    case Qt::Key_Right: setCurrentFrame(m_currentFrame + 1); break;
    case Qt::Key_Home: setCurrentFrame(0); break;
    case Qt::Key_End: setCurrentFrame(m_timeline.frameCount - 1); break;
    default: QAbstractScrollArea::keyPressEvent(event); return;
    }
}

} // namespace mothion::ui
