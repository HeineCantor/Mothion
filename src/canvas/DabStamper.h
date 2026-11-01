#pragma once

#include "core/brush/BrushStyle.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QRectF>

#include <vector>

class QPainter;

namespace mothion::canvas {

// Puts brush::BrushDab stamps on pixels.
//
// Built once per stroke: it prepares the stamps already in the stroke color
// (a soft round dab from the hardness profile, and each tip image), each
// with a few halved copies so small dabs are drawn from a stamp close to
// their size instead of a large one sampled sparsely. Hard round dabs are
// drawn as antialiased ellipses.
//
// A dab is drawn at its alpha with SourceOver, so overlapping dabs build up
// as a + c·(1 − a): flow < 1 accumulates towards full coverage.
//
// Dabs with color variations (brush::DabColor) are drawn in their own color:
// the stamp is recolored on the fly, so plain dabs stay as cheap as before.
class DabStamper
{
public:
    // `tips`: coverage in the alpha channel (color is ignored). Empty = round dabs.
    DabStamper(const QColor &color, double hardness, const QList<QImage> &tips = {},
               bool verticalThickness = false);

    // The color variations blend towards and shift this one too (default white).
    void setSecondaryColor(const QColor &color);
    QColor color() const { return m_color; }

    // The dab's color: main and secondary color shifted in H/S/V, then blended.
    QColor colorFor(const brush::DabColor &variation) const;

    void draw(QPainter &painter, const brush::BrushDab &dab) const;
    void draw(QPainter &painter, const brush::BrushDab &dab, const QColor &color) const;
    // Ribbon: the tip stretched over the quad joining two dabs, its width
    // across the segment (each dab's radius on either side), its height along it.
    void drawRibbon(QPainter &painter, const brush::BrushDab &from, const brush::BrushDab &to,
                    const QColor &color) const;

    // Document area a dab can touch.
    QRectF bounds(const brush::BrushDab &dab) const;

private:
    struct Mipmap
    {
        std::vector<QImage> levels; // largest first
        const QImage &pick(double pixels) const;
    };
    static Mipmap buildMipmap(const QImage &coverage, const QColor &color);
    QSizeF footprint(const brush::BrushDab &dab) const; // full width × height before rotation
    // `stamp` in `color` (it is stored in the main color).
    QImage recolored(const QImage &stamp, const QColor &color) const;
    const QImage *stampFor(const brush::BrushDab &dab, double pixels) const; // null = hard ellipse

    QColor m_color;
    QColor m_secondary = Qt::white;
    bool m_hard;
    bool m_verticalThickness;
    Mipmap m_soft;
    std::vector<Mipmap> m_tips;
};

} // namespace mothion::canvas
