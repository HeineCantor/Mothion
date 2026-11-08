#include "sync/Transport.h"

#include <QTcpSocket>
#include <QtEndian>

namespace mothion::sync {

namespace {

constexpr quint32 kMaxFrame = 256u * 1024u * 1024u; // a frame longer than this is not one of ours

} // namespace

// --- TcpTransport -------------------------------------------------------------------

TcpTransport::TcpTransport(QTcpSocket *socket, QObject *parent)
    : Transport(parent)
    , m_socket(socket)
{
    m_socket->setParent(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_open = m_socket->state() == QAbstractSocket::ConnectedState;
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpTransport::readFrames);
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_open = true;
        emit connected();
        readFrames();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        readFrames(); // what came before the end is still to be heard
        if (m_open) {
            m_open = false;
            emit closed();
        }
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_open) // never got there
            emit failed(m_socket->errorString());
    });
    if (m_open && m_socket->bytesAvailable() > 0)
        QMetaObject::invokeMethod(this, &TcpTransport::readFrames, Qt::QueuedConnection);
}

TcpTransport *TcpTransport::connectTo(const QString &host, quint16 port, QObject *parent)
{
    auto *socket = new QTcpSocket;
    auto *transport = new TcpTransport(socket, parent);
    socket->connectToHost(host, port);
    return transport;
}

void TcpTransport::send(const QByteArray &frame)
{
    if (!m_open)
        return;
    QByteArray header(4, '\0');
    qToBigEndian<quint32>(quint32(frame.size()), header.data());
    m_socket->write(header);
    m_socket->write(frame);
}

void TcpTransport::close()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->flush(); // what was said last (a reason, a goodbye) gets there before the line is cut
        m_socket->waitForBytesWritten(300);
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
    if (m_open) {
        m_open = false;
        emit closed();
    }
}

bool TcpTransport::isOpen() const { return m_open; }

QString TcpTransport::peerName() const
{
    return m_socket->peerAddress().toString() + QLatin1Char(':') + QString::number(m_socket->peerPort());
}

void TcpTransport::readFrames()
{
    m_buffer += m_socket->readAll();
    for (;;) {
        if (m_buffer.size() < 4)
            return;
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
        if (length > kMaxFrame) { // not our protocol: hang up
            m_buffer.clear();
            close();
            return;
        }
        if (quint32(m_buffer.size()) < 4 + length)
            return; // the rest is on its way
        const QByteArray frame = m_buffer.mid(4, int(length));
        m_buffer.remove(0, int(4 + length));
        emit received(frame);
        if (!m_open)
            return;
    }
}

// --- MemoryTransport ------------------------------------------------------------------

std::pair<MemoryTransport *, MemoryTransport *> MemoryTransport::makePair()
{
    auto *a = new MemoryTransport;
    auto *b = new MemoryTransport;
    a->m_other = b;
    b->m_other = a;
    return {a, b};
}

void MemoryTransport::send(const QByteArray &frame)
{
    if (!m_open || !m_other)
        return;
    QMetaObject::invokeMethod(m_other.data(), [other = m_other, frame] {
        if (other && other->m_open)
            emit other->received(frame);
    }, Qt::QueuedConnection);
}

void MemoryTransport::close()
{
    if (!m_open)
        return;
    m_open = false;
    if (m_other)
        QMetaObject::invokeMethod(m_other.data(), [other = m_other] {
            if (other && other->m_open) {
                other->m_open = false;
                emit other->closed();
            }
        }, Qt::QueuedConnection);
    emit closed();
}

} // namespace mothion::sync
