#pragma once

#include "canvas/CanvasTool.h"
#include "canvas/SelectionMask.h"
#include "core/document/TiledImage.h"

#include <QPointF>
#include <QRect>

#include <functional>
#include <optional>
#include <vector>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
enum class ToolId;
}
namespace mothion::doc {
enum class CommandKind;
}

namespace mothion::canvas {

// A brush that works on the pixels already on the current layer instead of laying down paint:
// press and drag, and it acts under the pen along the way. Its size and strength are the tool's
// size and opacity in ToolState. The selection, if there is one, limits it. One undo step per
// drag; cancelled, the layer is put back as it was.
//
// A subclass says what happens at each step (stamp()); the pen's path is cut into steps a
// fraction of the radius long, so the result does not depend on how fast the pen moves.
class EffectBrushTool : public CanvasTool
{
public:
    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_active; }
    bool hover(const QPointF &docPos) override;
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

protected:
    EffectBrushTool(document::DocumentController *document, tools::ToolState *state, tools::ToolId tool,
                    doc::CommandKind kind, QString nothingToDo, std::function<void(const QString &)> status);

    // At the press, before the first step: the brush's footprint is at `at`.
    virtual void begin(const QPointF &at) { (void)at; }
    // One step: the brush is now at `at`, having moved by `step` since the last one (zero for the
    // first step, at the press).
    virtual void stamp(const QPointF &at, const QPointF &step) = 0;

    double radius() const;   // document px
    double strength() const; // 0..1
    // 1 at the center of the brush falling to 0 at its edge, for `distance` from the center.
    double falloff(double distance) const;
    // How much the selection lets through at a pixel: 1 when there is none.
    double allowed(int x, int y) const;
    // The layer's pixels in `area`, row after row (anywhere: what is off the page is clear).
    std::vector<doc::Pixel> read(const QRect &area) const;
    // Puts back the pixels of `area` (the part inside the page).
    void write(const QRect &area, const std::vector<doc::Pixel> &pixels);

    const tools::ToolState *state() const { return m_state; }

private:
    void stepTo(const QPointF &docPos);

    document::DocumentController *m_document;
    tools::ToolState *m_state;
    tools::ToolId m_tool;
    doc::CommandKind m_kind;
    QString m_nothingToDo;
    std::function<void(const QString &)> m_status;

    bool m_active = false;
    doc::TiledImage *m_image = nullptr;
    doc::TiledImage m_before;
    std::optional<SelectionMask> m_mask;
    QPointF m_last;
    QRect m_dirty;
    QPointF m_pen;
    bool m_hasPen = false;
};

// Smudge: drags the color under the pen along with it. Blur: softens what is under the pen.
class BlendTool : public EffectBrushTool
{
public:
    BlendTool(document::DocumentController *document, tools::ToolState *state,
              std::function<void(const QString &)> status);

protected:
    void begin(const QPointF &at) override;
    void stamp(const QPointF &at, const QPointF &step) override;

private:
    std::vector<float> m_carried; // premultiplied ARGB, 4 floats a pixel, over the brush's square
};

// Push: moves the pixels along with the pen. Expand: swells what is under the pen. Shrink: pinches it.
class LiquifyTool : public EffectBrushTool
{
public:
    LiquifyTool(document::DocumentController *document, tools::ToolState *state,
                std::function<void(const QString &)> status);

protected:
    void stamp(const QPointF &at, const QPointF &step) override;
};

} // namespace mothion::canvas
