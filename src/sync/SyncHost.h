#pragma once

#include "sync/DocumentSync.h"
#include "sync/Transport.h"
#include "sync/Wire.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;

namespace mothion::document {
class DocumentController;
}

namespace mothion::sync {

// The host of a shared session: its document is the one that counts. Others join (over TCP, or over any Transport),
// receive the project, and from then on send the changes they make; the host puts them in its own document, passes
// them on to the others, and sends its own changes to everybody. Nobody has to wait for anybody: each applies its
// changes at once.
//
// People do not meet on the same frame: whoever is on a frame (the one shown at the current point of the current cel)
// holds it, and the others are refused editing it (the controller's edit guard) and cannot change it. The host
// decides who holds what, and puts right (sends the truth again) what it refuses.
//
// A session lasts until stop(), or until the host opens another project.
class SyncHost : public QObject
{
    Q_OBJECT

public:
    static constexpr unsigned kMaxParticipants = 8; // the host and seven more: ids are shared out by this

    SyncHost(document::DocumentController *controller, const QString &name, QObject *parent = nullptr);
    ~SyncHost() override;

    // Starts taking connections on a TCP port (0: any), from those who say `token`. False, with `error`, if it cannot.
    bool listen(quint16 port, const QString &token, QString *error = nullptr);
    quint16 port() const;
    // Takes one more participant, whose line is `transport` (owned from now on): for other kinds of line, and tests.
    void addTransport(Transport *transport);
    void setToken(const QString &token) { m_token = token; }
    void setName(const QString &name) { m_name = name.isEmpty() ? tr("Host") : name; }
    void stop();

    bool isSharing() const { return m_sharing; }
    QStringList participantNames() const; // the host first
    QString lockOwner(qint64 cel, qint64 drawing) const { return m_locks.ownerName(cel, drawing); }

signals:
    void participantsChanged();
    void locksChanged();
    void notice(const QString &text); // what a person would want to be told (somebody joined, a change was refused)

private:
    struct Peer
    {
        Transport *transport = nullptr;
        quint32 id = 0;
        QString name;
        bool joining = false; // the project is on its way: changes wait
        bool ready = false;
        QList<QByteArray> queued;
    };

    void ensureSharing();
    void onFrame(Peer *peer, const QByteArray &frame);
    void handleHello(Peer *peer, const QByteArray &payload);
    void handleReady(Peer *peer);
    void handlePropose(Peer *peer, const Op &op);
    void handleLockRequest(Peer *peer, const QByteArray &payload);
    QString validate(const Op &op, quint32 author) const;
    void onLocalChange();
    void updateOwnLock();
    void broadcast(const Op &op, quint32 author);
    void broadcastLocks();
    void sendTo(Peer *peer, const QByteArray &frame);
    void refuse(Peer *peer, const QString &why);
    void removePeer(Peer *peer);
    QString refusalFor(qint64 cel, qint64 drawing) const;

    document::DocumentController *m_controller;
    QString m_name;
    QString m_token;
    DocumentSync m_sync;
    QTcpServer *m_server = nullptr;
    QList<Peer *> m_peers;
    LockTable m_locks;
    std::pair<qint64, qint64> m_ownLock{0, 0};
    bool m_sharing = false;
};

} // namespace mothion::sync
