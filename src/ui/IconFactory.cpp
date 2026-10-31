#include "ui/IconFactory.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace mothion::ui {

namespace {

// Draws one icon in a 24x24 logical coordinate system.
void drawGlyph(QPainter &p, IconId id)
{
    QPainterPath path;
    switch (id) {
    case IconId::New:
        path.moveTo(6, 3);
        path.lineTo(14, 3);
        path.lineTo(18, 7);
        path.lineTo(18, 21);
        path.lineTo(6, 21);
        path.closeSubpath();
        path.moveTo(14, 3);
        path.lineTo(14, 7);
        path.lineTo(18, 7);
        break;
    case IconId::Open:
        path.moveTo(3, 6);
        path.lineTo(9, 6);
        path.lineTo(11, 8);
        path.lineTo(21, 8);
        path.lineTo(21, 19);
        path.lineTo(3, 19);
        path.closeSubpath();
        break;
    case IconId::Save:
        path.moveTo(12, 3);
        path.lineTo(12, 14);
        path.moveTo(7, 9);
        path.lineTo(12, 14);
        path.lineTo(17, 9);
        path.moveTo(4, 15);
        path.lineTo(4, 20);
        path.lineTo(20, 20);
        path.lineTo(20, 15);
        break;
    case IconId::Undo:
    case IconId::Redo:
        if (id == IconId::Redo) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        path.moveTo(18, 19);
        path.cubicTo(20, 12, 15, 8, 6, 9);
        path.moveTo(9, 5);
        path.lineTo(5, 9);
        path.lineTo(9, 13);
        break;
    case IconId::Brush: {
        path.moveTo(20, 4);
        path.lineTo(11, 13);
        p.drawPath(path);
        QPainterPath tip;
        tip.moveTo(11, 13);
        tip.cubicTo(7, 11, 4, 15, 4, 20);
        tip.cubicTo(9, 20, 13, 17, 11, 13);
        p.setBrush(p.pen().color());
        p.drawPath(tip);
        return;
    }
    case IconId::Eraser:
        p.translate(12, 12);
        p.rotate(-45);
        p.drawRoundedRect(QRectF(-9, -4.5, 18, 9), 1.5, 1.5);
        p.fillRect(QRectF(-9, -4.5, 7, 9), p.pen().color());
        return;
    case IconId::Hand:
        for (int x : {8, 11, 14, 17}) {
            path.moveTo(x, 13);
            path.lineTo(x, x == 8 || x == 17 ? 7 : 4.5);
        }
        path.moveTo(8, 13);
        path.lineTo(5, 10.5);
        path.addRoundedRect(QRectF(7, 11, 11.5, 10), 4, 4);
        break;
    case IconId::Zoom:
        path.addEllipse(QPointF(10, 10), 6, 6);
        path.moveTo(14.5, 14.5);
        path.lineTo(20, 20);
        path.moveTo(7, 10);
        path.lineTo(13, 10);
        path.moveTo(10, 7);
        path.lineTo(10, 13);
        break;
    case IconId::Collapse:
        path.moveTo(6, 15);
        path.lineTo(12, 9);
        path.lineTo(18, 15);
        break;
    case IconId::Expand:
        path.moveTo(6, 9);
        path.lineTo(12, 15);
        path.lineTo(18, 9);
        break;
    case IconId::Float:
        path.addRect(QRectF(4, 9, 11, 11));
        path.moveTo(9, 9);
        path.lineTo(9, 4);
        path.lineTo(20, 4);
        path.lineTo(20, 15);
        path.lineTo(15, 15);
        break;
    case IconId::Close:
        path.moveTo(6, 6);
        path.lineTo(18, 18);
        path.moveTo(18, 6);
        path.lineTo(6, 18);
        break;
    case IconId::ZoomOut:
        path.addEllipse(QPointF(10, 10), 6, 6);
        path.moveTo(14.5, 14.5);
        path.lineTo(20, 20);
        path.moveTo(7, 10);
        path.lineTo(13, 10);
        break;
    case IconId::ZoomFit: // four corners pointing out
        for (const QPointF &c : {QPointF(4, 4), QPointF(20, 4), QPointF(20, 20), QPointF(4, 20)}) {
            const double dx = c.x() < 12 ? 5 : -5;
            const double dy = c.y() < 12 ? 5 : -5;
            path.moveTo(c.x() + dx, c.y());
            path.lineTo(c);
            path.lineTo(c.x(), c.y() + dy);
        }
        break;
    case IconId::ZoomActual: // "1:1"
        path.moveTo(6, 8);
        path.lineTo(8, 6);
        path.lineTo(8, 18);
        path.moveTo(16, 8);
        path.lineTo(18, 6);
        path.lineTo(18, 18);
        path.addEllipse(QPointF(12, 10), 0.6, 0.6);
        path.addEllipse(QPointF(12, 15), 0.6, 0.6);
        break;
    case IconId::RotateLeft:
    case IconId::RotateRight:
        if (id == IconId::RotateRight) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        path.arcMoveTo(QRectF(5, 5, 14, 14), 90);
        path.arcTo(QRectF(5, 5, 14, 14), 90, 270);
        path.moveTo(9, 2);
        path.lineTo(12, 5);
        path.lineTo(9, 8);
        break;
    case IconId::RotateReset:
        path.addEllipse(QPointF(12, 12), 7, 7);
        path.moveTo(12, 12);
        path.lineTo(12, 5);
        break;
    case IconId::FlipHorizontal:
    case IconId::FlipVertical:
        if (id == IconId::FlipVertical) {
            p.translate(24, 0);
            p.rotate(90);
        }
        path.moveTo(12, 3);
        path.lineTo(12, 21);
        path.moveTo(9, 6);
        path.lineTo(3, 18);
        path.lineTo(9, 18);
        path.closeSubpath();
        path.moveTo(15, 6);
        path.lineTo(21, 18);
        path.lineTo(15, 18);
        path.closeSubpath();
        break;
    case IconId::Eye:
        path.moveTo(2, 12);
        path.quadTo(12, 3, 22, 12);
        path.quadTo(12, 21, 2, 12);
        path.addEllipse(QPointF(12, 12), 3, 3);
        break;
    }
    p.drawPath(path);
}

QPixmap render(IconId id, int size, const QColor &color)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    drawGlyph(p, id);
    return pm;
}

} // namespace

QIcon icon(IconId id)
{
    static QHash<int, QIcon> cache;
    const int key = static_cast<int>(id);
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;

    QIcon result;
    for (int size : {16, 24, 32, 48, 64}) {
        result.addPixmap(render(id, size, QColor(0xd0, 0xd0, 0xd6)), QIcon::Normal);
        result.addPixmap(render(id, size, QColor(0x6a, 0x6a, 0x70)), QIcon::Disabled);
        result.addPixmap(render(id, size, Qt::white), QIcon::Normal, QIcon::On);
    }
    cache.insert(key, result);
    return result;
}

} // namespace mothion::ui
