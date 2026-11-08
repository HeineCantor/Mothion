#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QtGlobal>

#include <map>
#include <utility>

namespace mothion::sync {

// What a session says, and how. A message is one byte for its kind and then its parts (QDataStream, Qt 6.5).
//
//   Hello      client -> host   protocol version, the client's name, the invitation token
//   Welcome    host -> client   the client's number, the id stride, the size of the project that follows, the host's name
//   FileChunk  host -> client   a piece of the project, as a .moth file; FileEnd closes it
//   Ready      client -> host   the project is loaded: what happened since goes to it from now on
//   Propose    client -> host   a change the client has made (an Op)
//   Apply      host -> clients  a change somebody made (who, and the Op)
//   Reject     host -> client   a change that was not taken, and why (the host sends the truth after it)
//   LockRequest client -> host  the frame the client is working on now (none: 0, 0)
//   Locks      host -> clients  who is working on which frames, and the names of the people
//   Error      host -> client   the reason the host does not take the client (then it hangs up)
enum class Msg : quint8 { Hello = 1, Welcome, FileChunk, FileEnd, Ready, Propose, Apply, Reject, LockRequest, Locks, Error };

constexpr quint32 kProtocolVersion = 1;

struct TileData
{
    qint32 x = 0;
    qint32 y = 0;
    QByteArray packed; // as the project file stores a tile; empty: the tile is transparent (gone)
};

// A change of the document, as the state of one part of it.
//   Global   the frame rate, scene length, paper, camera and compositing network (JSON)
//   CelList  the ids of the cels, bottom first (JSON array)
//   Cel      one cel: its properties, its timing and which drawings it has (JSON)
//   Drawing  one drawing: its name and its layers' properties (JSON)
//   Tiles    some tiles of a layer, or of its mask
struct Op
{
    enum class Kind : quint8 { Global, CelList, Cel, Drawing, Tiles };

    Kind kind = Kind::Global;
    qint64 cel = 0;
    qint64 drawing = 0;
    qint64 layer = 0;
    bool mask = false;
    QByteArray json;
    QList<TileData> tiles;
};

QByteArray encodeMessage(Msg type, const QByteArray &payload = {});
Msg messageType(const QByteArray &frame); // 0 if empty
QByteArray messagePayload(const QByteArray &frame);

QByteArray encodeOp(const Op &op);
bool decodeOp(const QByteArray &bytes, Op &op);

// Who is working on which frame: (cel, drawing) -> the number of the person; and the names of the people.
struct LockTable
{
    std::map<std::pair<qint64, qint64>, quint32> owners;
    QMap<quint32, QString> names;

    QString ownerName(qint64 cel, qint64 drawing) const
    {
        const auto it = owners.find({cel, drawing});
        return it == owners.end() ? QString() : names.value(it->second);
    }
};

QByteArray encodeLocks(const LockTable &table);
bool decodeLocks(const QByteArray &bytes, LockTable &table);

} // namespace mothion::sync
