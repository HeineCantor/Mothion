#include "ui/panels/timeline/TimelineView.h"

#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"
#include "document/DocumentController.h"
#include "ui/IconFactory.h"
#include "ui/Shortcuts.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

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

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] { m_blocksNewer = false; });
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
    if (const auto it = m_movePreview.find(celIndex); it != m_movePreview.end())
        return it->second;
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

bool TimelineView::isSelected(doc::CelId cel, int start) const
{
    return std::find(m_selected.begin(), m_selected.end(), doc::ExposureRef{cel, start}) != m_selected.end();
}

void TimelineView::pruneSelection()
{
    const doc::Document &d = m_document->document();
    std::erase_if(m_selected, [&d](const doc::ExposureRef &ref) {
        const doc::Cel *cel = d.cel(ref.cel);
        const doc::Exposure *e = cel ? cel->timing().exposureAt(ref.start) : nullptr;
        return !e || e->start != ref.start;
    });
}

void TimelineView::onStructureChanged()
{
    pruneSelection();
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
        const bool dragging = m_frameDrag && m_frameDrag->moved;
        const int shift = dragging && !m_frameDrag->blocked ? m_frameDrag->offset : 0;
        for (const doc::Exposure &e : timingForPainting(celIndex).exposures()) {
            if (e.end() < first || e.start > last)
                continue;
            const doc::Drawing *drawing = cel.drawing(e.drawing);
            const QString name = drawing ? QString::fromStdString(drawing->name) : QStringLiteral("?");
            const bool selected = isSelected(cel.id(), e.start - shift);
            paintExposure(p, e, name, colorForCel(celIndex), m.rowY(row), selected);
        }
        // A drop refused: the selected blocks still where they are, and
        // striped where the pen would put them.
        if (dragging && m_frameDrag->blocked)
            for (const doc::Exposure &e : cel.timing().exposures())
                if (isSelected(cel.id(), e.start))
                    paintBlockedExposure(p, {e.start + m_frameDrag->offset, e.length, e.drawing}, m.rowY(row));
    }
}

void TimelineView::paintExposure(QPainter &p, const doc::Exposure &e, const QString &name, const QColor &color,
                                 int y, bool selected) const
{
    const TimelineMetrics &m = m_metrics;
    const QRect block(m.frameX(e.start) + 1, y + 2, e.length * m.frameWidth - 2, m.rowHeight - 4);

    const QColor fill = selected ? color.darker(115) : color.darker(170);
    p.fillRect(block, fill);
    p.fillRect(QRect(block.left(), block.top(), 3, block.height()), color); // exposure start mark
    p.setPen(selected ? palette().highlightedText().color() : color.darker(120));
    p.drawRect(block.adjusted(0, 0, -1, -1));

    // Drawing name at the start of the block; the rest of the hold shows a line. In the color that reads on the
    // block's own (the theme's text color is dark in a light theme, on a dark block).
    p.setPen(fill.lightness() < 150 ? QColor(0xf2, 0xf2, 0xf5) : QColor(0x1e, 0x1e, 0x22));
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

void TimelineView::paintBlockedExposure(QPainter &p, const doc::Exposure &e, int y) const
{
    const TimelineMetrics &m = m_metrics;
    const QRect block(m.frameX(e.start) + 1, y + 2, e.length * m.frameWidth - 2, m.rowHeight - 4);
    const QColor red(220, 60, 60);
    p.save();
    p.fillRect(block, QColor(red.red(), red.green(), red.blue(), 60));
    p.fillRect(block, QBrush(red, Qt::BDiagPattern)); // zebra stripes
    p.setPen(QPen(red, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(block.adjusted(0, 0, -1, -1));
    p.restore();
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
        const bool current = cel.id() == m_document->currentCel();
        if (current)
            p.fillRect(r, palette().highlight().color().darker(200));
        // On the dark highlight of the current cel the text and glyphs are the highlighted text's color.
        const QColor ink = current ? palette().highlightedText().color() : palette().text().color();
        const IconTone tone = current ? IconTone::Selected : IconTone::Plain;
        p.fillRect(QRect(r.left() + 4, r.top() + 6, 4, r.height() - 12), colorForCel(celIndex));
        // The toggles: the icon when on, an empty slot when off.
        p.setPen(current ? palette().highlightedText().color() : palette().placeholderText().color());
        for (const QRect &slot : {eyeRect(row), lockRect(row)})
            p.drawRect(slot.adjusted(0, 0, -1, -1));
        if (cel.properties().visible)
            icon(IconId::Eye, tone).paint(&p, eyeRect(row).adjusted(1, 1, -1, -1));
        if (cel.properties().locked)
            icon(IconId::Lock, tone).paint(&p, lockRect(row).adjusted(1, 1, -1, -1));
        p.setPen(cel.properties().visible ? ink
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
    // On a block: its selection, and maybe a drag of the selected ones.
    const int row = rowAt(pos.y());
    if (row >= 0) {
        const doc::Cel &cel = m_document->document().celAt(celIndexForRow(row));
        const int frame = frameAt(pos.x());
        if (const doc::Exposure *e = cel.timing().exposureAt(frame)) {
            pressFrame({cel.id(), e->start}, frame, pos.x(), event->modifiers());
            return;
        }
        m_selected.clear(); // an empty cell
        m_anchor.reset();
        viewport()->update();
    }
    m_scrubbing = true;
    scrubTo(pos, pos.y() >= m_metrics.rulerHeight);
}

void TimelineView::pressFrame(const doc::ExposureRef &ref, int frame, int x, Qt::KeyboardModifiers modifiers)
{
    FrameDrag drag;
    drag.pressX = x;
    drag.grabbed = ref;
    drag.pressFrame = frame;
    if (modifiers & Qt::ShiftModifier) {
        // The row's blocks from the last one clicked to this one.
        if (m_anchor && m_anchor->cel == ref.cel) {
            const int from = std::min(m_anchor->start, ref.start), to = std::max(m_anchor->start, ref.start);
            for (const doc::Exposure &e : m_document->document().cel(ref.cel)->timing().exposures())
                if (e.start >= from && e.start <= to && !isSelected(ref.cel, e.start))
                    m_selected.push_back({ref.cel, e.start});
        } else if (!isSelected(ref.cel, ref.start)) {
            m_selected.push_back(ref);
        }
    } else if (modifiers & Qt::ControlModifier) {
        if (isSelected(ref.cel, ref.start))
            std::erase(m_selected, ref);
        else
            m_selected.push_back(ref);
        m_anchor = ref;
    } else {
        if (!isSelected(ref.cel, ref.start))
            m_selected = {ref};
        else
            drag.selectOnlyIt = m_selected.size() > 1;
        m_anchor = ref;
    }
    m_frameDrag = drag; // a click or a drag: known on release
    viewport()->update();
}

void TimelineView::dragFrames(int x)
{
    FrameDrag &drag = *m_frameDrag;
    if (!isSelected(drag.grabbed.cel, drag.grabbed.start))
        return; // Ctrl took it out of the selection: nothing to drag
    if (!drag.moved && std::abs(x - drag.pressX) < 4)
        return;
    drag.moved = true;
    // As many frames as the pen went, but none of them before frame 0.
    int earliest = std::numeric_limits<int>::max();
    for (const doc::ExposureRef &ref : m_selected)
        earliest = std::min(earliest, ref.start);
    const int offset = std::max(m_metrics.frameAt(x) - drag.pressFrame, -earliest);
    if (offset == drag.offset && !m_movePreview.empty())
        return;
    drag.offset = offset;
    const doc::Document &d = m_document->document();
    std::map<doc::CelId, std::vector<int>> byCel;
    for (const doc::ExposureRef &ref : m_selected)
        byCel[ref.cel].push_back(ref.start);
    m_movePreview.clear();
    drag.blocked = false;
    for (const auto &[cel, starts] : byCel)
        if (const int index = d.indexOf(cel); index >= 0) {
            drag.blocked = drag.blocked || !doc::exposuresFit(d.celAt(index).timing(), starts, offset);
            m_movePreview[index] = doc::movedExposures(d.celAt(index).timing(), starts, offset);
        }
    if (drag.blocked)
        m_movePreview.clear(); // the timing as it is, the blocks striped where they would go
    updateScrollBars();
    viewport()->update();
}

void TimelineView::dropFrames()
{
    const FrameDrag drag = *m_frameDrag;
    m_frameDrag.reset();
    m_movePreview.clear();
    if (!drag.moved) {
        // A click: the frame under it current (a drag leaves the cursor where it is).
        if (drag.selectOnlyIt)
            m_selected = {drag.grabbed};
        m_document->setCurrentCel(drag.grabbed.cel);
        m_document->setCurrentFrame(drag.pressFrame);
        viewport()->update();
        return;
    }
    if (!isSelected(drag.grabbed.cel, drag.grabbed.start))
        return; // Ctrl took it out of the selection: nothing was dragged
    if (drag.offset != 0 && !drag.blocked) {
        const std::vector<doc::ExposureRef> moving = m_selected;
        for (doc::ExposureRef &ref : m_selected) // where they will be
            ref.start += drag.offset;
        if (drag.grabbed.cel != doc::kNoId)
            m_anchor = doc::ExposureRef{drag.grabbed.cel, drag.grabbed.start + drag.offset};
        m_document->execute(doc::makeMoveExposures(m_document->document(), moving, drag.offset));
    }
    updateScrollBars();
    viewport()->update();
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
    if (m_frameDrag) {
        dragFrames(pos.x());
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
    if (m_frameDrag) {
        dropFrames();
        return;
    }
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

void TimelineView::copyFrames()
{
    // Each selected block, from the first of them.
    const doc::Document &d = m_document->document();
    int first = std::numeric_limits<int>::max();
    for (const doc::ExposureRef &ref : m_selected)
        first = std::min(first, ref.start);
    std::vector<doc::ExposureCopy> copied;
    for (const doc::ExposureRef &ref : m_selected) {
        const doc::Cel *cel = d.cel(ref.cel);
        const doc::Exposure *e = cel ? cel->timing().exposureAt(ref.start) : nullptr;
        if (e && e->start == ref.start)
            copied.push_back({ref.cel, e->drawing, e->start - first, e->length});
    }
    if (!copied.empty()) {
        m_copied = std::move(copied);
        m_blocksNewer = true;
    }
}

bool TimelineView::canPasteFrames() const
{
    // The same drawings again: a try that allocates nothing.
    return !m_copied.empty()
           && doc::makePasteExposures(m_document->documentForCommands(), m_copied, m_document->currentFrame(),
                                      false);
}

bool TimelineView::pasteFrames(bool asCopies)
{
    const int frame = m_document->currentFrame();
    auto command = doc::makePasteExposures(m_document->documentForCommands(), m_copied, frame, asCopies);
    if (!command)
        return false;
    m_document->execute(std::move(command));
    m_selected.clear(); // the pasted ones
    for (const doc::ExposureCopy &c : m_copied)
        m_selected.push_back({c.cel, frame + c.start});
    pruneSelection();
    viewport()->update();
    return true;
}

bool TimelineView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const Shortcuts &keymap = Shortcuts::instance();
        const bool copy = keymap.matches(QStringLiteral("edit.copy"), key);
        const bool paste = keymap.matches(QStringLiteral("edit.paste"), key)
                           || keymap.matches(QStringLiteral("timeline.pasteCopies"), key);
        // The timeline's own when it has blocks to copy, or blocks were copied last; else the window's copy and
        // paste (the pixels of the drawing) work from here too, as from the canvas.
        if ((copy && !m_selected.empty()) || (paste && m_blocksNewer)) {
            event->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(event);
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
    // A block not selected yet: it alone, as the menu's copy acts on it.
    if (const doc::Exposure *e = cel.timing().exposureAt(frame); e && !isSelected(celId, e->start)) {
        m_selected = {{celId, e->start}};
        viewport()->update();
    }

    QMenu menu(this);
    // A new frame here or just after, the ones after it moved on.
    menu.addAction(tr("New frame"), this, [this, celId, frame] { m_document->insertFrame(celId, frame, false); });
    const doc::Drawing *drawing = cel.drawingAt(frame);
    QAction *clear = menu.addAction(tr("Clear frame"), this, [this, celId, frame] {
        m_document->execute(doc::makeSetExposure(m_document->document(), celId, frame, 1, doc::kNoId));
    });
    clear->setEnabled(drawing != nullptr);
    // Two independent frames from here: the hold's second part gets a copy.
    const doc::Exposure *hold = cel.timing().exposureAt(frame);
    QAction *split = menu.addAction(tr("Split frame"), this, [this, celId, frame] {
        m_document->execute(doc::makeSplitDrawing(m_document->documentForCommands(), celId, frame).command);
    });
    split->setEnabled(hold && hold->start < frame);
    menu.addAction(tr("Insert inbetween"), this, [this, celId, frame] { m_document->insertFrame(celId, frame, true); });
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
    menu.addSeparator();
    QAction *copy = menu.addAction(tr("Copy frames"), this, &TimelineView::copyFrames);
    copy->setShortcut(Shortcuts::instance().key(QStringLiteral("edit.copy")));
    copy->setEnabled(!m_selected.empty());
    const bool fits = canPasteFrames();
    QAction *paste = menu.addAction(tr("Paste frames"), this, [this] { pasteFrames(false); });
    paste->setShortcut(Shortcuts::instance().key(QStringLiteral("edit.paste")));
    paste->setToolTip(tr("The same drawings again: a change to one shows on both"));
    paste->setEnabled(fits);
    QAction *pasteCopies = menu.addAction(tr("Paste frames as copies"), this, [this] { pasteFrames(true); });
    pasteCopies->setShortcut(Shortcuts::instance().key(QStringLiteral("timeline.pasteCopies")));
    pasteCopies->setEnabled(fits);
    menu.setToolTipsVisible(true);
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
    // Copy and paste of blocks: the window's keys for those commands (see event()).
    const Shortcuts &keymap = Shortcuts::instance();
    if (keymap.matches(QStringLiteral("edit.copy"), event)) {
        copyFrames();
        return;
    }
    const bool paste = keymap.matches(QStringLiteral("edit.paste"), event);
    if (paste || keymap.matches(QStringLiteral("timeline.pasteCopies"), event)) {
        if (!pasteFrames(!paste))
            QApplication::beep(); // no room here
        return;
    }
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
    case Qt::Key_Escape: // a drag dropped, else the selection cleared
        if (m_frameDrag) {
            m_frameDrag.reset();
            m_movePreview.clear();
        } else {
            m_selected.clear();
        }
        viewport()->update();
        break;
    default: QAbstractScrollArea::keyPressEvent(event); return;
    }
}

} // namespace mothion::ui
