#include "io/BlenderLinkServer.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <QtEndian>

namespace mothion::io {

namespace {

constexpr quint32 kMaxHeader = 1u << 20;
constexpr quint32 kMaxPayload = 1u << 28; // a frame is a few megabytes: more than this is not one

} // namespace

BlenderLinkServer::BlenderLinkServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &BlenderLinkServer::accept);
}

BlenderLinkServer::~BlenderLinkServer()
{
    // The sockets are children of this object and go after the members: they must not tell of it then.
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        it->socket->disconnect(this);
}

QList<quint16> BlenderLinkServer::defaultPorts()
{
    return {47653, 47654, 47655, 47656};
}

bool BlenderLinkServer::listen(quint16 port)
{
    if (m_server.isListening())
        return true;
    const QList<quint16> ports = port ? QList<quint16>{port} : defaultPorts();
    for (quint16 p : ports)
        if (m_server.listen(QHostAddress::LocalHost, p))
            return true;
    return false;
}

std::optional<BlenderSession> BlenderLinkServer::session(int id) const
{
    const auto it = m_sessions.constFind(id);
    return it == m_sessions.cend() ? std::nullopt : std::optional<BlenderSession>(*it);
}

std::optional<BlenderSession> BlenderLinkServer::sessionForKey(const QString &key) const
{
    for (const BlenderSession &s : m_sessions)
        if (s.key() == key)
            return s;
    return std::nullopt;
}

void BlenderLinkServer::accept()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_clients.insert(socket, Client{socket, {}, 0});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { closed(socket); });
    }
}

void BlenderLinkServer::readFrom(QTcpSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    it->buffer += socket->readAll();
    for (;;) {
        it = m_clients.find(socket); // (a handler may have closed it)
        if (it == m_clients.end() || it->buffer.size() < 8)
            return;
        const quint32 headLength = qFromLittleEndian<quint32>(it->buffer.constData());
        const quint32 payloadLength = qFromLittleEndian<quint32>(it->buffer.constData() + 4);
        if (headLength > kMaxHeader || payloadLength > kMaxPayload) { // not our add-on
            socket->disconnectFromHost();
            return;
        }
        if (quint64(it->buffer.size()) < 8ull + headLength + payloadLength)
            return;
        const QJsonObject header = QJsonDocument::fromJson(it->buffer.mid(8, int(headLength))).object();
        const QByteArray payload = it->buffer.mid(8 + int(headLength), int(payloadLength));
        it->buffer.remove(0, 8 + int(headLength) + int(payloadLength));
        Client client = *it;
        handle(client, header, payload);
        if (auto again = m_clients.find(socket); again != m_clients.end())
            again->session = client.session;
    }
}

BlenderSession BlenderLinkServer::sessionFrom(int id, const QJsonObject &hello)
{
    BlenderSession s;
    s.id = id;
    s.file = hello.value("file").toString();
    s.scene = hello.value("scene").toString();
    s.blender = hello.value("blender").toString();
    s.camera = hello.value("camera").toString();
    s.frameStart = hello.value("frameStart").toInt();
    s.frameEnd = hello.value("frameEnd").toInt();
    s.frame = hello.value("frame").toInt();
    s.fps = hello.value("fps").toDouble(24.0);
    s.viewport = hello.value("viewport").toBool();
    return s;
}

void BlenderLinkServer::handle(Client &client, const QJsonObject &header, const QByteArray &payload)
{
    const QString type = header.value("type").toString();
    if (type == QLatin1String("hello")) {
        const bool first = client.session == 0;
        if (first)
            client.session = m_nextSession++;
        m_sessions.insert(client.session, sessionFrom(client.session, header));
        m_socketOf.insert(client.session, client.socket);
        if (first)
            emit sessionConnected(client.session);
        else
            emit sessionUpdated(client.session);
        return;
    }
    if (client.session == 0) // nothing is heard before the hello
        return;
    if (type == QLatin1String("changed")) {
        m_sessions.insert(client.session, sessionFrom(client.session, header));
        emit sessionUpdated(client.session);
        emit sceneChanged(client.session);
    } else if (type == QLatin1String("frame")) {
        const int request = header.value("id").toInt();
        const int width = header.value("width").toInt(), height = header.value("height").toInt();
        if (!m_requests.contains(request) || width <= 0 || height <= 0 || payload.size() != width * height * 4)
            return;
        QImage image(reinterpret_cast<const uchar *>(payload.constData()), width, height, width * 4,
                     QImage::Format_RGBA8888_Premultiplied);
        emit frameReceived(request, header.value("frame").toInt(),
                           image.convertToFormat(QImage::Format_ARGB32_Premultiplied)); // (a copy: the payload goes)
    } else if (type == QLatin1String("done")) {
        const int request = header.value("id").toInt();
        if (m_requests.remove(request) > 0)
            emit requestFinished(request, header.value("ok").toBool(), header.value("cancelled").toBool(), QString());
    } else if (type == QLatin1String("error")) {
        const int request = header.value("id").toInt();
        // (the add-on says "done" after: the request finishes there; the reason is kept for it)
        if (m_requests.remove(request) > 0)
            emit requestFinished(request, false, false, header.value("message").toString());
    }
}

void BlenderLinkServer::closed(QTcpSocket *socket)
{
    const auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    const int session = it->session;
    m_clients.erase(it);
    socket->deleteLater();
    if (session == 0)
        return;
    m_sessions.remove(session);
    m_socketOf.remove(session);
    QList<int> lost;
    for (auto r = m_requests.cbegin(); r != m_requests.cend(); ++r)
        if (r.value() == session)
            lost << r.key();
    for (int request : lost) {
        m_requests.remove(request);
        emit requestFinished(request, false, false, tr("Blender was closed."));
    }
    emit sessionClosed(session);
}

void BlenderLinkServer::send(int sessionId, const QJsonObject &header)
{
    QTcpSocket *socket = m_socketOf.value(sessionId, nullptr);
    if (!socket)
        return;
    const QByteArray head = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray message(8, Qt::Uninitialized);
    qToLittleEndian<quint32>(quint32(head.size()), message.data());
    qToLittleEndian<quint32>(0u, message.data() + 4);
    message += head;
    socket->write(message);
}

int BlenderLinkServer::request(int sessionId, const BlenderFrameRequest &r)
{
    if (!m_socketOf.contains(sessionId))
        return 0;
    const int id = m_nextRequest++;
    m_requests.insert(id, sessionId);
    QJsonArray frames;
    for (int f : r.frames)
        frames.append(f);
    send(sessionId, QJsonObject{{"type", "render"}, {"id", id}, {"frames", frames}, {"width", r.width}, {"height", r.height},
                                {"shading", r.shading}, {"compositor", r.compositor}, {"background", r.background}});
    return id;
}

void BlenderLinkServer::cancel(int requestId)
{
    const auto it = m_requests.constFind(requestId);
    if (it == m_requests.cend())
        return;
    send(it.value(), QJsonObject{{"type", "cancel"}, {"id", requestId}});
}

} // namespace mothion::io
