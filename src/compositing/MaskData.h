#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>

namespace mothion::compositing {

// An area of a mask, in pixels of the document: a rectangle, or a free shape drawn by hand (a polygon that
// closes itself). The areas of a mask add up.
struct MaskShape
{
    enum class Type { Rectangle, Lasso };

    Type type = Type::Rectangle;
    QRectF rect;       // Rectangle
    QPolygonF polygon; // Lasso

    static MaskShape rectangle(const QRectF &rect)
    {
        MaskShape s;
        s.rect = rect;
        return s;
    }
    static MaskShape lasso(const QPolygonF &polygon)
    {
        MaskShape s;
        s.type = Type::Lasso;
        s.polygon = polygon;
        return s;
    }
    bool operator==(const MaskShape &other) const
    {
        return type == other.type && rect == other.rect && polygon == other.polygon;
    }
};

// The areas as the node keeps them (Node::data): JSON text. Anything that does not read is left out.
QList<MaskShape> parseMaskShapes(const QString &json);
QString maskShapesToJson(const QList<MaskShape> &shapes);

// The mask as a picture of `size` pixels: gray, 255 inside the areas and 0 outside. `scale` is how much smaller than
// the document the picture is (the preview).
QImage rasterizeMask(const QList<MaskShape> &shapes, const QSize &size, double scale);

// `image` with each pixel as much there as the mask says (255: all, 0: gone; a mask of another size is stretched).
// The result is premultiplied, like the pictures the nodes handle.
QImage applyMask(const QImage &image, const QImage &mask);

} // namespace mothion::compositing
