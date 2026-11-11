#include "ui/panels/timeline/TimelineView.h"

#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"
#include "document/DocumentController.h"
#include "ui/IconFactory.h"
#include "ui/Shortcuts.h"
#include "ui/Theme.h"

#include <QActionGroup>
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

constexpr int kAudioRowHeight = 44; // the waveform wants more than a cel's row
constexpr int kMinAudioFrames = 4;   // the block's width until the length of the sound is known

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
    connect(m_document, &DocumentController::audioChanged, viewport(), qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::cameraChanged, this, [this] {
        updateScrollBars(); // the row appears with the camera
        viewport()->update();
    });
    connect(m_document, &DocumentController::cameraSelectionChanged, viewport(), qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::markersChanged, viewport(), qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::motionChanged, this, [this] { // a row of keys comes and goes
        updateScrollBars();
        viewport()->update();
    });
    connect(&m_analyzer, &audio::AudioAnalyzer::finished, this,
            [this](const void *key, const audio::Waveform &wave) {
                const auto &track = m_document->audioTrack();
                if (!track || !track->data || track->data.get() != key)
                    return; // the track has changed since
                m_wave = wave;
                m_waveKey = key;
                m_document->setAudioDuration(wave.duration);
                updateScrollBars();
                viewport()->update();
            });
    connect(&m_analyzer, &audio::AudioAnalyzer::failed, this, [this](const void *, const QString &reason) {
        m_wave = {};
        m_waveKey = nullptr;
        viewport()->update();
        emit audioFailed(reason);
    });
    syncAudio();
    updateScrollBars();
}

// --- model helpers ---------------------------------------------------------------

int TimelineView::channelRowCount(int celIndex) const
{
    int n = 0;
    for (doc::Channel c : doc::kChannels)
        n += m_document->document().celAt(celIndex).motion().hasKeys(c) ? 1 : 0;
    return n;
}

std::optional<doc::Channel> TimelineView::channelOfRow(int row) const
{
    const RowRef ref = rowRef(row);
    if (!ref.motion || ref.channel < 0)
        return std::nullopt;
    return doc::kChannels[ref.channel];
}

bool TimelineView::hasKeys(int celIndex) const
{
    return !m_document->document().celAt(celIndex).motion().isEmpty();
}

bool TimelineView::keyframesRowShown(int celIndex) const
{
    return hasKeys(celIndex) && !m_foldedCel.count(m_document->document().celAt(celIndex).id());
}

bool TimelineView::channelRowsShown(int celIndex) const
{
    return keyframesRowShown(celIndex) && !m_folded.count(m_document->document().celAt(celIndex).id());
}

QRect TimelineView::foldRect(int row) const
{
    // The arrow at the right end of a header: in the cel's (when it has keys) it folds "Keyframes" away under it, in the
    // "Keyframes" one the rows of the channels. The channels' own have none.
    const RowRef ref = rowRef(row);
    if (ref.motion && ref.channel >= 0)
        return {};
    if (!ref.motion && !hasKeys(ref.celIndex))
        return {};
    const QRect r(0, m_metrics.rowY(row), m_metrics.headerWidth - 1, m_metrics.rowHeight);
    return QRect(r.right() - 18, r.top() + (r.height() - 14) / 2, 14, 14);
}

void TimelineView::toggleFold(doc::CelId cel, bool keyframesRow)
{
    std::set<doc::CelId> &set = keyframesRow ? m_folded : m_foldedCel;
    if (!set.erase(cel))
        set.insert(cel);
    updateScrollBars();
    viewport()->update();
}

int TimelineView::rowCount() const
{
    const doc::Document &d = m_document->document();
    int rows = d.celCount();
    for (int i = 0; i < d.celCount(); ++i)
        if (keyframesRowShown(i))
            rows += 1 + (channelRowsShown(i) ? channelRowCount(i) : 0); // the row of its keys, and one for each channel
    return rows;
}

TimelineView::RowRef TimelineView::rowRef(int row) const
{
    const doc::Document &d = m_document->document();
    int at = 0;
    for (int i = d.celCount() - 1; i >= 0; --i) {
        if (at++ == row)
            return {i, false};
        if (!keyframesRowShown(i))
            continue;
        if (at++ == row)
            return {i, true, -1};
        if (!channelRowsShown(i))
            continue;
        for (doc::Channel c : doc::kChannels)
            if (d.celAt(i).motion().hasKeys(c) && at++ == row)
                return {i, true, int(c)};
    }
    return {0, false};
}

bool TimelineView::isMotionRow(int row) const
{
    return rowRef(row).motion;
}

int TimelineView::celIndexForRow(int row) const
{
    return rowRef(row).celIndex;
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
    if (row < 0 || pos.x() < m_metrics.headerWidth || isMotionRow(row))
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

QColor TimelineView::colorForCel(int slot)
{
    static const QColor colors[] = {QColor(0x4f, 0x8c, 0xd6), QColor(0xd0, 0x8a, 0x4a), QColor(0x5a, 0x9a, 0x6a),
                                    QColor(0xb0, 0x6a, 0xd0), QColor(0xc8, 0x5a, 0x6a), QColor(0x4a, 0xa8, 0xa8)};
    return colors[slot % int(std::size(colors))];
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

void TimelineView::setScrubbing(bool on)
{
    if (on == m_scrubbing)
        return;
    m_scrubbing = on;
    emit scrubbingChanged(on);
}

void TimelineView::onStructureChanged()
{
    syncAudio();
    pruneSelection();
    updateScrollBars();
    viewport()->update();
}

void TimelineView::updateScrollBars()
{
    const QSize vp = viewport()->size();
    const int contentW = displayedFrames() * m_metrics.frameWidth;
    const int contentH = rowCount() * m_metrics.rowHeight + cameraHeight() + (hasAudio() ? kAudioRowHeight : 0);
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
    paintMarkerLines(p);
    p.restore();

    p.save();
    p.setClipRect(ruler);
    paintRuler(p);
    paintMarkerFlags(p);
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

    p.save();
    p.setClipRect(QRect(0, m.rulerHeight, vp.width(), vp.height() - m.rulerHeight));
    paintSelection(p);
    p.restore();
}

void TimelineView::paintSelection(QPainter &p) const
{
    // The current cel's row (and the camera's, when it is selected) is outlined across the header and the cells.
    const TimelineMetrics &m = m_metrics;
    const doc::Document &d = m_document->document();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ui::selectionOutlineColor(QApplication::palette()), 2.0));
    auto outline = [&](int top) {
        const QRectF bar(0, top, viewport()->width() - 1, m.rowHeight - 1);
        p.drawRect(bar.adjusted(1, 1, -1, -1));
    };
    const std::vector<doc::CelId> chosen = m_document->celSelection(); // the cels chosen together
    for (int row = 0; row < rowCount(); ++row)
        if (!isMotionRow(row) && !m_document->cameraSelected()
            && std::find(chosen.begin(), chosen.end(), d.celAt(celIndexForRow(row)).id()) != chosen.end())
            outline(m.rowY(row));
    if (m_celDrag && m_celDrag->moving) { // where the chosen cels would go
        const int y = celDropY(m_celDrag->insertAt);
        p.setPen(QPen(kCursorColor, 3.0));
        p.drawLine(0, y, viewport()->width(), y);
    }
    if (m_document->camera() && m_document->cameraSelected())
        outline(cameraTop());
}

void TimelineView::paintCells(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const doc::Document &d = m_document->document();
    const int bottom = viewport()->height();
    const int frames = displayedFrames();
    const int endX = m.frameX(frames);
    const int fps = std::max(1, int(std::lround(d.frameRate().fps())));

    // Row backgrounds (the current cel's row is outlined: paintSelection()).
    for (int row = 0; row < rowCount(); ++row) {
        const QColor bg = row % 2 ? palette().alternateBase().color() : palette().base().color();
        p.fillRect(QRect(m.headerWidth, m.rowY(row), endX - m.headerWidth, m.rowHeight), bg);
    }
    if (hasAudio()) // the audio row, under the cels
        p.fillRect(QRect(m.headerWidth, audioTop(), endX - m.headerWidth, kAudioRowHeight), palette().alternateBase());
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
        if (isMotionRow(row)) {
            paintMotionRow(p, celIndex, row);
            continue;
        }
        const doc::Cel &cel = d.celAt(celIndex);
        const bool dragging = m_frameDrag && m_frameDrag->moved;
        const int shift = dragging && !m_frameDrag->blocked ? m_frameDrag->offset : 0;
        for (const doc::Exposure &e : timingForPainting(celIndex).exposures()) {
            if (e.end() < first || e.start > last)
                continue;
            const doc::Drawing *drawing = cel.drawing(e.drawing);
            const QString name = drawing ? QString::fromStdString(drawing->name) : QStringLiteral("?");
            const bool selected = isSelected(cel.id(), e.start - shift);
            paintExposure(p, e, name, colorForCel(colorSlot(cel.id())), m.rowY(row), selected);
        }
        // A drop refused: the selected blocks still where they are, and
        // striped where the pen would put them.
        if (dragging && m_frameDrag->blocked)
            for (const doc::Exposure &e : cel.timing().exposures())
                if (isSelected(cel.id(), e.start))
                    paintBlockedExposure(p, {e.start + m_frameDrag->offset, e.length, e.drawing}, m.rowY(row));
    }
    paintCamera(p);
    paintAudio(p);
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

// --- camera -----------------------------------------------------------------------

bool TimelineView::hasCamera() const
{
    return m_document->camera().has_value();
}

int TimelineView::cameraHeight() const
{
    return hasCamera() ? m_metrics.rowHeight : 0;
}

int TimelineView::cameraTop() const
{
    return m_metrics.rowY(rowCount());
}

QRect TimelineView::cameraEyeRect() const
{
    return QRect(12, cameraTop() + 3, 16, 16);
}

bool TimelineView::inCameraRow(int y) const
{
    return hasCamera() && y >= m_metrics.rulerHeight && y >= cameraTop() && y < cameraTop() + cameraHeight();
}

namespace {

QColor motionColor() { return QColor(240, 178, 61); }

} // namespace

int TimelineView::motionKeyAt(const QPoint &pos, int row) const
{
    if (!isMotionRow(row) || pos.x() < m_metrics.headerWidth)
        return -1;
    const doc::CelMotion &motion = m_document->document().celAt(celIndexForRow(row)).motion();
    std::vector<int> frames;
    if (const auto channel = channelOfRow(row)) {
        for (const doc::CelKey &key : motion.keys(*channel))
            frames.push_back(key.frame);
    } else {
        frames = motion.keyFrames();
    }
    int best = -1, nearest = 8;
    for (const int frame : frames) {
        const int distance = std::abs(pos.x() - (m_metrics.frameX(frame) + m_metrics.frameWidth / 2));
        if (distance < nearest) {
            nearest = distance;
            best = frame;
        }
    }
    return best;
}

void TimelineView::paintMotionHeader(QPainter &p, int celIndex, int row) const
{
    const TimelineMetrics &m = m_metrics;
    const doc::Cel &cel = m_document->document().celAt(celIndex);
    const QRect r(0, m.rowY(row), m.headerWidth - 1, m.rowHeight);
    p.fillRect(r, palette().window());
    p.fillRect(QRect(r.left() + 4, r.top() + 6, 4, r.height() - 12), motionColor());
    QFont small = font();
    small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1.0));
    p.setFont(small);
    p.setPen(cel.properties().visible ? palette().placeholderText().color()
                                      : palette().color(QPalette::Disabled, QPalette::Text));
    const auto channel = channelOfRow(row);
    if (channel) {
        const QString name = *channel == doc::Channel::Position   ? tr("Position")
                             : *channel == doc::Channel::Rotation ? tr("Rotation")
                             : *channel == doc::Channel::Scale    ? tr("Scale")
                                                                  : tr("Opacity");
        p.drawText(r.adjusted(40, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
    } else {
        p.drawText(r.adjusted(28, 0, -22, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("Keyframes"));
        paintFoldArrow(p, foldRect(row), !channelRowsShown(celIndex));
    }
    p.setFont(font());
    p.setPen(palette().mid().color());
    p.drawLine(r.bottomLeft(), r.bottomRight());
}

void TimelineView::paintFoldArrow(QPainter &p, const QRect &r, bool folded) const
{
    const QPointF c = r.center();
    QPolygonF triangle;
    if (folded) // pointing right: the keys are folded away
        triangle << QPointF(c.x() - 2.5, c.y() - 4) << QPointF(c.x() + 3.5, c.y()) << QPointF(c.x() - 2.5, c.y() + 4);
    else // pointing down: the keys are open
        triangle << QPointF(c.x() - 4, c.y() - 2.5) << QPointF(c.x() + 4, c.y() - 2.5) << QPointF(c.x(), c.y() + 3.5);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(palette().placeholderText().color());
    p.drawPolygon(triangle);
    p.restore();
}

void TimelineView::paintMotionRow(QPainter &p, int celIndex, int row) const
{
    const TimelineMetrics &m = m_metrics;
    const doc::Cel &cel = m_document->document().celAt(celIndex);
    const int y = m.rowY(row);
    const int midY = y + m.rowHeight / 2;
    const QColor ink = motionColor();
    const auto channel = channelOfRow(row);
    // A channel's row: its keys, joined where one goes to the next; the "Keyframes" row: a diamond where any has one.
    std::vector<int> frames;
    if (channel) {
        const auto &keys = cel.motion().keys(*channel);
        for (std::size_t i = 0; i < keys.size(); ++i) {
            frames.push_back(keys[i].frame);
            if (i + 1 < keys.size() && keys[i].easing.kind != doc::Easing::Kind::Hold) {
                p.setPen(QPen(ink.darker(110), 1.5));
                p.drawLine(m.frameX(keys[i].frame) + m.frameWidth / 2, midY, m.frameX(keys[i + 1].frame) + m.frameWidth / 2, midY);
            }
        }
    } else {
        frames = cel.motion().keyFrames();
    }
    const int current = m_document->currentFrame();
    p.setRenderHint(QPainter::Antialiasing);
    for (const int keyFrame : frames) {
        int frame = keyFrame;
        const bool dragged = m_motionDrag && m_motionDrag->cel == cel.id() && m_motionDrag->from == frame
                             && m_motionDrag->channel == channel;
        if (dragged)
            frame = m_motionDrag->to;
        // The key of a channel alone shows chosen on its channel's row and on the "Keyframes" one; a click on the latter
        // chooses the keys of the frame, on every row.
        const bool chosen = m_selectedKey && m_selectedKey->first == cel.id() && m_selectedKey->second == keyFrame
                            && (!channel || !m_selectedChannel || *m_selectedChannel == *channel);
        const int cx = m.frameX(frame) + m.frameWidth / 2;
        const int r = keyFrame == current || dragged || chosen ? 7 : 5;
        const QPolygon diamond({QPoint(cx, midY - r), QPoint(cx + r, midY), QPoint(cx, midY + r), QPoint(cx - r, midY)});
        p.setPen(QPen(chosen || keyFrame == current ? Qt::white : ink.darker(170), chosen ? 2 : 1));
        p.setBrush(dragged ? ink.lighter(130) : ink);
        p.drawPolygon(diamond);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void TimelineView::showMotionMenu(const QPoint &globalPos, int celIndex, int frame, std::optional<doc::Channel> channel)
{
    const doc::Cel &cel = m_document->document().celAt(celIndex);
    const doc::CelId id = cel.id();
    const doc::CelMotion &motion = cel.motion();
    // The key the curve shown is of: the channel's, or the first of the channels that have one on the frame.
    const doc::CelKey *key = nullptr;
    for (doc::Channel c : doc::kChannels)
        if (!key && (!channel || *channel == c))
            key = motion.keyAt(c, frame);
    QMenu menu(this);
    if (key) {
        QMenu *easing = menu.addMenu(tr("To the next key"));
        auto add = [&](const QString &text, const doc::Easing &e) {
            QAction *action = easing->addAction(text, this, [this, id, frame, e, channel] {
                m_document->setCelKeyEasing(id, frame, e, channel);
            });
            action->setCheckable(true);
            action->setChecked(key->easing == e);
        };
        add(tr("Linear"), doc::Easing::linear());
        add(tr("Hold"), doc::Easing::hold());
        add(tr("Ease in"), doc::Easing::easeIn());
        add(tr("Ease out"), doc::Easing::easeOut());
        add(tr("Ease in and out"), doc::Easing::easeInOut());
        menu.addAction(channel ? tr("Delete keyframe") : tr("Delete keyframes"), this,
                       [this, id, frame, channel] { m_document->removeCelKey(id, frame, channel); });
    }
    if (channel) {
        if (!key)
            menu.addAction(tr("Add keyframe here"), this, [this, id, frame, channel] {
                m_document->setCelChannelKey(id, *channel, frame, m_document->celPoseAt(id, frame));
            });
    } else {
        // On the "Keyframes" row: all the channels that move, or one of the others.
        if (!motion.hasKeyAt(frame))
            menu.addAction(tr("Add keyframe here"), this,
                           [this, id, frame] { m_document->setCelKey(id, frame, m_document->celPoseAt(id, frame)); });
        QMenu *only = nullptr;
        for (doc::Channel c : doc::kChannels) {
            if (motion.keyAt(c, frame))
                continue;
            if (!only)
                only = menu.addMenu(tr("Add a keyframe of"));
            const QString name = c == doc::Channel::Position   ? tr("Position")
                                 : c == doc::Channel::Rotation ? tr("Rotation")
                                 : c == doc::Channel::Scale    ? tr("Scale")
                                                               : tr("Opacity");
            only->addAction(name, this, [this, id, frame, c] {
                m_document->setCelChannelKey(id, c, frame, m_document->celPoseAt(id, frame));
            });
        }
    }
    menu.addSeparator();
    menu.addAction(tr("Delete all keyframes of the cel"), this, [this, id] { m_document->clearCelMotion(id); });
    menu.exec(globalPos);
}

void TimelineView::paintCamera(QPainter &p) const
{
    const auto &camera = m_document->camera();
    if (!camera)
        return;
    const TimelineMetrics &m = m_metrics;
    const int top = cameraTop();
    const QRect row(m.headerWidth, top, viewport()->width() - m.headerWidth, m.rowHeight);
    p.fillRect(row, palette().base().color());
    p.setPen(palette().mid().color());
    p.drawLine(row.left(), row.bottom(), row.right(), row.bottom());

    const QColor red(235, 55, 55);
    const QColor ink = camera->visible ? red : QColor(150, 150, 150);
    const int midY = top + m.rowHeight / 2;
    // A line from a key to the next, unless the first holds.
    const auto &keys = camera->keys();
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i].easing.kind == doc::Easing::Kind::Hold)
            continue;
        p.setPen(QPen(ink, 1.5));
        p.drawLine(m.frameX(keys[i].frame) + m.frameWidth / 2, midY, m.frameX(keys[i + 1].frame) + m.frameWidth / 2, midY);
    }
    const int current = m_document->currentFrame();
    p.setRenderHint(QPainter::Antialiasing);
    for (const doc::CameraKey &key : keys) {
        int frame = key.frame;
        const bool dragged = m_cameraDrag && m_cameraDrag->from == frame;
        if (dragged)
            frame = m_cameraDrag->to;
        const int cx = m.frameX(frame) + m.frameWidth / 2;
        const int r = key.frame == current || dragged ? 7 : 5;
        const QPolygon diamond({QPoint(cx, midY - r), QPoint(cx + r, midY), QPoint(cx, midY + r), QPoint(cx - r, midY)});
        p.setPen(QPen(key.frame == current ? Qt::white : ink.darker(150), 1));
        p.setBrush(dragged ? ink.lighter(130) : ink);
        p.drawPolygon(diamond);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void TimelineView::paintCameraHeader(QPainter &p) const
{
    const auto &camera = m_document->camera();
    if (!camera)
        return;
    const TimelineMetrics &m = m_metrics;
    const QRect r(0, cameraTop(), m.headerWidth - 1, m.rowHeight);
    const QColor ink = palette().text().color();
    const IconTone tone = IconTone::Plain;
    p.fillRect(QRect(r.left() + 4, r.top() + 6, 4, r.height() - 12), QColor(235, 55, 55));
    p.setPen(palette().placeholderText().color());
    p.drawRect(cameraEyeRect().adjusted(0, 0, -1, -1));
    if (camera->visible)
        icon(IconId::Eye, tone).paint(&p, cameraEyeRect().adjusted(1, 1, -1, -1));
    p.setPen(camera->visible ? ink : palette().color(QPalette::Disabled, QPalette::Text));
    p.drawText(r.adjusted(cameraEyeRect().right() + 8, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("Camera"));
    p.setPen(palette().mid().color());
    p.drawLine(r.bottomLeft(), r.bottomRight());
}

void TimelineView::showCameraMenu(const QPoint &globalPos, int frame)
{
    const auto &camera = m_document->camera();
    if (!camera)
        return;
    QMenu menu(this);
    if (const doc::CameraKey *key = camera->keyAt(frame)) {
        QMenu *easing = menu.addMenu(tr("To the next key"));
        auto add = [&](const QString &text, const doc::Easing &e) {
            QAction *action = easing->addAction(text, this, [this, frame, e] { m_document->setCameraEasing(frame, e); });
            action->setCheckable(true);
            action->setChecked(key->easing == e);
        };
        add(tr("Linear"), doc::Easing::linear());
        add(tr("Hold"), doc::Easing::hold());
        add(tr("Ease in"), doc::Easing::easeIn());
        add(tr("Ease out"), doc::Easing::easeOut());
        add(tr("Ease in and out"), doc::Easing::easeInOut());
        menu.addAction(tr("Delete keyframe"), this, [this, frame] { m_document->removeCameraKey(frame); });
    } else {
        menu.addAction(tr("Add keyframe here"), this,
                       [this, frame] { m_document->setCameraKey(frame, m_document->camera()->poseAt(frame)); });
    }
    menu.addSeparator();
    menu.addAction(camera->visible ? tr("Turn the camera off") : tr("Turn the camera on"), this,
                   [this] { m_document->setCameraVisible(!m_document->camera()->visible); });
    menu.addAction(tr("Delete the camera"), this, [this] { m_document->removeCamera(); });
    menu.exec(globalPos);
}

// --- audio track ------------------------------------------------------------------

bool TimelineView::hasAudio() const
{
    return m_document->audioTrack().has_value();
}

int TimelineView::audioTop() const
{
    return cameraTop() + cameraHeight();
}

QRect TimelineView::audioHeaderRect() const
{
    return QRect(0, audioTop(), m_metrics.headerWidth - 1, kAudioRowHeight);
}

QRect TimelineView::audioMuteRect() const
{
    return QRect(12, audioTop() + 4, 16, 16);
}

QRect TimelineView::audioScrubRect() const
{
    return audioMuteRect().translated(18, 0);
}

QRect TimelineView::audioVolumeRect() const
{
    return QRect(12, audioTop() + 26, 90, 12);
}

float TimelineView::audioVolumeAt(int x) const
{
    const QRect r = audioVolumeRect();
    return std::clamp(float(x - r.left()) / float(std::max(1, r.width() - 1)), 0.0f, 1.0f);
}

bool TimelineView::inAudioRow(int y) const
{
    return hasAudio() && y >= m_metrics.rulerHeight && y >= audioTop() && y < audioTop() + kAudioRowHeight;
}

double TimelineView::audioSeconds() const
{
    const auto &track = m_document->audioTrack();
    if (!track)
        return 0.0;
    return track->duration > 0.0 ? track->duration : m_wave.duration;
}

QRect TimelineView::audioBlockRect(int offset) const
{
    const double fps = m_document->document().frameRate().fps();
    const double seconds = audioSeconds();
    const int width = seconds > 0.0 ? std::max(1, int(std::lround(seconds * fps * m_metrics.frameWidth)))
                                    : kMinAudioFrames * m_metrics.frameWidth;
    return QRect(m_metrics.frameX(offset), audioTop() + 2, width, kAudioRowHeight - 4);
}

void TimelineView::paintAudio(QPainter &p) const
{
    const auto &track = m_document->audioTrack();
    if (!track)
        return;
    const TimelineMetrics &m = m_metrics;
    const int offset = m_audioDrag ? m_audioDrag->offset : track->offset;
    const QRect row(m.headerWidth, audioTop(), viewport()->width() - m.headerWidth, kAudioRowHeight);
    p.setPen(palette().mid().color());
    p.drawLine(row.left(), row.top(), row.right(), row.top());

    const QRect block = audioBlockRect(offset);
    const QColor color(0x4a, 0xa8, 0xa8);
    const QColor fill = track->muted ? color.darker(300) : color.darker(190);
    p.fillRect(block, fill);
    p.setPen(track->muted ? color.darker(220) : color.darker(110));
    p.drawRect(block.adjusted(0, 0, -1, -1));

    // The waveform: a line for each column of pixels, as long as the largest peak of the slices under it.
    const double fps = m_document->document().frameRate().fps();
    const double pixelsPerSecond = fps * m.frameWidth;
    if (!m_wave.isEmpty() && m_waveKey == track->data.get() && pixelsPerSecond > 0.0) {
        const int middle = block.center().y();
        const double half = (block.height() - 4) / 2.0;
        const QRect visible = block & row;
        p.setPen(track->muted ? color.darker(160) : QColor(0xd8, 0xf2, 0xf2));
        for (int x = visible.left(); x <= visible.right(); ++x) {
            const double from = (x - block.left()) / pixelsPerSecond;
            const double to = (x + 1 - block.left()) / pixelsPerSecond;
            const int first = std::max(0, int(from / audio::Waveform::kPeakSeconds));
            const int last = std::min(int(m_wave.peaks.size()) - 1, std::max(first, int(to / audio::Waveform::kPeakSeconds)));
            float peak = 0.0f;
            for (int i = first; i <= last; ++i)
                peak = std::max(peak, m_wave.peaks[std::size_t(i)]);
            const int h = std::max(1, int(std::lround(peak * track->volume * half)));
            p.drawLine(x, middle - h, x, middle + h);
        }
    } else if (m_analyzer.isRunning()) {
        p.setPen(palette().text().color());
        p.drawText(block.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("Reading the sound…"));
    }
}

void TimelineView::paintAudioHeader(QPainter &p) const
{
    const auto &track = m_document->audioTrack();
    if (!track)
        return;
    const QRect r = audioHeaderRect();
    p.fillRect(r.adjusted(0, 0, 1, 0), palette().window());
    p.fillRect(QRect(r.left() + 4, r.top() + 8, 4, r.height() - 16), QColor(0x4a, 0xa8, 0xa8));
    const QColor textColor = palette().text().color();
    const QColor offColor = palette().color(QPalette::Disabled, QPalette::Text);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // The mute button, in the slot of a cel's eye: a loudspeaker, crossed out when the sound is off.
    const QRect mute = audioMuteRect();
    p.setPen(palette().placeholderText().color());
    p.drawRect(mute.adjusted(0, 0, -1, -1));
    const QColor muteInk = track->muted ? offColor : textColor;
    p.setPen(QPen(muteInk, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(muteInk);
    const QPointF c = mute.center() + QPointF(-1.5, 0);
    QPolygonF cone;
    cone << QPointF(c.x() - 3, c.y() - 2) << QPointF(c.x(), c.y() - 2) << QPointF(c.x() + 3, c.y() - 5)
         << QPointF(c.x() + 3, c.y() + 5) << QPointF(c.x(), c.y() + 2) << QPointF(c.x() - 3, c.y() + 2);
    p.drawPolygon(cone);
    p.setBrush(Qt::NoBrush);
    if (track->muted)
        p.drawLine(QPointF(mute.left() + 3, mute.top() + 3), QPointF(mute.right() - 3, mute.bottom() - 3));
    else
        p.drawArc(QRectF(c.x() + 2, c.y() - 3, 5, 6), -60 * 16, 120 * 16);

    // The scrubbing button, in the slot of a cel's lock: bars of a waveform with the play head through them,
    // crossed out when the sound is not heard while scrubbing.
    const QRect scrub = audioScrubRect();
    p.setPen(palette().placeholderText().color());
    p.drawRect(scrub.adjusted(0, 0, -1, -1));
    const QColor scrubInk = track->scrub ? textColor : offColor;
    p.setPen(QPen(scrubInk, 1.4, Qt::SolidLine, Qt::RoundCap));
    const QPointF m(scrub.center().x(), scrub.center().y());
    for (const auto &[dx, half] : {std::pair{-4.5, 2.0}, std::pair{-2.0, 4.0}, std::pair{2.0, 3.0}, std::pair{4.5, 1.5}})
        p.drawLine(QPointF(m.x() + dx, m.y() - half), QPointF(m.x() + dx, m.y() + half));
    p.setPen(QPen(scrubInk, 1.0));
    p.drawLine(QPointF(m.x() + 0.2, scrub.top() + 2), QPointF(m.x() + 0.2, scrub.bottom() - 2)); // the play head
    if (!track->scrub) {
        p.setPen(QPen(offColor, 1.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(scrub.left() + 3, scrub.top() + 3), QPointF(scrub.right() - 3, scrub.bottom() - 3));
    }

    // The name, after the buttons.
    p.setPen(track->muted ? offColor : textColor);
    const QRect text(scrub.right() + 6, r.top() + 3, r.right() - scrub.right() - 10, 17);
    p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(QString::fromStdString(track->name), Qt::ElideRight, text.width()));

    // The volume slider: a groove filled up to the volume, a handle, and the number.
    const QRect slider = audioVolumeRect();
    const int grooveY = slider.center().y();
    p.setPen(QPen(palette().mid().color().darker(130), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(slider.left() + 4, grooveY, slider.right() - 4, grooveY);
    const double handleX = double(slider.left() + 4) + double(track->volume) * double(slider.width() - 8);
    p.setPen(QPen(palette().highlight().color(), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(slider.left() + 4, grooveY), QPointF(handleX, grooveY));
    p.setPen(QPen(QColor(0x4a, 0x4a, 0x4a), 1));
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(handleX, grooveY), 4.5, 4.5);
    p.restore();
    p.setPen(palette().placeholderText().color());
    p.drawText(QRect(slider.right() + 3, slider.top() - 2, r.right() - slider.right() - 4, slider.height() + 4),
               Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("%1").arg(int(std::lround(track->volume * 100.0f))));
    p.setPen(palette().mid().color());
    p.drawLine(r.bottomLeft(), r.bottomRight());
}

void TimelineView::syncAudio()
{
    const auto &track = m_document->audioTrack();
    const void *key = track && track->data ? track->data.get() : nullptr;
    if (key == m_waveKey)
        return;
    m_wave = {};
    m_waveKey = nullptr;
    if (key)
        m_analyzer.analyze(track->data, key); // the answer comes in the background: finished() or failed()
}

void TimelineView::showAudioMenu(const QPoint &globalPos)
{
    const auto &track = m_document->audioTrack();
    if (!track)
        return;
    QMenu menu(this);
    menu.addAction(tr("Replace the audio…"), this, &TimelineView::audioImportRequested);
    QAction *mute = menu.addAction(tr("Mute"), this, [this](bool on) { m_document->setAudioMuted(on); });
    mute->setCheckable(true);
    mute->setChecked(track->muted);
    QAction *scrub = menu.addAction(tr("Hear it while scrubbing"), this, [this](bool on) { m_document->setAudioScrub(on); });
    scrub->setCheckable(true);
    scrub->setChecked(track->scrub);
    scrub->setToolTip(tr("A short burst of the sound as the timeline is scrubbed or stepped"));
    menu.addAction(tr("Start at the first frame"), this, [this] { m_document->setAudioOffset(0); })
        ->setEnabled(track->offset != 0);
    menu.addSeparator();
    menu.addAction(tr("Remove the audio"), this, [this] { m_document->removeAudio(); });
    menu.exec(globalPos);
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
            p.drawText(QRect(x - 20, m.markerStrip, m.frameWidth + 40, m.rulerHeight - m.markerStrip - 8), Qt::AlignCenter,
                       QString::number(number));
        }
    }
}

namespace {

QColor markerColor() { return QColor(0xe0, 0x9a, 0x2b); }

} // namespace

void TimelineView::paintMarkerLines(QPainter &p) const
{
    // A thin line down the cells at each marker: where it is, as the eye runs along the rows.
    const TimelineMetrics &m = m_metrics;
    QColor line = markerColor();
    line.setAlpha(150);
    p.setPen(QPen(line, 1, Qt::DashLine));
    for (const auto &[frame, name] : m_document->markers()) {
        int at = frame;
        if (m_markerDrag && m_markerDrag->from == frame)
            at = m_markerDrag->to;
        const int x = m.frameX(at) + m.frameWidth / 2; // the middle of the slot, where the cursor's line is
        if (x < m.headerWidth - m.frameWidth || x > viewport()->width())
            continue;
        p.drawLine(x, m.rulerHeight, x, viewport()->height());
    }
}

void TimelineView::paintMarkerFlags(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const auto &markers = m_document->markers();
    // Where each flag is now (a dragged one is where it would drop), with its name.
    std::vector<std::pair<int, QString>> shown;
    for (const auto &[frame, name] : markers) {
        const int at = m_markerDrag && m_markerDrag->from == frame ? m_markerDrag->to : frame;
        shown.emplace_back(at, QString::fromStdString(name));
    }
    std::sort(shown.begin(), shown.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QFont small = font();
    small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1.5));
    p.setFont(small);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const int x = m.frameX(shown[i].first) + m.frameWidth / 2; // the pole is at the middle of the slot, as the cursor
        if (x + flagWidth() < m.headerWidth || x > viewport()->width())
            continue;
        // A pennant, flying to the right of its pole.
        const QRectF flag(x, 1.5, flagWidth(), m.markerStrip - 3);
        QPainterPath shape;
        shape.moveTo(flag.topLeft());
        shape.lineTo(flag.topRight());
        shape.lineTo(flag.right() - 3, flag.center().y());
        shape.lineTo(flag.bottomRight());
        shape.lineTo(flag.bottomLeft());
        shape.closeSubpath();
        p.setPen(shown[i].first == m_document->currentFrame() ? palette().text().color() : markerColor().darker(140));
        p.setBrush(markerColor());
        p.drawPath(shape);
        // The name, to the next flag (or the edge).
        if (!shown[i].second.isEmpty()) {
            const int left = x + int(flag.width()) + 3;
            const int right = i + 1 < shown.size() ? m.frameX(shown[i + 1].first) + m.frameWidth / 2 - 2 : viewport()->width();
            if (right - left > 12) {
                p.setPen(palette().text().color());
                p.drawText(QRect(left, 0, right - left, m.markerStrip), Qt::AlignVCenter | Qt::AlignLeft,
                           QFontMetrics(small).elidedText(shown[i].second, Qt::ElideRight, right - left));
            }
        }
    }
    p.setBrush(Qt::NoBrush);
}

int TimelineView::flagWidth() const { return std::max(8, m_metrics.frameWidth - 3); }

std::optional<int> TimelineView::markerAt(const QPoint &pos) const
{
    if (pos.y() >= m_metrics.markerStrip || pos.x() < m_metrics.headerWidth)
        return std::nullopt;
    // The flag and its pole: from just left of the pole (the middle of the slot) to the end of the pennant.
    std::optional<int> found;
    for (const auto &[frame, name] : m_document->markers()) {
        const int pole = m_metrics.frameX(frame) + m_metrics.frameWidth / 2;
        if (pos.x() >= pole - 3 && pos.x() <= pole + flagWidth() && (!found || std::abs(pos.x() - pole) < std::abs(pos.x() - (m_metrics.frameX(*found) + m_metrics.frameWidth / 2))))
            found = frame;
    }
    return found;
}

void TimelineView::renameMarker(int frame)
{
    const auto it = m_document->markers().find(frame);
    if (it == m_document->markers().end())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Marker"), tr("Name of the marker on frame %1:").arg(frame + 1),
                                               QLineEdit::Normal, QString::fromStdString(it->second), &ok);
    if (ok)
        m_document->setMarker(frame, name.trimmed());
}

void TimelineView::showMarkerMenu(const QPoint &global, int frame)
{
    QMenu menu(this);
    if (m_document->markers().count(frame)) {
        menu.addAction(tr("Rename marker…"), this, [this, frame] { renameMarker(frame); });
        menu.addAction(tr("Delete marker"), this, [this, frame] { m_document->removeMarker(frame); });
    } else {
        menu.addAction(tr("Add marker here"), this, [this, frame] {
            m_document->setMarker(frame, QString());
            renameMarker(frame); // asked for a name, which may stay empty
        });
    }
    menu.exec(global);
}

void TimelineView::paintHeaders(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const doc::Document &d = m_document->document();
    p.fillRect(QRect(0, m.rulerHeight, m.headerWidth, viewport()->height()), palette().window());
    for (int row = 0; row < rowCount(); ++row) {
        const int celIndex = celIndexForRow(row);
        if (isMotionRow(row)) {
            paintMotionHeader(p, celIndex, row);
            continue;
        }
        const doc::Cel &cel = d.celAt(celIndex);
        const QRect r(0, m.rowY(row), m.headerWidth - 1, m.rowHeight);
        const QColor ink = palette().text().color();
        const IconTone tone = IconTone::Plain;
        p.fillRect(QRect(r.left() + 4, r.top() + 6, 4, r.height() - 12), colorForCel(colorSlot(cel.id())));
        // The toggles: the icon when on, an empty slot when off.
        p.setPen(palette().placeholderText().color());
        for (const QRect &slot : {eyeRect(row), lockRect(row)})
            p.drawRect(slot.adjusted(0, 0, -1, -1));
        if (cel.properties().visible)
            icon(IconId::Eye, tone).paint(&p, eyeRect(row).adjusted(1, 1, -1, -1));
        if (cel.properties().locked)
            icon(IconId::Lock, tone).paint(&p, lockRect(row).adjusted(1, 1, -1, -1));
        p.setPen(cel.properties().visible ? ink
                                          : palette().color(QPalette::Disabled, QPalette::Text));
        const bool keyed = hasKeys(celIndex);
        p.drawText(r.adjusted(lockRect(row).right() + 6, 0, keyed ? -22 : -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromStdString(cel.properties().name));
        if (cel.properties().trajectory) { // a trajectory: not drawn, only moved
            const int right = r.right() - (keyed ? 24 : 4);
            icon(IconId::Trajectory, tone).paint(&p, QRect(right - 14, r.top() + (r.height() - 14) / 2, 14, 14));
        }
        if (keyed)
            paintFoldArrow(p, foldRect(row), !keyframesRowShown(celIndex));
        p.setPen(palette().mid().color());
        p.drawLine(r.bottomLeft(), r.bottomRight());
    }
    paintCameraHeader(p);
    paintAudioHeader(p);
}

void TimelineView::paintCursor(QPainter &p) const
{
    const TimelineMetrics &m = m_metrics;
    const int frame = m_document->currentFrame();
    const int x = m.frameX(frame);
    // Frame number badge in the ruler (under the strip of the markers), line through the cells.
    const QRect badge(x, m.markerStrip + 1, std::max(m.frameWidth, 26), m.rulerHeight - m.markerStrip - 3);
    p.fillRect(badge, kCursorColor);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, QString::number(frame + 1));
    p.setPen(QPen(kCursorColor, 1));
    const int cx = x + m.frameWidth / 2;
    p.drawLine(cx, m.rulerHeight, cx, viewport()->height());
}

// --- interaction ------------------------------------------------------------------

int TimelineView::celDropPosition(int y) const
{
    const int n = m_document->document().celCount();
    const int row = (y - m_metrics.rulerHeight + m_metrics.scrollY) / m_metrics.rowHeight;
    if (y < m_metrics.rulerHeight || row < 0)
        return n;
    if (row >= rowCount())
        return 0;
    const int celIndex = celIndexForRow(row);
    // Above the middle of the cel's own row: above the cel; anywhere lower (its keys' rows are its own): below it.
    const bool upperHalf = !isMotionRow(row) && y < m_metrics.rowY(row) + m_metrics.rowHeight / 2;
    return upperHalf ? celIndex + 1 : celIndex;
}

int TimelineView::celDropY(int position) const
{
    const int n = m_document->document().celCount();
    if (position >= n)
        return m_metrics.rowY(0);
    // The rows are top first: the line is under the rows of the cels at or above the position, which is above the others.
    int last = 0;
    for (int row = 0; row < rowCount(); ++row)
        if (celIndexForRow(row) >= position)
            last = row + 1; // one past the rows of the cels at or above the position
    return m_metrics.rowY(last);
}

void TimelineView::scrubTo(const QPoint &pos, bool selectCel)
{
    if (selectCel) {
        const int row = rowAt(pos.y());
        if (row >= 0)
            m_document->setCurrentCel(m_document->document().celAt(celIndexForRow(row)).id());
    }
    const int frame = frameAt(pos.x());
    m_document->setCurrentFrame(frame);
    emit frameScrubbed(m_document->currentFrame());
}

void TimelineView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->position().x() < m_metrics.headerWidth) {
        // In a header: the toggles, or the cel made current.
        if (event->button() == Qt::LeftButton && inCameraRow(int(event->position().y()))) {
            if (cameraEyeRect().contains(event->position().toPoint()))
                m_document->setCameraVisible(!m_document->camera()->visible);
            else
                m_document->selectCamera(true); // the camera is what the canvas moves now
            return;
        }
        if (event->button() == Qt::LeftButton && inAudioRow(int(event->position().y()))) {
            const QPoint at = event->position().toPoint();
            if (const auto &track = m_document->audioTrack()) {
                if (audioMuteRect().contains(at)) {
                    m_document->setAudioMuted(!track->muted);
                } else if (audioScrubRect().contains(at)) {
                    m_document->setAudioScrub(!track->scrub);
                } else if (audioVolumeRect().adjusted(-2, -2, 2, 2).contains(at)) {
                    m_volumeBefore = track->volume; // dragged: heard as it goes, one step when it is let go
                    m_document->previewAudioVolume(audioVolumeAt(at.x()));
                }
            }
            return;
        }
        const int row = rowAt(int(event->position().y()));
        if (event->button() != Qt::LeftButton || row < 0)
            return;
        const doc::Cel &cel = m_document->document().celAt(celIndexForRow(row));
        const QPoint pos = event->position().toPoint();
        if (hasKeys(celIndexForRow(row)) && foldRect(row).contains(pos)) { // the arrow: fold or unfold the keys
            toggleFold(cel.id(), isMotionRow(row));
            return;
        }
        if (isMotionRow(row)) { // the keys' row: its cel is chosen, and no toggles are there
            m_document->chooseCel(cel.id());
            return;
        }
        doc::CelProperties p = cel.properties();
        if (eyeRect(row).contains(pos))
            p.visible = !p.visible;
        else if (lockRect(row).contains(pos))
            p.locked = !p.locked;
        else if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
            // Chosen together with the others: Ctrl adds it or takes it out, Shift all the cels from the current one.
            const doc::Document &d = m_document->document();
            std::vector<doc::CelId> cels = m_document->celSelection();
            const auto at = std::find(cels.begin(), cels.end(), cel.id());
            if (event->modifiers() & Qt::ShiftModifier) {
                const int from = d.indexOf(m_document->currentCel()), to = d.indexOf(cel.id());
                cels.clear();
                for (int i = std::min(from, to); i <= std::max(from, to); ++i)
                    cels.push_back(d.celAt(i).id());
                m_document->setCelSelection(cels, cel.id());
            } else if (at == cels.end()) {
                cels.push_back(cel.id());
                m_document->setCelSelection(cels, cel.id());
            } else if (cels.size() > 1) { // taken out: another is the current one
                cels.erase(at);
                m_document->setCelSelection(cels, cels.back());
            }
            return;
        } else {
            // A cel chosen with others stays so (the press may begin a drag of all of them); alone, it is chosen whole:
            // all its frames (a transform moves them all).
            const std::vector<doc::CelId> cels = m_document->celSelection();
            if (cels.size() > 1 && std::find(cels.begin(), cels.end(), cel.id()) != cels.end()) {
                if (m_document->currentCel() != cel.id())
                    m_document->setCelSelection(cels, cel.id());
            } else {
                m_document->chooseCel(cel.id());
            }
            m_celDrag = CelDrag{pos, false, 0, cel.id()};
        }
        m_document->execute(doc::makeSetCelProperties(m_document->document(), cel.id(), p));
        return;
    }
    const QPoint pos = event->position().toPoint();
    if (pos.y() < m_metrics.markerStrip) {
        // The strip of the markers: a drag on a flag moves the marker; the strip is not for moving the cursor.
        if (const auto marker = markerAt(pos))
            m_markerDrag = MarkerDrag{*marker, *marker, frameAt(pos.x())};
        return;
    }
    if (inCameraRow(pos.y())) {
        // On a keyframe: its frame is shown, and a drag moves the key; elsewhere in the row: the frame under the pointer.
        const int frame = frameAt(pos.x());
        if (m_document->camera()->keyAt(frame))
            m_cameraDrag = CameraDrag{frame, frame};
        else
            setScrubbing(true);
        scrubTo(pos, false);
        return;
    }
    if (inAudioRow(pos.y())) {
        // On the sound's block: dragged along the frames; elsewhere in the row: the frame under the pointer.
        const int start = m_document->audioTrack()->offset;
        if (audioBlockRect(start).contains(pos)) {
            m_audioDrag = AudioDrag{m_metrics.frameAt(pos.x()), start, start, false};
            viewport()->update();
            return;
        }
        setScrubbing(true);
        scrubTo(pos, false);
        return;
    }
    if (const int row = rowAt(pos.y()); row >= 0 && isMotionRow(row)) {
        // The keys of a cel: on a diamond its frame is shown, and a drag moves the key; elsewhere the frame under the pointer.
        const doc::Cel &cel = m_document->document().celAt(celIndexForRow(row));
        m_document->setCurrentCel(cel.id());
        if (const int frame = motionKeyAt(pos, row); frame >= 0) {
            m_motionDrag = MotionDrag{cel.id(), frame, frame, channelOfRow(row)};
            m_selectedKey = std::make_pair(cel.id(), frame);
            m_selectedChannel = channelOfRow(row);
            m_document->setCurrentFrame(frame);
            emit frameScrubbed(frame);
        } else {
            m_selectedKey.reset();
            setScrubbing(true);
            scrubTo(pos, false);
        }
        viewport()->update();
        return;
    }
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
        m_selectedKey.reset();
        m_anchor.reset();
        viewport()->update();
    }
    setScrubbing(true);
    scrubTo(pos, pos.y() >= m_metrics.rulerHeight);
}

void TimelineView::pressFrame(const doc::ExposureRef &ref, int frame, int x, Qt::KeyboardModifiers modifiers)
{
    m_selectedKey.reset(); // Delete is for the blocks now
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
    if (m_volumeBefore) {
        m_document->previewAudioVolume(audioVolumeAt(pos.x()));
        return;
    }
    if (m_celDrag) {
        if (!m_celDrag->moving && (pos - m_celDrag->pressed).manhattanLength() >= QApplication::startDragDistance())
            m_celDrag->moving = true;
        if (m_celDrag->moving) {
            m_celDrag->insertAt = celDropPosition(pos.y());
            viewport()->update();
        }
        return;
    }
    if (m_markerDrag) {
        // By as many frames as the pointer has moved (it may have taken the flag by its pennant, past the pole).
        const int to = std::max(0, m_markerDrag->from + frameAt(pos.x()) - m_markerDrag->grabbed);
        if (to != m_markerDrag->to) {
            m_markerDrag->to = to;
            viewport()->update();
        }
        return;
    }
    if (m_motionDrag) {
        const int to = std::max(0, frameAt(pos.x()));
        if (to != m_motionDrag->to) {
            m_motionDrag->to = to;
            viewport()->update();
        }
        return;
    }
    if (m_cameraDrag) {
        const int to = std::max(0, frameAt(pos.x()));
        if (to != m_cameraDrag->to) {
            m_cameraDrag->to = to;
            viewport()->update();
        }
        return;
    }
    if (m_audioDrag) {
        const int offset = m_audioDrag->startOffset + m_metrics.frameAt(pos.x()) - m_audioDrag->pressFrame;
        if (offset != m_audioDrag->offset) {
            m_audioDrag->offset = offset;
            m_audioDrag->moved = true;
            viewport()->update();
        }
        return;
    }
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
    viewport()->setCursor(edgeAt(pos) || markerAt(pos) ? Qt::SizeHorCursor : Qt::ArrowCursor); // a flag moves sideways
}

void TimelineView::mouseReleaseEvent(QMouseEvent *)
{
    setScrubbing(false);
    if (m_volumeBefore) {
        const float before = *m_volumeBefore;
        m_volumeBefore.reset();
        m_document->commitAudioVolume(before);
        viewport()->update();
        return;
    }
    if (m_celDrag) {
        const CelDrag drag = *m_celDrag;
        m_celDrag.reset();
        if (drag.moving)
            m_document->moveChosenCels(drag.insertAt);
        else if (m_document->celSelection().size() > 1)
            m_document->chooseCel(drag.cel); // pressed and let go: that cel alone
        viewport()->update();
        return;
    }
    if (m_markerDrag) {
        const MarkerDrag drag = *m_markerDrag;
        m_markerDrag.reset();
        // A marker goes where there is none: one on the way is a place taken.
        if (drag.to != drag.from)
            m_document->moveMarker(drag.from, drag.to);
        viewport()->update();
        return;
    }
    if (m_motionDrag) {
        const MotionDrag drag = *m_motionDrag;
        m_motionDrag.reset();
        // A key goes where there is none (one on the way is not jumped: it is a place taken).
        if (drag.to != drag.from && m_document->moveCelKey(drag.cel, drag.from, drag.to, drag.channel)) {
            m_selectedKey = std::make_pair(drag.cel, drag.to);
            m_selectedChannel = drag.channel;
            m_document->setCurrentFrame(drag.to);
            emit frameScrubbed(drag.to);
        }
        viewport()->update();
        return;
    }
    if (m_cameraDrag) {
        const CameraDrag drag = *m_cameraDrag;
        m_cameraDrag.reset();
        // A key goes where there is none (one on the way is not jumped: it is a place taken).
        if (drag.to != drag.from && m_document->moveCameraKey(drag.from, drag.to)) {
            m_document->setCurrentFrame(drag.to);
            emit frameScrubbed(m_document->currentFrame());
        }
        viewport()->update();
        return;
    }
    if (m_audioDrag) {
        const AudioDrag drag = *m_audioDrag;
        m_audioDrag.reset();
        if (drag.moved && drag.offset != drag.startOffset)
            m_document->setAudioOffset(drag.offset); // one undo step
        viewport()->update();
        return;
    }
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
    if (event->button() == Qt::LeftButton && pos.y() < m_metrics.markerStrip && pos.x() >= m_metrics.headerWidth) {
        const int frame = frameAt(pos.x()); // on a flag: its name; on the strip elsewhere: a marker here
        if (frame >= 0) {
            if (!m_document->markers().count(frame))
                m_document->setMarker(frame, QString());
            renameMarker(frame);
        }
        return;
    }
    if (event->button() == Qt::LeftButton && inCameraRow(pos.y()) && pos.x() >= m_metrics.headerWidth) {
        const int frame = frameAt(pos.x()); // a key here, at the pose the camera has
        if (frame >= 0 && !m_document->camera()->keyAt(frame))
            m_document->setCameraKey(frame, m_document->camera()->poseAt(frame));
        return;
    }
    if (event->button() == Qt::LeftButton && row >= 0 && isMotionRow(row)) {
        // On the keys' row: a key here, at the pose the cel has. (In its header: nothing.)
        if (pos.x() >= m_metrics.headerWidth) {
            const doc::CelId cel = m_document->document().celAt(celIndexForRow(row)).id();
            const int frame = frameAt(pos.x());
            const auto channel = channelOfRow(row);
            const doc::CelMotion &motion = *m_document->celMotion(cel);
            if (frame >= 0 && channel && !motion.keyAt(*channel, frame))
                m_document->setCelChannelKey(cel, *channel, frame, m_document->celPoseAt(cel, frame));
            else if (frame >= 0 && !channel && !motion.hasKeyAt(frame))
                m_document->setCelKey(cel, frame, m_document->celPoseAt(cel, frame));
        }
        return;
    }
    if (event->button() == Qt::LeftButton && row >= 0 && pos.x() < m_metrics.headerWidth
        && !eyeRect(row).contains(pos) && !lockRect(row).contains(pos) && !(hasKeys(celIndexForRow(row)) && foldRect(row).contains(pos))) {
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
    if (event->pos().y() < m_metrics.rulerHeight && event->pos().x() >= m_metrics.headerWidth) {
        const int frame = frameAt(event->pos().x());
        if (frame >= 0)
            showMarkerMenu(event->globalPos(), frame);
        return;
    }
    if (inCameraRow(event->pos().y())) {
        const int frame = event->pos().x() < m_metrics.headerWidth ? m_document->currentFrame() : frameAt(event->pos().x());
        if (frame >= 0)
            showCameraMenu(event->globalPos(), frame);
        return;
    }
    if (inAudioRow(event->pos().y())) {
        showAudioMenu(event->globalPos());
        return;
    }
    const int row = rowAt(event->pos().y());
    if (row < 0 || event->pos().x() < m_metrics.headerWidth)
        return;
    const int frame = frameAt(event->pos().x());
    if (isMotionRow(row)) {
        m_document->setCurrentCel(m_document->document().celAt(celIndexForRow(row)).id());
        showMotionMenu(event->globalPos(), celIndexForRow(row), frame, channelOfRow(row));
        return;
    }
    const doc::Cel &cel = m_document->document().celAt(celIndexForRow(row));
    const doc::CelId celId = cel.id();
    m_document->setCurrentCel(celId);
    m_document->setCurrentFrame(frame);
    if (cel.properties().trajectory) { // no frames to work on: only its keys
        QMenu menu(this);
        QAction *key = menu.addAction(tr("Add keyframe here"), this, [this, celId, frame] {
            m_document->setCelKey(celId, frame, m_document->celPoseAt(celId, frame));
        });
        key->setEnabled(!m_document->celMotion(celId)->hasKeyAt(frame));
        menu.exec(event->globalPos());
        return;
    }
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
    // The whole hold cut at regular intervals from where it starts.
    QMenu *every = menu.addMenu(tr("Split frame every"));
    auto splitEvery = [this, celId, frame](int step) {
        if (auto command = doc::makeSplitEvery(m_document->documentForCommands(), celId, frame, step))
            m_document->execute(std::move(command));
    };
    for (int step : {1, 2, 3, 4})
        every->addAction(tr("%n frame(s)", nullptr, step), this, [splitEvery, step] { splitEvery(step); });
    const int holdLength = hold ? hold->length : 0;
    every->addAction(tr("Other…"), this, [this, holdLength, splitEvery] {
        bool ok = false;
        const int step = QInputDialog::getInt(this, tr("Split frame"), tr("Cut every (frames):"), 2, 1,
                                              std::max(1, holdLength - 1), 1, &ok);
        if (ok)
            splitEvery(step);
    });
    every->setEnabled(hold && hold->length > 1);
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
    // What the cel shows, keys and all, as plain pictures in a new cel.
    QAction *raster = menu.addAction(tr("Raster into new Cel"), this, [this, celId] { m_document->rasterCel(celId); });
    raster->setEnabled(!cel.timing().exposures().empty());
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
    if (!hasAudio()) {
        menu.addSeparator();
        menu.addAction(tr("Import audio…"), this, &TimelineView::audioImportRequested);
    }
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
    // The camera selected, on one of its keys: Delete takes the key away.
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_document->cameraSelected()
        && m_document->removeCameraKey(m_document->currentFrame()))
        return;
    // A key of motion chosen (its channel's, or those of the frame): Delete takes it away, one undo step.
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selectedKey) {
        const auto [cel, keyFrame] = *m_selectedKey;
        const std::optional<doc::Channel> channel = m_selectedChannel;
        m_selectedKey.reset();
        m_selectedChannel.reset();
        viewport()->update();
        if (m_document->removeCelKey(cel, keyFrame, channel))
            return;
    }
    const int celIndex = d.indexOf(m_document->currentCel());
    switch (event->key()) {
    case Qt::Key_Left: m_document->setCurrentFrame(frame - 1); emit frameScrubbed(m_document->currentFrame()); break;
    case Qt::Key_Right: m_document->setCurrentFrame(frame + 1); emit frameScrubbed(m_document->currentFrame()); break;
    case Qt::Key_Home: m_document->setCurrentFrame(0); emit frameScrubbed(0); break;
    case Qt::Key_End: m_document->setCurrentFrame(d.frameCount() - 1); emit frameScrubbed(m_document->currentFrame()); break;
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
