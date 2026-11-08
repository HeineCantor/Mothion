#pragma once

#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace mothion::ai {

// Talks to a ComfyUI server (scripts/comfyui installs one) over its HTTP interface, for one request at a time:
//   the picture goes up (/upload/image), the workflow with the texts put in is queued (/prompt), the history is asked
//   until the run is there (/history), and the picture it made comes back (/view).
// Everything is asynchronous: one of finished() or failed() ends each generate(); cancel() ends it silently.
class ComfyClient : public QObject
{
    Q_OBJECT

public:
    explicit ComfyClient(QObject *parent = nullptr);

    void setServer(const QUrl &server) { m_server = server; }
    void setWorkflowPath(const QString &path) { m_workflowPath = path; }
    void setPollInterval(int milliseconds) { m_poll.setInterval(milliseconds); }
    bool busy() const { return m_busy; }

    // Starts a request: `reference` is what the workflow redraws. Ignored while busy.
    void generate(const QImage &reference, const QString &prompt, const QString &negative);
    // Stops waiting (and tells the server to stop the run, if it is running).
    void cancel();

signals:
    void status(const QString &text); // what it is doing now, for the person
    void finished(const QImage &image);
    void failed(const QString &message);

private:
    QNetworkRequest request(const QString &path) const;
    void fail(const QString &message);
    void queue(const QString &uploadedName);
    void askHistory();
    void fetch(const QJsonObject &entry);
    QString connectionProblem(QNetworkReply *reply) const;

    QNetworkAccessManager m_network;
    QTimer m_poll;
    QUrl m_server;
    QString m_workflowPath;
    QString m_clientId;
    QJsonObject m_workflow;
    QString m_prompt;
    QString m_negative;
    QString m_promptId;
    int m_job = 0; // a number for each request: an answer to an earlier one is ignored
    int m_failedPolls = 0;
    bool m_busy = false;
    bool m_asking = false;
};

} // namespace mothion::ai
