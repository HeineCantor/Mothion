#include "ui/panels/timeline/TimelineView.h"

#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"
#include "document/DocumentController.h"
#include "ui/IconFactory.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

using document::DocumentController;

namespace {

const QColor kCursorColor(0xe0, 0x4a, 0x4a);
const QColor kSecondLine(0x60, 0x60, 0x68);
constexpr int kEdgeGrab = 4;          // pixels around a block's right edge that start a resize
constexpr int kExtraFrames = 24;      // shown past the last exposure, room to keep animating

} // namespace

TimelineView::TimelineView(DocumentController *document, QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_document(document)
{
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true); // resize cursor over block edges

    connect(m_document, &DocumentController::structureChanged, this, &TimelineView::onStructureChanged);
    connect(m_document, &DocumentController::currentFrameChanged, this, [this](int frame) {
        ensureFrameVisible(frame);
        viewport()->update();
    });
    connect(m_document, &DocumentController::currentCelChanged, viewport(), qOverload<>(&QWidget::update));
    updateScrollBars();
}

// --- model helpers ---------------------------------------------------------------

int TimelineView::rowCount() const
{
    return m_document->document().celCount();
}

int TimelineView::celIndexForRow(int row) const
{
    return rowCount() - 1 - row;
}

int TimelineView::rowAt(int y) const
{
    if (y < m_metrics.rulerHeight)
        return -1;
    const int row = (y - m_metrics.rulerHeight + m_metrics.scrollY) / m_metrics.rowHeight;
    return row < rowCount() ? row : -1;
}

int TimelineView::frameAt(int x) const
{
    return std::clamp(m_metrics.frameAt(x), 0, displayedFrames() - 1);
}

int TimelineView::displayedFrames() const
{
    const doc::Document &d = m_document->document();
    int last = 0;
    for (int i = 0; i < d.celCount(); ++i)
        last = std::max(last, d.celAt(i).timing().lastFrame());
    if (m_resize)
        last = std::max(last, m_previewTiming.lastFrame());
    return std::max({d.frameCount(), last + kExtraFrames, m_document->currentFrame() + 1});
}

const doc::Timing &TimelineView::timingForPainting(int celIndex) const
{
    if (m_resize && m_resize->celIndex == celIndex)
        return m_previewTiming;
    return m_document->document().celAt(celIndex).timing();
}

std::optional<TimelineView::Resize> TimelineView::edgeAt(const QPoint &pos) const
{
    const int row = rowAt(pos.y());
    if (row < 0 || pos.x() < m_metrics.headerWidth)
        return std::nullopt;
    const int celIndex = celIndexForRow(row);
    for (const doc::Exposure &e : m_document->document().celAt(celIndex).timing().exposures()) {
        if (std::abs(pos.x() - m_metrics.frameX(e.end())) <= kEdgeGrab)
            return Resize{celIndex, e.start, e.length};
    }
    return std::nullopt;
}

QRect TimelineView::eyeRect(int row) const
{
    return QRect(12, m_metrics.rowY(row) + (m_metrics.rowHeight - 16) / 2, 16, 16);
}

QRect TimelineView::lockRect(int row) const
{
    return eyeRect(row).translated(18, 0);
}

QColor TimelineView::colorForCel(int celIndex)
{
    static const QColor colors[] = {QColor(0x4f, 0x8c, 0xd6), QColor(0xd0, 0x8a, 0x4a), QColor(0x5a, 0x9a, 0x6a),
                                    QColor(0xb0, 0x6a, 0xd0), QColor(0xc8, 0x5a, 0x6a), QColor(0x4a, 0xa8, 0xa8)};
    return colors[celIndex % int(std::size(colors))];
}

// --- scrolling --------------------------------------------------------------------

void TimelineView::setFrameWidth(int width)
{
    width = std::clamp(width, 6, 60);
    if (width == m_metrics.frameWidth)
        return;
    // Keep the current frame at the same screen position while zooming.
    const int frame = m_document->currentFrame();
    const int cursorX = m_metrics.frameX(frame);
    m_metrics.frameWidth = width;
    updateScrollBars();
    horizontalScrollBar()->setValue(m_metrics.headerWidth + frame * width - cursorX);
    viewport()->update();
    emit frameWidthChanged(width);
}

void TimelineView::onStructureChanged()
{
    updateScrollBars();
    viewport()->update();
}

void TimelineView::updateScrollBars()
{
    const QSize vp = viewport()->size();
    const int contentW = displayedFrames() * m_metrics.frameWidth;
    const int contentH = rowCount() * m_metrics.rowHeight;
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
    updateScrollBars(); // the frame may be past the previous range
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

// --- painting ---------------------------------------------------------------------

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
    const doc::Document &d = m_document->document();
    const int bottom = viewport()->height();
    const int frames = displayedFrames();
    const int endX = m.frameX(frames);
    const int fps = std::max(1, int(std::lround(d.frameRate().fps())));

    // Row backgrounds; the current cel's row is highlighted.
    for (int row = 0; row < rowCount(); ++row) {
        const bool current = d.celAt(celIndexForRow(row)).id() == m_document->currentCel();
        const QColor bg = current ? palette().highlight().color().darker(260)
                                  : (row % 2 ? palette().alternateBase().color() : palette().base().color());
        p.fillRect(QRect(m.headerWidth, m.rowY(row), endX - m.headerWidth, m.rowHeight), bg);
    }
    // Past the scene length: dimmed.
    p.fillRect(QRect(m.frameX(d.frameCount()), m.rulerHeight, std::max(0, endX - m.frameX(d.frameCount())), bottom),
               QColor(0, 0, 0, 60));
    // Current frame column.
    p.fillRect(QRect(m.frameX(m_document->currentFrame()), m.rulerHeight, m.frameWidth, bottom),
               QColor(kCursorColor.red(), kCursorColor.green(), kCursorColor.blue(), 40));

    // Frame grid: thin line per frame, strong line every second.
    const int first = std::max(0, m.frameAt(m.headerWidth));
    const int last = std::min(frames, m.frameAt(viewport()->width()) + 1);
    for (int f = first; f <= last; ++f) {
        p.setPen(f % fps == 0 ? kSecondLine : palette().mid().color());
        p.drawLine(m.frameX(f), m.rulerHeight, m.frameX(f), bottom);
    }

    // Exposures.
    for (int row = 0; row < rowCount(); ++row) {
        const int celIndex = celIndexForRow(row);
        const doc::Cel &cel = d.celAt(celIndex);
        for (const doc::Exposure &e : timingForPainting(celIndex).exposures()) {
            if (e.end() < first || e.start > last)
                continue;
            const doc::Drawing *drawing = cel.drawing(e.drawing);
            const QString name = drawing ? QString::fromStdString(drawing->name) : QStringLiteral("?");
            paintExposure(p, e, name, colorForCel(celIndex), m.rowY(row));
        }
    }
}

void TimelineView::paintExposure(QPainter &p, const doc::Exposure &e, const QString &name, const QColor &color,
                                 int y) const
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
    const QString shown = p.fontMetrics().elidedText(name, Qt::ElideRight, textRect.width());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);
    const int lineStart = textRect.left() + p.fontMetrics().horizontalAdvance(shown) + 4;
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
    const int last = std::min(displayedFrames() - 1, m.frameAt(viewport()->width()));
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
    const doc::Document &d = m_document->document();
    p.fillRect(QRect(0, m.rulerHeight, m.headerWidth, viewport()->height()), palette().window());
    for (int row = 0; row < rowCount(); ++row) {
        const int celIndex = celIndexForRow(row);
        const doc::Cel &cel = d.celAt(celIndex);
        const QRect r(0, m.rowY(row), m.headerWidth - 1, m.rowHeight);
        if (cel.id() == m_document->currentCel())
            p.fillRect(r, palette().highlight().color().darker(200));
        p.fillRect(QRect(r.left() + 4, r.top() + 6, 4, r.height() - 12), colorForCel(celIndex));
        // The toggles: the icon when on, an empty slot when off.
        p.setPen(palette().mid().color());
        for (const QRect &slot : {eyeRect(row), lockRect(row)})
            p.drawRect(slot.adjusted(0, 0, -1, -1));
        if (cel.properties().visible)
            icon(IconId::Eye).paint(&p, eyeRect(row).adjusted(1, 1, -1, -1));
        if (cel.properties().locked)
            icon(IconId::Lock).paint(&p, lockRect(row).adjusted(1, 1, -1, -1));
        p.setPen(cel.properties().visible ? palette().text().color()
                                          : palette().color(QPalette::Disabled, QPalette::Text));
        p.drawText(r.adjusted(lockRect(row).right() + 6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromStdString(cel.properties().name));
        p.setPen(palette().mid().color());
        p.drawLine(r.bottomLeft(), r.bottomRight());
    }
}

void TimelineView::paintCursor(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const int frame = m_document->currentFrame();
    const int x = m.frameX(frame);
    // Frame number badge in the ruler, line through the cells.
    const QRect badge(x, 1, std::max(m.frameWidth, 26), m.rulerHeight - 3);
    p.fillRect(badge, kCursorColor);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, QString::number(frame + 1));
    p.setPen(QPen(kCursorColor, 1));
    const int cx = x + m.frameWidth / 2;
    p.drawLine(cx, m.rulerHeight, cx, viewport()->height());
}

// --- interaction ------------------------------------------------------------------

void TimelineView::scrubTo(const QPoint &pos, bool selectCel)
{
    if (selectCel) {
        const int row = rowAt(pos.y());
        if (row >= 0)
            m_document->setCurrentCel(m_document->document().celAt(celIndexForRow(row)).id());
    }
    m_document->setCurrentFrame(frameAt(pos.x()));
}

void TimelineView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->position().x() < m_metrics.headerWidth) {
        // In a header: the toggles, or the cel made current.
        const int row = rowAt(int(event->position().y()));
        if (event->button() != Qt::LeftButton || row < 0)
            return;
        const doc::Cel &cel = m_document->document().celAt(celIndexForRow(row));
        const QPoint pos = event->position().toPoint();
        doc::CelProperties p = cel.properties();
        if (eyeRect(row).contains(pos))
            p.visible = !p.visible;
        else if (lockRect(row).contains(pos))
            p.locked = !p.locked;
        else
            m_document->setCurrentCel(cel.id());
        m_document->execute(doc::makeSetCelProperties(m_document->document(), cel.id(), p));
        return;
    }
    const QPoint pos = event->position().toPoint();
    if (auto edge = edgeAt(pos)) {
        m_resize = edge;
        m_previewTiming = m_document->document().celAt(edge->celIndex).timing();
        return;
    }
    m_scrubbing = true;
    scrubTo(pos, pos.y() >= m_metrics.rulerHeight);
}

void TimelineView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_resize) {
        // New length: up to the frame under the pointer, at least one frame.
        const int length = std::max(1, m_metrics.frameAt(pos.x() + m_metrics.frameWidth / 2) - m_resize->start);
        if (length != m_resize->newLength) {
            m_resize->newLength = length;
            m_previewTiming = m_document->document().celAt(m_resize->celIndex).timing();
            m_previewTiming.resizeExposure(m_resize->start, length);
            updateScrollBars();
            viewport()->update();
        }
        return;
    }
    if (m_scrubbing) {
        scrubTo(pos, false);
        return;
    }
    viewport()->setCursor(edgeAt(pos) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineView::mouseReleaseEvent(QMouseEvent *)
{
    m_scrubbing = false;
    if (!m_resize)
        return;
    const Resize r = *m_resize;
    m_resize.reset();
    const doc::CelId cel = m_document->document().celAt(r.celIndex).id();
    m_document->execute(doc::makeResizeExposure(m_document->document(), cel, r.start, r.newLength));
    updateScrollBars();
    viewport()->update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int row = rowAt(int(event->position().y()));
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton && row >= 0 && pos.x() < m_metrics.headerWidth
        && !eyeRect(row).contains(pos) && !lockRect(row).contains(pos)) {
        renameCel(celIndexForRow(row));
        return;
    }
    mousePressEvent(event); // a quick second click on the cells is a click
}

void TimelineView::renameCel(int celIndex)
{
    const doc::Cel &cel = m_document->document().celAt(celIndex);
    const doc::CelId id = cel.id();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename cel"), tr("Name:"), QLineEdit::Normal,
                                               QString::fromStdString(cel.properties().name), &ok);
    const doc::Cel *still = m_document->document().cel(id);
    if (!ok || name.trimmed().isEmpty() || !still)
        return;
    doc::CelProperties props = still->properties();
    props.name = name.trimmed().toStdString();
    m_document->execute(doc::makeSetCelProperties(m_document->document(), id, props));
}

void TimelineView::contextMenuEvent(QContextMenuEvent *event)
{
    const int row = rowAt(event->pos().y());
    if (row < 0 || event->pos().x() < m_metrics.headerWidth)
        return;
    const int frame = frameAt(event->pos().x());
    const doc::Cel &cel = m_document->document().celAt(celIndexForRow(row));
    const doc::CelId celId = cel.id();
    m_document->setCurrentCel(celId);
    m_document->setCurrentFrame(frame);

    QMenu menu(this);
    menu.addAction(tr("New frame"), this, [this, celId, frame] {
        m_document->execute(
            doc::makeNewDrawing(m_document->documentForCommands(), celId, frame, 1, tr("Layer 1").toStdString())
                .command);
    });
    const doc::Drawing *drawing = cel.drawingAt(frame);
    QAction *clear = menu.addAction(tr("Clear frame"), this, [this, celId, frame] {
        m_document->execute(doc::makeSetExposure(m_document->document(), celId, frame, 1, doc::kNoId));
    });
    clear->setEnabled(drawing != nullptr);
    menu.addSeparator();
    menu.addAction(tr("Insert empty frame"), this, [this, celId, frame] {
        m_document->execute(doc::makeInsertEmptyFrames(m_document->document(), celId, frame, 1));
    });
    menu.addAction(tr("Delete frame"), this, [this, celId, frame] {
        m_document->execute(doc::makeRemoveFrames(m_document->document(), celId, frame, 1));
    });
    if (drawing) {
        menu.addSeparator();
        const doc::DrawingId id = drawing->id;
        menu.addAction(tr("Delete frame “%1” (from the whole timeline)").arg(QString::fromStdString(drawing->name)),
                       this, [this, celId, id] {
                           m_document->execute(std::make_unique<doc::RemoveDrawingCommand>(celId, id));
                       });
    }
    menu.exec(event->globalPos());
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
    const int frame = m_document->currentFrame();
    const doc::Document &d = m_document->document();
    const int celIndex = d.indexOf(m_document->currentCel());
    switch (event->key()) {
    case Qt::Key_Left: m_document->setCurrentFrame(frame - 1); break;
    case Qt::Key_Right: m_document->setCurrentFrame(frame + 1); break;
    case Qt::Key_Home: m_document->setCurrentFrame(0); break;
    case Qt::Key_End: m_document->setCurrentFrame(d.frameCount() - 1); break;
    case Qt::Key_Up: // up the list = higher cel
        if (celIndex >= 0 && celIndex + 1 < d.celCount())
            m_document->setCurrentCel(d.celAt(celIndex + 1).id());
        break;
    case Qt::Key_Down:
        if (celIndex > 0)
            m_document->setCurrentCel(d.celAt(celIndex - 1).id());
        break;
    default: QAbstractScrollArea::keyPressEvent(event); return;
    }
}

} // namespace mothion::ui
