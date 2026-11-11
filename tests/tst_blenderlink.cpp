#include "document/DocumentController.h"
#include "io/BlenderAddOn.h"
#include "io/BlenderFrameCache.h"
#include "io/BlenderLinkServer.h"
#include "ui/BlenderLiveLink.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QScopeGuard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace mothion;

namespace {

QImage solid(int w, int h, QRgb color)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

// What the add-on in Blender does, enough for the server to be tried: speaks the messages of the link.
class FakeBlender
{
public:
    explicit FakeBlender(quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        connected = socket.waitForConnected(3000);
    }
    void send(const QJsonObject &header, const QByteArray &payload = {})
    {
        const QByteArray head = QJsonDocument(header).toJson(QJsonDocument::Compact);
        QByteArray message(8, Qt::Uninitialized);
        qToLittleEndian<quint32>(quint32(head.size()), message.data());
        qToLittleEndian<quint32>(quint32(payload.size()), message.data() + 4);
        socket.write(message + head + payload);
        socket.flush();
    }
    void hello(const QString &file = QStringLiteral("/scenes/walk.blend"), const QString &scene = QStringLiteral("Scene"))
    {
        send({{"type", "hello"}, {"protocol", 1}, {"blender", "5.1.1"}, {"file", file}, {"scene", scene}, {"camera", "Camera"},
              {"frameStart", 10}, {"frameEnd", 109}, {"frame", 54}, {"fps", 24.0}, {"viewport", true}});
    }
    // The messages that came from the server, as headers.
    QList<QJsonObject> received()
    {
        socket.waitForReadyRead(50);
        buffer += socket.readAll();
        QList<QJsonObject> out;
        while (buffer.size() >= 8) {
            const quint32 h = qFromLittleEndian<quint32>(buffer.constData());
            const quint32 p = qFromLittleEndian<quint32>(buffer.constData() + 4);
            if (quint32(buffer.size()) < 8 + h + p)
                break;
            out << QJsonDocument::fromJson(buffer.mid(8, int(h))).object();
            buffer.remove(0, int(8 + h + p));
        }
        return out;
    }
    QTcpSocket socket;
    QByteArray buffer;
    bool connected = false;
};

// A Blender that serves frames as the add-on does: each frame is a flat gray that tells its number and the version of the
// scene, so what arrived where can be checked. `changeScene()` is the person editing in Blender.
class ServingBlender : public QObject
{
public:
    ServingBlender(quint16 port, const QString &file = QStringLiteral("/scenes/walk.blend"), int frameEnd = 12)
        : m_file(file)
        , m_frameEnd(frameEnd)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        m_connected = m_socket.waitForConnected(3000);
        connect(&m_socket, &QTcpSocket::readyRead, this, &ServingBlender::read);
        connect(&m_timer, &QTimer::timeout, this, &ServingBlender::serveOne);
        sendHeader("hello");
    }
    static int grayOf(int frame, int version) { return 20 + frame * 5 + version * 60; }
    // The scene was changed: another version (and another length, if asked), and the link is told.
    void changeScene(int frameEnd = -1)
    {
        ++version;
        if (frameEnd > 0)
            m_frameEnd = frameEnd;
        sendHeader("changed");
    }
    void close() { m_socket.disconnectFromHost(); }
    bool connected() const { return m_connected; }

    int version = 0;
    int delayMs = 0;                // between two frames
    QList<QList<int>> requested;    // the frames asked for, request by request
    int cancels = 0;

private:
    void sendHeader(const char *type)
    {
        send({{"type", type}, {"protocol", 1}, {"blender", "5.1.1"}, {"file", m_file}, {"scene", "Scene"}, {"camera", "Camera"},
              {"frameStart", 1}, {"frameEnd", m_frameEnd}, {"frame", 1}, {"fps", 24.0}, {"viewport", true}});
    }
    void send(const QJsonObject &header, const QByteArray &payload = {})
    {
        const QByteArray head = QJsonDocument(header).toJson(QJsonDocument::Compact);
        QByteArray message(8, Qt::Uninitialized);
        qToLittleEndian<quint32>(quint32(head.size()), message.data());
        qToLittleEndian<quint32>(quint32(payload.size()), message.data() + 4);
        m_socket.write(message + head + payload);
        m_socket.flush();
    }
    void read()
    {
        m_buffer += m_socket.readAll();
        while (m_buffer.size() >= 8) {
            const quint32 h = qFromLittleEndian<quint32>(m_buffer.constData());
            if (quint32(m_buffer.size()) < 8 + h)
                break;
            const QJsonObject message = QJsonDocument::fromJson(m_buffer.mid(8, int(h))).object();
            m_buffer.remove(0, int(8 + h));
            const QString type = message.value("type").toString();
            if (type == QLatin1String("render")) {
                Job job{message.value("id").toInt(), {}, message.value("width").toInt(), message.value("height").toInt()};
                for (const QJsonValue &f : message.value("frames").toArray())
                    job.frames << f.toInt();
                requested << job.frames;
                m_jobs << job;
                if (!m_timer.isActive())
                    m_timer.start(delayMs);
            } else if (type == QLatin1String("cancel")) {
                ++cancels;
                if (!m_jobs.isEmpty() && m_jobs.first().id == message.value("id").toInt()) {
                    const int id = m_jobs.takeFirst().id;
                    send({{"type", "done"}, {"id", id}, {"ok", false}, {"cancelled", true}});
                }
            }
        }
    }
    void serveOne()
    {
        if (m_jobs.isEmpty()) {
            m_timer.stop();
            return;
        }
        Job &job = m_jobs.first();
        if (job.frames.isEmpty()) {
            send({{"type", "done"}, {"id", job.id}, {"ok", true}, {"cancelled", false}});
            m_jobs.removeFirst();
            return;
        }
        const int frame = job.frames.takeFirst();
        const int gray = grayOf(frame, version);
        QByteArray pixels(job.width * job.height * 4, char(0));
        for (int i = 0; i < job.width * job.height; ++i) {
            pixels[i * 4] = char(gray);
            pixels[i * 4 + 1] = char(gray);
            pixels[i * 4 + 2] = char(gray);
            pixels[i * 4 + 3] = char(255);
        }
        send({{"type", "frame"}, {"id", job.id}, {"frame", frame}, {"width", job.width}, {"height", job.height},
              {"format", "rgba8"}, {"premultiplied", true}},
             pixels);
        m_timer.setInterval(delayMs);
    }
    struct Job
    {
        int id;
        QList<int> frames;
        int width, height;
    };
    QTcpSocket m_socket;
    QByteArray m_buffer;
    QString m_file;
    int m_frameEnd;
    bool m_connected = false;
    QList<Job> m_jobs;
    QTimer m_timer;
};

// A scene saved by the Blender found here: a cube seen by a camera, three frames.
QString makeScene(const QString &blender, const QString &path)
{
    const QString python = QStringLiteral(R"PY(
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
s = bpy.context.scene
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.object
data = bpy.data.cameras.new('cam')
cam = bpy.data.objects.new('cam', data)
s.collection.objects.link(cam)
cam.location = (0, -6, 0)
cam.rotation_euler = (1.5708, 0, 0)
s.camera = cam
s.frame_start = 1
s.frame_end = 3
cube.location.x = -1
cube.keyframe_insert('location', frame=1)
cube.location.x = 1
cube.keyframe_insert('location', frame=3)
bpy.ops.wm.save_as_mainfile(filepath=r'%1')
)PY").arg(path);
    QProcess process;
    process.start(blender, {QStringLiteral("-b"), QStringLiteral("--factory-startup"), QStringLiteral("--python-expr"), python});
    if (!process.waitForFinished(120000) || process.exitCode() != 0)
        return {};
    return QFile::exists(path) ? path : QString();
}

// Starting a real Blender is slow and noisy (a window, the GPU): only when asked for.
bool realBlenderWanted()
{
    return qEnvironmentVariableIntValue("MOTHION_TEST_BLENDER") != 0;
}

} // namespace

class TestBlenderLink : public QObject
{
    Q_OBJECT

private slots:
    void theCacheKeepsFramesByGeneration()
    {
        io::BlenderFrameCache cache;
        const QString key = QStringLiteral("a.blend|Scene");
        QVERIFY(!cache.has(key, 3));
        QVERIFY(cache.get(key, 3).isNull());
        cache.put(key, 3, solid(8, 4, 0xffff0000));
        cache.put(key, 1, solid(8, 4, 0xff00ff00));
        QVERIFY(cache.has(key, 3) && cache.isFresh(key, 3));
        QCOMPARE(cache.get(key, 3).pixel(0, 0), QRgb(0xffff0000));
        QCOMPARE(cache.frames(key), (QList<int>{1, 3}));
        QCOMPARE(cache.freshCount(key), 2);
        // The scene changed: what is kept is old, still there to be shown.
        const quint64 before = cache.generation(key);
        QCOMPARE(cache.invalidate(key), before + 1);
        QVERIFY(cache.has(key, 3) && !cache.isFresh(key, 3));
        QCOMPARE(cache.freshCount(key), 0);
        QCOMPARE(cache.get(key, 3).pixel(0, 0), QRgb(0xffff0000));
        cache.put(key, 3, solid(8, 4, 0xff0000ff)); // made again: fresh, and the new picture
        QVERIFY(cache.isFresh(key, 3) && !cache.isFresh(key, 1));
        QCOMPARE(cache.get(key, 3).pixel(0, 0), QRgb(0xff0000ff));
        QCOMPARE(cache.freshCount(key), 1);
        // Another scene has its own.
        QVERIFY(!cache.has(QStringLiteral("b.blend|Scene"), 3));
        cache.clear(key);
        QVERIFY(!cache.has(key, 3));
        QCOMPARE(cache.memoryBytes(), qint64(0));
    }

    void theCachePutsWhatItCannotHoldOnDisk()
    {
        const qint64 frame = 64 * 64 * 4;
        io::BlenderFrameCache cache(frame * 3); // room for three
        const QString key = QStringLiteral("big|Scene");
        for (int i = 0; i < 8; ++i)
            cache.put(key, i, solid(64, 64, qRgb(i * 30, 0, 0)));
        QVERIFY(cache.memoryBytes() <= frame * 3);
        QCOMPARE(cache.frames(key).size(), 8); // none was lost
        for (int i = 0; i < 8; ++i) { // those on disk come back as they were
            const QImage image = cache.get(key, i);
            QVERIFY2(!image.isNull(), qPrintable(QString::number(i)));
            QCOMPARE(image.size(), QSize(64, 64));
            QCOMPARE(image.pixel(10, 10), qRgb(i * 30, 0, 0));
        }
        QVERIFY(cache.memoryBytes() <= frame * 3);
        // A frame put again replaces the one that was on disk.
        cache.put(key, 0, solid(64, 64, qRgb(0, 0, 200)));
        for (int i = 1; i < 8; ++i)
            cache.put(key, 100 + i, solid(64, 64, qRgb(0, 200, 0))); // push it out again
        QCOMPARE(cache.get(key, 0).pixel(1, 1), qRgb(0, 0, 200));
        cache.clear(key);
        QCOMPARE(cache.memoryBytes(), qint64(0));
    }

    void theServerKnowsTheBlendersThatConnect()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0) && server.isListening());
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy updated(&server, &io::BlenderLinkServer::sessionUpdated);
        QSignalSpy changed(&server, &io::BlenderLinkServer::sceneChanged);
        QSignalSpy closed(&server, &io::BlenderLinkServer::sessionClosed);
        {
            FakeBlender blender(server.port());
            QVERIFY(blender.connected);
            blender.hello();
            QTRY_COMPARE(connected.size(), 1);
            const int id = connected.first().at(0).toInt();
            const auto session = server.session(id);
            QVERIFY(session);
            QCOMPARE(session->file, QStringLiteral("/scenes/walk.blend"));
            QCOMPARE(session->scene, QStringLiteral("Scene"));
            QCOMPARE(session->frameCount(), 100);
            QCOMPARE(session->fps, 24.0);
            QVERIFY(session->viewport);
            QCOMPARE(server.sessionForKey(session->key())->id, id);
            // A change tells the scene again (it may be another one now).
            blender.send({{"type", "changed"}, {"file", "/scenes/other.blend"}, {"scene", "Scene"}, {"frameStart", 1}, {"frameEnd", 24}});
            QTRY_COMPARE(changed.size(), 1);
            QCOMPARE(server.session(id)->file, QStringLiteral("/scenes/other.blend"));
            QCOMPARE(server.session(id)->frameCount(), 24);
            QVERIFY(updated.size() >= 1);
            QVERIFY(!server.sessionForKey(QStringLiteral("/scenes/walk.blend|Scene")));
        }
        QTRY_COMPARE(closed.size(), 1);
        QVERIFY(server.sessions().isEmpty());
    }

    void framesAreAskedForAndComeBack()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy frames(&server, &io::BlenderLinkServer::frameReceived);
        QSignalSpy finished(&server, &io::BlenderLinkServer::requestFinished);
        FakeBlender blender(server.port());
        blender.hello();
        QTRY_COMPARE(connected.size(), 1);
        const int session = connected.first().at(0).toInt();
        QCOMPARE(server.request(99, {}), 0); // no such Blender

        io::BlenderFrameRequest request;
        request.frames = {54, 55, 10};
        request.width = 4;
        request.height = 2;
        request.shading = QStringLiteral("MATERIAL");
        request.compositor = QStringLiteral("ALWAYS");
        const int id = server.request(session, request);
        QVERIFY(id > 0);
        QList<QJsonObject> asked;
        QTRY_VERIFY(!(asked += blender.received()).isEmpty());
        QCOMPARE(asked.first().value("type").toString(), QStringLiteral("render"));
        QCOMPARE(asked.first().value("id").toInt(), id);
        QCOMPARE(asked.first().value("frames").toArray().size(), 3);
        QCOMPARE(asked.first().value("width").toInt(), 4);
        QCOMPARE(asked.first().value("shading").toString(), QStringLiteral("MATERIAL"));
        QCOMPARE(asked.first().value("compositor").toString(), QStringLiteral("ALWAYS"));
        QCOMPARE(asked.first().value("background").toBool(), false);

        // The frames, as the add-on sends them: RGBA bytes, premultiplied, the top row first.
        QByteArray pixels;
        for (int i = 0; i < 8; ++i)
            pixels += QByteArray::fromHex(i < 4 ? "ff000080" : "00ff0000"); // the top row half red (premultiplied: 0xff over 0x80 would be too much, but kept), the bottom clear
        for (int frame : {54, 55}) {
            blender.send({{"type", "frame"}, {"id", id}, {"frame", frame}, {"width", 4}, {"height", 2}, {"format", "rgba8"}, {"premultiplied", true}}, pixels);
        }
        QTRY_COMPARE(frames.size(), 2);
        QCOMPARE(frames.at(0).at(0).toInt(), id);
        QCOMPARE(frames.at(0).at(1).toInt(), 54);
        const QImage image = frames.at(1).at(2).value<QImage>();
        QCOMPARE(image.size(), QSize(4, 2));
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0x80); // the top row first
        QCOMPARE(qAlpha(image.pixel(0, 1)), 0);
        QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
        // A frame of a request nobody asked for, or of the wrong size, is not taken.
        blender.send({{"type", "frame"}, {"id", id + 40}, {"frame", 1}, {"width", 4}, {"height", 2}}, pixels);
        blender.send({{"type", "frame"}, {"id", id}, {"frame", 1}, {"width", 5}, {"height", 2}}, pixels);
        blender.send({{"type", "done"}, {"id", id}, {"ok", true}, {"cancelled", false}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(finished.first().at(0).toInt(), id);
        QVERIFY(finished.first().at(1).toBool());

        // Cancelling says so to the Blender, which answers with done.
        const int second = server.request(session, request);
        QList<QJsonObject> more;
        QTRY_VERIFY(!(more += blender.received()).isEmpty());
        server.cancel(second);
        QTRY_VERIFY(!(more += blender.received()).isEmpty() && more.last().value("type").toString() == QLatin1String("cancel"));
        QCOMPARE(more.last().value("id").toInt(), second);
        blender.send({{"type", "done"}, {"id", second}, {"ok", false}, {"cancelled", true}});
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!finished.last().at(1).toBool() && finished.last().at(2).toBool());

        // An error from the Blender finishes the request with its reason.
        const int third = server.request(session, request);
        QTRY_VERIFY(!(more += blender.received()).isEmpty());
        blender.send({{"type", "error"}, {"id", third}, {"message", "The scene has no active camera."}});
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished.last().at(3).toString(), QStringLiteral("The scene has no active camera."));
    }

    void aBlenderThatLeavesFinishesWhatWasAskedOfIt()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy finished(&server, &io::BlenderLinkServer::requestFinished);
        int id = 0;
        {
            FakeBlender blender(server.port());
            blender.hello();
            QTRY_COMPARE(connected.size(), 1);
            id = server.request(connected.first().at(0).toInt(), {});
            QVERIFY(id > 0);
            QTest::qWait(50);
        }
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toInt(), id);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!finished.first().at(3).toString().isEmpty());
    }

    void somethingThatIsNotOurAddOnIsLetGo()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QTcpSocket stranger;
        stranger.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(stranger.waitForConnected(3000));
        stranger.write("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"); // as if a browser: a length far too long
        QTRY_VERIFY(stranger.state() != QAbstractSocket::ConnectedState || stranger.waitForDisconnected(200));
        // Nothing before the hello counts.
        FakeBlender blender(server.port());
        blender.send({{"type", "changed"}, {"file", "x"}});
        QTest::qWait(100);
        QCOMPARE(connected.size(), 0);
        QVERIFY(server.sessions().isEmpty());
        blender.hello();
        QTRY_COMPARE(connected.size(), 1);
    }

    // Bringing a scene in: the cel is there at once with a frame for each frame of the scene, the frames fill in from where the
    // timeline is, the cel remembers the scene, and the whole is one undo step.
    void aSceneIsBroughtInAsACel()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        io::BlenderFrameCache cache;
        document::DocumentController controller;
        controller.setSceneLength(30);
        ui::BlenderLiveLink live(&controller, &server, &cache);
        ui::BlenderLiveLink::Quality quality;
        quality.percent = 5;
        live.setQuality(quality);
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy finished(&live, &ui::BlenderLiveLink::finished);
        ServingBlender blender(server.port());
        QVERIFY(blender.connected());
        QTRY_COMPARE(connected.size(), 1);
        const int session = connected.first().at(0).toInt();
        const int cels = controller.document().celCount();
        QVERIFY(live.import(session, 4, 7)); // the timeline is on frame 8 of the document: the 4th frame of the scene
        QVERIFY(live.isBusy());
        QVERIFY(!live.import(session, 4, 7)); // one at a time
        // The cel is there before any picture: a drawing at each frame from 4 to 15.
        QCOMPARE(controller.document().celCount(), cels + 1);
        const doc::Cel &cel = controller.document().celAt(controller.document().celCount() - 1);
        QCOMPARE(cel.properties().name, std::string("walk"));
        for (int f = 4; f < 16; ++f)
            QVERIFY(cel.timing().drawingAt(f) != doc::kNoId);
        QCOMPARE(cel.timing().drawingAt(16), doc::kNoId);
        QCOMPARE(controller.currentCel(), cel.id());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(finished.first().at(2).toInt(), 12);
        QVERIFY(!finished.first().at(3).toBool()); // an import, not an update
        // From the frame looked at to the end, then the frames before it.
        QCOMPARE(blender.requested.size(), 1);
        QCOMPARE(blender.requested.first(), (QList<int>{4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 2, 3}));
        // Each frame has its own gray, as the page.
        for (int frame = 1; frame <= 12; ++frame) {
            const doc::Drawing *drawing = cel.drawingAt(4 + frame - 1);
            QVERIFY(drawing);
            const QRgb pixel = drawing->layers.front().image.pixel(controller.document().width() / 2, controller.document().height() / 2);
            QCOMPARE(qRed(pixel), ServingBlender::grayOf(frame, 0));
            QCOMPARE(qAlpha(pixel), 255);
        }
        // The cel knows its scene, and the frames are in the cache.
        const auto links = controller.blenderLinks();
        QCOMPARE(links.size(), size_t(1));
        QCOMPARE(QString::fromStdString(links.front().second.path), QStringLiteral("/scenes/walk.blend"));
        QCOMPARE(QString::fromStdString(links.front().second.scene), QStringLiteral("Scene"));
        QCOMPARE(links.front().second.startFrame, 4);
        QCOMPARE(links.front().second.percent, 5);
        const QString key = QStringLiteral("/scenes/walk.blend|Scene");
        QCOMPARE(cache.freshCount(key), 12);
        QVERIFY(live.isConnected(cel.id()));
        // Undo: the pictures, then the cel.
        controller.undo();
        QCOMPARE(controller.document().celCount(), cels + 1);
        QCOMPARE(qAlpha(controller.document().celAt(cels).drawingAt(4)->layers.front().image.pixel(10, 10)), 0);
        controller.undo();
        QCOMPARE(controller.document().celCount(), cels);
        QVERIFY(controller.blenderLinks().empty());
    }

    // A change in the scene brings the cel up to date by itself: the picture looked at first, the others together at the end,
    // and the whole is one undo step. A scene that got longer gets more frames.
    void aChangedSceneBringsItsCelUpToDate()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        io::BlenderFrameCache cache;
        document::DocumentController controller;
        controller.setSceneLength(30);
        ui::BlenderLiveLink live(&controller, &server, &cache);
        ui::BlenderLiveLink::Quality quality;
        quality.percent = 5;
        live.setQuality(quality);
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy finished(&live, &ui::BlenderLiveLink::finished);
        ServingBlender blender(server.port(), QStringLiteral("/scenes/walk.blend"), 6);
        QTRY_COMPARE(connected.size(), 1);
        QVERIFY(live.import(connected.first().at(0).toInt(), 0, 0));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        const doc::CelId cel = controller.blenderLinks().front().first;
        auto gray = [&](int documentFrame) {
            const doc::Drawing *d = controller.document().cel(cel)->drawingAt(documentFrame);
            return d ? qRed(d->layers.front().image.pixel(controller.document().width() / 2, controller.document().height() / 2)) : -1;
        };
        for (int f = 1; f <= 6; ++f)
            QCOMPARE(gray(f - 1), ServingBlender::grayOf(f, 0));

        // The scene changes (and gets two frames longer): all that is new, the new frames too.
        controller.setCurrentFrame(2);
        QList<int> events; // the order of things: a picture put in (1) and a frame that arrived (2)
        connect(&controller, &document::DocumentController::pixelsChanged, &live, [&](doc::CelId, doc::DrawingId, const QRect &) { events << 1; });
        connect(&server, &io::BlenderLinkServer::frameReceived, &live, [&](int, int, const QImage &) { events << 2; });
        blender.delayMs = 40;
        blender.changeScene(8);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 20000);
        QVERIFY(finished.last().at(0).toBool());
        QVERIFY(finished.last().at(3).toBool()); // an update
        for (int f = 1; f <= 8; ++f)
            QCOMPARE(gray(f - 1), ServingBlender::grayOf(f, 1));
        // Stopped, the picture looked at (the third frame) goes in as it comes, before the later frames have come.
        // (the picture goes in as the frame arrives, before the test hears of the frame)
        QVERIFY(events.size() > 3);
        QCOMPARE(events.first(), 1);
        QCOMPARE(events.at(1), 2);
        QCOMPARE(events.at(2), 2);
        QVERIFY(events.indexOf(1) < events.lastIndexOf(2));
        QCOMPARE(blender.requested.last().first(), 3); // asked from the frame looked at
        // One undo step brings back all the old pictures, and the added frames go.
        controller.undo();
        for (int f = 1; f <= 6; ++f)
            QCOMPARE(gray(f - 1), ServingBlender::grayOf(f, 0));
        QCOMPARE(gray(6), -1);
        controller.redo();
        QCOMPARE(gray(7), ServingBlender::grayOf(8, 1));
    }

    // While the animation plays, the new pictures wait and go in together when all are there.
    void anUpdateWaitsWhileThePlayPlays()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        io::BlenderFrameCache cache;
        document::DocumentController controller;
        controller.setSceneLength(30);
        ui::BlenderLiveLink live(&controller, &server, &cache);
        ui::BlenderLiveLink::Quality quality;
        quality.percent = 5;
        live.setQuality(quality);
        bool playing = false;
        live.setPlayingProbe([&] { return playing; });
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy finished(&live, &ui::BlenderLiveLink::finished);
        ServingBlender blender(server.port(), QStringLiteral("/scenes/walk.blend"), 6);
        QTRY_COMPARE(connected.size(), 1);
        QVERIFY(live.import(connected.first().at(0).toInt(), 0, 0));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QList<int> events;
        connect(&controller, &document::DocumentController::pixelsChanged, &live, [&](doc::CelId, doc::DrawingId, const QRect &) { events << 1; });
        connect(&server, &io::BlenderLinkServer::frameReceived, &live, [&](int, int, const QImage &) { events << 2; });
        playing = true;
        blender.delayMs = 20;
        blender.changeScene();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 20000);
        QCOMPARE(events.count(2), 6);
        QCOMPARE(events.first(), 2);
        QVERIFY2(events.indexOf(1) > events.lastIndexOf(2), qPrintable(QString::number(events.indexOf(1))));
        QCOMPARE(events.count(1), 6); // and then all of them
    }

    // A scene that changes again while it is being brought up to date starts over; the cel ends with the last version.
    void aSceneChangedAgainStartsOver()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        io::BlenderFrameCache cache;
        document::DocumentController controller;
        controller.setSceneLength(30);
        ui::BlenderLiveLink live(&controller, &server, &cache);
        ui::BlenderLiveLink::Quality quality;
        quality.percent = 5;
        live.setQuality(quality);
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy finished(&live, &ui::BlenderLiveLink::finished);
        ServingBlender blender(server.port(), QStringLiteral("/scenes/walk.blend"), 8);
        QTRY_COMPARE(connected.size(), 1);
        QVERIFY(live.import(connected.first().at(0).toInt(), 0, 0));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        const doc::CelId cel = controller.blenderLinks().front().first;
        blender.delayMs = 60;
        blender.changeScene();
        QTRY_VERIFY_WITH_TIMEOUT(blender.requested.size() >= 2, 5000);
        QTest::qWait(150); // some frames of the first update are in
        blender.changeScene(); // and the scene changes again
        QTRY_VERIFY_WITH_TIMEOUT(!live.isBusy(), 30000);
        QCOMPARE(blender.version, 2);
        for (int f = 1; f <= 8; ++f) {
            const doc::Drawing *d = controller.document().cel(cel)->drawingAt(f - 1);
            QCOMPARE(qRed(d->layers.front().image.pixel(controller.document().width() / 2, controller.document().height() / 2)),
                     ServingBlender::grayOf(f, 2));
        }
        QVERIFY(blender.cancels >= 1);
        QVERIFY(finished.size() >= 2);
        QVERIFY(finished.last().at(0).toBool());
        QCOMPARE(blender.requested.size(), 3); // the import, the update that was cut, the one that went on
    }

    // Stopped half way, or the Blender closed, or the cel undone: nothing is left running, and what was made stays.
    void aImportThatIsStoppedLeavesWhatWasMade()
    {
        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        io::BlenderFrameCache cache;
        document::DocumentController controller;
        controller.setSceneLength(30);
        ui::BlenderLiveLink live(&controller, &server, &cache);
        ui::BlenderLiveLink::Quality quality;
        quality.percent = 5;
        live.setQuality(quality);
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy finished(&live, &ui::BlenderLiveLink::finished);
        // Stopped by the person.
        {
            ServingBlender blender(server.port(), QStringLiteral("/scenes/walk.blend"), 12);
            blender.delayMs = 60;
            QTRY_COMPARE(connected.size(), 1);
            QVERIFY(live.import(connected.first().at(0).toInt(), 0, 0));
            QTest::qWait(250);
            live.cancel();
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
            QVERIFY(!finished.first().at(0).toBool());
            QVERIFY(finished.first().at(1).toString().isEmpty()); // a stop, not a failure
            QVERIFY(finished.first().at(2).toInt() > 0 && finished.first().at(2).toInt() < 12);
            QVERIFY(!live.isBusy());
            QCOMPARE(controller.document().celCount() > 1, true); // the cel is there, with what came
        }
        // The Blender closes in the middle: a failure with a reason.
        QSignalSpy closed(&server, &io::BlenderLinkServer::sessionClosed);
        {
            ServingBlender blender(server.port(), QStringLiteral("/scenes/other.blend"), 12);
            blender.delayMs = 60;
            QTRY_COMPARE(connected.size(), 2);
            QVERIFY(live.import(connected.last().at(0).toInt(), 0, 0));
            QTest::qWait(150);
            blender.close();
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);
            QVERIFY(!finished.last().at(0).toBool());
            QVERIFY(!finished.last().at(1).toString().isEmpty());
        }
        QTRY_VERIFY(closed.size() >= 1);
        // The cel undone while the frames come: it stops.
        {
            ServingBlender blender(server.port(), QStringLiteral("/scenes/third.blend"), 12);
            blender.delayMs = 60;
            QTRY_COMPARE(connected.size(), 3);
            const int cels = controller.document().celCount();
            QVERIFY(live.import(connected.last().at(0).toInt(), 0, 0));
            QCOMPARE(controller.document().celCount(), cels + 1);
            QTest::qWait(150);
            controller.undo(); // the cel goes
            QCOMPARE(controller.document().celCount(), cels);
            QTRY_VERIFY_WITH_TIMEOUT(!live.isBusy(), 10000);
        }
    }

    // Through the window: the dialog's job, with the real MainWindow.
    void theWindowBringsASceneInFromAConnectedBlender()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        io::BlenderLinkServer *server = window.blenderServer();
        QVERIFY(server && server->isListening());
        auto *controller = window.findChild<document::DocumentController *>();
        QVERIFY(controller);
        ui::BlenderLiveLink::Quality quality;
        quality.percent = 5;
        window.blenderLive()->setQuality(quality);
        QSignalSpy connected(server, &io::BlenderLinkServer::sessionConnected);
        ServingBlender blender(server->port(), QStringLiteral("/scenes/walk.blend"), 5);
        QTRY_COMPARE(connected.size(), 1);
        const int cels = controller->document().celCount();
        QVERIFY(window.startBlenderImport(connected.first().at(0).toInt(), 2));
        QVERIFY(!window.startBlenderImport(connected.first().at(0).toInt(), 2));
        QTRY_VERIFY_WITH_TIMEOUT(!window.blenderImportRunning(), 20000);
        QCOMPARE(controller->document().celCount(), cels + 1);
        QCOMPARE(controller->blenderLinks().size(), size_t(1));
        // A change in the scene updates it (the window has the link on by itself).
        const doc::CelId cel = controller->blenderLinks().front().first;
        blender.changeScene();
        auto grayAt = [&](int documentFrame) {
            const doc::Drawing *d = controller->document().cel(cel)->drawingAt(documentFrame);
            return qRed(d->layers.front().image.pixel(controller->document().width() / 2, controller->document().height() / 2));
        };
        QTRY_COMPARE_WITH_TIMEOUT(grayAt(2), ServingBlender::grayOf(1, 1), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.blenderImportRunning(), 20000);
        // The Refresh command asks for the current cel again.
        controller->setCurrentCel(cel);
        blender.version = 2;
        window.refreshFromBlender();
        QTRY_COMPARE_WITH_TIMEOUT(grayAt(2), ServingBlender::grayOf(1, 2), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.blenderImportRunning(), 20000);
    }

    // The add-on is kept in the program and goes into a Blender's extensions (here into a folder of the test's own, not the
    // person's).
    void theAddOnIsInstalledIntoBlender()
    {
        QFile zip(QStringLiteral(":/blender/mothion_link.zip"));
        QVERIFY(zip.exists());
        QVERIFY(zip.size() > 1000);
        if (!realBlenderWanted())
            QSKIP("The tests with a real Blender are off: set MOTHION_TEST_BLENDER=1 to run them");
        const QString blender = io::findBlender();
        if (blender.isEmpty())
            QSKIP("Blender is not installed here");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("BLENDER_USER_CONFIG", dir.filePath(QStringLiteral("config")).toUtf8());
        qputenv("BLENDER_USER_EXTENSIONS", dir.filePath(QStringLiteral("extensions")).toUtf8());
        QDir().mkpath(dir.filePath(QStringLiteral("config")));
        QDir().mkpath(dir.filePath(QStringLiteral("extensions")));
        QString message;
        const bool ok = io::installBlenderAddOn(blender, &message);
        qunsetenv("BLENDER_USER_CONFIG");
        qunsetenv("BLENDER_USER_EXTENSIONS");
        QVERIFY2(ok, qPrintable(message));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("extensions/user_default/mothion_link/__init__.py"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("extensions/user_default/mothion_link/blender_manifest.toml"))));
        // Something that is not a Blender cannot install it.
        QString failure;
        QVERIFY(!io::installBlenderAddOn(QStringLiteral("/bin/false"), &failure));
        QVERIFY(!failure.isEmpty());
    }

    // The real thing: a Blender with a window (a virtual screen here) and the add-on, connected to the server, asked for the
    // frames of a scene.
    void theAddOnInBlenderServesFrames()
    {
        if (!realBlenderWanted())
            QSKIP("The tests with a real Blender are off: set MOTHION_TEST_BLENDER=1 to run them");
        const QString blender = QStandardPaths::findExecutable(QStringLiteral("blender"));
        const QString xvfb = QStandardPaths::findExecutable(QStringLiteral("xvfb-run"));
        if (blender.isEmpty() || xvfb.isEmpty())
            QSKIP("Blender and xvfb-run are needed for this one");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scene = makeScene(blender, dir.filePath(QStringLiteral("scene.blend")));
        QVERIFY2(!scene.isEmpty(), "the test scene could not be made");
        const QString boot = dir.filePath(QStringLiteral("boot.py"));
        QFile bootFile(boot);
        QVERIFY(bootFile.open(QIODevice::WriteOnly));
        bootFile.write(QStringLiteral(R"PY(
import sys, os, importlib.util, bpy
spec = importlib.util.spec_from_file_location("mothion_link", r"%1/blender/mothion_link/__init__.py")
mod = importlib.util.module_from_spec(spec)
sys.modules["mothion_link"] = mod
spec.loader.exec_module(mod)
mod.register()
def quit_when_asked():
    if os.path.exists(r"%2"):
        mod.unregister()
        os._exit(0)
    edit = r"%2".replace("quit.flag", "edit.flag")
    if os.path.exists(edit):  # the person moves something in the scene
        os.remove(edit)
        bpy.data.objects["Cube"].location.y += 0.5
        bpy.context.view_layer.update()
    rng = r"%2".replace("quit.flag", "range.flag")
    if os.path.exists(rng):  # the person shortens the scene
        os.remove(rng)
        bpy.context.scene.frame_end = 2
    return 0.3
bpy.app.timers.register(quit_when_asked, first_interval=1.0, persistent=True)
)PY").arg(QStringLiteral(MOTHION_SOURCE_DIR), dir.filePath(QStringLiteral("quit.flag"))).toUtf8());
        bootFile.close();

        io::BlenderLinkServer server;
        QVERIFY(server.listen(0));
        QSignalSpy connected(&server, &io::BlenderLinkServer::sessionConnected);
        QSignalSpy frames(&server, &io::BlenderLinkServer::frameReceived);
        QSignalSpy finished(&server, &io::BlenderLinkServer::requestFinished);
        QSignalSpy changed(&server, &io::BlenderLinkServer::sceneChanged);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("MOTHION_LINK_PORT"), QString::number(server.port()));
        QProcess process;
        process.setProcessEnvironment(env);
        process.start(xvfb, {QStringLiteral("-a"), QStringLiteral("-s"), QStringLiteral("-screen 0 1280x800x24"), blender,
                             QStringLiteral("--factory-startup"), scene, QStringLiteral("--python"), boot});
        const auto cleanup = qScopeGuard([&] {
            QFile(dir.filePath(QStringLiteral("quit.flag"))).open(QIODevice::WriteOnly);
            if (process.state() != QProcess::NotRunning && !process.waitForFinished(15000)) {
                process.kill();
                process.waitForFinished(5000);
            }
        });
        auto quit = [&] {
            QFile(dir.filePath(QStringLiteral("quit.flag"))).open(QIODevice::WriteOnly);
            if (!process.waitForFinished(15000)) {
                process.kill();
                process.waitForFinished(5000);
            }
        };
        const bool arrived = QTest::qWaitFor([&] { return connected.size() == 1; }, 90000);
        if (!arrived) {
            quit();
            QFAIL("the Blender did not connect");
        }
        const int sessionId = connected.first().at(0).toInt();
        const auto session = server.session(sessionId);
        qInfo() << "session" << session->file << session->scene << session->camera << session->frameStart << session->frameEnd;
        QCOMPARE(QFileInfo(session->file).canonicalFilePath(), QFileInfo(scene).canonicalFilePath());
        QCOMPARE(session->frameStart, 1);
        QCOMPARE(session->frameEnd, 3);
        QVERIFY(session->viewport);
        QCOMPARE(session->camera, QStringLiteral("cam"));
        const int frameBefore = session->frame;

        io::BlenderFrameRequest request;
        request.frames = {3, 1, 2};
        request.width = 96;
        request.height = 54;
        request.shading = QStringLiteral("SOLID");
        const int id = server.request(sessionId, request);
        const bool done = QTest::qWaitFor([&] { return finished.size() == 1; }, 120000);
        if (!done) {
            quit();
            QFAIL("the frames did not come");
        }
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(3).toString()));
        QCOMPARE(frames.size(), 3);
        QCOMPARE(frames.at(0).at(1).toInt(), 3); // in the order asked
        QCOMPARE(frames.at(1).at(1).toInt(), 1);
        QMap<int, QImage> byFrame;
        for (const QList<QVariant> &args : frames)
            byFrame[args.at(1).toInt()] = args.at(2).value<QImage>();
        const QImage first = byFrame[1], last = byFrame[3];
        QCOMPARE(first.size(), QSize(96, 54));
        QCOMPARE(qAlpha(first.pixel(first.width() - 2, 1)), 0); // the background is clear (the cube is at the left in the first frame)
        QCOMPARE(qAlpha(last.pixel(1, 1)), 0);                  // and at the right in the last
        // The cube goes from the left to the right along the three frames.
        auto weight = [](const QImage &image) { // the mean column of what is there
            double sum = 0, total = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const double a = qAlpha(image.pixel(x, y)) / 255.0;
                    sum += a * x;
                    total += a;
                }
            return total > 0 ? sum / total : -1.0;
        };
        QVERIFY(weight(first) > 0 && weight(last) > 0);
        QVERIFY2(weight(first) < weight(last) - 10, qPrintable(QStringLiteral("%1 %2").arg(weight(first)).arg(weight(last))));

        // What the frames asked for did to the scene (moving along the timeline) is not a change of the scene...
        QTest::qWait(1200);
        QCOMPARE(changed.size(), 0);
        // ...but the person moving something in it is: the link says so, once.
        QFile(dir.filePath(QStringLiteral("edit.flag"))).open(QIODevice::WriteOnly);
        const bool told = QTest::qWaitFor([&] { return changed.size() >= 1; }, 20000);
        if (told)
            QTest::qWait(1500);
        QVERIFY(told);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(server.session(sessionId)->frame, frameBefore); // the frames were drawn on a copy: the timeline never moved
        // The range of frames is not geometry, but a change of it is told too.
        QCOMPARE(server.session(sessionId)->frameEnd, 3);
        QFile(dir.filePath(QStringLiteral("range.flag"))).open(QIODevice::WriteOnly);
        QVERIFY(QTest::qWaitFor([&] { return server.session(sessionId)->frameEnd == 2; }, 20000));
        quit();
    }
};

QTEST_MAIN(TestBlenderLink)
#include "tst_blenderlink.moc"
