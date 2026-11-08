#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <utility>

class QTcpSocket;

namespace mothion::sync {

// A line to the other side of a shared session, carrying whole messages (frames), in order. What sits under it
// (a TCP connection, or in a test two ends in the same process) does not matter to the session.
class Transport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void send(const QByteArray &frame) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString peerName() const { return {}; }

signals:
    void received(const QByteArray &frame);
    void closed(); // the line is gone (the other side left, or the network)
};

// Frames on a TCP connection: each is its length (4 bytes, big-endian) and then its bytes.
class TcpTransport : public Transport
{
    Q_OBJECT

public:
    // Takes over the socket (it must be, or become, connected).
    explicit TcpTransport(QTcpSocket *socket, QObject *parent = nullptr);
    // A connection to a host, being made: connected() or failed() follow.
    static TcpTransport *connectTo(const QString &host, quint16 port, QObject *parent = nullptr);

    void send(const QByteArray &frame) override;
    void close() override;
    bool isOpen() const override;
    QString peerName() const override;

signals:
    void connected();
    void failed(const QString &reason);

private:
    void readFrames();

    QTcpSocket *m_socket;
    QByteArray m_buffer;
    bool m_open = false;
};

// The two ends of a line inside this process (for tests): what one sends, the other receives, from the event loop.
class MemoryTransport : public Transport
{
    Q_OBJECT

public:
    static std::pair<MemoryTransport *, MemoryTransport *> makePair();

    void send(const QByteArray &frame) override;
    void close() override;
    bool isOpen() const override { return m_open; }

private:
    QPointer<MemoryTransport> m_other;
    bool m_open = true;
};

} // namespace mothion::sync
