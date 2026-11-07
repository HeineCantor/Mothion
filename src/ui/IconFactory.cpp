#include "ui/IconFactory.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::ui {

namespace {

// The selection frame: a dashed square, inset by `inset` from the 24x24 edge.
void addDashedSquare(QPainterPath &path, double inset = 5)
{
    const double side = 24 - 2 * inset, dash = 3, gap = (side - 3 * dash) / 2;
    for (int k = 0; k < 4; ++k) {
        const QTransform t = QTransform().translate(12, 12).rotate(90 * k).translate(-12, -12);
        for (int i = 0; i < 3; ++i) {
            const double x = inset + i * (dash + gap);
            path.moveTo(t.map(QPointF(x, inset)));
            path.lineTo(t.map(QPointF(x + dash, inset)));
        }
    }
}

// A small sheet in the bottom-right corner: "onto a new layer".
void addLayerSheet(QPainterPath &path)
{
    path.addRect(QRectF(14.5, 14.5, 6, 6));
}

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
    case IconId::Play: { // a filled triangle
        path.moveTo(7, 4.5);
        path.lineTo(19, 12);
        path.lineTo(7, 19.5);
        path.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(path);
        return;
    }
    case IconId::Stop: // a filled square
        p.setBrush(p.pen().color());
        p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5);
        return;
    case IconId::Pause: // two filled bars
        p.setBrush(p.pen().color());
        p.drawRoundedRect(QRectF(6.5, 5, 4, 14), 1, 1);
        p.drawRoundedRect(QRectF(13.5, 5, 4, 14), 1, 1);
        return;
    case IconId::Speaker:
    case IconId::SpeakerOff: { // a loudspeaker; waves when on, a slash when off
        path.moveTo(4, 9.5);
        path.lineTo(8.5, 9.5);
        path.lineTo(13, 5.5);
        path.lineTo(13, 18.5);
        path.lineTo(8.5, 14.5);
        path.lineTo(4, 14.5);
        path.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(path);
        p.setBrush(Qt::NoBrush);
        if (id == IconId::Speaker) {
            p.drawArc(QRectF(11, 8, 6, 8), -60 * 16, 120 * 16);
            p.drawArc(QRectF(11, 4.5, 11, 15), -60 * 16, 120 * 16);
        } else {
            p.drawLine(QPointF(16, 9), QPointF(22, 15));
            p.drawLine(QPointF(22, 9), QPointF(16, 15));
        }
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
    case IconId::Eyedropper: // a pipette, tip down-left
        path.moveTo(4, 20);
        path.lineTo(5, 17);
        path.lineTo(13, 9);
        path.moveTo(15, 11);
        path.lineTo(7, 19);
        path.lineTo(4, 20);
        path.moveTo(11, 7);
        path.lineTo(17, 13);
        path.moveTo(14, 8);
        path.lineTo(17, 5);
        path.quadTo(19, 3, 20.5, 4.5); // the bulb
        path.quadTo(21, 6, 19, 7);
        path.lineTo(16, 10);
        break;
    case IconId::Fill: // a tipped bucket and a drop
        path.moveTo(11, 3);
        path.lineTo(19, 11);
        path.lineTo(12, 18);
        path.lineTo(4, 10);
        path.closeSubpath();
        path.moveTo(4, 10);
        path.lineTo(19, 11);
        path.moveTo(20, 15);
        path.quadTo(22.5, 18.5, 20, 20);
        path.quadTo(17.5, 18.5, 20, 15);
        break;
    case IconId::Blend: // a round fingertip and the streaks it drags
        path.addEllipse(QPointF(7.5, 12), 4.5, 4.5);
        path.moveTo(13, 8);
        path.lineTo(21, 8);
        path.moveTo(13, 12);
        path.lineTo(19, 12);
        path.moveTo(13, 16);
        path.lineTo(16.5, 16);
        break;
    case IconId::Liquify: // a wave with an arrow
        path.moveTo(3, 9);
        path.cubicTo(6, 4, 9, 4, 12, 9);
        path.cubicTo(15, 14, 18, 14, 21, 9);
        path.moveTo(3, 16);
        path.cubicTo(6, 11, 9, 11, 12, 16);
        path.cubicTo(15, 21, 18, 21, 21, 16);
        break;
    case IconId::Snap: // a horseshoe magnet
        path.moveTo(6, 4);
        path.lineTo(6, 13);
        path.cubicTo(6, 21, 18, 21, 18, 13);
        path.lineTo(18, 4);
        path.moveTo(6, 8);
        path.lineTo(10, 8);
        path.moveTo(14, 8);
        path.lineTo(18, 8);
        break;
    case IconId::Crosshair: // a sight
        path.addEllipse(QPointF(12, 12), 6.5, 6.5);
        path.moveTo(12, 2.5);
        path.lineTo(12, 8);
        path.moveTo(12, 16);
        path.lineTo(12, 21.5);
        path.moveTo(2.5, 12);
        path.lineTo(8, 12);
        path.moveTo(16, 12);
        path.lineTo(21.5, 12);
        break;
    case IconId::Shape: // a square and a circle overlapping
        path.addRect(QRectF(3.5, 4.5, 11, 11));
        path.addEllipse(QPointF(15, 15), 5.5, 5.5);
        break;
    case IconId::Transform: // a box with handles
        path.addRect(QRectF(6, 6, 12, 12));
        for (QPointF at : {QPointF(6, 6), QPointF(18, 6), QPointF(18, 18), QPointF(6, 18), QPointF(12, 6),
                           QPointF(18, 12), QPointF(12, 18), QPointF(6, 12)})
            path.addRect(QRectF(at.x() - 1.5, at.y() - 1.5, 3, 3));
        break;
    case IconId::Move: // four arrows
        path.moveTo(12, 3);
        path.lineTo(12, 21);
        path.moveTo(3, 12);
        path.lineTo(21, 12);
        for (int k = 0; k < 4; ++k) {
            const QTransform t = QTransform().translate(12, 12).rotate(90 * k).translate(-12, -12);
            path.moveTo(t.map(QPointF(9, 6)));
            path.lineTo(t.map(QPointF(12, 3)));
            path.lineTo(t.map(QPointF(15, 6)));
        }
        break;
    case IconId::RectSelect: // dashed square
        addDashedSquare(path);
        break;
    case IconId::Deselect: // dashed square struck through
        addDashedSquare(path);
        path.moveTo(3, 3);
        path.lineTo(21, 21);
        break;
    case IconId::InvertSelection: // dashed square, its other half solid
        addDashedSquare(path);
        path.moveTo(19, 9);
        path.lineTo(19, 19);
        path.lineTo(9, 19);
        path.closeSubpath();
        path.moveTo(19, 14);
        path.lineTo(14, 19);
        break;
    case IconId::EraseSelection: // a burst: what was there is gone
        for (int k = 0; k < 8; ++k) {
            const QTransform t = QTransform().translate(12, 12).rotate(45 * k);
            path.moveTo(t.map(QPointF(0, -4)));
            path.lineTo(t.map(QPointF(0, -8.5)));
        }
        break;
    case IconId::CutToLayer: // scissors over a sheet
        path.addEllipse(QPointF(5.5, 17.5), 2.3, 2.3);
        path.addEllipse(QPointF(11.5, 17.5), 2.3, 2.3);
        path.moveTo(6.8, 15.6);
        path.lineTo(12.5, 3.5);
        path.moveTo(10.2, 15.6);
        path.lineTo(4.5, 3.5);
        addLayerSheet(path);
        break;
    case IconId::CopyToLayer: // two sheets, and the layer
        path.addRect(QRectF(3.5, 3.5, 7, 9));
        path.moveTo(6.5, 12.5);
        path.lineTo(6.5, 15.5);
        path.lineTo(13.5, 15.5);
        path.lineTo(13.5, 6.5);
        path.lineTo(10.5, 6.5);
        addLayerSheet(path);
        break;
    case IconId::MergeLayers: // two sheets, an arrow, one sheet
        path.addRect(QRectF(5.5, 3.5, 13, 4));
        path.addRect(QRectF(5.5, 9.5, 13, 4));
        path.moveTo(12, 15);
        path.lineTo(12, 20);
        path.moveTo(9, 17.5);
        path.lineTo(12, 20.5);
        path.lineTo(15, 17.5);
        break;
    case IconId::LassoSelect: // loop with a tail
        path.moveTo(9, 17);
        path.cubicTo(1, 15, 3, 4, 13, 4);
        path.cubicTo(22, 4, 22, 15, 13, 16);
        path.cubicTo(10, 16.5, 8, 17, 9, 20);
        break;
    case IconId::Puppet: // a pin in a mesh
        path.moveTo(4, 20);
        path.lineTo(12, 13);
        path.lineTo(20, 20);
        path.moveTo(4, 20);
        path.lineTo(20, 20);
        path.moveTo(12, 13);
        path.lineTo(12, 9);
        path.addEllipse(QPointF(12, 6.5), 2.5, 2.5);
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
    case IconId::Lock:
        path.addRoundedRect(QRectF(6, 11, 12, 9), 1.5, 1.5);
        path.moveTo(8.5, 11);
        path.lineTo(8.5, 8);
        path.arcTo(QRectF(8.5, 4.5, 7, 7), 180, -180);
        path.lineTo(15.5, 11);
        break;
    case IconId::Folder: // a cel: a folder with a tab
        path.moveTo(3.5, 7);
        path.lineTo(3.5, 18.5);
        path.lineTo(20.5, 18.5);
        path.lineTo(20.5, 8.5);
        path.lineTo(11, 8.5);
        path.lineTo(9, 5.5);
        path.lineTo(3.5, 5.5);
        path.closeSubpath();
        break;
    case IconId::Camera: // a film camera: the body and its lens
        path.addRect(QRectF(3.5, 7.5, 12, 9));
        path.moveTo(15.5, 10.5);
        path.lineTo(20.5, 7.5);
        path.lineTo(20.5, 16.5);
        path.lineTo(15.5, 13.5);
        break;
    case IconId::MaskAdd: // a square with a hole in it, and a plus
    case IconId::MaskDelete: { // the same, and a cross
        path.addRect(QRectF(3.5, 3.5, 13, 13));
        path.addEllipse(QPointF(10, 10), 3.5, 3.5);
        if (id == IconId::MaskAdd) {
            path.moveTo(18.5, 14);
            path.lineTo(18.5, 22);
            path.moveTo(14.5, 18);
            path.lineTo(22.5, 18);
        } else {
            path.moveTo(15, 14.5);
            path.lineTo(22, 21.5);
            path.moveTo(22, 14.5);
            path.lineTo(15, 21.5);
        }
        break;
    }
    case IconId::Frame: // a frame of film: the picture between sprocket holes
        path.addRect(QRectF(4.5, 3.5, 15, 17));
        path.addRect(QRectF(8, 7, 8, 10));
        for (double y : {5.5, 12.0, 18.5}) {
            path.addRect(QRectF(5.5, y - 1, 1, 2));
            path.addRect(QRectF(17.5, y - 1, 1, 2));
        }
        break;
    case IconId::PreviousFrame:
    case IconId::NextFrame: // a step to a bar: the next (or previous) frame's start
        if (id == IconId::PreviousFrame) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        path.moveTo(7, 6);
        path.lineTo(14, 12);
        path.lineTo(7, 18);
        path.closeSubpath();
        path.moveTo(17, 6);
        path.lineTo(17, 18);
        break;
    case IconId::Wrench: { // an open-ended wrench, head up-right
        p.translate(12, 12);
        p.rotate(-45);
        const double w = 1.6, r = 4.5, cx = 6.5, dx = std::sqrt(r * r - w * w);
        const double a = std::atan2(w, dx) * 180 / std::numbers::pi; // where the handle and the jaw meet the head
        const QRectF head(cx - r, -r, 2 * r, 2 * r);
        path.moveTo(-10, -w);
        path.lineTo(cx - dx, -w);
        path.arcTo(head, 180 - a, -(180 - 2 * a));
        path.lineTo(cx, -w); // the jaw
        path.lineTo(cx, w);
        path.lineTo(cx + dx, w);
        path.arcTo(head, -a, -(180 - 2 * a));
        path.lineTo(-10, w);
        path.arcTo(QRectF(-10 - w, -w, 2 * w, 2 * w), 270, -180);
        path.closeSubpath();
        break;
    }
    case IconId::Add:
        path.moveTo(12, 5);
        path.lineTo(12, 19);
        path.moveTo(5, 12);
        path.lineTo(19, 12);
        break;
    case IconId::Clear: // a sheet crossed out
        path.addRect(QRectF(4.5, 4.5, 15, 15));
        path.moveTo(9, 9);
        path.lineTo(15, 15);
        path.moveTo(15, 9);
        path.lineTo(9, 15);
        break;
    case IconId::Delete: // bin
        path.moveTo(5, 7);
        path.lineTo(19, 7);
        path.moveTo(10, 7);
        path.lineTo(10, 4.5);
        path.lineTo(14, 4.5);
        path.lineTo(14, 7);
        path.moveTo(7, 7);
        path.lineTo(8, 20);
        path.lineTo(16, 20);
        path.lineTo(17, 7);
        break;
    case IconId::ArrowUp:
    case IconId::ArrowDown:
        if (id == IconId::ArrowDown) {
            p.translate(0, 24);
            p.scale(1, -1);
        }
        path.moveTo(12, 19);
        path.lineTo(12, 5);
        path.moveTo(6, 11);
        path.lineTo(12, 5);
        path.lineTo(18, 11);
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

// Renders the glyph when Qt asks for a pixmap, in the colors of the current
// application palette, so icons follow a theme switch without being set again.
// "On" is drawn over the highlight of checked buttons.
class GlyphIconEngine : public QIconEngine
{
public:
    explicit GlyphIconEngine(IconId id, IconTone tone = IconTone::Auto) : m_id(id), m_tone(tone) {}

    QIconEngine *clone() const override { return new GlyphIconEngine(m_id, m_tone); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const QColor color = colorFor(mode, state);
        const int px = qMax(1, qRound(qMin(size.width(), size.height()) * scale));
        const QString key = QStringLiteral("mothion-icon-%1-%2-%3")
                                .arg(static_cast<int>(m_id))
                                .arg(px)
                                .arg(color.rgba(), 8, 16, QLatin1Char('0'));
        QPixmap pm;
        if (!QPixmapCache::find(key, &pm)) {
            pm = render(m_id, px, color);
            QPixmapCache::insert(key, pm);
        }
        pm.setDevicePixelRatio(scale);
        return pm;
    }

private:
    QColor colorFor(QIcon::Mode mode, QIcon::State state) const
    {
        const QPalette pal = QApplication::palette();
        if (mode == QIcon::Disabled)
            return pal.color(QPalette::Disabled, QPalette::ButtonText);
        if (m_tone == IconTone::Selected)
            return pal.color(QPalette::Normal, QPalette::HighlightedText);
        if (m_tone == IconTone::Plain)
            return pal.color(QPalette::Normal, QPalette::ButtonText);
        if (state == QIcon::On)
            return pal.color(QPalette::Normal, QPalette::HighlightedText);
        return pal.color(QPalette::Normal, QPalette::ButtonText);
    }

    IconId m_id;
    IconTone m_tone;
};

} // namespace

QIcon icon(IconId id)
{
    return QIcon(new GlyphIconEngine(id));
}

QIcon icon(IconId id, IconTone tone)
{
    return QIcon(new GlyphIconEngine(id, tone));
}

} // namespace mothion::ui
