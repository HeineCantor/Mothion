#include "canvas/VectorPointTool.h"

#include "canvas/VectorPoints.h"
#include "canvas/VectorRender.h"
#include "canvas/ViewTransform.h"
#include "tools/ToolState.h"

#include <QObject>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace mothion::canvas {

namespace {

using Mode = tools::VectorPointSettings::Mode;

QRectF united(const QRectF &a, const QRectF &b)
{
    if (a.isNull())
        return b;
    if (b.isNull())
        return a;
    return a.united(b);
}

} // namespace

VectorPointTool::VectorPointTool(document::DocumentController *document, tools::ToolState *toolState,
                                 std::function<void(const QString &)> status)
    : m_document(document)
    , m_toolState(toolState)
    , m_status(std::move(status))
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(0);
    m_flushTimer.callOnTimeout([this] { flush(); });
}

const doc::Layer *VectorPointTool::currentLayer() const
{
    if (!m_document->currentLayerIsVector())
        return nullptr;
    return m_document->document().layer(m_document->currentCel(), m_document->currentDrawingId(),
                                        m_document->currentLayer());
}

void VectorPointTool::refresh() const
{
    // During a drag the stroke chosen is the one being edited, which the layer gets only on the release.
    if (!m_chosen || m_edit)
        return;
    const doc::Layer *layer = currentLayer();
    if (!layer || m_chosen->cel != m_document->currentCel() || m_chosen->drawing != m_document->currentDrawingId()
        || m_chosen->layer != layer->id || m_chosen->index >= int(layer->strokes.size())) {
        m_chosen.reset();
        return;
    }
    const doc::VectorStroke &now = layer->strokes[std::size_t(m_chosen->index)];
    if (now == m_chosen->stroke)
        return;
    m_chosen->stroke = now;
    m_chosen->anchors = controlPoints(now, kTolerance / m_zoom);
    m_chosen->chosen.clear();
}

int VectorPointTool::anchorAt(const QPointF &docPos) const
{
    if (!m_chosen)
        return -1;
    const double reach = kGrab / m_zoom;
    int best = -1;
    double nearest = reach;
    for (std::size_t k = 0; k < m_chosen->anchors.size(); ++k) {
        const brush::BrushInput &in = m_chosen->stroke.points[std::size_t(m_chosen->anchors[k])];
        const double d = std::hypot(in.x - docPos.x(), in.y - docPos.y());
        if (d <= nearest) {
            nearest = d;
            best = int(k);
        }
    }
    return best;
}

int VectorPointTool::strokeAt(const QPointF &docPos) const
{
    const doc::Layer *layer = currentLayer();
    if (!layer)
        return -1;
    for (int i = int(layer->strokes.size()) - 1; i >= 0; --i) {
        const doc::VectorStroke &stroke = layer->strokes[std::size_t(i)];
        if (stroke.erase)
            continue; // the eraser's strokes are not lines to correct
        const auto place = nearestPlace(stroke, docPos);
        if (place && place->distance <= std::max(kLineGrab / m_zoom, stroke.size / 2.0))
            return i;
    }
    return -1;
}

void VectorPointTool::choose(int index)
{
    const doc::Layer *layer = currentLayer();
    if (!layer || index < 0 || index >= int(layer->strokes.size())) {
        m_chosen.reset();
        return;
    }
    Chosen c;
    c.cel = m_document->currentCel();
    c.drawing = m_document->currentDrawingId();
    c.layer = layer->id;
    c.index = index;
    c.stroke = layer->strokes[std::size_t(index)];
    c.anchors = controlPoints(c.stroke, kTolerance / m_zoom);
    m_chosen = std::move(c);
}

int VectorPointTool::chosenStroke() const
{
    refresh();
    return m_chosen ? m_chosen->index : -1;
}

std::vector<int> VectorPointTool::anchors() const
{
    refresh();
    return m_chosen ? m_chosen->anchors : std::vector<int>();
}

std::set<int> VectorPointTool::chosenAnchors() const
{
    refresh();
    return m_chosen ? m_chosen->chosen : std::set<int>();
}

std::optional<QPointF> VectorPointTool::anchorPoint(int k) const
{
    refresh();
    if (!m_chosen || k < 0 || k >= int(m_chosen->anchors.size()))
        return std::nullopt;
    const brush::BrushInput &in = m_chosen->stroke.points[std::size_t(m_chosen->anchors[std::size_t(k)])];
    return QPointF(in.x, in.y);
}

void VectorPointTool::press(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (m_edit)
        return;
    if (!currentLayer()) {
        m_status(QObject::tr("Correct line works on the strokes of a vector layer"));
        return;
    }
    refresh();
    const Mode mode = m_toolState->vectorPoints().mode;
    const int anchor = anchorAt(docPos);

    if (anchor >= 0) {
        if (modifiers & Qt::ShiftModifier) { // more anchors chosen, or one taken away
            if (!m_chosen->chosen.erase(anchor))
                m_chosen->chosen.insert(anchor);
            return;
        }
        if (mode == Mode::Move && (modifiers & Qt::AltModifier)) {
            const int index = m_chosen->index;
            changeAtOnce([&](std::vector<doc::VectorStroke> &strokes) {
                return removeAnchor(strokes[std::size_t(index)], m_chosen->anchors, anchor);
            });
            return;
        }
        if (mode == Mode::Corner) {
            const int index = m_chosen->index;
            changeAtOnce([&](std::vector<doc::VectorStroke> &strokes) {
                toggleCorner(strokes[std::size_t(index)], m_chosen->anchors, anchor);
                return true;
            });
            return;
        }
        if (mode == Mode::Move || mode == Mode::Width) {
            if (!m_chosen->chosen.count(anchor))
                m_chosen->chosen = {anchor};
            if (beginEdit(mode == Mode::Move ? Edit::Kind::Move : Edit::Kind::Width))
                m_edit->press = docPos;
            return;
        }
    }

    // On the line of the stroke chosen: an anchor added there (and dragged), or the line cut.
    if (m_chosen && (mode == Mode::Move || mode == Mode::Cut)) {
        const auto place = nearestPlace(m_chosen->stroke, docPos);
        if (place && place->distance <= std::max(kLineGrab / m_zoom, m_chosen->stroke.size / 2.0)) {
            const double snap = 2.0 / m_zoom;
            if (mode == Mode::Cut) {
                const int index = m_chosen->index;
                changeAtOnce([&](std::vector<doc::VectorStroke> &strokes) {
                    auto pieces = cutStroke(strokes[std::size_t(index)], *place, snap);
                    if (!pieces)
                        return false;
                    strokes[std::size_t(index)] = std::move(pieces->first);
                    strokes.insert(strokes.begin() + index + 1, std::move(pieces->second));
                    return true;
                });
                return;
            }
            if (!beginEdit(Edit::Kind::Move))
                return;
            const std::size_t pointsBefore = m_edit->start.points.size();
            const int k = addAnchor(m_edit->start, m_edit->anchors, *place, snap);
            m_edit->changed = m_edit->start.points.size() != pointsBefore;
            m_edit->strokes[std::size_t(m_chosen->index)] = m_edit->start;
            m_chosen->stroke = m_edit->start;
            m_chosen->anchors = m_edit->anchors;
            m_chosen->chosen = {k};
            m_edit->press = docPos;
            return;
        }
    }

    // Elsewhere: the stroke there chosen (or none).
    const int index = strokeAt(docPos);
    if (index < 0)
        m_chosen.reset();
    else if (!m_chosen || m_chosen->index != index)
        choose(index);
}

bool VectorPointTool::beginEdit(Edit::Kind kind)
{
    QString reason;
    const auto target = m_document->beginPaint(false, &reason, true, true);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return false;
    }
    const doc::Layer *layer = m_document->document().layer(target->cel, target->drawing, target->layer);
    if (!target->vector || !layer || !m_chosen || target->layer != m_chosen->layer
        || m_chosen->index >= int(layer->strokes.size())) {
        m_document->cancelPaint();
        return false;
    }
    Edit e;
    e.kind = kind;
    e.target = *target;
    e.before = *target->image;
    e.original = layer->strokes;
    e.strokes = layer->strokes;
    e.start = m_chosen->stroke;
    e.anchors = m_chosen->anchors;
    e.shown = strokeReach(e.start);
    for (std::size_t i = std::size_t(m_chosen->index) + 1; i < e.strokes.size(); ++i)
        if (e.strokes[i].erase)
            e.aboveLaidOver = false;
    m_edit = std::move(e);
    return true;
}

void VectorPointTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_edit || !m_chosen)
        return;
    Edit &e = *m_edit;
    if (e.to < 0 && !m_chosen->chosen.empty() && !e.anchors.empty()) {
        // The points a drag of the anchors chosen changes (and one more each side, whose direction of travel follows),
        // known now that the anchor pressed is among them (one added on the line too).
        const int first = *m_chosen->chosen.begin(), last = *m_chosen->chosen.rbegin();
        e.from = e.anchors[std::size_t(std::max(first - 1, 0))] - 1;
        e.to = e.anchors[std::size_t(std::min(last + 1, int(e.anchors.size()) - 1))] + 1;
        e.changedShown = pointsReach(e.start, e.from, e.to);
        if (e.changed) // a point was put in at the press: the whole stroke is drawn again once
            e.pending = e.shown;
    }
    doc::VectorStroke stroke;
    if (e.kind == Edit::Kind::Move) {
        std::vector<QPointF> offsets(e.anchors.size());
        for (int k : m_chosen->chosen)
            offsets[std::size_t(k)] = docPos - e.press;
        stroke = movedAnchors(e.start, e.anchors, offsets);
    } else {
        // Up widens, down thins: twice as wide for each 80 px on screen.
        const double factor = std::pow(2.0, -(docPos.y() - e.press.y()) * m_zoom / 80.0);
        std::vector<double> factors(e.anchors.size(), 1.0);
        for (int k : m_chosen->chosen)
            factors[std::size_t(k)] = factor;
        stroke = scaledWidths(e.start, e.anchors, factors);
        m_status(QObject::tr("Width ×%1").arg(factor, 0, 'f', 2));
    }
    const QRectF reach = pointsReach(stroke, e.from, e.to);
    e.pending = united(e.pending, united(e.changedShown, reach));
    e.changedShown = reach;
    e.shown = strokeReach(stroke);
    e.strokes[std::size_t(m_chosen->index)] = std::move(stroke);
    m_chosen->stroke = e.strokes[std::size_t(m_chosen->index)];
    e.changed = true;
    if (!m_flushTimer.isActive())
        m_flushTimer.start(); // drawn once the events waiting are handled
}

void VectorPointTool::cacheLayers(const QRect &region)
{
    Edit &e = *m_edit;
    const QRegion missing = QRegion(region).subtracted(e.cached);
    if (missing.isEmpty())
        return;
    const QRect r = missing.boundingRect();
    const std::size_t index = std::size_t(m_chosen->index);
    auto make = [&](doc::TiledImage &cache, std::size_t first, std::size_t last) {
        std::vector<doc::VectorStroke> in;
        for (std::size_t i = first; i < last; ++i)
            if (strokeReach(e.strokes[i]).intersects(r))
                in.push_back(e.strokes[i]);
        doc::TiledImage made;
        drawStrokes(made, in, *m_toolState, r);
        std::vector<doc::Pixel> pixels(std::size_t(r.width()) * std::size_t(r.height()));
        made.read(r.x(), r.y(), r.width(), r.height(), pixels.data(), r.width());
        cache.write(r.x(), r.y(), r.width(), r.height(), pixels.data(), r.width());
    };
    make(e.below, 0, index);
    if (e.aboveLaidOver)
        make(e.above, index + 1, e.strokes.size());
    e.cached += r;
}

void VectorPointTool::flush()
{
    if (!m_edit || !m_chosen || m_edit->pending.isNull())
        return;
    Edit &e = *m_edit;
    const QRect region = e.pending.toAlignedRect().adjusted(-1, -1, 1, 1);
    e.pending = QRectF();
    if (region.isEmpty())
        return;
    cacheLayers(region);
    const std::size_t index = std::size_t(m_chosen->index);
    const std::size_t count = std::size_t(region.width()) * std::size_t(region.height());
    std::vector<doc::Pixel> pixels(count);
    // The strokes under it, the stroke chosen over them, then the strokes over it.
    doc::TiledImage patch;
    e.below.read(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    patch.write(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    std::vector<doc::VectorStroke> in{e.strokes[index]};
    if (!e.aboveLaidOver)
        for (std::size_t i = index + 1; i < e.strokes.size(); ++i)
            if (strokeReach(e.strokes[i]).intersects(region))
                in.push_back(e.strokes[i]);
    drawStrokes(patch, in, *m_toolState, region);
    patch.read(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    if (e.aboveLaidOver) {
        std::vector<doc::Pixel> top(count);
        e.above.read(region.x(), region.y(), region.width(), region.height(), top.data(), region.width());
        for (std::size_t i = 0; i < count; ++i) {
            const doc::Pixel s = top[i];
            const std::uint32_t alpha = s >> 24;
            if (alpha == 0)
                continue;
            if (alpha == 255) {
                pixels[i] = s;
                continue;
            }
            // Premultiplied: what is over plus what is under, as much as shows through.
            const doc::Pixel d = pixels[i];
            const std::uint32_t through = 255 - alpha;
            doc::Pixel out = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const std::uint32_t over = (s >> shift) & 0xFF, under = (d >> shift) & 0xFF;
                out |= std::min<std::uint32_t>(255, over + (under * through + 127) / 255) << shift;
            }
            pixels[i] = out;
        }
    }
    e.target.image->write(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    m_document->notifyPainted(region);
}

void VectorPointTool::showEdit(std::vector<doc::VectorStroke> strokes, const QRectF &area)
{
    m_edit->strokes = std::move(strokes);
    m_chosen->stroke = m_edit->strokes[std::size_t(m_chosen->index)];
    redraw(m_edit->strokes, area);
}

void VectorPointTool::redraw(const std::vector<doc::VectorStroke> &strokes, const QRectF &area)
{
    // Only the area is drawn again, from the strokes that reach into it.
    const QRect region = area.toAlignedRect().adjusted(-1, -1, 1, 1);
    if (region.isEmpty())
        return;
    std::vector<doc::VectorStroke> in;
    for (const doc::VectorStroke &stroke : strokes)
        if (strokeReach(stroke).intersects(region))
            in.push_back(stroke);
    doc::TiledImage patch;
    drawStrokes(patch, in, *m_toolState, region);
    std::vector<doc::Pixel> pixels(std::size_t(region.width()) * std::size_t(region.height()));
    patch.read(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    m_edit->target.image->write(region.x(), region.y(), region.width(), region.height(), pixels.data(),
                                region.width());
    m_document->notifyPainted(region);
}

void VectorPointTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_edit)
        return;
    move(docPos, modifiers);
    m_flushTimer.stop();
    flush();
    finishEdit();
}

void VectorPointTool::finishEdit()
{
    Edit e = std::move(*m_edit);
    m_edit.reset();
    if (!e.changed || e.strokes == e.original) {
        *e.target.image = e.before;
        m_document->cancelPaint();
        if (m_chosen) {
            m_chosen->stroke = e.original[std::size_t(m_chosen->index)];
            if (m_chosen->stroke.points.size() != e.start.points.size()) // an anchor was put in, and nothing came of it
                m_chosen->anchors = controlPoints(m_chosen->stroke, kTolerance / m_zoom);
        }
        return;
    }
    if (m_chosen)
        m_chosen->anchors = e.anchors; // the points moved, but they are the same points
    m_document->attachStrokeChange(e.target, std::move(e.original), e.strokes);
    m_document->endPaint(e.before, doc::CommandKind::Paint);
}

void VectorPointTool::changeAtOnce(const std::function<bool(std::vector<doc::VectorStroke> &)> &apply)
{
    const std::vector<int> anchorsBefore = m_chosen->anchors;
    if (!beginEdit(Edit::Kind::Move))
        return;
    std::vector<doc::VectorStroke> strokes = m_edit->strokes;
    if (!apply(strokes)) {
        m_chosen->anchors = anchorsBefore;
        m_edit->changed = false;
        finishEdit();
        return;
    }
    QRectF area = m_edit->shown;
    const int index = m_chosen->index;
    for (int i = index; i <= index + 1 && i < int(strokes.size()); ++i) // the cut's second piece too
        area = united(area, strokeReach(strokes[std::size_t(i)]));
    const std::vector<int> anchorsNow = m_chosen->anchors;
    showEdit(std::move(strokes), area);
    m_edit->anchors = anchorsNow;
    m_edit->changed = true;
    const bool cut = m_edit->strokes.size() != m_edit->original.size();
    finishEdit();
    m_chosen->chosen.clear();
    if (cut)
        choose(index); // the first piece, with its own anchors
}

void VectorPointTool::cancel()
{
    m_flushTimer.stop();
    if (!m_edit)
        return;
    Edit e = std::move(*m_edit);
    m_edit.reset();
    *e.target.image = e.before;
    m_document->notifyPainted(document::kWholeCanvas);
    m_document->cancelPaint();
    if (m_chosen && m_chosen->index < int(e.original.size())) {
        m_chosen->stroke = e.original[std::size_t(m_chosen->index)];
        m_chosen->anchors = controlPoints(m_chosen->stroke, kTolerance / m_zoom);
        m_chosen->chosen.clear();
    }
}

bool VectorPointTool::hover(const QPointF &docPos)
{
    refresh();
    const int anchor = anchorAt(docPos);
    const int stroke = anchor >= 0 ? -1 : strokeAt(docPos);
    if (anchor == m_hoverAnchor && stroke == m_hoverStroke)
        return false;
    m_hoverAnchor = anchor;
    m_hoverStroke = stroke;
    return true;
}

QCursor VectorPointTool::cursor() const
{
    if (m_hoverAnchor >= 0) {
        switch (m_toolState->vectorPoints().mode) {
        case Mode::Move: return Qt::SizeAllCursor;
        case Mode::Width: return Qt::SizeVerCursor;
        case Mode::Corner: return Qt::PointingHandCursor;
        case Mode::Cut: break;
        }
    }
    return Qt::CrossCursor;
}

void VectorPointTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    refresh();
    const QTransform toView = view.documentToView();
    auto pathOf = [&](const doc::VectorStroke &stroke) {
        QPainterPath path;
        for (std::size_t i = 0; i < stroke.points.size(); ++i) {
            const QPointF p = toView.map(QPointF(stroke.points[i].x, stroke.points[i].y));
            if (i == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        return path;
    };
    const QColor accent(0x3d, 0x9b, 0xff);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);

    // The stroke under the pointer, faint, to show what a click would choose.
    if (const doc::Layer *layer = currentLayer();
        layer && m_hoverStroke >= 0 && m_hoverStroke < int(layer->strokes.size())
        && (!m_chosen || m_chosen->index != m_hoverStroke)) {
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 120), 1.5));
        painter.drawPath(pathOf(layer->strokes[std::size_t(m_hoverStroke)]));
    }
    if (!m_chosen) {
        painter.restore();
        return;
    }
    const QPainterPath path = pathOf(m_chosen->stroke);
    painter.setPen(QPen(QColor(255, 255, 255, 200), 3.0));
    painter.drawPath(path);
    painter.setPen(QPen(accent, 1.2));
    painter.drawPath(path);

    constexpr double r = 4.0;
    for (std::size_t k = 0; k < m_chosen->anchors.size(); ++k) {
        const brush::BrushInput &in = m_chosen->stroke.points[std::size_t(m_chosen->anchors[k])];
        const QPointF p = toView.map(QPointF(in.x, in.y));
        const bool chosen = m_chosen->chosen.count(int(k)) > 0;
        const bool hovered = int(k) == m_hoverAnchor;
        painter.setPen(QPen(chosen ? Qt::white : accent, 1.2));
        painter.setBrush(chosen ? accent : (hovered ? QColor(0xcf, 0xe6, 0xff) : QColor(Qt::white)));
        const QRectF box(p.x() - r, p.y() - r, 2 * r, 2 * r);
        if (in.corner)
            painter.drawRect(box);
        else
            painter.drawEllipse(box);
    }
    painter.restore();
}

} // namespace mothion::canvas
