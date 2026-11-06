#include "compositing/MaskData.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace mothion::compositing {

QList<MaskShape> parseMaskShapes(const QString &json)
{
    QList<MaskShape> shapes;
    for (const QJsonValue &value : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject o = value.toObject();
        const QString type = o.value("t").toString();
        if (type == QLatin1String("rect")) {
            const QRectF rect(o.value("x").toDouble(), o.value("y").toDouble(), o.value("w").toDouble(),
                              o.value("h").toDouble());
            if (rect.isValid())
                shapes.append(MaskShape::rectangle(rect));
        } else if (type == QLatin1String("lasso")) {
            const QJsonArray points = o.value("p").toArray();
            QPolygonF polygon;
            for (int i = 0; i + 1 < points.size(); i += 2)
                polygon << QPointF(points[i].toDouble(), points[i + 1].toDouble());
            if (polygon.size() >= 3)
                shapes.append(MaskShape::lasso(polygon));
        }
    }
    return shapes;
}

QString maskShapesToJson(const QList<MaskShape> &shapes)
{
    QJsonArray array;
    for (const MaskShape &shape : shapes) {
        if (shape.type == MaskShape::Type::Rectangle) {
            array.append(QJsonObject{{"t", "rect"}, {"x", shape.rect.x()}, {"y", shape.rect.y()},
                                     {"w", shape.rect.width()}, {"h", shape.rect.height()}});
        } else {
            QJsonArray points;
            for (const QPointF &p : shape.polygon) {
                points.append(p.x());
                points.append(p.y());
            }
            array.append(QJsonObject{{"t", "lasso"}, {"p", points}});
        }
    }
    return shapes.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QImage rasterizeMask(const QList<MaskShape> &shapes, const QSize &size, double scale)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter p(&mask);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    for (const MaskShape &shape : shapes) {
        if (shape.type == MaskShape::Type::Rectangle) {
            p.drawRect(QRectF(shape.rect.x() * scale, shape.rect.y() * scale, shape.rect.width() * scale,
                              shape.rect.height() * scale));
        } else {
            QPolygonF scaled;
            for (const QPointF &point : shape.polygon)
                scaled << point * scale;
            p.drawPolygon(scaled);
        }
    }
    return mask;
}

QImage applyMask(const QImage &image, const QImage &mask)
{
    QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (result.isNull() || mask.isNull())
        return result;
    QImage gray = mask.convertToFormat(QImage::Format_Grayscale8);
    if (gray.size() != result.size())
        gray = gray.scaled(result.size());
    for (int y = 0; y < result.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *m = gray.constScanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            const int k = m[x];
            if (k == 255)
                continue;
            const QRgb px = row[x];
            row[x] = qRgba((qRed(px) * k + 127) / 255, (qGreen(px) * k + 127) / 255, (qBlue(px) * k + 127) / 255,
                           (qAlpha(px) * k + 127) / 255);
        }
    }
    return result;
}

} // namespace mothion::compositing
