#pragma once

#include "core/document/TiledImage.h"

#include <QImage>
#include <QPainterPath>
#include <QRect>

namespace mothion::canvas {

// A selection rasterized for pixel work: coverage 0..255 (antialiased edge)
// over `bounds`, in document coordinates.
struct SelectionMask
{
    QImage coverage; // Format_Alpha8, bounds.size()
    QRect bounds;

    static SelectionMask fromPath(const QPainterPath &area);
    bool isEmpty() const { return bounds.isEmpty(); }
    int at(int x, int y) const; // 0 outside `bounds`
};

// Splits `image` into the part under the mask (pixels × coverage) and the
// rest (pixels × (1 − coverage)); together they composite back to `image`.
void splitByMask(const doc::TiledImage &image, const SelectionMask &mask, doc::TiledImage &inside,
                 doc::TiledImage &outside);

// `color` (premultiplied) laid over `image` where the mask covers it, in proportion to the coverage; with `clear`,
// the image is taken away there instead, by the color's alpha (what a fill does to a layer's mask).
void fillByMask(doc::TiledImage &image, const SelectionMask &mask, doc::Pixel color, bool clear = false);

// `top` drawn over `bottom` (premultiplied source-over), in place.
void compositeOver(doc::TiledImage &bottom, const doc::TiledImage &top);

} // namespace mothion::canvas
