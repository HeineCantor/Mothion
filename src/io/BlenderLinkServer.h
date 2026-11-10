#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>

#include <optional>

class QTcpSocket;

namespace mothion::io {

// A Blender that is connected (through the Mothion Link add-on, see blender/mothion_link): the scene it has open.
struct BlenderSession
{
    int id = 0;
    QString file;     // the .blend (empty: not saved yet)
    QString scene;
    QString blender;  // its version
    QString camera;   // the active camera of the scene
    int frameStart = 0, frameEnd = 0, frame = 0;
    double fps = 24;
    bool viewport = false; // a 3D viewport is there to draw with

    // What a cel remembers to find it again: the file and the scene.
    QString key() const { return file + QLatin1Char('|') + scene; }
    int frameCount() const { return frameEnd - frameStart + 1; }
};

// What is asked of a Blender: the frames of the scene as its 3D viewport draws them off screen, seen by the scene's active
// camera, `width` x `height` pixels. The shading ("KEEP": the viewport's own, or SOLID, MATERIAL, RENDERED) and the
// compositor of the viewport ("KEEP", DISABLED, CAMERA, ALWAYS) are for the capture only; with a `background` the picture has
// the world behind it, without it the picture is transparent where nothing is.
struct BlenderFrameRequest
{
    QList<int> frames;
    int width = 960, height = 540;
    QString shading = QStringLiteral("KEEP");
    QString compositor = QStringLiteral("KEEP");
    bool background = false;
};

// Mothion's end of the live link with Blender: it listens on the computer only (nothing is reachable from outside), and the
// add-on in each Blender connects to it, tells which scene it has and when it changes, and draws the frames asked for.
// The messages are a header of JSON and a payload (the pixels of a frame), each with its length before it.
class BlenderLinkServer : public QObject
{
    Q_OBJECT

public:
    explicit BlenderLinkServer(QObject *parent = nullptr);
    ~BlenderLinkServer() override;

    // The ports the add-on tries, in turn: the first free one is used.
    static QList<quint16> defaultPorts();
    // Starts listening: on `port`, or (0) on the first free of the default ones. False if none could be.
    bool listen(quint16 port = 0);
    quint16 port() const { return m_server.serverPort(); }
    bool isListening() const { return m_server.isListening(); }

    QList<BlenderSession> sessions() const { return m_sessions.values(); }
    std::optional<BlenderSession> session(int id) const;
    // The session that has this file and scene open (the key, see BlenderSession::key()).
    std::optional<BlenderSession> sessionForKey(const QString &key) const;

    // Asks for frames; the answer comes as frameReceived() for each and requestFinished() at the end. 0 if there is no such
    // session. Several requests wait their turn at the Blender.
    int request(int sessionId, const BlenderFrameRequest &request);
    void cancel(int requestId);

signals:
    void sessionConnected(int sessionId);
    // The scene it has open is another one (its file or scene changed), or its details changed.
    void sessionUpdated(int sessionId);
    // Something in the scene was changed by the person (told once the changes have stopped for a moment).
    void sceneChanged(int sessionId);
    void sessionClosed(int sessionId);
    // A frame of a request (a picture of the size asked for, premultiplied).
    void frameReceived(int requestId, int frame, const QImage &image);
    // A request is over: all the frames came (`ok`), it was cancelled, or it failed (`error` says why).
    void requestFinished(int requestId, bool ok, bool cancelled, const QString &error);

private:
    struct Client
    {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        int session = 0;
    };
    void accept();
    void readFrom(QTcpSocket *socket);
    void handle(Client &client, const QJsonObject &header, const QByteArray &payload);
    void closed(QTcpSocket *socket);
    void send(int sessionId, const QJsonObject &header);
    static BlenderSession sessionFrom(int id, const QJsonObject &hello);

    QTcpServer m_server;
    QHash<QTcpSocket *, Client> m_clients;
    QHash<int, BlenderSession> m_sessions;
    QHash<int, QTcpSocket *> m_socketOf; // by session
    QHash<int, int> m_requests;          // request -> session
    int m_nextSession = 1;
    int m_nextRequest = 1;
};

} // namespace mothion::io
