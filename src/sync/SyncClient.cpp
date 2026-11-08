#include "sync/SyncClient.h"

#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"

#include <QDataStream>
#include <QFile>

namespace mothion::sync {

namespace {

QByteArray payloadOf(const std::function<void(QDataStream &)> &write)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_5);
    write(s);
    return bytes;
}

} // namespace

SyncClient::SyncClient(document::DocumentController *controller, const QString &name, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_name(name.isEmpty() ? tr("Guest") : name)
    , m_sync(*controller)
{
    using document::DocumentController;
    connect(controller, &DocumentController::historyChanged, this, [this] { onLocalChange(); });
    connect(controller, &DocumentController::structureChanged, this, [this] { updateOwnLock(); });
    connect(controller, &DocumentController::currentFrameChanged, this, [this] { updateOwnLock(); });
    connect(controller, &DocumentController::currentCelChanged, this, [this] { updateOwnLock(); });
    connect(controller, &DocumentController::documentReplaced, this, [this] {
        if (!m_replacing && m_state != State::Idle)
            leave(); // the user opened another project: it is not the session's any more
    });
}

SyncClient::~SyncClient()
{
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->close();
    }
}

QStringList SyncClient::participantNames() const
{
    QStringList names;
    for (auto it = m_locks.names.begin(); it != m_locks.names.end(); ++it)
        names << it.value();
    return names;
}

void SyncClient::connectTo(const QString &host, quint16 port, const QString &token)
{
    auto *transport = TcpTransport::connectTo(host, port, this);
    connect(transport, &TcpTransport::failed, this, [this](const QString &reason) {
        end(tr("Cannot connect: %1").arg(reason));
    });
    attach(transport, token);
}

void SyncClient::attach(Transport *transport, const QString &token)
{
    leave();
    m_token = token;
    m_transport = transport;
    transport->setParent(this);
    m_state = State::Waiting;
    m_file.clear();
    connect(transport, &Transport::received, this, &SyncClient::onFrame);
    connect(transport, &Transport::closed, this, [this] {
        end(m_state == State::Joined ? QString() : tr("The host closed the connection."));
    });
    auto hello = [this, transport] {
        transport->send(encodeMessage(Msg::Hello, payloadOf([&](QDataStream &s) {
                                          s << kProtocolVersion << m_name << m_token;
                                      })));
    };
    if (auto *tcp = qobject_cast<TcpTransport *>(transport); tcp && !tcp->isOpen())
        connect(tcp, &TcpTransport::connected, this, hello);
    else
        hello();
}

void SyncClient::leave()
{
    if (m_state == State::Idle)
        return;
    if (m_transport)
        m_transport->close(); // its closed() ends it
    end(QString());
}

void SyncClient::end(const QString &failure)
{
    if (m_state == State::Idle)
        return;
    const bool wasJoined = m_state == State::Joined;
    m_state = State::Idle;
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    m_file.clear();
    m_locks = {};
    m_ownLock = {0, 0};
    if (wasJoined) {
        m_controller->remoteDocument().shareIds(1, 0);
        m_controller->setEditGuard({});
        m_controller->setLockLabeler({});
    }
    if (wasJoined)
        emit left();
    else if (!failure.isEmpty())
        emit failed(failure);
    emit participantsChanged();
}

void SyncClient::onFrame(const QByteArray &frame)
{
    const QByteArray payload = messagePayload(frame);
    switch (messageType(frame)) {
    case Msg::Welcome: {
        QDataStream s(payload);
        s.setVersion(QDataStream::Qt_6_5);
        s >> m_id >> m_stride >> m_expected >> m_hostName;
        if (s.status() != QDataStream::Ok || m_stride < 2 || m_id >= m_stride)
            return end(tr("The host's answer was not understood."));
        m_state = State::Receiving;
        m_file.clear();
        m_file.reserve(int(std::min<qint64>(m_expected, 1 << 30)));
        break;
    }
    case Msg::FileChunk:
        if (m_state != State::Receiving)
            break;
        m_file += payload;
        emit progress(m_expected > 0 ? int(std::min<qint64>(99, m_file.size() * 100 / m_expected)) : 0);
        break;
    case Msg::FileEnd:
        if (m_state == State::Receiving)
            finishReceiving();
        break;
    case Msg::Apply: {
        if (m_state != State::Joined)
            break;
        QDataStream s(payload);
        s.setVersion(QDataStream::Qt_6_5);
        quint32 author = 0;
        QByteArray opBytes;
        s >> author >> opBytes;
        Op op;
        if (s.status() == QDataStream::Ok && decodeOp(opBytes, op))
            m_sync.apply(op);
        break;
    }
    case Msg::Reject: {
        QDataStream s(payload);
        s.setVersion(QDataStream::Qt_6_5);
        QString why;
        s >> why;
        emit notice(tr("A change of yours was not taken: %1").arg(why));
        break;
    }
    case Msg::Locks:
        if (m_state == State::Joined && decodeLocks(payload, m_locks)) {
            emit locksChanged();
            emit participantsChanged();
            m_controller->notifyEditGuardChanged();
            updateOwnLock(true); // a frame we wanted may have been let go
        }
        break;
    case Msg::Error: {
        QDataStream s(payload);
        s.setVersion(QDataStream::Qt_6_5);
        QString why;
        s >> why;
        end(why.isEmpty() ? tr("The host did not take you.") : why);
        break;
    }
    default: break;
    }
}

void SyncClient::finishReceiving()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("session.moth"));
    QFile file(path);
    QString failure;
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || file.write(m_file) != m_file.size()) {
        end(tr("The project could not be written to disk."));
        return;
    }
    file.close();
    m_file.clear();
    std::unique_ptr<doc::Document> loaded = io::loadProject(path, &failure);
    if (!loaded) {
        end(tr("The project received could not be opened: %1").arg(failure));
        return;
    }
    m_replacing = true;
    m_controller->replaceDocument(std::move(loaded));
    m_replacing = false;
    m_controller->remoteDocument().shareIds(m_stride, m_id); // this one's ids are its own
    m_sync.resetBaseline();
    m_state = State::Joined;
    m_controller->setEditGuard([this](doc::CelId cel, doc::DrawingId drawing) {
        const auto it = m_locks.owners.find({qint64(cel), qint64(drawing)});
        if (it == m_locks.owners.end() || it->second == m_id)
            return QString();
        return tr("%1 is working on this frame").arg(m_locks.names.value(it->second));
    });
    m_controller->setLockLabeler([this](doc::CelId cel, doc::DrawingId drawing) {
        const auto it = m_locks.owners.find({qint64(cel), qint64(drawing)});
        return it == m_locks.owners.end() || it->second == m_id ? QString() : m_locks.names.value(it->second);
    });
    m_transport->send(encodeMessage(Msg::Ready));
    updateOwnLock();
    emit progress(100);
    emit joined();
}

void SyncClient::onLocalChange()
{
    if (m_state != State::Joined)
        return;
    for (const Op &op : m_sync.collectChanges())
        m_transport->send(encodeMessage(Msg::Propose, encodeOp(op)));
}

void SyncClient::updateOwnLock(bool retry)
{
    if (m_state != State::Joined)
        return;
    const doc::Document &d = m_controller->document();
    const doc::Cel *cel = d.cel(m_controller->currentCel());
    const doc::DrawingId drawing = cel ? cel->timing().drawingAt(m_controller->currentFrame()) : doc::kNoId;
    const std::pair<qint64, qint64> wanted = drawing != doc::kNoId ? std::pair<qint64, qint64>{qint64(cel->id()), qint64(drawing)}
                                                                      : std::pair<qint64, qint64>{0, 0};
    // Asked already, unless it is free now (somebody had it, and let go).
    if (wanted == m_ownLock && !(retry && wanted.second != 0 && !m_locks.owners.count(wanted)))
        return;
    m_ownLock = wanted;
    m_transport->send(encodeMessage(Msg::LockRequest, payloadOf([&](QDataStream &s) { s << wanted.first << wanted.second; })));
}

} // namespace mothion::sync
