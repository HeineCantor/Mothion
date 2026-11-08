#include "sync/Wire.h"

#include <QDataStream>
#include <QIODevice>

namespace mothion::sync {

QByteArray encodeMessage(Msg type, const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(1 + payload.size());
    frame.append(char(type));
    frame.append(payload);
    return frame;
}

Msg messageType(const QByteArray &frame) { return frame.isEmpty() ? Msg(0) : Msg(quint8(frame.at(0))); }

QByteArray messagePayload(const QByteArray &frame) { return frame.mid(1); }

QByteArray encodeOp(const Op &op)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_5);
    s << quint8(op.kind) << op.cel << op.drawing << op.layer << op.mask << op.json << quint32(op.tiles.size());
    for (const TileData &t : op.tiles)
        s << t.x << t.y << t.packed;
    return out;
}

bool decodeOp(const QByteArray &bytes, Op &op)
{
    QDataStream s(bytes);
    s.setVersion(QDataStream::Qt_6_5);
    quint8 kind = 0;
    quint32 count = 0;
    s >> kind >> op.cel >> op.drawing >> op.layer >> op.mask >> op.json >> count;
    if (s.status() != QDataStream::Ok || kind > quint8(Op::Kind::Tiles) || count > 100000)
        return false;
    op.kind = Op::Kind(kind);
    op.tiles.clear();
    for (quint32 i = 0; i < count; ++i) {
        TileData t;
        s >> t.x >> t.y >> t.packed;
        if (s.status() != QDataStream::Ok)
            return false;
        op.tiles.append(std::move(t));
    }
    return true;
}

QByteArray encodeLocks(const LockTable &table)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_5);
    s << quint32(table.owners.size());
    for (const auto &[key, owner] : table.owners)
        s << key.first << key.second << owner;
    s << table.names;
    return out;
}

bool decodeLocks(const QByteArray &bytes, LockTable &table)
{
    QDataStream s(bytes);
    s.setVersion(QDataStream::Qt_6_5);
    quint32 count = 0;
    s >> count;
    if (s.status() != QDataStream::Ok || count > 1000000)
        return false;
    table.owners.clear();
    for (quint32 i = 0; i < count; ++i) {
        qint64 cel = 0, drawing = 0;
        quint32 owner = 0;
        s >> cel >> drawing >> owner;
        table.owners[{cel, drawing}] = owner;
    }
    s >> table.names;
    return s.status() == QDataStream::Ok;
}

} // namespace mothion::sync
