#pragma once

#include "sync/DocumentSync.h"
#include "sync/Transport.h"
#include "sync/Wire.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

namespace mothion::document {
class DocumentController;
}

namespace mothion::sync {

// One who joins a shared session (see SyncHost): it receives the project, which replaces the document of the
// controller, and from then on it makes its changes at once in its own document and sends them to the host, and puts
// in the ones the host passes on. When it ends (it leaves, the host does, the line goes) the document stays as it
// is, a project of its own.
class SyncClient : public QObject
{
    Q_OBJECT

public:
    SyncClient(document::DocumentController *controller, const QString &name, QObject *parent = nullptr);
    ~SyncClient() override;

    // Joins the session of a host over TCP, with the host's invitation code; joined() or failed() follow.
    void connectTo(const QString &host, quint16 port, const QString &token);
    // The same over another kind of line (and tests): owned from now on.
    void attach(Transport *transport, const QString &token);
    void leave();

    bool isJoined() const { return m_state == State::Joined; }
    bool isActive() const { return m_state != State::Idle; }
    QStringList participantNames() const;
    QString lockOwner(qint64 cel, qint64 drawing) const { return m_locks.ownerName(cel, drawing); }

signals:
    void progress(int percent); // of the project being received
    void joined();
    void failed(const QString &reason); // it did not get in
    void left();                        // it was in and is not any more
    void participantsChanged();
    void locksChanged();
    void notice(const QString &text);

private:
    enum class State { Idle, Waiting, Receiving, Joined };

    void onFrame(const QByteArray &frame);
    void finishReceiving();
    void onLocalChange();
    void updateOwnLock(bool retry = false);
    void end(const QString &failure);

    document::DocumentController *m_controller;
    QString m_name;
    QString m_token;
    DocumentSync m_sync;
    Transport *m_transport = nullptr;
    State m_state = State::Idle;
    quint32 m_id = 0;
    quint32 m_stride = 1;
    qint64 m_expected = 0;
    QByteArray m_file;
    LockTable m_locks;
    std::pair<qint64, qint64> m_ownLock{0, 0};
    bool m_replacing = false; // the project we received is going in: that is not the user leaving it
};

} // namespace mothion::sync
