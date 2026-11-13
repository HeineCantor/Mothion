#include "canvas/TransformTool.h"

#include "canvas/SelectionMask.h"
#include "core/Parallel.h"
#include "canvas/TileQt.h"
#include "canvas/VectorRender.h"
#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QLineF>
#include "canvas/Resample.h"
#include "tools/Preferences.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr int kReachBeyondPage = 4096; // px: how far out of the canvas a transformed layer is kept

constexpr double kHandleSize = 8.0;  // screen px, as drawn
constexpr double kGrabRadius = 6.0;  // screen px, to pick a handle: just outside it is the turn
constexpr double kMinScale = 0.01;
constexpr double kRotateSnap = 15.0; // degrees, with Shift

QRect tileBounds(const doc::TiledImage &image)
{
    QRect r;
    for (const doc::TileCoord &c : image.tileCoords())
        r |= tileRect(c);
    return r;
}

// The smallest rect holding the image's visible pixels, in its own coordinates.
QRect opaqueBounds(const QImage &image)
{
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const doc::Pixel *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (doc::alphaOf(line[x]) == 0)
                continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

// Scales by (sx, sy) around `anchor`, in the coordinates `t` maps from.
QTransform scaledAround(const QTransform &t, QPointF anchor, double sx, double sy)
{
    return QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(sx, sy)
           * QTransform::fromTranslate(anchor.x(), anchor.y()) * t;
}

double clampScale(double s)
{
    return std::abs(s) < kMinScale ? (s < 0 ? -kMinScale : kMinScale) : s;
}

} // namespace

namespace {

// `moving`'s pixels over `box`.
QImage sourceOf(const doc::TiledImage &moving, const QRect &box)
{
    QImage source(box.size(), QImage::Format_ARGB32_Premultiplied);
    moving.read(box.x(), box.y(), box.width(), box.height(), reinterpret_cast<doc::Pixel *>(source.bits()),
                source.bytesPerLine() / 4);
    return source;
}

} // namespace

TransformTool::TransformTool(document::DocumentController *document, tools::ToolState *toolState,
                             std::function<void(const QString &)> status, std::function<void()> repaint)
    : m_document(document)
    , m_toolState(toolState)
    , m_status(std::move(status))
    , m_repaint(std::move(repaint))
{
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(0);
    QObject::connect(&m_renderTimer, &QTimer::timeout, [this] { flushRender(); });
}

TransformTool::~TransformTool() = default;

void TransformTool::activated()
{
    if (!m_session)
        start();
    m_repaint();
}

bool TransformTool::start()
{
    QString reason;
    const auto targets = m_document->beginPaintLayers(&reason, true);
    if (targets.empty()) {
        m_status(reason.isEmpty() ? QObject::tr("Nothing to transform on this frame") : reason);
        return false;
    }
    auto s = std::make_unique<Session>();
    s->selection = m_document->selection();
    const std::optional<SelectionMask> mask =
        s->selection.isEmpty() ? std::nullopt : std::optional(SelectionMask::fromPath(s->selection));
    // What moves of each layer, and one box around all of it. Each layer is looked at on its own core.
    std::vector<Part> parts(targets.size());
    std::vector<QRect> opaque(targets.size());
    const int shownFrame = m_document->currentFrame();
    const tools::OnionSkinSettings onion = m_toolState->onionSkin();
    for (std::size_t i = 0; i < targets.size(); ++i) { // the strokes of the vector layers, as they are now
        const auto &target = targets[i];
        if (!target.vector)
            continue;
        Part &part = parts[i];
        part.vector = true;
        part.cel = target.cel;
        part.drawing = target.drawing;
        part.layer = target.layer;
        if (const doc::Layer *layer = m_document->document().layer(target.cel, target.drawing, target.layer)) {
            part.allStrokes = layer->strokes;
            if (!mask) {
                part.strokes = layer->strokes;
            } else { // the strokes are cut along the outline: those inside move, the rest stays
                for (const doc::VectorStroke &stroke : layer->strokes) {
                    SplitStroke cut = splitStrokeByMask(stroke, *mask);
                    part.strokes.insert(part.strokes.end(), cut.inside.begin(), cut.inside.end());
                    part.restStrokes.insert(part.restStrokes.end(), cut.outside.begin(), cut.outside.end());
                }
            }
        }
    }
    parallelFor(int(targets.size()), [&](int i) {
        const auto &target = targets[std::size_t(i)];
        Part &part = parts[std::size_t(i)];
        part.image = target.image;
        part.before = *target.image;
        if (mask)
            splitByMask(part.before, *mask, part.moving, part.rest);
        else
            part.moving = part.before;
        const QRect tiles = tileBounds(part.moving);
        if (!tiles.isEmpty()) {
            QImage area(tiles.size(), QImage::Format_ARGB32_Premultiplied);
            part.moving.read(tiles.x(), tiles.y(), tiles.width(), tiles.height(),
                             reinterpret_cast<doc::Pixel *>(area.bits()), area.bytesPerLine() / 4);
            opaque[std::size_t(i)] = opaqueBounds(area).translated(tiles.topLeft());
        }
        const doc::Cel *cel = m_document->document().cel(target.cel);
        part.live = cel && cel->timing().drawingAt(shownFrame) == target.drawing;
        // The frames the onion skin shows move with it while the box is dragged (they are what one lines the pose up with).
        if (!part.live && cel && onion.enabled && cel->id() == m_document->currentCel())
            for (int k = -onion.before; k <= onion.after && !part.live; ++k) {
                const int frame = shownFrame + k;
                part.live = frame >= 0 && frame < m_document->document().frameCount()
                            && cel->timing().drawingAt(frame) == target.drawing;
            }
    });
    QRect box;
    for (const QRect &r : opaque)
        box |= r;
    if (box.isEmpty()) {
        m_document->cancelPaint();
        m_status(QObject::tr("Nothing to transform here"));
        return false;
    }
    // The frame is the selection's own bounding box, not that of the pixels in it.
    if (!s->selection.isEmpty()) {
        // Not cut to the page: an image pasted bigger than it is moved whole.
        const QRect selected = s->selection.boundingRect().toAlignedRect();
        if (!selected.isEmpty())
            box = selected;
    }
    // Layers of other frames than the one shown are drawn only when the transform is applied. If none is in
    // the frame shown, all are drawn at each step (there would be nothing to see otherwise).
    if (std::none_of(parts.begin(), parts.end(), [](const Part &p) { return p.live; }))
        for (Part &p : parts)
            p.live = true;
    parallelFor(int(parts.size()), [&](int i) {
        if (parts[std::size_t(i)].live)
            parts[std::size_t(i)].source = sourceOf(parts[std::size_t(i)].moving, box);
    });
    s->parts = std::move(parts);
    s->box = box;
    s->shown = box;
    m_session = std::move(s);
    return true;
}

QPointF TransformTool::handlePoint(int index) const
{
    const QRectF b = QRectF(m_session->box);
    switch (index) {
    case 0: return b.topLeft();
    case 1: return b.topRight();
    case 2: return b.bottomRight();
    case 3: return b.bottomLeft();
    case 4: return {b.center().x(), b.top()};
    case 5: return {b.right(), b.center().y()};
    case 6: return {b.center().x(), b.bottom()};
    default: return {b.left(), b.center().y()};
    }
}

TransformTool::Pick TransformTool::pick(const QPointF &docPos) const
{
    const Session &s = *m_session;
    // The handle nearest the pen, if close enough on screen; on a small box
    // less close, so there is room inside it to move.
    const QPolygonF corners = s.transform.map(QPolygonF(QRectF(s.box)));
    const double shorter = std::min(QLineF(corners[0], corners[1]).length(), QLineF(corners[1], corners[2]).length());
    int nearest = -1;
    double best = std::min(kGrabRadius / m_zoom, shorter / 4);
    for (int i = 0; i < 8; ++i) {
        const double distance = QLineF(s.transform.map(handlePoint(i)), docPos).length();
        if (distance <= best) {
            best = distance;
            nearest = i;
        }
    }
    if (nearest >= 0)
        return {nearest < 4 ? Grip::Corner : Grip::Side, nearest};
    return {corners.containsPoint(docPos, Qt::OddEvenFill) ? Grip::Move : Grip::Rotate, -1};
}

void TransformTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_session && !start()) {
        m_repaint();
        return;
    }
    const Session &s = *m_session;
    Drag d;
    d.start = s.transform;
    d.pressDoc = docPos;
    const Pick picked = pick(docPos);
    d.grip = picked.grip;
    if (picked.handle >= 0) {
        const int nearest = picked.handle;
        d.handle = handlePoint(nearest);
        // The opposite corner, or the middle of the opposite side.
        d.anchor = handlePoint(nearest < 4 ? (nearest + 2) % 4 : 4 + (nearest - 4 + 2) % 4);
        d.horizontal = nearest == 5 || nearest == 7;
    }
    m_hover = picked; // what the pointer stays over during the drag
    m_drag = d;
}

bool TransformTool::hover(const QPointF &docPos)
{
    if (!m_session) {
        m_hover.reset();
        return false;
    }
    m_hover = pick(docPos);
    return false;
}

QCursor turnCursor()
{
    // A turning arrow, drawn once: dark with a white edge to show on any paper.
    static const QCursor turning = [] {
        QPixmap pixmap(28, 28);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath arc;
        arc.arcMoveTo(QRectF(5, 5, 18, 18), 40);
        arc.arcTo(QRectF(5, 5, 18, 18), 40, 260);
        QPainterPath head;
        head.moveTo(23.5, 6.5);
        head.lineTo(24, 14.5);
        head.lineTo(16.5, 11.5);
        head.closeSubpath();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 4.5, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(arc);
        p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        p.drawPath(head);
        p.setPen(QPen(Qt::black, 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arc);
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        p.drawPath(head);
        return QCursor(pixmap, 14, 14);
    }();
    return turning;
}

QCursor resizeCursor(const QPointF &from, const QPointF &to)
{
    // The arrows along the way from the middle of the box to a handle.
    double degrees = std::atan2(to.y() - from.y(), to.x() - from.x()) * 180.0 / std::numbers::pi;
    degrees = std::fmod(std::fmod(degrees, 180.0) + 180.0, 180.0); // 0..180, y points down
    if (degrees < 22.5 || degrees >= 157.5)
        return Qt::SizeHorCursor;
    if (degrees < 67.5)
        return Qt::SizeFDiagCursor;
    if (degrees < 112.5)
        return Qt::SizeVerCursor;
    return Qt::SizeBDiagCursor;
}

QCursor TransformTool::cursor() const
{
    if (!m_session || !m_hover)
        return Qt::SizeAllCursor;
    switch (m_hover->grip) {
    case Grip::Move: return Qt::SizeAllCursor;
    case Grip::Rotate: return turnCursor();
    default: break;
    }
    return resizeCursor(m_session->transform.map(QRectF(m_session->box).center()),
                        m_session->transform.map(handlePoint(m_hover->handle)));
}

void TransformTool::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session || !m_drag)
        return;
    const Drag &d = *m_drag;
    Session &s = *m_session;
    bool invertible = false;
    const QTransform toBox = d.start.inverted(&invertible);
    if (!invertible)
        return;
    switch (d.grip) {
    case Grip::Move: {
        const QPointF by = docPos - d.pressDoc;
        s.transform = d.start * QTransform::fromTranslate(by.x(), by.y());
        break;
    }
    case Grip::Rotate: {
        const QPointF c = d.start.map(QRectF(s.box).center());
        double degrees = (std::atan2(docPos.y() - c.y(), docPos.x() - c.x())
                          - std::atan2(d.pressDoc.y() - c.y(), d.pressDoc.x() - c.x()))
                         * 180.0 / std::numbers::pi;
        if (modifiers & Qt::ShiftModifier)
            degrees = std::round(degrees / kRotateSnap) * kRotateSnap;
        s.transform = d.start * QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(degrees)
                      * QTransform::fromTranslate(c.x(), c.y());
        break;
    }
    case Grip::Corner:
    case Grip::Side: {
        const QPointF p = toBox.map(docPos);
        const QPointF span = d.handle - d.anchor;
        double sx = d.grip == Grip::Side && !d.horizontal ? 1.0 : clampScale((p.x() - d.anchor.x()) / span.x());
        double sy = d.grip == Grip::Side && d.horizontal ? 1.0 : clampScale((p.y() - d.anchor.y()) / span.y());
        if (d.grip == Grip::Corner && (modifiers & Qt::ShiftModifier)) {
            const double both = std::max(std::abs(sx), std::abs(sy));
            sx = std::copysign(both, sx);
            sy = std::copysign(both, sy);
        }
        s.transform = scaledAround(d.start, d.anchor, sx, sy);
        break;
    }
    }
    requestRender();
}

void TransformTool::requestRender()
{
    if (!m_deferred) {
        render();
        return;
    }
    m_renderPending = true;
    if (!m_renderTimer.isActive())
        m_renderTimer.start();
    m_repaint(); // the box follows the pen at once
}

void TransformTool::flushRender()
{
    if (!m_renderPending || !m_session)
        return;
    render();
}

void TransformTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    move(docPos, modifiers);
    flushRender();
    if (m_session && m_session->transform != m_drag->start) {
        m_session->undo.push_back(m_drag->start);
        m_session->redo.clear();
    }
    m_drag.reset();
    if (m_session && m_rough)
        render(); // the pen is up: the pixels with the filter chosen
}

void TransformTool::refreshRendering()
{
    if (m_session && !m_drag && !m_session->undo.empty())
        render(); // (before any drag the layers are as they were: nothing to draw)
}

void TransformTool::show()
{
    Session &s = *m_session;
    if (!s.undo.empty()) {
        render();
        return;
    }
    // Back at the start: the layers and selection as they were, exactly.
    m_renderPending = false; // what was pending is moot
    m_renderTimer.stop();
    s.everythingDrawn = true; // all of them as they were
    for (Part &part : s.parts)
        *part.image = part.before;
    if (!s.selection.isEmpty())
        m_document->setSelection(s.selection);
    m_document->notifyPainted(s.shown | s.box);
    s.changed = false;
    m_repaint();
}

bool TransformTool::undoStep()
{
    if (!canUndoStep())
        return false;
    Session &s = *m_session;
    s.redo.push_back(s.transform);
    s.transform = s.undo.back();
    s.undo.pop_back();
    show();
    return true;
}

bool TransformTool::redoStep()
{
    if (!canRedoStep())
        return false;
    Session &s = *m_session;
    s.undo.push_back(s.transform);
    s.transform = s.redo.back();
    s.redo.pop_back();
    show();
    return true;
}

void TransformTool::render()
{
    renderParts(true);
}

void TransformTool::renderParts(bool onlyLive)
{
    m_renderPending = false;
    m_renderTimer.stop();
    Session &s = *m_session;
    // Dragging, the fast way (a pen sends samples faster than a big layer is read with a wide kernel); the filter chosen
    // is used once the pen is up.
    const ResampleFilter filter = ResampleFilter(tools::Preferences::instance().transformFilter());
    const bool rough = m_drag.has_value() && (filter == ResampleFilter::Bicubic || filter == ResampleFilter::Lanczos3);
    // A vector layer is stretched while the pen drags too, and drawn again from its strokes once the pen is up.
    m_rough = rough || (m_drag.has_value() && std::any_of(s.parts.begin(), s.parts.end(),
                                                         [](const Part &p) { return p.vector; }));
    const doc::Document &d = m_document->document();
    // Where the pixels land: not cut to the page, what lies out of it (a layer moved partly out of the canvas) comes
    // along. Only so far around the page, to keep a big scale from asking for a picture that does not fit memory.
    const QRect reach = QRect(0, 0, d.width(), d.height()).adjusted(-kReachBeyondPage, -kReachBeyondPage,
                                                                    kReachBeyondPage, kReachBeyondPage);
    const QRect now = s.transform.mapRect(QRectF(s.box)).toAlignedRect().adjusted(-1, -1, 1, 1) & reach;
    std::vector<Part *> todo, drawnAgain;
    bool skipped = false;
    for (Part &part : s.parts) {
        if (onlyLive && !part.live)
            skipped = true;
        else if (part.vector && !m_drag)
            drawnAgain.push_back(&part);
        else
            todo.push_back(&part);
    }
    // The vector layers: their strokes drawn again at the transform, not stretched (one at a time: the brushes are
    // looked up in the library).
    const QRect page(0, 0, d.width(), d.height());
    for (Part *part : drawnAgain) {
        std::vector<doc::VectorStroke> moved = part->restStrokes;
        moved.reserve(moved.size() + part->strokes.size());
        for (const doc::VectorStroke &stroke : part->strokes)
            moved.push_back(transformedStroke(stroke, s.transform));
        doc::TiledImage result;
        drawStrokes(result, moved, *m_toolState, page);
        *part->image = std::move(result);
        part->source = QImage(); // what it stretched is stale
    }
    // Each layer on its own core: they do not share anything but the transform.
    parallelFor(int(todo.size()), [&](int i) {
        Part &part = *todo[std::size_t(i)];
        doc::TiledImage result = part.rest;
        if (!now.isEmpty()) {
            if (part.source.isNull())
                part.source = sourceOf(part.moving, s.box);
            QImage moved;
            if (rough || filter == ResampleFilter::Bilinear || filter == ResampleFilter::Nearest) {
                // The fast way: what the painter does itself (while the pen drags, and for the two plain filters).
                moved = QImage(now.size(), QImage::Format_ARGB32_Premultiplied);
                moved.fill(Qt::transparent);
                QPainter p(&moved);
                p.setRenderHint(QPainter::SmoothPixmapTransform, filter != ResampleFilter::Nearest);
                p.setTransform(s.transform * QTransform::fromTranslate(-now.x(), -now.y()));
                p.drawImage(s.box.topLeft(), part.source);
            } else {
                moved = resampleTransformed(part.source,
                                            QTransform::fromTranslate(s.box.x(), s.box.y()) * s.transform
                                                * QTransform::fromTranslate(-now.x(), -now.y()),
                                            now.size(), filter);
            }
            doc::TiledImage piece;
            piece.write(now.x(), now.y(), now.width(), now.height(),
                        reinterpret_cast<const doc::Pixel *>(moved.constBits()), moved.bytesPerLine() / 4);
            compositeOver(result, piece);
            if (!part.live)
                part.source = QImage(); // out of sight and done: not kept (a cel of many frames would fill the memory)
        }
        *part.image = std::move(result);
    });
    s.everythingDrawn = !skipped;
    if (!s.selection.isEmpty())
        m_document->setSelection(s.transform.map(s.selection)); // the outline follows live
    m_document->notifyPainted(s.shown | now);
    s.shown = now.isEmpty() ? s.shown : now;
    s.changed = true;
    m_repaint();
}

void TransformTool::commit()
{
    if (!m_session)
        return;
    m_drag.reset();
    flushRender();
    if (m_rough)
        renderParts(true); // what the pen left drawn the fast way, with the filter chosen
    if (m_session->changed && !m_session->everythingDrawn)
        renderParts(false); // the layers of the other frames, now
    const std::unique_ptr<Session> s = std::move(m_session);
    m_drag.reset();
    if (s->changed) {
        std::vector<doc::TiledImage> before;
        for (const Part &part : s->parts) {
            before.push_back(part.before);
            if (!part.vector)
                continue;
            // The strokes of a vector layer go where the transform took them, in the same undo step as its pixels.
            std::vector<doc::VectorStroke> moved = part.restStrokes; // what stays, then what moved
            moved.reserve(moved.size() + part.strokes.size());
            for (const doc::VectorStroke &stroke : part.strokes)
                moved.push_back(transformedStroke(stroke, s->transform));
            document::DocumentController::PaintTarget target;
            target.cel = part.cel;
            target.drawing = part.drawing;
            target.layer = part.layer;
            target.image = part.image;
            target.vector = true;
            m_document->attachStrokeChange(target, part.allStrokes, std::move(moved));
        }
        m_document->endPaint(before, doc::CommandKind::Transform);
    }
    else
        m_document->cancelPaint();
    m_repaint();
}

void TransformTool::cancel()
{
    m_renderPending = false;
    m_renderTimer.stop();
    if (!m_session)
        return;
    const std::unique_ptr<Session> s = std::move(m_session);
    m_drag.reset();
    for (const Part &part : s->parts)
        *part.image = part.before;
    if (!s->selection.isEmpty())
        m_document->setSelection(s->selection);
    m_document->notifyPainted(s->shown | s->box);
    m_document->cancelPaint();
    m_repaint();
}

void TransformTool::finish(bool apply)
{
    if (apply)
        commit();
    else
        cancel();
    m_toolState->setActiveTool(m_toolState->previousTool());
}

bool TransformTool::keyPress(int key)
{
    if (!m_session)
        return false;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: finish(true); return true;
    case Qt::Key_Escape: finish(false); return true;
    default: return false;
    }
}

void TransformTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    if (!m_session)
        return;
    const QTransform toView = m_session->transform * view.documentToView();
    const QPolygonF frame = toView.map(QPolygonF(QRectF(m_session->box)));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 3));
    painter.drawPolygon(frame);
    painter.setPen(QPen(QColor(40, 110, 230), 1));
    painter.drawPolygon(frame);
    painter.setBrush(Qt::white);
    for (int i = 0; i < 8; ++i) {
        const QPointF at = toView.map(handlePoint(i));
        painter.drawRect(QRectF(at.x() - kHandleSize / 2, at.y() - kHandleSize / 2, kHandleSize, kHandleSize));
    }
}

} // namespace mothion::canvas
