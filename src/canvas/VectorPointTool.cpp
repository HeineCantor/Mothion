#include "canvas/VectorPointTool.h"

#include "canvas/VectorPoints.h"
#include "canvas/VectorRender.h"
#include "canvas/ViewTransform.h"
#include "tools/ToolState.h"

#include <QObject>
#include <QPainter>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
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

QPointF pointOf(const doc::VectorStroke &stroke, int index)
{
    const brush::BrushInput &in = stroke.points[std::size_t(index)];
    return {in.x, in.y};
}

} // namespace

VectorPointTool::Picked *VectorPointTool::Chosen::find(int index)
{
    for (Picked &p : strokes)
        if (p.index == index)
            return &p;
    return nullptr;
}

const VectorPointTool::Picked *VectorPointTool::Chosen::find(int index) const
{
    for (const Picked &p : strokes)
        if (p.index == index)
            return &p;
    return nullptr;
}

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
    // During a drag the strokes chosen are those being edited, which the layer gets only on the release.
    if (!m_chosen || m_edit)
        return;
    const doc::Layer *layer = currentLayer();
    if (!layer || m_chosen->cel != m_document->currentCel() || m_chosen->drawing != m_document->currentDrawingId()
        || m_chosen->layer != layer->id
        || std::any_of(m_chosen->strokes.begin(), m_chosen->strokes.end(),
                       [&](const Picked &p) { return p.index >= int(layer->strokes.size()); })) {
        m_chosen.reset();
        return;
    }
    for (Picked &p : m_chosen->strokes) {
        const doc::VectorStroke &now = layer->strokes[std::size_t(p.index)];
        if (now == p.stroke)
            continue;
        p.stroke = now;
        const std::size_t before = p.anchors.size();
        p.anchors = controlPoints(now, kTolerance / m_zoom);
        if (p.anchors.size() == before) // the same anchors moved (a move undone): those chosen stay
            continue;
        std::erase_if(m_chosen->anchors, [&](const AnchorRef &a) { return a.stroke == p.index; });
        if (m_chosen->pivot && m_chosen->pivot->stroke == p.index)
            m_chosen->pivot.reset();
    }
}

std::optional<VectorPointTool::AnchorRef> VectorPointTool::anchorAt(const QPointF &docPos) const
{
    if (!m_chosen)
        return std::nullopt;
    std::optional<AnchorRef> best;
    double nearest = kGrab / m_zoom;
    for (auto p = m_chosen->strokes.rbegin(); p != m_chosen->strokes.rend(); ++p) // the topmost first
        for (std::size_t k = 0; k < p->anchors.size(); ++k) {
            const QPointF at = pointOf(p->stroke, p->anchors[k]);
            const double d = std::hypot(at.x() - docPos.x(), at.y() - docPos.y());
            if (d < nearest || (!best && d <= nearest)) {
                nearest = d;
                best = AnchorRef{p->index, int(k)};
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
        if (stroke.erase || stroke.fill)
            continue; // the eraser's strokes and the shapes of colour are not lines to correct
        const auto place = nearestPlace(stroke, docPos);
        if (place && place->distance <= std::max(kLineGrab / m_zoom, stroke.size / 2.0))
            return i;
    }
    return -1;
}

std::optional<VectorPointTool::End> VectorPointTool::endAt(const QPointF &docPos, int except) const
{
    if (!m_chosen)
        return std::nullopt;
    std::optional<End> best;
    double nearest = kGrab / m_zoom;
    for (auto p = m_chosen->strokes.rbegin(); p != m_chosen->strokes.rend(); ++p) {
        if (p->index == except || p->stroke.erase || p->stroke.points.empty())
            continue;
        for (const bool atEnd : {false, true}) {
            const QPointF at = pointOf(p->stroke, atEnd ? int(p->stroke.points.size()) - 1 : 0);
            const double d = std::hypot(at.x() - docPos.x(), at.y() - docPos.y());
            if (d < nearest) {
                nearest = d;
                best = End{p->index, atEnd};
            }
        }
    }
    return best;
}

VectorPointTool::Picked VectorPointTool::makePicked(int index) const
{
    const doc::VectorStroke &stroke = currentLayer()->strokes[std::size_t(index)];
    return Picked{index, stroke, controlPoints(stroke, kTolerance / m_zoom)};
}

void VectorPointTool::choose(int index, bool add)
{
    const doc::Layer *layer = currentLayer();
    if (!layer || index < 0 || index >= int(layer->strokes.size())) {
        if (!add)
            m_chosen.reset();
        return;
    }
    if (!add || !m_chosen) {
        Chosen c;
        c.cel = m_document->currentCel();
        c.drawing = m_document->currentDrawingId();
        c.layer = layer->id;
        c.strokes.push_back(makePicked(index));
        c.current = index;
        m_chosen = std::move(c);
        return;
    }
    Chosen &c = *m_chosen;
    if (c.find(index)) { // taken away
        std::erase_if(c.strokes, [&](const Picked &p) { return p.index == index; });
        std::erase_if(c.anchors, [&](const AnchorRef &a) { return a.stroke == index; });
        if (c.pivot && c.pivot->stroke == index)
            c.pivot.reset();
        if (c.strokes.empty()) {
            m_chosen.reset();
            return;
        }
        if (c.current == index)
            c.current = c.strokes.back().index;
        return;
    }
    const auto at = std::lower_bound(c.strokes.begin(), c.strokes.end(), index,
                                     [](const Picked &p, int i) { return p.index < i; });
    c.strokes.insert(at, makePicked(index));
    c.current = index;
}

bool VectorPointTool::chooseAllAnchors()
{
    if (m_edit)
        return true; // a drag goes on: nothing else now
    refresh();
    if (!m_chosen)
        return false;
    const Choice before = choice();
    m_chosen->anchors.clear();
    for (const Picked &p : m_chosen->strokes)
        for (int k = 0; k < int(p.anchors.size()); ++k)
            m_chosen->anchors.insert({p.index, k});
    m_strokesChanged = false;
    recordChoice(before);
    return true;
}

VectorPointTool::Choice VectorPointTool::choice() const
{
    refresh();
    Choice c;
    if (!m_chosen)
        return c;
    c.cel = m_chosen->cel;
    c.drawing = m_chosen->drawing;
    c.layer = m_chosen->layer;
    for (const Picked &p : m_chosen->strokes)
        c.strokes.push_back(p.index);
    c.anchors = m_chosen->anchors;
    c.pivot = m_chosen->pivot;
    c.current = m_chosen->current;
    return c;
}

void VectorPointTool::restore(const Choice &choice)
{
    m_lasso.reset();
    const doc::Layer *layer = currentLayer();
    if (m_edit || choice.strokes.empty() || !layer || choice.cel != m_document->currentCel()
        || choice.drawing != m_document->currentDrawingId() || choice.layer != layer->id) {
        if (!m_edit)
            m_chosen.reset(); // none chosen, or on a layer no longer the current one
        return;
    }
    Chosen c;
    c.cel = choice.cel;
    c.drawing = choice.drawing;
    c.layer = choice.layer;
    for (int index : choice.strokes)
        if (index >= 0 && index < int(layer->strokes.size()))
            c.strokes.push_back(makePicked(index));
    if (c.strokes.empty()) {
        m_chosen.reset();
        return;
    }
    auto valid = [&](const AnchorRef &a) {
        const Picked *p = c.find(a.stroke);
        return p && a.k >= 0 && a.k < int(p->anchors.size());
    };
    for (const AnchorRef &a : choice.anchors)
        if (valid(a))
            c.anchors.insert(a);
    if (choice.pivot && valid(*choice.pivot))
        c.pivot = choice.pivot;
    c.current = c.find(choice.current) ? choice.current : c.strokes.back().index;
    m_chosen = std::move(c);
}

void VectorPointTool::recordChoice(const Choice &before)
{
    if (m_strokesChanged)
        return;
    Choice after = choice();
    if (after == before)
        return;
    m_document->recordToolChoice([this, before] { restore(before); }, [this, after] { restore(after); }, m_alive);
}

std::vector<int> VectorPointTool::chosenStrokes() const
{
    refresh();
    std::vector<int> out;
    if (m_chosen)
        for (const Picked &p : m_chosen->strokes)
            out.push_back(p.index);
    return out;
}

int VectorPointTool::chosenStroke() const
{
    refresh();
    return m_chosen ? m_chosen->current : -1;
}

std::vector<int> VectorPointTool::anchors() const { return anchorsOf(chosenStroke()); }

std::vector<int> VectorPointTool::anchorsOf(int stroke) const
{
    refresh();
    const Picked *p = m_chosen ? m_chosen->find(stroke) : nullptr;
    return p ? p->anchors : std::vector<int>();
}

std::set<int> VectorPointTool::chosenAnchors() const
{
    const int stroke = chosenStroke();
    std::set<int> out;
    if (m_chosen)
        for (const AnchorRef &a : m_chosen->anchors)
            if (a.stroke == stroke)
                out.insert(a.k);
    return out;
}

std::set<VectorPointTool::AnchorRef> VectorPointTool::chosenAnchorRefs() const
{
    refresh();
    return m_chosen ? m_chosen->anchors : std::set<AnchorRef>();
}

std::optional<QPointF> VectorPointTool::anchorPoint(int k) const { return anchorPoint(chosenStroke(), k); }

std::optional<QPointF> VectorPointTool::anchorPoint(int stroke, int k) const
{
    refresh();
    const Picked *p = m_chosen ? m_chosen->find(stroke) : nullptr;
    if (!p || k < 0 || k >= int(p->anchors.size()))
        return std::nullopt;
    return pointOf(p->stroke, p->anchors[std::size_t(k)]);
}

void VectorPointTool::press(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (m_edit)
        return;
    if (!currentLayer()) {
        m_status(QObject::tr("Correct line works on the strokes of a vector layer"));
        return;
    }
    const Choice before = choice();
    m_strokesChanged = false;
    const std::shared_ptr<void> record(nullptr, [&](void *) { recordChoice(before); });
    const Mode mode = m_toolState->vectorPoints().mode;

    if (mode == Mode::Lasso) {
        m_lasso = Lasso{{docPos}, modifiers};
        return;
    }

    if (const std::optional<AnchorRef> anchor = anchorAt(docPos)) {
        Chosen &c = *m_chosen;
        Picked &picked = *c.find(anchor->stroke);
        c.current = anchor->stroke;
        if (modifiers & Qt::ShiftModifier) { // the anchors from the one clicked last to this one (on its stroke)
            const int from = c.pivot && c.pivot->stroke == anchor->stroke ? c.pivot->k : anchor->k;
            c.anchors.clear();
            for (int k = std::min(from, anchor->k); k <= std::max(from, anchor->k); ++k)
                c.anchors.insert({anchor->stroke, k});
            c.pivot = AnchorRef{anchor->stroke, from};
            return;
        }
        if (modifiers & Qt::ControlModifier) { // one more anchor chosen, or one taken away
            if (!c.anchors.erase(*anchor))
                c.anchors.insert(*anchor);
            c.pivot = anchor;
            return;
        }
        if (mode == Mode::Move && (modifiers & Qt::AltModifier)) {
            const int k = anchor->k;
            changeAtOnce(anchor->stroke, [&](std::vector<doc::VectorStroke> &strokes) {
                return removeAnchor(strokes[std::size_t(picked.index)], picked.anchors, k);
            });
            return;
        }
        if (mode == Mode::Corner) {
            const int k = anchor->k;
            changeAtOnce(anchor->stroke, [&](std::vector<doc::VectorStroke> &strokes) {
                toggleCorner(strokes[std::size_t(picked.index)], picked.anchors, k);
                return true;
            });
            return;
        }
        if (mode == Mode::Move || mode == Mode::Width) {
            if (!c.anchors.count(*anchor))
                c.anchors = {*anchor};
            c.pivot = anchor;
            std::set<int> strokes;
            for (const AnchorRef &a : c.anchors)
                strokes.insert(a.stroke);
            const bool loneEnd = mode == Mode::Move && c.anchors.size() == 1 && picked.anchors.size() >= 2
                                 && (anchor->k == 0 || anchor->k == int(picked.anchors.size()) - 1);
            const End end{anchor->stroke, anchor->k != 0};
            if (beginEdit(mode == Mode::Move ? Edit::Kind::Move : Edit::Kind::Width, strokes)) {
                m_edit->press = docPos;
                if (loneEnd)
                    m_edit->mergeFrom = end;
            }
            return;
        }
    }

    // On the line of a stroke chosen: an anchor added there (and dragged), or the line cut.
    if (m_chosen && !(modifiers & Qt::ShiftModifier) && (mode == Mode::Move || mode == Mode::Cut)) {
        for (auto p = m_chosen->strokes.rbegin(); p != m_chosen->strokes.rend(); ++p) {
            const auto place = nearestPlace(p->stroke, docPos);
            if (!place || place->distance > std::max(kLineGrab / m_zoom, p->stroke.size / 2.0))
                continue;
            const int index = p->index;
            const double snap = 2.0 / m_zoom;
            m_chosen->current = index;
            if (mode == Mode::Cut) {
                const PathPlace at = *place;
                changeAtOnce(index, [&](std::vector<doc::VectorStroke> &strokes) {
                    auto pieces = cutStroke(strokes[std::size_t(index)], at, snap);
                    if (!pieces)
                        return false;
                    strokes[std::size_t(index)] = std::move(pieces->first);
                    strokes.insert(strokes.begin() + index + 1, std::move(pieces->second));
                    return true;
                });
                return;
            }
            const PathPlace at = *place;
            m_strokesChanged = true; // the anchor added is part of the edit's step
            if (!beginEdit(Edit::Kind::Move, {index}))
                return;
            Edit::Stroke &es = m_edit->edited.front();
            const std::size_t pointsBefore = es.start.points.size();
            const int k = addAnchor(es.start, es.anchors, at, snap);
            m_edit->changed = es.start.points.size() != pointsBefore;
            m_edit->strokes[std::size_t(index)] = es.start;
            Picked &picked = *m_chosen->find(index);
            picked.stroke = es.start;
            picked.anchors = es.anchors;
            m_chosen->anchors = {{index, k}};
            m_chosen->pivot = AnchorRef{index, k};
            m_edit->press = docPos;
            return;
        }
    }

    chooseStrokeAt(docPos, modifiers);
}

void VectorPointTool::chooseStrokeAt(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    // The stroke there chosen (Shift: one more, or taken away), or none.
    const int index = strokeAt(docPos);
    if (modifiers & Qt::ShiftModifier) {
        if (index >= 0)
            choose(index, true);
    } else if (index < 0) {
        m_chosen.reset();
    } else if (m_chosen && m_chosen->find(index)) {
        m_chosen->current = index; // chosen already: the others stay
    } else {
        choose(index);
    }
}

bool VectorPointTool::beginEdit(Edit::Kind kind, const std::set<int> &indices)
{
    if (!m_chosen || indices.empty())
        return false;
    QString reason;
    const auto target = m_document->beginPaint(false, &reason, true, true);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return false;
    }
    const doc::Layer *layer = m_document->document().layer(target->cel, target->drawing, target->layer);
    const bool fine = target->vector && layer && target->layer == m_chosen->layer
                      && *indices.rbegin() < int(layer->strokes.size())
                      && std::all_of(indices.begin(), indices.end(), [&](int i) { return m_chosen->find(i); });
    if (!fine) {
        m_document->cancelPaint();
        return false;
    }
    Edit e;
    e.kind = kind;
    e.target = *target;
    e.before = *target->image;
    e.original = layer->strokes;
    e.strokes = layer->strokes;
    for (int i : indices) {
        const Picked &p = *m_chosen->find(i);
        e.edited.push_back({i, p.stroke, p.anchors, 0, -1, {}});
    }
    e.lowest = *indices.begin();
    e.highest = *indices.rbegin();
    for (std::size_t i = std::size_t(e.highest) + 1; i < e.strokes.size(); ++i)
        if (e.strokes[i].erase || e.strokes[i].fill)
            e.aboveLaidOver = false;
    m_edit = std::move(e);
    return true;
}

void VectorPointTool::syncPicked()
{
    if (!m_chosen || !m_edit)
        return;
    for (Picked &p : m_chosen->strokes)
        if (p.index < int(m_edit->strokes.size()))
            p.stroke = m_edit->strokes[std::size_t(p.index)];
}

void VectorPointTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (m_lasso) {
        if (QLineF(m_lasso->points.back(), docPos).length() * m_zoom >= 2.0) // a point each 2 px on screen
            m_lasso->points.push_back(docPos);
        return;
    }
    if (!m_edit || !m_chosen)
        return;
    Edit &e = *m_edit;
    if (!e.rangesKnown) {
        // The points a drag of the anchors chosen changes, known now that the anchor pressed is among them (one added
        // on the line too).
        e.rangesKnown = true;
        for (Edit::Stroke &es : e.edited) {
            std::vector<int> ks;
            for (const AnchorRef &a : m_chosen->anchors)
                if (a.stroke == es.index)
                    ks.push_back(a.k);
            if (ks.empty() || es.anchors.empty())
                continue;
            const int first = ks.front(), last = ks.back();
            es.from = es.anchors[std::size_t(std::max(first - 1, 0))] - 1;
            es.to = es.anchors[std::size_t(std::min(last + 1, int(es.anchors.size()) - 1))] + 1;
            es.changedShown = pointsReach(es.start, es.from, es.to);
            if (e.changed) // a point was put in at the press: the whole stroke is drawn again once
                e.pending = united(e.pending, strokeReach(es.start));
        }
    }
    // A lone end on an end of another stroke chosen: it stays where it was, and the strokes joined show over it.
    if (e.mergeFrom)
        e.mergeTo = endAt(docPos, e.mergeFrom->stroke);
    const double factor = std::pow(2.0, -(docPos.y() - e.press.y()) * m_zoom / 80.0); // Width: ×2 each 80 px up
    for (Edit::Stroke &es : e.edited) {
        doc::VectorStroke stroke;
        if (e.mergeTo) {
            stroke = es.start;
        } else if (e.kind == Edit::Kind::Move) {
            std::vector<QPointF> offsets(es.anchors.size());
            for (const AnchorRef &a : m_chosen->anchors)
                if (a.stroke == es.index)
                    offsets[std::size_t(a.k)] = docPos - e.press;
            stroke = movedAnchors(es.start, es.anchors, offsets);
        } else {
            std::vector<double> factors(es.anchors.size(), 1.0);
            for (const AnchorRef &a : m_chosen->anchors)
                if (a.stroke == es.index)
                    factors[std::size_t(a.k)] = factor;
            stroke = scaledWidths(es.start, es.anchors, factors);
        }
        const QRectF reach = pointsReach(stroke, es.from, es.to);
        e.pending = united(e.pending, united(es.changedShown, reach));
        es.changedShown = reach;
        e.strokes[std::size_t(es.index)] = std::move(stroke);
    }
    if (e.kind == Edit::Kind::Width)
        m_status(QObject::tr("Width ×%1").arg(factor, 0, 'f', 2));
    e.changed = true;
    syncPicked();
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
    make(e.below, 0, std::size_t(e.lowest));
    if (e.aboveLaidOver)
        make(e.above, std::size_t(e.highest) + 1, e.strokes.size());
    e.cached += r;
}

void VectorPointTool::flush()
{
    if (!m_edit || m_edit->pending.isNull())
        return;
    Edit &e = *m_edit;
    const QRect region = e.pending.toAlignedRect().adjusted(-1, -1, 1, 1);
    e.pending = QRectF();
    if (region.isEmpty())
        return;
    cacheLayers(region);
    const std::size_t count = std::size_t(region.width()) * std::size_t(region.height());
    std::vector<doc::Pixel> pixels(count);
    // The strokes under them, the strokes edited (and those between) over them, then the strokes over them.
    doc::TiledImage patch;
    e.below.read(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    patch.write(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    std::vector<doc::VectorStroke> in;
    const std::size_t last = e.aboveLaidOver ? std::size_t(e.highest) + 1 : e.strokes.size();
    for (std::size_t i = std::size_t(e.lowest); i < last; ++i)
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

void VectorPointTool::finishLasso()
{
    const Lasso lasso = std::move(*m_lasso);
    m_lasso.reset();
    refresh();
    const bool add = lasso.modifiers & Qt::ShiftModifier;
    const bool remove = !add && (lasso.modifiers & Qt::AltModifier);
    auto take = [&](const std::set<AnchorRef> &found) {
        Chosen &c = *m_chosen;
        if (remove) {
            for (const AnchorRef &a : found)
                c.anchors.erase(a);
        } else {
            if (!add)
                c.anchors.clear();
            c.anchors.insert(found.begin(), found.end());
        }
        if (!found.empty()) {
            c.pivot = *found.rbegin();
            c.current = found.rbegin()->stroke;
        }
    };

    // A click: the anchor there, or the stroke.
    QPolygonF loop(QList<QPointF>(lasso.points.begin(), lasso.points.end()));
    const QRectF bounds = loop.boundingRect();
    if (std::max(bounds.width(), bounds.height()) * m_zoom < 3.0) {
        if (const std::optional<AnchorRef> anchor = anchorAt(lasso.points.front()))
            take({*anchor});
        else
            chooseStrokeAt(lasso.points.front(), lasso.modifiers & Qt::ShiftModifier);
        return;
    }
    // A stroke not chosen yet that the loop takes in or crosses, even in part: chosen, with all its anchors.
    std::vector<int> taken;
    if (const doc::Layer *layer = currentLayer(); layer && !remove) {
        std::vector<QLineF> edges;
        for (qsizetype i = 0; i < loop.size(); ++i)
            edges.emplace_back(loop[i], loop[(i + 1) % loop.size()]);
        auto touched = [&](const doc::VectorStroke &stroke) {
            QPointF last;
            for (std::size_t i = 0; i < stroke.points.size(); ++i) {
                const QPointF at(stroke.points[i].x, stroke.points[i].y);
                if (loop.containsPoint(at, Qt::OddEvenFill))
                    return true;
                if (i > 0 && bounds.intersects(QRectF(last, at).normalized().adjusted(-0.5, -0.5, 0.5, 0.5))) {
                    const QLineF piece(last, at);
                    for (const QLineF &edge : edges)
                        if (piece.intersects(edge) == QLineF::BoundedIntersection)
                            return true;
                }
                last = at;
            }
            return false;
        };
        for (int i = 0; i < int(layer->strokes.size()); ++i) {
            const doc::VectorStroke &stroke = layer->strokes[std::size_t(i)];
            if ((m_chosen && m_chosen->find(i)) || stroke.points.empty() || stroke.erase || stroke.fill
                || !bounds.intersects(strokeReach(stroke)) || !touched(stroke))
                continue; // (the eraser's strokes and the shapes of colour are not lines to correct, as a click has them)
            choose(i, true);
            taken.push_back(i);
        }
    }
    if (!m_chosen) {
        m_status(QObject::tr("Draw the loop across strokes, or choose them first (click one, Shift+click one more)"));
        return;
    }
    std::set<AnchorRef> found;
    for (const Picked &p : m_chosen->strokes) {
        const bool whole = std::find(taken.begin(), taken.end(), p.index) != taken.end();
        for (int k = 0; k < int(p.anchors.size()); ++k)
            if (whole || loop.containsPoint(pointOf(p.stroke, p.anchors[std::size_t(k)]), Qt::OddEvenFill))
                found.insert({p.index, k});
    }
    take(found);
}

void VectorPointTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (m_lasso) {
        move(docPos, modifiers);
        const Choice before = choice();
        m_strokesChanged = false;
        finishLasso();
        recordChoice(before);
        return;
    }
    if (!m_edit)
        return;
    move(docPos, modifiers);
    m_flushTimer.stop();
    flush();
    const std::optional<End> from = m_edit->mergeFrom, to = m_edit->mergeTo;
    finishEdit(); // dropped on an end, the stroke is as it was: nothing recorded
    if (from && to)
        merge(*from, *to);
}

void VectorPointTool::finishEdit()
{
    Edit e = std::move(*m_edit);
    m_edit.reset();
    if (!e.changed || e.strokes == e.original) {
        *e.target.image = e.before;
        if (e.rangesKnown)
            m_document->notifyPainted(document::kWholeCanvas); // drawn while dragged
        m_document->cancelPaint();
        if (m_chosen)
            for (const Edit::Stroke &es : e.edited)
                if (Picked *p = m_chosen->find(es.index); p && es.index < int(e.original.size())) {
                    p->stroke = e.original[std::size_t(es.index)];
                    if (p->stroke.points.size() != es.start.points.size()) // an anchor was put in, for nothing
                        p->anchors = controlPoints(p->stroke, kTolerance / m_zoom);
                }
        return;
    }
    if (m_chosen)
        for (const Edit::Stroke &es : e.edited)
            if (Picked *p = m_chosen->find(es.index))
                p->anchors = es.anchors; // the points moved, but they are the same points
    m_document->attachStrokeChange(e.target, std::move(e.original), e.strokes);
    m_document->endPaint(e.before, doc::CommandKind::Paint);
}

void VectorPointTool::changeAtOnce(int index, const std::function<bool(std::vector<doc::VectorStroke> &)> &apply)
{
    Picked *picked = m_chosen ? m_chosen->find(index) : nullptr;
    if (!picked)
        return;
    m_strokesChanged = true;
    const std::vector<int> anchorsBefore = picked->anchors;
    if (!beginEdit(Edit::Kind::Move, {index}))
        return;
    std::vector<doc::VectorStroke> strokes = m_edit->strokes;
    if (!apply(strokes)) {
        picked->anchors = anchorsBefore;
        m_edit->changed = false;
        finishEdit();
        return;
    }
    const bool cut = strokes.size() != m_edit->original.size();
    QRectF area = strokeReach(m_edit->original[std::size_t(index)]);
    for (int i = index; i <= index + 1 && i < int(strokes.size()); ++i) // the cut's second piece too
        area = united(area, strokeReach(strokes[std::size_t(i)]));
    m_edit->strokes = std::move(strokes);
    redraw(m_edit->strokes, area);
    m_edit->edited.front().anchors = picked->anchors;
    m_edit->changed = true;
    if (!cut)
        picked->stroke = m_edit->strokes[std::size_t(index)];
    finishEdit();
    if (cut) {
        choose(index); // the first piece, with its own anchors
        return;
    }
    std::erase_if(m_chosen->anchors, [&](const AnchorRef &a) { return a.stroke == index; });
    m_chosen->pivot.reset();
}

void VectorPointTool::merge(const End &from, const End &to)
{
    if (!m_chosen || !beginEdit(Edit::Kind::Move, {from.stroke}))
        return;
    m_strokesChanged = true;
    std::vector<doc::VectorStroke> strokes = m_edit->strokes;
    const int a = from.stroke, b = to.stroke;
    if (a == b || b < 0 || b >= int(strokes.size())) {
        m_edit->changed = false;
        finishEdit();
        return;
    }
    doc::VectorStroke merged = mergedStrokes(strokes[std::size_t(a)], from.atEnd, strokes[std::size_t(b)], to.atEnd);
    const QRectF area = united(united(strokeReach(strokes[std::size_t(a)]), strokeReach(strokes[std::size_t(b)])),
                               strokeReach(merged));
    strokes[std::size_t(a)] = std::move(merged); // where the stroke dragged was among the others
    strokes.erase(strokes.begin() + b);
    m_edit->strokes = std::move(strokes);
    redraw(m_edit->strokes, area);
    m_edit->edited.clear(); // the strokes chosen are put right below
    m_edit->changed = true;
    finishEdit();

    // The strokes chosen stay chosen, the one made among them (those over the second one a place lower now).
    std::set<int> keep;
    for (const Picked &p : m_chosen->strokes)
        if (p.index != b)
            keep.insert(p.index > b ? p.index - 1 : p.index);
    const int made = b < a ? a - 1 : a;
    Chosen c;
    c.cel = m_chosen->cel;
    c.drawing = m_chosen->drawing;
    c.layer = m_chosen->layer;
    for (int i : keep)
        c.strokes.push_back(makePicked(i));
    c.current = made;
    m_chosen = std::move(c);
}

void VectorPointTool::cancel()
{
    m_flushTimer.stop();
    m_lasso.reset();
    if (!m_edit)
        return;
    Edit e = std::move(*m_edit);
    m_edit.reset();
    *e.target.image = e.before;
    m_document->notifyPainted(document::kWholeCanvas);
    m_document->cancelPaint();
    if (m_chosen)
        for (Picked &p : m_chosen->strokes)
            if (p.index < int(e.original.size())) {
                p.stroke = e.original[std::size_t(p.index)];
                p.anchors = controlPoints(p.stroke, kTolerance / m_zoom);
            }
    if (m_chosen)
        m_chosen->anchors.clear();
}

bool VectorPointTool::hover(const QPointF &docPos)
{
    refresh();
    const std::optional<AnchorRef> anchor = anchorAt(docPos);
    const int stroke = anchor ? -1 : strokeAt(docPos);
    if (anchor == m_hoverAnchor && stroke == m_hoverStroke)
        return false;
    m_hoverAnchor = anchor;
    m_hoverStroke = stroke;
    return true;
}

QCursor VectorPointTool::cursor() const
{
    if (m_hoverAnchor) {
        switch (m_toolState->vectorPoints().mode) {
        case Mode::Move: return Qt::SizeAllCursor;
        case Mode::Width: return Qt::SizeVerCursor;
        case Mode::Corner: return Qt::PointingHandCursor;
        case Mode::Cut: break;
        case Mode::Lasso: return Qt::PointingHandCursor;
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
        && (!m_chosen || !m_chosen->find(m_hoverStroke))) {
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 120), 1.5));
        painter.drawPath(pathOf(layer->strokes[std::size_t(m_hoverStroke)]));
    }
    // The lasso being drawn: a loop, black and white dashes so that it shows on any colour.
    auto drawLasso = [&] {
        if (!m_lasso || m_lasso->points.size() < 2)
            return;
        QPolygonF loop;
        for (const QPointF &q : m_lasso->points)
            loop << toView.map(q);
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 30));
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawPolygon(loop);
        QPen dash(Qt::black, 1.0);
        dash.setDashPattern({4, 4});
        painter.setBrush(Qt::NoBrush);
        painter.setPen(dash);
        painter.drawPolygon(loop);
    };
    if (!m_chosen) {
        drawLasso();
        painter.restore();
        return;
    }
    for (const Picked &p : m_chosen->strokes) {
        const QPainterPath path = pathOf(p.stroke);
        painter.setPen(QPen(QColor(255, 255, 255, 200), 3.0));
        painter.drawPath(path);
        painter.setPen(QPen(accent, 1.2));
        painter.drawPath(path);
    }

    // A lone end dragged: the ends it can be dropped on, and the strokes joined when it is on one.
    if (m_edit && m_edit->mergeFrom) {
        for (const Picked &p : m_chosen->strokes) {
            if (p.index == m_edit->mergeFrom->stroke || p.stroke.points.empty() || p.stroke.erase)
                continue;
            painter.setPen(QPen(accent, 1.5));
            painter.setBrush(Qt::NoBrush);
            for (const int i : {0, int(p.stroke.points.size()) - 1})
                painter.drawEllipse(toView.map(pointOf(p.stroke, i)), 9.0, 9.0);
        }
        if (const auto &to = m_edit->mergeTo) {
            const auto &original = m_edit->original;
            const doc::VectorStroke joined =
                mergedStrokes(original[std::size_t(m_edit->mergeFrom->stroke)], m_edit->mergeFrom->atEnd,
                              original[std::size_t(to->stroke)], to->atEnd);
            const QPainterPath path = pathOf(joined);
            painter.setPen(QPen(QColor(255, 255, 255, 220), 4.0));
            painter.drawPath(path);
            painter.setPen(QPen(accent, 2.2));
            painter.drawPath(path);
        }
    }

    constexpr double r = 4.0;
    drawLasso();
    for (const Picked &p : m_chosen->strokes)
        for (std::size_t k = 0; k < p.anchors.size(); ++k) {
            const AnchorRef ref{p.index, int(k)};
            const brush::BrushInput &in = p.stroke.points[std::size_t(p.anchors[k])];
            const QPointF at = toView.map(QPointF(in.x, in.y));
            const bool chosen = m_chosen->anchors.count(ref) > 0;
            const bool hovered = m_hoverAnchor == ref;
            painter.setPen(QPen(chosen ? Qt::white : accent, 1.2));
            painter.setBrush(chosen ? accent : (hovered ? QColor(0xcf, 0xe6, 0xff) : QColor(Qt::white)));
            const QRectF box(at.x() - r, at.y() - r, 2 * r, 2 * r);
            if (in.corner)
                painter.drawRect(box);
            else
                painter.drawEllipse(box);
        }
    painter.restore();
}

} // namespace mothion::canvas
