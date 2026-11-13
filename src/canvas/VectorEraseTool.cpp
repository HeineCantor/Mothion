#include "canvas/VectorEraseTool.h"

#include "canvas/VectorErase.h"
#include "canvas/VectorRender.h"
#include "tools/ToolState.h"

#include <QLineF>

namespace mothion::canvas {

namespace {

VectorEraseMode modeOf(tools::VectorEraserSettings::Mode mode)
{
    switch (mode) {
    case tools::VectorEraserSettings::Mode::ToIntersection: return VectorEraseMode::ToIntersection;
    case tools::VectorEraserSettings::Mode::WholeLine: return VectorEraseMode::WholeLine;
    default: return VectorEraseMode::Part;
    }
}

} // namespace

VectorEraseTool::VectorEraseTool(document::DocumentController *document, tools::ToolState *toolState,
                                 std::function<void(const QString &)> status)
    : m_document(document)
    , m_toolState(toolState)
    , m_status(std::move(status))
{
}

void VectorEraseTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    QString reason;
    const auto target = m_document->beginPaint(false, &reason, true, true);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return;
    }
    if (!target->vector) { // a mask, or not a vector layer after all: nothing for this eraser
        m_document->cancelPaint();
        return;
    }
    Session s;
    s.target = *target;
    s.before = *target->image;
    if (const doc::Layer *layer = m_document->document().layer(target->cel, target->drawing, target->layer))
        s.original = layer->strokes;
    s.left = s.original;
    s.path = {docPos};
    m_session = std::move(s);
    update();
}

void VectorEraseTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_session)
        return;
    if (QLineF(m_session->path.back(), docPos).length() < 1.0)
        return;
    m_session->path.push_back(docPos);
    update();
}

void VectorEraseTool::update()
{
    Session &s = *m_session;
    const double radius = m_toolState->settings(tools::ToolId::Eraser).size / 2.0;
    VectorErasure erased =
        eraseFromStrokes(s.original, s.path, radius, modeOf(m_toolState->vectorEraser().mode));
    if (!erased.changed && !s.changed)
        return;
    s.changed = erased.changed;
    s.left = std::move(erased.strokes);
    // The layer drawn again from what is left.
    const doc::Document &d = m_document->document();
    const QRect page(0, 0, d.width(), d.height());
    *s.target.image = doc::TiledImage();
    drawStrokes(*s.target.image, s.left, *m_toolState, page);
    m_document->notifyPainted(page);
}

void VectorEraseTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session)
        return;
    move(docPos, modifiers);
    Session s = std::move(*m_session);
    m_session.reset();
    if (!s.changed) {
        *s.target.image = s.before;
        m_document->cancelPaint();
        return;
    }
    m_document->attachStrokeChange(s.target, std::move(s.original), std::move(s.left));
    m_document->endPaint(s.before, doc::CommandKind::Paint);
}

void VectorEraseTool::cancel()
{
    if (!m_session)
        return;
    Session s = std::move(*m_session);
    m_session.reset();
    *s.target.image = s.before;
    const doc::Document &d = m_document->document();
    m_document->notifyPainted(QRect(0, 0, d.width(), d.height()));
    m_document->cancelPaint();
}

} // namespace mothion::canvas
