#include "canvas/StrokeSession.h"

#include "canvas/TileQt.h"

#include <QPainter>

#include <algorithm>

namespace mothion::canvas {

StrokeSession::StrokeSession(doc::TiledImage &target, const QColor &color, double opacity, bool erase,
                             const QRect &clip)
    : m_target(target)
    , m_before(target) // pointer copy: shares all tiles
    , m_color(color)
    , m_opacity(opacity)
    , m_erase(erase)
    , m_clip(clip)
{
    m_color.setAlpha(255); // stroke opacity is applied when composing
}

QRect StrokeSession::addDabs(std::span<const Dab> dabs)
{
    if (dabs.empty())
        return {};
    QRectF boundsF;
    for (const Dab &d : dabs) {
        const double r = std::max(d.radius, 0.5);
        boundsF |= QRectF(d.center.x() - r, d.center.y() - r, 2 * r, 2 * r);
    }
    QRect bounds = boundsF.toAlignedRect().adjusted(-1, -1, 1, 1);
    if (!m_clip.isEmpty())
        bounds &= m_clip;
    if (bounds.isEmpty())
        return {};

    for (const doc::TileCoord &c : tilesIn(bounds)) {
        // Stamp the dabs on this tile of the mask, in document coordinates.
        QImage mask = tileImage(m_mask.writableTile(c));
        {
            QPainter p(&mask);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(-c.x * doc::kTileSize, -c.y * doc::kTileSize);
            if (!m_clip.isEmpty())
                p.setClipRect(m_clip);
            p.setPen(Qt::NoPen);
            p.setBrush(m_color);
            for (const Dab &d : dabs) {
                const double r = std::max(d.radius, 0.5);
                p.drawEllipse(d.center, r, r);
            }
        }
        m_touched.insert(c);
        recomposeTile(c);
    }
    return bounds;
}

void StrokeSession::recomposeTile(doc::TileCoord c)
{
    const doc::TilePtr maskTile = m_mask.tile(c);
    const doc::TilePtr beforeTile = m_before.tile(c);
    if (!maskTile) { // nothing of the stroke here (fully clipped)
        m_target.setTile(c, beforeTile);
        return;
    }
    // writableTile() clones the shared "before" tile, or creates a fresh one.
    QImage out = tileImage(m_target.writableTile(c));
    {
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        if (beforeTile)
            p.drawImage(0, 0, tileImage(*beforeTile));
        else
            p.fillRect(out.rect(), Qt::transparent);
        p.setCompositionMode(m_erase ? QPainter::CompositionMode_DestinationOut
                                     : QPainter::CompositionMode_SourceOver);
        p.setOpacity(m_opacity);
        p.drawImage(0, 0, tileImage(*maskTile));
    }
    if (m_target.tile(c)->isTransparent()) // e.g. erased to nothing
        m_target.setTile(c, nullptr);
}

void StrokeSession::cancel()
{
    for (const doc::TileCoord &c : m_touched)
        m_target.setTile(c, m_before.tile(c));
    m_touched.clear();
    m_mask = {};
}

} // namespace mothion::canvas
