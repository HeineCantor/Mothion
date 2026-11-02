#include "canvas/EyedropperTool.h"

#include "tools/ToolState.h"

namespace mothion::canvas {

EyedropperTool::EyedropperTool(tools::ToolState *state, Sample sample)
    : m_state(state)
    , m_sample(std::move(sample))
{
}

void EyedropperTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    m_active = true;
    m_before = m_state->color();
    pick(docPos);
}

void EyedropperTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (m_active)
        pick(docPos);
}

void EyedropperTool::release(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_active)
        return;
    pick(docPos);
    m_active = false;
}

void EyedropperTool::cancel()
{
    if (!m_active)
        return;
    m_active = false;
    m_state->setColor(m_before);
}

void EyedropperTool::pick(const QPointF &docPos)
{
    QColor color = m_sample(docPos);
    if (!color.isValid())
        return; // off the sheet: keep the last one
    color.setAlpha(255); // brushes take their opacity elsewhere
    m_state->setColor(color);
}

} // namespace mothion::canvas
