#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QLocalSocket;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Tells a running Discord what is being done: "Mothion", for how long, and the frame the timeline is on
// ("Frame 12 / 120"). It talks to Discord through the local socket the desktop client opens (a named pipe
// on Windows, a Unix socket on Linux), as the old Rich Presence library did; nothing is shown in the app.
//
// Discord may not be running yet (or at all): connecting is tried again every few seconds, quietly, and
// the presence is sent as soon as Discord answers. Updates are spaced out (Discord limits them), so
// stepping through frames fast shows the latest one a moment later.
//
// Without an application id (see defaultApplicationId()) it does nothing.
class DiscordPresence : public QObject
{
    Q_OBJECT

public:
    DiscordPresence(document::DocumentController *document, const QString &applicationId, QObject *parent = nullptr);

    // The id of Mothion's application in Discord's developer portal, from the build or, to try another,
    // the MOTHION_DISCORD_APP_ID environment variable. Empty: no presence.
    static QString defaultApplicationId();
    // Where Discord's socket may be on this system, in the order to try.
    static QStringList candidatePaths();

    // For tests: where to look for Discord, how often to send, and how soon to try again.
    void setCandidatePaths(const QStringList &paths);
    void setMinimumUpdateInterval(int ms) { m_minimumInterval = ms; }
    void setRetryInterval(int ms) { m_retry.setInterval(ms); }

    // Connect (done at construction when there is an application id).
    void start();
    // Discord answered the handshake: presence is being sent.
    bool isConnected() const { return m_ready; }

private:
    void tryNext();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void sendFrame(int opcode, const QByteArray &json);
    void scheduleUpdate();
    void sendActivity();
    QString frameText() const;

    document::DocumentController *m_document;
    QString m_applicationId;
    QLocalSocket *m_socket;
    QStringList m_candidates;
    int m_next = 0;
    QByteArray m_buffer;
    bool m_ready = false;
    qint64 m_startSeconds;
    QTimer m_retry;
    QTimer m_throttle;
    QElapsedTimer m_lastSent;
    int m_minimumInterval = 4000;
    QString m_lastText;
};

} // namespace mothion::ui
