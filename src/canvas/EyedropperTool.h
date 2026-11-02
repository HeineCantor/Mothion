#pragma once

#include "canvas/CanvasTool.h"

#include <QColor>

#include <functional>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Picks the main color from the canvas: on press, and live while dragging.
// What is picked is the frame as shown (all visible layers over the paper).
class EyedropperTool : public CanvasTool
{
public:
    // The color shown at a document point; invalid outside the document.
    using Sample = std::function<QColor(const QPointF &docPos)>;

    EyedropperTool(tools::ToolState *state, Sample sample);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    // Gives back the color there was before the press.
    void cancel() override;
    bool isActive() const override { return m_active; }

private:
    void pick(const QPointF &docPos);

    tools::ToolState *m_state;
    Sample m_sample;
    bool m_active = false;
    QColor m_before;
};

} // namespace mothion::canvas
