#pragma once

#include "canvas/CanvasTool.h"
#include "canvas/VectorErase.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"
#include "document/DocumentController.h"

#include <QPointF>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// The eraser on a vector layer, in the mode chosen in the tool's properties (VectorEraserSettings): it takes away
// what its circle covers of the strokes it passes over, or each from there to the nearest crossing with another
// stroke, or the whole of each. The layer is drawn again from what is left while the pen drags; the strokes and the
// pixels are one undo step.
class VectorEraseTool : public CanvasTool
{
public:
    VectorEraseTool(document::DocumentController *document, tools::ToolState *toolState,
                    std::function<void(const QString &)> status);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_session.has_value(); }

private:
    struct Session
    {
        document::DocumentController::PaintTarget target;
        doc::TiledImage before;                  // the layer's pixels at the press
        std::vector<doc::VectorStroke> original; // its strokes at the press
        VectorEraseState state;                  // what is left, as the eraser goes on
        QPointF last;                            // the last point of the path
        explicit Session(std::vector<doc::VectorStroke> strokes)
            : original(strokes)
            , state(std::move(strokes))
        {
        }
    };
    // Takes away what the eraser covers going from the last point to `to`, and draws again only the area that changed.
    void eraseTo(const QPointF &to, bool pressed = false);

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::optional<Session> m_session;
};

} // namespace mothion::canvas
