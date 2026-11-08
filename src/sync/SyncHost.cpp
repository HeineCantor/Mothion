#include "sync/SyncHost.h"

#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"

#include <QDataStream>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTemporaryDir>

#include <algorithm>
#include <set>

namespace mothion::sync {

namespace {

constexpr int kFileChunk = 256 * 1024;

QByteArray payloadOf(const std::function<void(QDataStream &)> &write)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_5);
    write(s);
    return bytes;
}

} // namespace

SyncHost::SyncHost(document::DocumentController *controller, const QString &name, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_name(name.isEmpty() ? tr("Host") : name)
    , m_sync(*controller)
{
    using document::DocumentController;
    connect(controller, &DocumentController::historyChanged, this, [this] { onLocalChange(); });
    connect(controller, &DocumentController::structureChanged, this, [this] { updateOwnLock(); });
    connect(controller, &DocumentController::currentFrameChanged, this, [this] { updateOwnLock(); });
    connect(controller, &DocumentController::currentCelChanged, this, [this] { updateOwnLock(); });
    connect(controller, &DocumentController::documentReplaced, this, [this] {
        if (m_sharing)
            stop(); // another project: the session was about this one
    });
}

SyncHost::~SyncHost() { stop(); }

QStringList SyncHost::participantNames() const
{
    QStringList names;
    for (auto it = m_locks.names.begin(); it != m_locks.names.end(); ++it)
        names << it.value();
    return names;
}

bool SyncHost::listen(quint16 port, const QString &token, QString *error)
{
    m_token = token;
    if (!m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server->nextPendingConnection())
                addTransport(new TcpTransport(socket, this));
        });
    }
    if (!m_server->isListening() && !m_server->listen(QHostAddress::Any, port)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    ensureSharing();
    return true;
}

quint16 SyncHost::port() const { return m_server && m_server->isListening() ? m_server->serverPort() : 0; }

void SyncHost::ensureSharing()
{
    if (m_sharing)
        return;
    m_sharing = true;
    m_controller->remoteDocument().shareIds(kMaxParticipants, 0); // the host's ids are the multiples of 8
    m_sync.resetBaseline();
    m_locks = {};
    m_locks.names[0] = m_name;
    m_ownLock = {0, 0};
    m_controller->setEditGuard([this](doc::CelId cel, doc::DrawingId drawing) {
        return refusalFor(qint64(cel), qint64(drawing));
    });
    m_controller->setLockLabeler([this](doc::CelId cel, doc::DrawingId drawing) {
        const auto it = m_locks.owners.find({qint64(cel), qint64(drawing)});
        return it == m_locks.owners.end() || it->second == 0 ? QString() : m_locks.names.value(it->second);
    });
    updateOwnLock();
    emit participantsChanged();
}

void SyncHost::stop()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    const QList<Peer *> peers = m_peers;
    for (Peer *peer : peers) {
        peer->transport->disconnect(this);
        peer->transport->close();
        peer->transport->deleteLater();
        delete peer;
    }
    m_peers.clear();
    if (!m_sharing)
        return;
    m_sharing = false;
    m_locks = {};
    m_ownLock = {0, 0};
    m_controller->remoteDocument().shareIds(1, 0);
    m_controller->setEditGuard({});
    m_controller->setLockLabeler({});
    emit participantsChanged();
    emit locksChanged();
}

void SyncHost::addTransport(Transport *transport)
{
    ensureSharing();
    transport->setParent(this);
    auto *peer = new Peer;
    peer->transport = transport;
    m_peers.append(peer);
    connect(transport, &Transport::received, this, [this, peer](const QByteArray &frame) { onFrame(peer, frame); });
    connect(transport, &Transport::closed, this, [this, peer] {
        if (m_peers.contains(peer))
            removePeer(peer);
    });
}

void SyncHost::removePeer(Peer *peer)
{
    m_peers.removeOne(peer);
    const bool wasIn = peer->id != 0;
    if (wasIn) {
        emit notice(tr("%1 left").arg(peer->name));
        std::erase_if(m_locks.owners, [&](const auto &e) { return e.second == peer->id; });
        m_locks.names.remove(peer->id);
    }
    peer->transport->disconnect(this);
    peer->transport->deleteLater();
    delete peer;
    if (wasIn) {
        emit participantsChanged();
        updateOwnLock(); // a frame the person held may be ours to take
        broadcastLocks();
    }
}

void SyncHost::sendTo(Peer *peer, const QByteArray &frame)
{
    if (peer->transport->isOpen())
        peer->transport->send(frame);
}

void SyncHost::refuse(Peer *peer, const QString &why)
{
    sendTo(peer, encodeMessage(Msg::Error, payloadOf([&](QDataStream &s) { s << why; })));
    peer->transport->close();
}

QString SyncHost::refusalFor(qint64 cel, qint64 drawing) const
{
    const auto it = m_locks.owners.find({cel, drawing});
    if (it == m_locks.owners.end() || it->second == 0)
        return {};
    return tr("%1 is working on this frame").arg(m_locks.names.value(it->second));
}

void SyncHost::onFrame(Peer *peer, const QByteArray &frame)
{
    const QByteArray payload = messagePayload(frame);
    switch (messageType(frame)) {
    case Msg::Hello: handleHello(peer, payload); break;
    case Msg::Ready: handleReady(peer); break;
    case Msg::Propose: {
        Op op;
        if (peer->ready && decodeOp(payload, op))
            handlePropose(peer, op);
        break;
    }
    case Msg::LockRequest:
        if (peer->ready)
            handleLockRequest(peer, payload);
        break;
    default: break;
    }
}

void SyncHost::handleHello(Peer *peer, const QByteArray &payload)
{
    QDataStream s(payload);
    s.setVersion(QDataStream::Qt_6_5);
    quint32 protocol = 0;
    QString name, token;
    s >> protocol >> name >> token;
    if (s.status() != QDataStream::Ok || peer->id != 0)
        return refuse(peer, tr("This is not a Mothion session."));
    if (protocol != kProtocolVersion)
        return refuse(peer, tr("This version of Mothion does not speak the same way as the host's."));
    if (!m_token.isEmpty() && token != m_token)
        return refuse(peer, tr("The invitation code is not right."));
    quint32 id = 1;
    while (id < kMaxParticipants && m_locks.names.contains(id))
        ++id;
    if (id >= kMaxParticipants)
        return refuse(peer, tr("The session is full."));

    // What the host has made up to now goes to the others first, so the project sent is what they all share.
    onLocalChange();
    QTemporaryDir dir;
    QString failure;
    const QString path = dir.filePath(QStringLiteral("session.moth"));
    if (!dir.isValid() || !io::saveProject(m_controller->document(), path, &failure))
        return refuse(peer, tr("The host could not prepare the project: %1").arg(failure));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return refuse(peer, tr("The host could not read the project."));

    peer->id = id;
    peer->name = name.isEmpty() ? tr("Guest %1").arg(id) : name;
    m_locks.names[id] = peer->name;
    sendTo(peer, encodeMessage(Msg::Welcome, payloadOf([&](QDataStream &out) {
                                   out << id << quint32(kMaxParticipants) << qint64(file.size()) << m_name;
                               })));
    for (;;) {
        const QByteArray chunk = file.read(kFileChunk);
        if (chunk.isEmpty())
            break;
        sendTo(peer, encodeMessage(Msg::FileChunk, chunk));
    }
    sendTo(peer, encodeMessage(Msg::FileEnd));
    peer->joining = true; // what happens from here on waits for its Ready
    emit notice(tr("%1 is joining").arg(peer->name));
    emit participantsChanged();
}

void SyncHost::handleReady(Peer *peer)
{
    if (!peer->joining || peer->ready)
        return;
    peer->ready = true;
    peer->joining = false;
    for (const QByteArray &frame : std::as_const(peer->queued))
        sendTo(peer, frame);
    peer->queued.clear();
    emit notice(tr("%1 joined").arg(peer->name));
    broadcastLocks();
    emit participantsChanged();
}

QString SyncHost::validate(const Op &op, quint32 author) const
{
    auto heldByOther = [&](qint64 cel, qint64 drawing) {
        const auto it = m_locks.owners.find({cel, drawing});
        return it != m_locks.owners.end() && it->second != author;
    };
    const doc::Document &d = m_controller->document();
    switch (op.kind) {
    case Op::Kind::Drawing:
    case Op::Kind::Tiles:
        if (heldByOther(op.cel, op.drawing))
            return tr("%1 is working on that frame").arg(m_locks.ownerName(op.cel, op.drawing));
        break;
    case Op::Kind::Cel: {
        const doc::Cel *cel = d.cel(doc::CelId(op.cel));
        if (!cel)
            break;
        std::set<qint64> wanted;
        for (const QJsonValue &v : QJsonDocument::fromJson(op.json).object().value("drawings").toArray())
            wanted.insert(v.toInteger());
        for (const auto &[id, drawing] : cel->drawings())
            if (!wanted.count(qint64(id)) && heldByOther(op.cel, qint64(id)))
                return tr("%1 is working on a frame that change would delete").arg(m_locks.ownerName(op.cel, qint64(id)));
        break;
    }
    case Op::Kind::CelList: {
        std::set<qint64> wanted;
        for (const QJsonValue &v : QJsonDocument::fromJson(op.json).array())
            wanted.insert(v.toInteger());
        for (int i = 0; i < d.celCount(); ++i) {
            const doc::Cel &cel = d.celAt(i);
            if (wanted.count(qint64(cel.id())))
                continue;
            for (const auto &[id, drawing] : cel.drawings())
                if (heldByOther(qint64(cel.id()), qint64(id)))
                    return tr("%1 is working on a frame of a cel that change would delete")
                        .arg(m_locks.ownerName(qint64(cel.id()), qint64(id)));
        }
        break;
    }
    case Op::Kind::Global: break;
    }
    return {};
}

void SyncHost::handlePropose(Peer *peer, const Op &op)
{
    const QString why = validate(op, peer->id);
    if (!why.isEmpty()) {
        sendTo(peer, encodeMessage(Msg::Reject, payloadOf([&](QDataStream &s) { s << why; })));
        for (const Op &truth : m_sync.current(op)) // what the host has: the person's copy goes back to it
            sendTo(peer, encodeMessage(Msg::Apply, payloadOf([&](QDataStream &s) { s << quint32(0) << encodeOp(truth); })));
        emit notice(tr("A change by %1 was not taken: %2").arg(peer->name, why));
        return;
    }
    m_sync.apply(op);
    broadcast(op, peer->id);
    if (op.kind == Op::Kind::CelList) // two may have changed the order at once: the host's is the one
        broadcast(m_sync.celOrder(), quint32(-1)); // nobody is the author: everybody, the sender too
    // A frame the person made is the person's, unless somebody holds it.
    if (op.kind == Op::Kind::Drawing && !m_locks.owners.count({op.cel, op.drawing})) {
        m_locks.owners[{op.cel, op.drawing}] = peer->id;
        broadcastLocks();
    }
}

void SyncHost::handleLockRequest(Peer *peer, const QByteArray &payload)
{
    QDataStream s(payload);
    s.setVersion(QDataStream::Qt_6_5);
    qint64 cel = 0, drawing = 0;
    s >> cel >> drawing;
    if (s.status() != QDataStream::Ok)
        return;
    std::erase_if(m_locks.owners, [&](const auto &e) { return e.second == peer->id; }); // the old one is let go
    if (drawing != 0 && !m_locks.owners.count({cel, drawing}))
        m_locks.owners[{cel, drawing}] = peer->id;
    updateOwnLock();
    broadcastLocks();
}

void SyncHost::onLocalChange()
{
    if (!m_sharing)
        return;
    for (const Op &op : m_sync.collectChanges())
        broadcast(op, 0);
}

void SyncHost::broadcast(const Op &op, quint32 author)
{
    const QByteArray frame =
        encodeMessage(Msg::Apply, payloadOf([&](QDataStream &s) { s << author << encodeOp(op); }));
    for (Peer *peer : std::as_const(m_peers)) {
        if (peer->id == author)
            continue;
        if (peer->ready)
            sendTo(peer, frame);
        else if (peer->joining)
            peer->queued.append(frame);
    }
}

void SyncHost::broadcastLocks()
{
    const QByteArray frame = encodeMessage(Msg::Locks, encodeLocks(m_locks));
    for (Peer *peer : std::as_const(m_peers))
        if (peer->ready)
            sendTo(peer, frame);
    emit locksChanged();
    m_controller->notifyEditGuardChanged();
}

void SyncHost::updateOwnLock()
{
    if (!m_sharing)
        return;
    const doc::Document &d = m_controller->document();
    const doc::Cel *cel = d.cel(m_controller->currentCel());
    const doc::DrawingId drawing = cel ? cel->timing().drawingAt(m_controller->currentFrame()) : doc::kNoId;
    const std::pair<qint64, qint64> wanted = drawing != doc::kNoId ? std::pair<qint64, qint64>{qint64(cel->id()), qint64(drawing)}
                                                                      : std::pair<qint64, qint64>{0, 0};
    const auto held = m_locks.owners.find(m_ownLock);
    const bool stillHeld = m_ownLock.second != 0 && held != m_locks.owners.end() && held->second == 0;
    if (wanted == m_ownLock && (stillHeld || wanted.second == 0))
        return;
    bool changed = false;
    if (stillHeld) {
        m_locks.owners.erase(held);
        changed = true;
    }
    m_ownLock = {0, 0};
    if (wanted.second != 0 && !m_locks.owners.count(wanted)) {
        m_locks.owners[wanted] = 0;
        m_ownLock = wanted;
        changed = true;
    }
    if (changed)
        broadcastLocks();
}

} // namespace mothion::sync
