#include "canvas/StrokeSession.h"

#include "canvas/TileQt.h"

#include <QPainter>

#include <algorithm>

namespace mothion::canvas {

StrokeSession::StrokeSession(doc::TiledImage &target, std::shared_ptr<const DabStamper> stamper, double opacity,
                             bool erase, const QRect &clip)
    : m_target(target)
    , m_before(target) // pointer copy: shares all tiles
    , m_stamper(std::move(stamper))
    , m_opacity(opacity)
    , m_erase(erase)
    , m_clip(clip)
{
}

StrokeSession::StrokeSession(doc::TiledImage &target, const QColor &color, double opacity, bool erase,
                             const QRect &clip)
    : StrokeSession(target, std::make_shared<DabStamper>(color, 1.0), opacity, erase, clip)
{
}

QRect StrokeSession::addDabs(std::span<const brush::BrushDab> dabs)
{
    if (dabs.empty())
        return {};
    std::vector<QRectF> dabBounds;
    dabBounds.reserve(dabs.size());
    QRectF boundsF;
    for (const brush::BrushDab &d : dabs) {
        dabBounds.push_back(m_stamper->bounds(d));
        boundsF |= dabBounds.back();
    }
    QRect bounds = boundsF.toAlignedRect();
    if (!m_clip.isEmpty())
        bounds &= m_clip;
    if (bounds.isEmpty())
        return {};

    for (const doc::TileCoord &c : tilesIn(bounds)) {
        const QRectF tileArea = tileRect(c);
        // Stamp the dabs touching this tile on the mask, in document coordinates.
        bool any = false;
        for (const QRectF &b : dabBounds)
            any = any || b.intersects(tileArea);
        if (!any)
            continue;
        QImage mask = tileImage(m_mask.writableTile(c));
        {
            QPainter p(&mask);
            p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            p.translate(-c.x * doc::kTileSize, -c.y * doc::kTileSize);
            if (!m_clip.isEmpty())
                p.setClipRect(m_clip);
            for (std::size_t i = 0; i < dabs.size(); ++i)
                if (dabBounds[i].intersects(tileArea))
                    m_stamper->draw(p, dabs[i]);
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
