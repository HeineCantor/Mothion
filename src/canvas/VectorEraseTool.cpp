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
    std::vector<doc::VectorStroke> strokes;
    if (const doc::Layer *layer = m_document->document().layer(target->cel, target->drawing, target->layer))
        strokes = layer->strokes;
    Session s(std::move(strokes));
    s.target = *target;
    s.before = *target->image;
    s.last = docPos;
    m_session = std::move(s);
    eraseTo(docPos, true);
}

void VectorEraseTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_session)
        return;
    if (QLineF(m_session->last, docPos).length() < 1.0)
        return;
    eraseTo(docPos);
}

void VectorEraseTool::eraseTo(const QPointF &to, bool pressed)
{
    Session &s = *m_session;
    const double radius = m_toolState->settings(tools::ToolId::Eraser).size / 2.0;
    s.state.erase({s.last, to}, radius, modeOf(m_toolState->vectorEraser().mode), pressed);
    s.last = to;
    const QRectF dirty = s.state.takeDirty();
    if (dirty.isNull())
        return;
    // Only the area the cut strokes covered is drawn again, from the strokes that reach into it.
    const doc::Document &d = m_document->document();
    const QRect region = dirty.toAlignedRect().adjusted(-1, -1, 1, 1) & workArea(QRect(0, 0, d.width(), d.height()));
    if (region.isEmpty())
        return;
    doc::TiledImage patch;
    drawStrokes(patch, s.state.strokesIn(region), *m_toolState, region);
    std::vector<doc::Pixel> pixels(std::size_t(region.width()) * std::size_t(region.height()));
    patch.read(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    s.target.image->write(region.x(), region.y(), region.width(), region.height(), pixels.data(), region.width());
    m_document->notifyPainted(region);
}

void VectorEraseTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session)
        return;
    move(docPos, modifiers);
    Session s = std::move(*m_session);
    m_session.reset();
    if (!s.state.changed()) {
        *s.target.image = s.before;
        m_document->cancelPaint();
        return;
    }
    m_document->attachStrokeChange(s.target, std::move(s.original), s.state.strokes());
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
