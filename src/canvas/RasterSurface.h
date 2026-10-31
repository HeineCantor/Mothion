#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>

#include <span>

namespace mothion::canvas {

// A round brush stamp in document coordinates.
struct Dab
{
    QPointF center;
    double radius = 1.0;
};

// In-memory raster the strokes are drawn on.
//
// A stroke is first accumulated in a separate mask at full strength, and the
// visible image is recomposed as  committed + mask × opacity  (or committed
// minus the mask for the eraser). This way overlapping dabs inside one stroke
// do not build up: opacity is per stroke, as in painting apps.
class RasterSurface
{
public:
    explicit RasterSurface(const QSize &size);

    QSize size() const { return m_committed.size(); }
    const QImage &image() const { return m_composite; } // what to display

    void beginStroke(const QColor &color, double opacity, bool erase);
    // Draws the dabs into the stroke and returns the document rect that changed.
    QRect addDabs(std::span<const Dab> dabs);
    void endStroke();
    bool inStroke() const { return m_inStroke; }

private:
    void recompose(const QRect &rect);

    QImage m_committed;  // finished strokes
    QImage m_strokeMask; // current stroke, full strength, in stroke color
    QImage m_composite;  // committed + current stroke (displayed)
    QColor m_color;
    double m_opacity = 1.0;
    bool m_erase = false;
    bool m_inStroke = false;
    QRect m_strokeBounds;
};

} // namespace mothion::canvas
