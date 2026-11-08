#include "ai/ComfyClient.h"

#include "ai/Workflow.h"

#include <QBuffer>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QUuid>

namespace mothion::ai {

namespace {

QJsonObject parseObject(const QByteArray &bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

// What ComfyUI said was wrong with a workflow it refused (the node errors), as a line or two.
QString validationProblem(const QJsonObject &answer)
{
    QStringList lines;
    const QJsonObject nodes = answer.value(QStringLiteral("node_errors")).toObject();
    for (auto it = nodes.begin(); it != nodes.end() && lines.size() < 3; ++it) {
        const QJsonObject node = it.value().toObject();
        for (const QJsonValue &e : node.value(QStringLiteral("errors")).toArray()) {
            const QJsonObject error = e.toObject();
            const QString details = error.value(QStringLiteral("details")).toString();
            lines << QStringLiteral("%1: %2").arg(node.value(QStringLiteral("class_type")).toString(),
                                                    details.isEmpty() ? error.value(QStringLiteral("message")).toString() : details);
            if (lines.size() >= 3)
                break;
        }
    }
    if (lines.isEmpty())
        lines << answer.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    return lines.join(QLatin1Char('\n'));
}

} // namespace

ComfyClient::ComfyClient(QObject *parent)
    : QObject(parent)
    , m_server(QStringLiteral("http://127.0.0.1:8188"))
    , m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_poll.setInterval(500);
    connect(&m_poll, &QTimer::timeout, this, &ComfyClient::askHistory);
}

QNetworkRequest ComfyClient::request(const QString &path) const
{
    QUrl url = m_server;
    const QString query = path.section(QLatin1Char('?'), 1);
    url.setPath(url.path().remove(QRegularExpression(QStringLiteral("/+$"))) + path.section(QLatin1Char('?'), 0, 0));
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    return request;
}

QString ComfyClient::connectionProblem(QNetworkReply *reply) const
{
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::RemoteHostClosedError:
        return tr("ComfyUI does not answer at %1. Is it running? (scripts/comfyui/README.md says how to start it.)")
            .arg(m_server.toString());
    default:
        return tr("ComfyUI: %1").arg(reply->errorString());
    }
}

void ComfyClient::fail(const QString &message)
{
    m_poll.stop();
    m_busy = false;
    ++m_job;
    emit failed(message);
}

void ComfyClient::generate(const QImage &reference, const QString &prompt, const QString &negative)
{
    if (m_busy)
        return;
    QString error;
    if (!loadWorkflow(m_workflowPath, &m_workflow, &error)) {
        emit failed(error);
        return;
    }
    m_prompt = prompt;
    m_negative = negative;
    m_promptId.clear();
    m_failedPolls = 0;
    m_asking = false;
    m_busy = true;
    const int job = ++m_job;
    emit status(tr("Sending the sketch…"));

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    reference.save(&buffer, "PNG");

    auto *parts = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart image;
    image.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    image.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"image\"; filename=\"mothion-%1.png\"")
                        .arg(QUuid::createUuid().toString(QUuid::Id128).left(12)));
    image.setBody(png);
    parts->append(image);
    for (const auto &field : {std::pair{"type", "input"}, std::pair{"overwrite", "true"}}) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(QLatin1String(field.first)));
        part.setBody(field.second);
        parts->append(part);
    }
    QNetworkReply *reply = m_network.post(request(QStringLiteral("/upload/image")), parts);
    parts->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();
        if (job != m_job)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(connectionProblem(reply));
            return;
        }
        const QJsonObject answer = parseObject(reply->readAll());
        const QString name = answer.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            fail(tr("ComfyUI did not accept the picture."));
            return;
        }
        const QString folder = answer.value(QStringLiteral("subfolder")).toString();
        queue(folder.isEmpty() ? name : folder + QLatin1Char('/') + name);
    });
}

void ComfyClient::queue(const QString &uploadedName)
{
    WorkflowInputs inputs;
    inputs.image = uploadedName;
    inputs.prompt = m_prompt;
    inputs.negative = m_negative;
    inputs.seed = qint64(QRandomGenerator::global()->generate64() % (quint64(1) << 50));
    QJsonObject filled;
    QString error;
    if (!fillWorkflow(m_workflow, inputs, &filled, &error)) {
        fail(error);
        return;
    }
    emit status(tr("Waiting for ComfyUI…"));
    QNetworkRequest r = request(QStringLiteral("/prompt"));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("prompt"), filled}, {QStringLiteral("client_id"), m_clientId}};
    QNetworkReply *reply = m_network.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const int job = m_job;
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();
        if (job != m_job)
            return;
        const QJsonObject answer = parseObject(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && answer.isEmpty()) {
            fail(connectionProblem(reply));
            return;
        }
        m_promptId = answer.value(QStringLiteral("prompt_id")).toString();
        if (m_promptId.isEmpty()) {
            fail(tr("ComfyUI refused the workflow:\n%1").arg(validationProblem(answer)));
            return;
        }
        emit status(tr("Generating…"));
        m_poll.start();
    });
}

void ComfyClient::askHistory()
{
    if (!m_busy || m_asking || m_promptId.isEmpty())
        return;
    m_asking = true;
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/history/") + m_promptId));
    const int job = m_job;
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();
        if (job != m_job)
            return;
        m_asking = false;
        if (reply->error() != QNetworkReply::NoError) {
            if (++m_failedPolls >= 3)
                fail(connectionProblem(reply));
            return;
        }
        m_failedPolls = 0;
        const QJsonObject entry = parseObject(reply->readAll()).value(m_promptId).toObject();
        if (entry.isEmpty())
            return; // still waiting or running
        m_poll.stop();
        const QJsonObject state = entry.value(QStringLiteral("status")).toObject();
        if (state.value(QStringLiteral("status_str")).toString() == QStringLiteral("error")) {
            QString why = tr("ComfyUI could not finish the run.");
            for (const QJsonValue &m : state.value(QStringLiteral("messages")).toArray()) {
                const QJsonArray message = m.toArray();
                if (message.size() >= 2 && message.at(0).toString() == QStringLiteral("execution_error"))
                    why = tr("ComfyUI could not finish the run:\n%1")
                              .arg(message.at(1).toObject().value(QStringLiteral("exception_message")).toString().trimmed());
            }
            fail(why);
            return;
        }
        fetch(entry);
    });
}

void ComfyClient::fetch(const QJsonObject &entry)
{
    const QJsonObject outputs = entry.value(QStringLiteral("outputs")).toObject();
    // The picture of the node titled MOTHION_OUTPUT, else the first one any node made.
    QJsonObject picture;
    const QString wanted = findNode(m_workflow, QStringLiteral("MOTHION_OUTPUT"));
    auto firstImage = [](const QJsonObject &node) {
        const QJsonArray images = node.value(QStringLiteral("images")).toArray();
        return images.isEmpty() ? QJsonObject() : images.at(0).toObject();
    };
    if (!wanted.isEmpty())
        picture = firstImage(outputs.value(wanted).toObject());
    for (auto it = outputs.begin(); picture.isEmpty() && it != outputs.end(); ++it)
        picture = firstImage(it.value().toObject());
    if (picture.isEmpty()) {
        fail(tr("The run made no picture. Does the workflow end in a Save Image node?"));
        return;
    }
    emit status(tr("Getting the picture…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("filename"), picture.value(QStringLiteral("filename")).toString());
    query.addQueryItem(QStringLiteral("subfolder"), picture.value(QStringLiteral("subfolder")).toString());
    query.addQueryItem(QStringLiteral("type"), picture.value(QStringLiteral("type")).toString(QStringLiteral("output")));
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/view?") + query.toString(QUrl::FullyEncoded)));
    const int job = m_job;
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        reply->deleteLater();
        if (job != m_job)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(connectionProblem(reply));
            return;
        }
        const QImage image = QImage::fromData(reply->readAll());
        if (image.isNull()) {
            fail(tr("The picture ComfyUI made cannot be read."));
            return;
        }
        m_busy = false;
        ++m_job;
        emit finished(image);
    });
}

void ComfyClient::cancel()
{
    if (!m_busy)
        return;
    m_poll.stop();
    m_busy = false;
    ++m_job;
    if (m_promptId.isEmpty())
        return;
    // Only this run, if the server knows how (older ones stop whatever is running).
    QNetworkRequest r = request(QStringLiteral("/interrupt"));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_network.post(r, QJsonDocument(QJsonObject{{QStringLiteral("prompt_id"), m_promptId}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QNetworkRequest q = request(QStringLiteral("/queue")); // and out of the line, if it had not started
    q.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *removal = m_network.post(q, QJsonDocument(QJsonObject{{QStringLiteral("delete"), QJsonArray{m_promptId}}}).toJson(QJsonDocument::Compact));
    connect(removal, &QNetworkReply::finished, removal, &QObject::deleteLater);
}

} // namespace mothion::ai
