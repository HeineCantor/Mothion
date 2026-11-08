#include "ai/AiSettings.h"
#include "ai/ComfyClient.h"
#include "ai/Reference.h"
#include "ai/ServerProcess.h"
#include "ai/Workflow.h"
#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "ui/RefineDialog.h"
#include "ui/SettingsDialog.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace mothion;

namespace {

// Just enough of a ComfyUI server: the pictures it is given, the workflow it is sent, and one it makes.
class FakeComfy : public QObject
{
public:
    FakeComfy()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        m_server.listen(QHostAddress::LocalHost, 0);
        QImage made(8, 6, QImage::Format_RGB32);
        made.fill(QColor(10, 200, 30));
        m_made = made;
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    QByteArray uploaded;      // the body of the upload
    QJsonObject queued;       // the "prompt" of the last /prompt
    QStringList requests;     // "METHOD path"
    int pollsBeforeDone = 2;
    bool refuse = false;      // /prompt answers with a validation error
    bool fail = false;        // the run ends in an error
    QImage m_made;

private:
    void read(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const int headEnd = buffer.indexOf("\r\n\r\n");
        if (headEnd < 0)
            return;
        const QByteArray head = buffer.left(headEnd);
        int length = 0;
        for (const QByteArray &line : head.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        if (buffer.size() < headEnd + 4 + length)
            return;
        const QByteArray body = buffer.mid(headEnd + 4, length);
        buffer.clear();
        const QList<QByteArray> first = head.split('\n').first().split(' ');
        const QString method = QString::fromLatin1(first.value(0));
        const QString path = QString::fromLatin1(first.value(1));
        requests << method + QLatin1Char(' ') + path.section(QLatin1Char('?'), 0, 0);

        QByteArray answer;
        QByteArray type = "application/json";
        int status = 200;
        if (path.startsWith(QLatin1String("/upload/image"))) {
            uploaded = body;
            answer = R"({"name":"uploaded.png","subfolder":"","type":"input"})";
        } else if (path.startsWith(QLatin1String("/prompt"))) {
            queued = QJsonDocument::fromJson(body).object().value(QStringLiteral("prompt")).toObject();
            if (refuse) {
                status = 400;
                answer = R"({"error":{"type":"prompt_outputs_failed_validation","message":"Prompt outputs failed validation"},)"
                         R"("node_errors":{"1":{"class_type":"CheckpointLoaderSimple","errors":[{"message":"Value not in list",)"
                         R"("details":"ckpt_name: 'X.safetensors' not in []"}]}}})";
            } else {
                answer = R"({"prompt_id":"abc","number":1,"node_errors":{}})";
            }
        } else if (path.startsWith(QLatin1String("/history/abc"))) {
            if (--pollsBeforeDone > 0) {
                answer = "{}";
            } else if (fail) {
                answer = R"({"abc":{"outputs":{},"status":{"status_str":"error","messages":[["execution_error",{"exception_message":"out of memory"}]]}}})";
            } else {
                answer = R"({"abc":{"outputs":{"12":{"images":[{"filename":"refine_00001_.png","subfolder":"Mothion","type":"output"}]}},)"
                         R"("status":{"status_str":"success","completed":true}}})";
            }
        } else if (path.startsWith(QLatin1String("/view"))) {
            QBuffer png(&answer);
            png.open(QIODevice::WriteOnly);
            m_made.save(&png, "PNG");
            type = "image/png";
        } else {
            answer = "{}";
        }
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nContent-Type: " + type
                      + "\r\nContent-Length: " + QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

QString shippedWorkflow()
{
    return QDir(QFileInfo(QStringLiteral(__FILE__)).dir().filePath(QStringLiteral("../scripts/comfyui/workflows")))
        .filePath(QStringLiteral("mothion_sketch_refine.json"));
}

// A workflow reduced to the nodes Mothion fills.
QString smallWorkflow(const QTemporaryDir &dir, bool withInput = true)
{
    QJsonObject w;
    auto node = [](const char *title, const char *type, QJsonObject inputs) {
        return QJsonObject{{QStringLiteral("class_type"), QLatin1String(type)},
                           {QStringLiteral("_meta"), QJsonObject{{QStringLiteral("title"), QLatin1String(title)}}},
                           {QStringLiteral("inputs"), inputs}};
    };
    if (withInput)
        w[QStringLiteral("1")] = node("MOTHION_INPUT", "LoadImage", {{QStringLiteral("image"), QStringLiteral("example.png")}});
    w[QStringLiteral("2")] = node("MOTHION_PROMPT", "CLIPTextEncode", {{QStringLiteral("text"), QStringLiteral("old")}});
    w[QStringLiteral("3")] = node("MOTHION_NEGATIVE", "CLIPTextEncode", {{QStringLiteral("text"), QStringLiteral("old")}});
    w[QStringLiteral("4")] = node("MOTHION_SAMPLER", "KSampler", {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 28}});
    w[QStringLiteral("12")] = node("MOTHION_OUTPUT", "SaveImage", {});
    const QString path = dir.filePath(withInput ? QStringLiteral("small.json") : QStringLiteral("noinput.json"));
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(w).toJson());
    return path;
}

// A project with a sketch: a black square on a white page, one layer.
void sketch(document::DocumentController &controller)
{
    QImage image(100, 80, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.fillRect(20, 20, 40, 30, Qt::black);
    p.end();
    controller.importImage(image, QStringLiteral("sketch"));
    controller.setLayerChosen(true);
}

} // namespace

class TestAi : public QObject
{
    Q_OBJECT

    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("MothionTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MothionTestAi"));
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void thePromptsAreJoinedStandingFirst()
    {
        QCOMPARE(ai::combinedPrompt(QStringLiteral("a, b"), QStringLiteral("c")), QStringLiteral("a, b, c"));
        QCOMPARE(ai::combinedPrompt(QStringLiteral("a"), QString()), QStringLiteral("a"));
        QCOMPARE(ai::combinedPrompt(QString(), QStringLiteral(" c ")), QStringLiteral("c"));
        ai::resetSettings();
        QCOMPARE(ai::serverUrl(), ai::defaultServer());
        ai::setSystemPrompt(QStringLiteral("x"));
        QCOMPARE(ai::systemPrompt(), QStringLiteral("x"));
        ai::resetSettings();
        QCOMPARE(ai::systemPrompt(), ai::defaultSystemPrompt());
    }

    void theSettingsWindowHasAnAiSection()
    {
        ai::resetSettings();
        {
            ui::SettingsDialog dialog;
            auto *server = dialog.findChild<QLineEdit *>(QStringLiteral("aiServer"));
            auto *prompt = dialog.findChild<QPlainTextEdit *>(QStringLiteral("aiPrompt"));
            auto *negative = dialog.findChild<QPlainTextEdit *>(QStringLiteral("aiNegative"));
            auto *workflow = dialog.findChild<QLineEdit *>(QStringLiteral("aiWorkflow"));
            auto *folder = dialog.findChild<QLineEdit *>(QStringLiteral("aiFolder"));
            auto *options = dialog.findChild<QLineEdit *>(QStringLiteral("aiArguments"));
            QVERIFY(server && prompt && negative && workflow && folder && options);
            folder->setText(QStringLiteral("/opt/comfy"));
            options->setText(QStringLiteral("--lowvram"));
            QCOMPARE(server->text(), ai::defaultServer());
            QCOMPARE(prompt->toPlainText(), ai::defaultSystemPrompt());
            server->setText(QStringLiteral("http://192.168.1.5:8188"));
            prompt->setPlainText(QStringLiteral("my standing prompt"));
            negative->setPlainText(QStringLiteral("my negative"));
            workflow->setText(QStringLiteral("/somewhere/w.json"));
        }
        QCOMPARE(ai::serverUrl(), QStringLiteral("http://192.168.1.5:8188")); // applied as typed
        QCOMPARE(ai::systemPrompt(), QStringLiteral("my standing prompt"));
        QCOMPARE(ai::systemNegative(), QStringLiteral("my negative"));
        QCOMPARE(ai::workflowPath(), QStringLiteral("/somewhere/w.json"));
        QCOMPARE(ai::comfyFolder(), QStringLiteral("/opt/comfy"));
        QCOMPARE(ai::serverArguments(), QStringLiteral("--lowvram"));
        ai::resetSettings();
        QCOMPARE(ai::comfyFolder(), ai::defaultComfyFolder());
        QVERIFY(ai::defaultWorkflowPath().startsWith(ai::defaultComfyFolder())); // the workflow follows the folder
    }

    // ComfyUI started and stopped by Mothion, with a stand-in for it (a script that answers /system_stats).
    void theServerIsStartedAndStopped()
    {
#ifdef Q_OS_WIN
        QSKIP("the stand-in server is a POSIX layout");
#else
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            QSKIP("no python3");
        QTemporaryDir folder;
        QVERIFY(QDir(folder.path()).mkpath(QStringLiteral(".venv/bin")));
        QVERIFY(QDir(folder.path()).mkpath(QStringLiteral("ComfyUI")));
        QVERIFY(QFile::link(python, folder.filePath(QStringLiteral(".venv/bin/python"))));
        QFile main(folder.filePath(QStringLiteral("ComfyUI/main.py")));
        QVERIFY(main.open(QIODevice::WriteOnly));
        main.write("import argparse, http.server\n"
                   "a = argparse.ArgumentParser(); a.add_argument('--listen'); a.add_argument('--port', type=int)\n"
                   "a.add_argument('--lowvram', action='store_true'); n = a.parse_args()\n"
                   "class H(http.server.BaseHTTPRequestHandler):\n"
                   "    def do_GET(self):\n"
                   "        self.send_response(200); self.send_header('Content-Length', '2'); self.end_headers(); self.wfile.write(b'{}')\n"
                   "    def log_message(self, *x): pass\n"
                   "http.server.HTTPServer((n.listen, n.port), H).serve_forever()\n");
        main.close();
        QTcpServer probe; // a free port
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();
        ai::resetSettings();
        ai::setComfyFolder(folder.path());
        ai::setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
        ai::setServerArguments(QStringLiteral("--lowvram"));

        ai::ServerProcess server;
        QSignalSpy changed(&server, &ai::ServerProcess::changed);
        QString why;
        QVERIFY2(server.canStart(&why), qPrintable(why));
        server.check();
        QTRY_COMPARE_WITH_TIMEOUT(int(server.state()), int(ai::ServerProcess::State::Stopped), 3000);
        server.start();
        QVERIFY(server.startedHere());
        QTRY_COMPARE_WITH_TIMEOUT(int(server.state()), int(ai::ServerProcess::State::Running), 15000);
        QVERIFY(server.problem().isEmpty());
        server.stop();
        QTRY_COMPARE_WITH_TIMEOUT(int(server.state()), int(ai::ServerProcess::State::Stopped), 15000);
        QVERIFY(!server.startedHere());
        ai::resetSettings();
#endif
    }

    void theServerSaysWhyItCannotStart()
    {
        QTemporaryDir empty;
        ai::resetSettings();
        ai::setComfyFolder(empty.path());
        ai::ServerProcess server;
        QString why;
        QVERIFY(!server.canStart(&why));
        QVERIFY2(why.contains(QLatin1String("not installed")), qPrintable(why));
        server.start();
        QVERIFY(server.problem().contains(QLatin1String("not installed")));
        QVERIFY(!server.startedHere());
        ai::setServerUrl(QStringLiteral("http://192.168.1.9:8188")); // somebody else's computer
        QVERIFY(!server.canStart(&why));
        ai::resetSettings();
    }

    void theShippedWorkflowHasTheNodesMothionFills()
    {
        QJsonObject workflow;
        QString error;
        QVERIFY2(ai::loadWorkflow(shippedWorkflow(), &workflow, &error), qPrintable(error));
        for (const char *title : {"MOTHION_INPUT", "MOTHION_PROMPT", "MOTHION_NEGATIVE", "MOTHION_SAMPLER", "MOTHION_OUTPUT"})
            QVERIFY2(!ai::findNode(workflow, QLatin1String(title)).isEmpty(), title);
        QJsonObject filled;
        ai::WorkflowInputs inputs{QStringLiteral("up.png"), QStringLiteral("a cat"), QStringLiteral("blurry"), 77};
        QVERIFY(ai::fillWorkflow(workflow, inputs, &filled, &error));
        auto input = [&](const char *title, const char *name) {
            return filled.value(ai::findNode(filled, QLatin1String(title))).toObject().value(QStringLiteral("inputs")).toObject().value(QLatin1String(name));
        };
        QCOMPARE(input("MOTHION_INPUT", "image").toString(), QStringLiteral("up.png"));
        QCOMPARE(input("MOTHION_PROMPT", "text").toString(), QStringLiteral("a cat"));
        QCOMPARE(input("MOTHION_NEGATIVE", "text").toString(), QStringLiteral("blurry"));
        QCOMPARE(input("MOTHION_SAMPLER", "seed").toInt(), 77);
        QCOMPARE(input("MOTHION_SAMPLER", "denoise").toDouble(), 0.75); // the rest stays as it was
    }

    void aWorkflowWithoutTheInputNodeIsRefused()
    {
        QTemporaryDir dir;
        QJsonObject workflow;
        QString error;
        QVERIFY(ai::loadWorkflow(smallWorkflow(dir, false), &workflow, &error));
        QVERIFY(!ai::fillWorkflow(workflow, {}, nullptr, &error));
        QVERIFY2(error.contains(QLatin1String("MOTHION_INPUT")), qPrintable(error));
        // A file the ComfyUI page saved (nodes and links) is not the API format.
        QFile page(dir.filePath(QStringLiteral("page.json")));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write(R"({"nodes":[],"links":[]})");
        page.close();
        QVERIFY(!ai::loadWorkflow(page.fileName(), &workflow, &error));
        QVERIFY2(error.contains(QLatin1String("API")), qPrintable(error));
        QVERIFY(!ai::loadWorkflow(dir.filePath(QStringLiteral("missing.json")), &workflow, &error));
    }

    void theReferenceIsWhatTheChosenLayersShow()
    {
        document::DocumentController controller;
        QVERIFY(!ai::makeReference(controller).valid()); // nothing to refine yet
        sketch(controller);
        ai::Reference reference = ai::makeReference(controller);
        QVERIFY2(reference.valid(), qPrintable(reference.problem));
        QCOMPARE(reference.image.size(), QSize(controller.document().width(), controller.document().height()));
        QCOMPARE(reference.layers, 1);
        // The page is bigger than the sketch, which is centered on it.
        const QPoint at((controller.document().width() - 100) / 2, (controller.document().height() - 80) / 2);
        QCOMPARE(QColor(reference.image.pixel(at.x() + 30, at.y() + 30)), QColor(Qt::black)); // the sketch
        QCOMPARE(QColor(reference.image.pixel(2, 2)), QColor(Qt::white));                     // over the paper

        // The selection: only its area, and what is outside a selection that is not a rectangle goes white.
        QPainterPath area;
        area.addRect(QRectF(at.x() + 10, at.y() + 10, 60, 50));
        controller.setSelection(area);
        reference = ai::makeReference(controller);
        QVERIFY2(reference.valid(), qPrintable(reference.problem));
        QCOMPARE(reference.image.size(), QSize(60, 50));
        QCOMPARE(reference.origin, QPoint(at.x() + 10, at.y() + 10));
        QCOMPARE(QColor(reference.image.pixel(15, 15)), QColor(Qt::black)); // (at + 25, at + 25)
        QPainterPath ellipse;
        ellipse.addEllipse(QRectF(at.x() + 10, at.y() + 10, 60, 50));
        controller.setSelection(ellipse);
        reference = ai::makeReference(controller);
        QCOMPARE(QColor(reference.image.pixel(0, 0)), QColor(Qt::white)); // the corner is outside the ellipse

        // A selection over nothing drawn, and a layer that is not on the frame.
        QPainterPath empty;
        empty.addRect(QRectF(0, 0, 5, 5));
        controller.setSelection(empty);
        QVERIFY(!ai::makeReference(controller).valid());
        controller.clearSelection();
        controller.setLayerSelection({{controller.currentCel(), doc::DrawingId(9999), controller.currentLayer()},
                                      {controller.currentCel(), controller.currentDrawingId(), controller.currentLayer()}});
        reference = ai::makeReference(controller);
        QVERIFY(!reference.valid());
        QVERIFY2(reference.problem.contains(QLatin1String("frame")), qPrintable(reference.problem));
    }

    void theResultBecomesALayerOnTopOfTheFrame()
    {
        document::DocumentController controller;
        sketch(controller);
        const doc::CelId sketchCelId = controller.currentCel();
        controller.addCel(); // a cel above, with no drawing on the frame yet
        const int cels = controller.document().celCount();
        QImage result(60, 50, QImage::Format_RGB32);
        result.fill(Qt::red);
        auto sketchDrawing = [&] { return controller.document().cel(sketchCelId)->drawingAt(0); };
        const doc::Drawing *drawing = sketchDrawing();
        QVERIFY(drawing);
        const std::size_t layers = drawing->layers.size();
        QString why;
        // The topmost cel that has a drawing here is the one with the sketch (the new cel is empty).
        QVERIFY2(controller.addImageLayerOnTop(result, QPoint(10, 10), {}, 0, QStringLiteral("AI refine"), &why), qPrintable(why));
        QCOMPARE(controller.document().celCount(), cels);
        drawing = sketchDrawing();
        QCOMPARE(drawing->layers.size(), layers + 1);
        QCOMPARE(drawing->layers.back().properties.name, std::string("AI refine"));
        QCOMPARE(drawing->layers.back().image.pixel(20, 20), doc::Pixel(0xffff0000));
        controller.undo();
        QCOMPARE(sketchDrawing()->layers.size(), layers);
        controller.redo();
        QCOMPARE(sketchDrawing()->layers.size(), layers + 1);
        // Within a selection that is a circle, the corners are left out.
        QPainterPath circle;
        circle.addEllipse(QRectF(10, 10, 60, 50));
        QVERIFY(controller.addImageLayerOnTop(result, QPoint(10, 10), circle, 0, QStringLiteral("round"), &why));
        const auto &round = sketchDrawing()->layers.back();
        QCOMPARE(qAlpha(round.image.pixel(10, 10)), 0);
        QCOMPARE(qAlpha(round.image.pixel(40, 35)), 255);
        QVERIFY(!controller.addImageLayerOnTop(result, QPoint(0, 0), {}, 5000, QStringLiteral("x"), &why)); // no such frame
    }

    void theClientRunsTheWorkflowOnTheServer()
    {
        QTemporaryDir dir;
        FakeComfy server;
        ai::ComfyClient client;
        client.setServer(QUrl(server.url()));
        client.setWorkflowPath(smallWorkflow(dir));
        client.setPollInterval(20);
        QSignalSpy finished(&client, &ai::ComfyClient::finished);
        QSignalSpy failed(&client, &ai::ComfyClient::failed);
        QImage reference(30, 20, QImage::Format_RGB32);
        reference.fill(Qt::white);
        client.generate(reference, QStringLiteral("1girl"), QStringLiteral("blurry"));
        QVERIFY(client.busy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(failed.isEmpty());
        QVERIFY(!client.busy());
        const QImage got = finished.first().first().value<QImage>();
        QCOMPARE(got.size(), QSize(8, 6));
        QCOMPARE(QColor(got.pixel(0, 0)), QColor(10, 200, 30));
        // What went up and what was queued.
        QVERIFY(server.uploaded.contains("image/png"));
        QVERIFY(server.uploaded.contains("name=\"image\""));
        auto input = [&](const char *title, const char *name) {
            return server.queued.value(ai::findNode(server.queued, QLatin1String(title))).toObject().value(QStringLiteral("inputs")).toObject().value(QLatin1String(name));
        };
        QCOMPARE(input("MOTHION_INPUT", "image").toString(), QStringLiteral("uploaded.png"));
        QCOMPARE(input("MOTHION_PROMPT", "text").toString(), QStringLiteral("1girl"));
        QCOMPARE(input("MOTHION_NEGATIVE", "text").toString(), QStringLiteral("blurry"));
        QVERIFY(input("MOTHION_SAMPLER", "seed").toDouble() != 1.0); // a seed of its own
        QVERIFY(server.requests.contains(QStringLiteral("GET /view")));
    }

    void theClientSaysWhyItFailed()
    {
        QTemporaryDir dir;
        QImage reference(30, 20, QImage::Format_RGB32);
        reference.fill(Qt::white);
        {   // no server
            ai::ComfyClient client;
            client.setServer(QUrl(QStringLiteral("http://127.0.0.1:1")));
            client.setWorkflowPath(smallWorkflow(dir));
            QSignalSpy failed(&client, &ai::ComfyClient::failed);
            client.generate(reference, {}, {});
            QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
            QVERIFY2(failed.first().first().toString().contains(QLatin1String("does not answer")), qPrintable(failed.first().first().toString()));
            QVERIFY(!client.busy());
        }
        {   // the workflow is refused (a model that is not there)
            FakeComfy server;
            server.refuse = true;
            ai::ComfyClient client;
            client.setServer(QUrl(server.url()));
            client.setWorkflowPath(smallWorkflow(dir));
            QSignalSpy failed(&client, &ai::ComfyClient::failed);
            client.generate(reference, {}, {});
            QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
            QVERIFY2(failed.first().first().toString().contains(QLatin1String("X.safetensors")), qPrintable(failed.first().first().toString()));
        }
        {   // the run goes wrong
            FakeComfy server;
            server.fail = true;
            ai::ComfyClient client;
            client.setServer(QUrl(server.url()));
            client.setWorkflowPath(smallWorkflow(dir));
            client.setPollInterval(20);
            QSignalSpy failed(&client, &ai::ComfyClient::failed);
            client.generate(reference, {}, {});
            QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
            QVERIFY2(failed.first().first().toString().contains(QLatin1String("out of memory")), qPrintable(failed.first().first().toString()));
        }
        {   // no workflow file
            ai::ComfyClient client;
            client.setWorkflowPath(dir.filePath(QStringLiteral("nowhere.json")));
            QSignalSpy failed(&client, &ai::ComfyClient::failed);
            client.generate(reference, {}, {});
            QCOMPARE(failed.size(), 1);
            QVERIFY(!client.busy());
        }
    }

    void stoppingTheClientEndsQuietly()
    {
        QTemporaryDir dir;
        FakeComfy server;
        server.pollsBeforeDone = 1000; // never done
        ai::ComfyClient client;
        client.setServer(QUrl(server.url()));
        client.setWorkflowPath(smallWorkflow(dir));
        client.setPollInterval(20);
        QSignalSpy finished(&client, &ai::ComfyClient::finished);
        QSignalSpy failed(&client, &ai::ComfyClient::failed);
        QImage reference(30, 20, QImage::Format_RGB32);
        reference.fill(Qt::white);
        client.generate(reference, {}, {});
        QTRY_VERIFY_WITH_TIMEOUT(server.requests.contains(QStringLiteral("GET /history/abc")), 5000);
        client.cancel();
        QVERIFY(!client.busy());
        QTRY_VERIFY_WITH_TIMEOUT(server.requests.contains(QStringLiteral("POST /interrupt")), 5000);
        QTest::qWait(150);
        QVERIFY(finished.isEmpty() && failed.isEmpty());
    }

    // Against a real ComfyUI (MOTHION_COMFY_URL=http://127.0.0.1:8188): with no models in it the workflow is refused
    // and the reason comes back; with them, a picture does.
    void againstARealServer()
    {
        const QString url = qEnvironmentVariable("MOTHION_COMFY_URL");
        if (url.isEmpty())
            QSKIP("MOTHION_COMFY_URL is not set");
        ai::ComfyClient client;
        client.setServer(QUrl(url));
        client.setWorkflowPath(shippedWorkflow());
        QSignalSpy finished(&client, &ai::ComfyClient::finished);
        QSignalSpy failed(&client, &ai::ComfyClient::failed);
        QImage reference(256, 192, QImage::Format_RGB32);
        reference.fill(Qt::white);
        client.generate(reference, QStringLiteral("1girl"), QStringLiteral("blurry"));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(), 600000);
        qInfo() << "finished:" << finished.size() << "failed:" << (failed.isEmpty() ? QString() : failed.first().first().toString());
    }

    void theWindowSendsWhatIsChosenAndPutsTheResultOnTop()
    {
        QTemporaryDir dir;
        FakeComfy server;
        ai::resetSettings();
        ai::setServerUrl(server.url());
        ai::setWorkflowPath(smallWorkflow(dir));
        ai::setSystemPrompt(QStringLiteral("standing"));
        ai::setSystemNegative(QStringLiteral("never"));
        document::DocumentController controller;
        ai::ServerProcess server2;
        ui::RefineDialog dialog(&controller, &server2);
        auto *generate = dialog.findChild<QPushButton *>(QStringLiteral("generate"));
        auto *prompt = dialog.findChild<QPlainTextEdit *>(QStringLiteral("prompt"));
        auto *negative = dialog.findChild<QPlainTextEdit *>(QStringLiteral("negative"));
        QVERIFY(generate && prompt && negative);
        QVERIFY(!generate->isEnabled()); // nothing to refine
        sketch(controller);
        const doc::CelId sketchCelId = controller.currentCel();
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 2000); // it follows what is chosen
        QVERIFY(dialog.reference().valid());

        dialog.client()->setPollInterval(20);
        prompt->setPlainText(QStringLiteral("cat"));
        negative->setPlainText(QStringLiteral("dog"));
        const std::size_t layers = controller.document().cel(sketchCelId)->drawingAt(0)->layers.size();
        generate->click();
        QVERIFY(dialog.generating());
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.generating(), 5000);
        auto input = [&](const char *title, const char *name) {
            return server.queued.value(ai::findNode(server.queued, QLatin1String(title))).toObject().value(QStringLiteral("inputs")).toObject().value(QLatin1String(name));
        };
        QCOMPARE(input("MOTHION_PROMPT", "text").toString(), QStringLiteral("standing, cat"));
        QCOMPARE(input("MOTHION_NEGATIVE", "text").toString(), QStringLiteral("never, dog"));
        const auto &top = controller.document().cel(sketchCelId)->drawingAt(0)->layers;
        QCOMPARE(top.size(), layers + 1);
        QCOMPARE(top.back().properties.name, std::string("AI refine"));
        ai::resetSettings();
    }
};

QTEST_MAIN(TestAi)
#include "tst_ai.moc"
