#pragma once

#include "canvas/Dab.h"
#include "core/document/TiledImage.h"

#include <QColor>
#include <QRect>

#include <span>
#include <unordered_set>

namespace mothion::canvas {

// Paints one stroke into a TiledImage, live.
//
// The stroke is accumulated at full strength in a separate mask, and each
// touched tile of the target is recomposed as
//     before + mask × opacity      (brush)
//     before − mask × opacity      (eraser)
// so overlapping dabs inside one stroke do not build up opacity.
//
// `before` is a snapshot taken at construction: it shares every tile with
// the target, which makes every touched tile copy-on-write, and it is what
// PaintCommand::capture(before(), target) compares against at the end.
// The session knows nothing about documents or undo.
class StrokeSession
{
public:
    // `clip`: area that may be painted (the document bounds); empty = unbounded.
    StrokeSession(doc::TiledImage &target, const QColor &color, double opacity, bool erase,
                  const QRect &clip = {});

    // Paints the dabs; returns the changed document rect.
    QRect addDabs(std::span<const Dab> dabs);

    // Puts the target back as it was before the stroke.
    void cancel();

    const doc::TiledImage &before() const { return m_before; }
    doc::TiledImage &target() { return m_target; }

private:
    void recomposeTile(doc::TileCoord coord);

    doc::TiledImage &m_target;
    const doc::TiledImage m_before;
    doc::TiledImage m_mask;
    std::unordered_set<doc::TileCoord, doc::TileCoordHash> m_touched;
    QColor m_color;
    double m_opacity;
    bool m_erase;
    QRect m_clip;
};

} // namespace mothion::canvas
