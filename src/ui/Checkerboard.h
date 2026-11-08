#pragma once

#include <QBrush>
#include <QPainter>
#include <QPixmap>
#include <QRectF>

namespace mothion::ui {

// The squares behind what is see-through: a light gray and white, 8 px each, from the top left of `area`.
inline void paintCheckerboard(QPainter &p, const QRectF &area)
{
    static const QPixmap tile = [] {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::white);
        QPainter t(&pixmap);
        t.fillRect(0, 0, 8, 8, QColor(0xcc, 0xcc, 0xcc));
        t.fillRect(8, 8, 8, 8, QColor(0xcc, 0xcc, 0xcc));
        return pixmap;
    }();
    p.save();
    p.setBrushOrigin(area.topLeft());
    p.fillRect(area, QBrush(tile));
    p.restore();
}

} // namespace mothion::ui
