#include "canvas/TransformTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QLineF>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr double kHandleSize = 8.0;  // screen px, as drawn
constexpr double kGrabRadius = 10.0; // screen px, to pick a handle
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

TransformTool::TransformTool(document::DocumentController *document, tools::ToolState *toolState,
                             std::function<void(const QString &)> status, std::function<void()> repaint)
    : m_document(document)
    , m_toolState(toolState)
    , m_status(std::move(status))
    , m_repaint(std::move(repaint))
{
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
    const auto target = m_document->beginPaint(false, &reason);
    if (!target) {
        m_status(reason.isEmpty() ? QObject::tr("Nothing to transform on this frame") : reason);
        return false;
    }
    auto s = std::make_unique<Session>();
    s->image = target->image;
    s->before = *target->image;
    s->selection = m_document->selection();
    doc::TiledImage moving;
    if (!s->selection.isEmpty())
        splitByMask(s->before, SelectionMask::fromPath(s->selection), moving, s->rest);
    else
        moving = s->before;

    const QRect tiles = tileBounds(moving);
    QRect box;
    if (!tiles.isEmpty()) {
        QImage area(tiles.size(), QImage::Format_ARGB32_Premultiplied);
        moving.read(tiles.x(), tiles.y(), tiles.width(), tiles.height(), reinterpret_cast<doc::Pixel *>(area.bits()),
                    area.bytesPerLine() / 4);
        box = opaqueBounds(area);
        if (!box.isEmpty()) {
            s->source = area.copy(box);
            box.translate(tiles.topLeft());
        }
    }
    if (box.isEmpty()) {
        m_document->cancelPaint();
        m_status(QObject::tr("Nothing to transform here"));
        return false;
    }
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
    if (nearest >= 0) {
        d.handle = handlePoint(nearest);
        // The opposite corner, or the middle of the opposite side.
        d.anchor = handlePoint(nearest < 4 ? (nearest + 2) % 4 : 4 + (nearest - 4 + 2) % 4);
        d.grip = nearest < 4 ? Grip::Corner : Grip::Side;
        d.horizontal = nearest == 5 || nearest == 7;
    } else {
        d.grip = corners.containsPoint(docPos, Qt::OddEvenFill) ? Grip::Move : Grip::Rotate;
    }
    m_drag = d;
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
    render();
}

void TransformTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    move(docPos, modifiers);
    if (m_session && m_session->transform != m_drag->start) {
        m_session->undo.push_back(m_drag->start);
        m_session->redo.clear();
    }
    m_drag.reset();
}

void TransformTool::show()
{
    Session &s = *m_session;
    if (!s.undo.empty()) {
        render();
        return;
    }
    // Back at the start: the drawing and selection as they were, exactly.
    *s.image = s.before;
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
    Session &s = *m_session;
    const doc::Document &d = m_document->document();
    const QRect now = s.transform.mapRect(QRectF(s.box)).toAlignedRect().adjusted(-1, -1, 1, 1)
                      & QRect(0, 0, d.width(), d.height());
    doc::TiledImage result = s.rest;
    if (!now.isEmpty()) {
        QImage moved(now.size(), QImage::Format_ARGB32_Premultiplied);
        moved.fill(Qt::transparent);
        {
            QPainter p(&moved);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setTransform(s.transform * QTransform::fromTranslate(-now.x(), -now.y()));
            p.drawImage(s.box.topLeft(), s.source);
        }
        doc::TiledImage layer;
        layer.write(now.x(), now.y(), now.width(), now.height(), reinterpret_cast<const doc::Pixel *>(moved.constBits()),
                    moved.bytesPerLine() / 4);
        compositeOver(result, layer);
    }
    *s.image = std::move(result);
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
    const std::unique_ptr<Session> s = std::move(m_session);
    m_drag.reset();
    if (s->changed)
        m_document->endPaint(s->before, doc::CommandKind::Transform);
    else
        m_document->cancelPaint();
    m_repaint();
}

void TransformTool::cancel()
{
    if (!m_session)
        return;
    const std::unique_ptr<Session> s = std::move(m_session);
    m_drag.reset();
    *s->image = s->before;
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
