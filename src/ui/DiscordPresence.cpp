#include "ui/DiscordPresence.h"

#include "document/DocumentController.h"
#include "tools/Preferences.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QUuid>
#include <QtEndian>

namespace mothion::ui {

namespace {

// The application's id in Discord's developer portal (the number under "Application ID"). Put it here to
// have the presence in every build; empty and no environment variable: the presence is off.
constexpr const char *kApplicationId = "1567674277786820698";

// The messages of Discord's socket: a 4-byte opcode and a 4-byte length (little endian), then JSON.
enum Opcode { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

} // namespace

QString DiscordPresence::defaultApplicationId()
{
    const QString fromEnvironment = qEnvironmentVariable("MOTHION_DISCORD_APP_ID");
    return fromEnvironment.isEmpty() ? QString::fromLatin1(kApplicationId) : fromEnvironment;
}

QStringList DiscordPresence::candidatePaths()
{
    QStringList paths;
#ifdef Q_OS_WIN
    for (int i = 0; i < 10; ++i)
        paths << QStringLiteral("discord-ipc-%1").arg(i); // a named pipe: \\.\pipe\discord-ipc-N
#else
    // Where the desktop client, its Flatpak and its Snap put it.
    QStringList directories;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty())
        directories << runtime << runtime + QStringLiteral("/app/com.discordapp.Discord")
                    << runtime + QStringLiteral("/snap.discord");
    for (const char *name : {"TMPDIR", "TMP", "TEMP"})
        if (const QString dir = qEnvironmentVariable(name); !dir.isEmpty())
            directories << dir;
    directories << QStringLiteral("/tmp");
    for (int i = 0; i < 10; ++i)
        for (const QString &dir : std::as_const(directories))
            paths << QStringLiteral("%1/discord-ipc-%2").arg(dir).arg(i);
#endif
    paths.removeDuplicates();
    return paths;
}

DiscordPresence::DiscordPresence(document::DocumentController *document, const QString &applicationId,
                                 QObject *parent, std::optional<QStringList> paths)
    : QObject(parent)
    , m_document(document)
    , m_applicationId(applicationId)
    , m_socket(new QLocalSocket(this))
    , m_candidates(paths ? *paths : candidatePaths())
    , m_startSeconds(QDateTime::currentSecsSinceEpoch())
{
    m_retry.setInterval(15000);
    m_retry.setSingleShot(true);
    m_throttle.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &DiscordPresence::start);
    connect(&m_throttle, &QTimer::timeout, this, &DiscordPresence::sendActivity);
    connect(m_socket, &QLocalSocket::connected, this, &DiscordPresence::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordPresence::onDisconnected);
    // A path with nothing behind it: the next, and when there is none left, a new round later.
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this] {
        if (!m_ready && m_socket->state() != QLocalSocket::ConnectedState)
            QMetaObject::invokeMethod(this, &DiscordPresence::tryNext, Qt::QueuedConnection);
    });
    connect(m_document, &document::DocumentController::currentFrameChanged, this, &DiscordPresence::scheduleUpdate);
    connect(m_document, &document::DocumentController::documentReplaced, this, &DiscordPresence::scheduleUpdate);
    connect(m_document, &document::DocumentController::structureChanged, this, &DiscordPresence::scheduleUpdate);
    m_enabled = tools::Preferences::instance().discordEnabled();
    if (!m_applicationId.isEmpty())
        start();
}

void DiscordPresence::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on) {
        start();
        return;
    }
    m_retry.stop();
    m_throttle.stop();
    m_ready = false;
    m_lastText.clear();
    m_socket->abort(); // the activity goes from Discord with the connection
}

void DiscordPresence::setCandidatePaths(const QStringList &paths)
{
    m_candidates = paths;
}

void DiscordPresence::start()
{
    if (!m_enabled || m_applicationId.isEmpty() || m_ready || m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    m_next = 0;
    tryNext();
}

void DiscordPresence::tryNext()
{
    if (!m_enabled)
        return;
    if (m_next >= m_candidates.size()) { // Discord is not there (yet): look again later
        m_retry.start();
        return;
    }
    m_socket->abort();
    m_socket->connectToServer(m_candidates[m_next++]);
}

void DiscordPresence::onConnected()
{
    m_buffer.clear();
    const QJsonObject hello{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), m_applicationId}};
    sendFrame(Handshake, QJsonDocument(hello).toJson(QJsonDocument::Compact));
}

void DiscordPresence::onDisconnected()
{
    m_ready = false;
    m_lastText.clear();
    if (m_enabled)
        m_retry.start();
}

void DiscordPresence::sendFrame(int opcode, const QByteArray &json)
{
    QByteArray message(8, '\0');
    qToLittleEndian<quint32>(quint32(opcode), message.data());
    qToLittleEndian<quint32>(quint32(json.size()), message.data() + 4);
    message += json;
    m_socket->write(message);
}

void DiscordPresence::onReadyRead()
{
    m_buffer += m_socket->readAll();
    while (m_buffer.size() >= 8) {
        const quint32 opcode = qFromLittleEndian<quint32>(m_buffer.constData());
        const quint32 length = qFromLittleEndian<quint32>(m_buffer.constData() + 4);
        if (quint32(m_buffer.size()) < 8 + length)
            return; // the rest is on its way
        const QByteArray payload = m_buffer.mid(8, int(length));
        m_buffer.remove(0, 8 + int(length));
        if (opcode == Ping) {
            sendFrame(Pong, payload);
        } else if (opcode == Close) {
            m_socket->disconnectFromServer();
            return;
        } else if (opcode == Frame) {
            const QJsonObject message = QJsonDocument::fromJson(payload).object();
            if (message.value(QStringLiteral("evt")).toString() == QStringLiteral("READY")) {
                m_ready = true;
                m_lastText.clear();
                sendActivity();
            }
        }
    }
}

QString DiscordPresence::frameText() const
{
    return tr("Frame %1 / %2").arg(m_document->currentFrame() + 1).arg(m_document->document().frameCount());
}

void DiscordPresence::scheduleUpdate()
{
    if (!m_ready)
        return; // what is true when Discord answers is sent then
    const qint64 sinceLast = m_lastSent.isValid() ? m_lastSent.elapsed() : m_minimumInterval;
    if (sinceLast >= m_minimumInterval)
        sendActivity();
    else if (!m_throttle.isActive())
        m_throttle.start(int(m_minimumInterval - sinceLast));
}

void DiscordPresence::sendActivity()
{
    if (!m_ready)
        return;
    const QString text = frameText();
    if (text == m_lastText)
        return;
    m_lastText = text;
    m_lastSent.start();
    const QJsonObject activity{
        {QStringLiteral("state"), text},
        {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), m_startSeconds}}},
    };
    const QJsonObject message{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())},
                                             {QStringLiteral("activity"), activity}}},
        {QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
    };
    sendFrame(Frame, QJsonDocument(message).toJson(QJsonDocument::Compact));
}

} // namespace mothion::ui
