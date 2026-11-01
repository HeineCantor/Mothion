#pragma once

#include "canvas/DabStamper.h"
#include "core/brush/BrushStyle.h"
#include "core/document/TiledImage.h"

#include <QColor>
#include <QRect>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace mothion::canvas {

// Paints one stroke into a TiledImage, live.
//
// The stroke is accumulated in a separate mask, each dab at its own alpha
// (the brush flow: overlapping dabs build up towards full coverage), and each
// touched tile of the target is recomposed as
//     before + mask × opacity      (brush)
//     before − mask × opacity      (eraser)
// so the stroke opacity is a ceiling the dabs never go past.
//
// With setStyle(), the session also handles what needs the pixels:
// watercolor mixing (each dab takes some of the color under it, the stroke
// so far included), ribbon mode (the tip stretched between dabs) and the
// water edge, applied by finish().
//
// `before` is a snapshot taken at construction: it shares every tile with
// the target, which makes every touched tile copy-on-write, and it is what
// PaintCommand::capture(before(), target) compares against at the end.
// The session knows nothing about documents or undo.
class StrokeSession
{
public:
    // `clip`: area that may be painted (the document bounds); empty = unbounded.
    StrokeSession(doc::TiledImage &target, std::shared_ptr<const DabStamper> stamper, double opacity, bool erase,
                  const QRect &clip = {});
    // Hard round dabs in `color`.
    StrokeSession(doc::TiledImage &target, const QColor &color, double opacity, bool erase,
                  const QRect &clip = {});

    // Ribbon, mixing and water edge settings of the brush.
    void setStyle(const brush::BrushStyle &style);

    // Paints the dabs; returns the changed document rect.
    QRect addDabs(std::span<const brush::BrushDab> dabs);

    // Paints a whole stroke at once, as it is drawn for good (its final
    // points, then finish()), with setStyle(style) applied. For previews and
    // tools; the canvas draws live with addDabs() instead.
    QRect paintStroke(const brush::BrushStyle &style, std::uint32_t seed, std::vector<brush::BrushInput> points);

    // Clears the stroke to paint it again (the final pass); returns the area cleared.
    QRect restart();
    // Applies what is done when the pen lifts (the water edge); returns the changed area.
    QRect finish();

    // Puts the target back as it was before the stroke.
    void cancel();

    const doc::TiledImage &before() const { return m_before; }
    doc::TiledImage &target() { return m_target; }

private:
    struct Paint
    {
        QColor color;
        double alpha = 1.0; // multiplies the dab's alpha
    };
    Paint mixed(const brush::BrushDab &dab, const QColor &color);
    void recomposeTile(doc::TileCoord coord);
    QRect touchedArea() const;

    doc::TiledImage &m_target;
    const doc::TiledImage m_before;
    doc::TiledImage m_mask;
    std::unordered_set<doc::TileCoord, doc::TileCoordHash> m_touched;
    std::shared_ptr<const DabStamper> m_stamper;
    double m_opacity;
    bool m_erase;
    QRect m_clip;

    bool m_ribbon = false;
    brush::MixStyle m_mix;
    brush::WaterEdgeStyle m_waterEdge;
    std::optional<brush::BrushDab> m_previous; // ribbon: the dab the next piece starts from
    std::optional<Paint> m_carried;             // mixing: the paint the brush holds
};

} // namespace mothion::canvas
