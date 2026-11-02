#pragma once

#include "canvas/CanvasTool.h"

#include <QImage>

#include <functional>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// The paint bucket: a press fills the area around it with the main color,
// behind what is already drawn there, in the drawing at the current layer and frame (made if the frame is
// empty), as one undo step. The area is found in the current layer or in
// the frame as shown, by the tool's fill settings, and kept inside the
// selection if there is one.
class FillTool : public CanvasTool
{
public:
    // The frame as shown, for areas found in all layers.
    using Frame = std::function<const QImage &()>;

    FillTool(document::DocumentController *document, tools::ToolState *state, Frame frame,
             std::function<void(const QString &)> status);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &, Qt::KeyboardModifiers) override {}
    void release(const QPointF &, Qt::KeyboardModifiers) override {}
    void cancel() override {}
    bool isActive() const override { return false; } // all done on the press

private:
    document::DocumentController *m_document;
    tools::ToolState *m_state;
    Frame m_frame;
    std::function<void(const QString &)> m_status;
};

} // namespace mothion::canvas
