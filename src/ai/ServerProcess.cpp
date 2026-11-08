#include "ai/ServerProcess.h"

#include "ai/AiSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace mothion::ai {

namespace {

constexpr int kOutputKept = 3000;

bool isThisComputer(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host.isEmpty() || host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
           || host == QLatin1String("::1");
}

} // namespace

ServerProcess::ServerProcess(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_output += m_process.readAllStandardOutput();
        if (m_output.size() > kOutputKept)
            m_output = m_output.right(kOutputKept);
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_stopping && (status == QProcess::CrashExit || code != 0)) {
            const QString said = QString::fromLocal8Bit(m_output).trimmed();
            m_problem = tr("ComfyUI stopped (exit code %1).").arg(code)
                        + (said.isEmpty() ? QString() : QStringLiteral("\n") + said.section(QLatin1Char('\n'), -6));
        }
        m_stopping = false;
        setState(State::Stopped);
        check(); // another server may still answer on that address
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_problem = tr("ComfyUI could not be started: %1").arg(m_process.errorString());
            setState(State::Stopped);
        }
    });
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &ServerProcess::check);
}

ServerProcess::~ServerProcess()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill(); // with Mothion goes the server it started
        m_process.waitForFinished(3000);
    }
}

QString ServerProcess::program() const
{
    const QDir folder(comfyFolder());
#ifdef Q_OS_WIN
    return folder.filePath(QStringLiteral(".venv/Scripts/python.exe"));
#else
    return folder.filePath(QStringLiteral(".venv/bin/python"));
#endif
}

QString ServerProcess::script() const { return QDir(comfyFolder()).filePath(QStringLiteral("ComfyUI/main.py")); }

bool ServerProcess::canStart(QString *why) const
{
    auto no = [why](const QString &reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (!QFileInfo::exists(program()) || !QFileInfo::exists(script()))
        return no(tr("ComfyUI is not installed in %1 (scripts/comfyui/install does it; Settings > AI says where Mothion looks).")
                      .arg(QDir::toNativeSeparators(comfyFolder())));
    if (!isThisComputer(QUrl(serverUrl())))
        return no(tr("The server is at %1, not on this computer: start it there.").arg(serverUrl()));
    return true;
}

void ServerProcess::setWatching(bool watching)
{
    m_watching = watching;
    if (watching) {
        check();
        m_poll.start();
    } else if (m_state != State::Starting) {
        m_poll.stop();
    }
}

void ServerProcess::start()
{
    if (m_process.state() != QProcess::NotRunning || m_state == State::Running)
        return;
    QString why;
    if (!canStart(&why)) {
        m_problem = why;
        emit changed();
        return;
    }
    m_problem.clear();
    m_output.clear();
    m_stopping = false;
    QStringList args{script(), QStringLiteral("--listen"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"),
                     QString::number(QUrl(serverUrl()).port(8188))};
    args << QProcess::splitCommand(serverArguments());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(QFileInfo(script()).absolutePath());
    m_process.start(program(), args);
    setState(State::Starting);
    m_poll.start();
}

void ServerProcess::stop()
{
    if (m_process.state() == QProcess::NotRunning)
        return; // a server started elsewhere is not ours to stop
    m_stopping = true;
#ifdef Q_OS_WIN
    m_process.kill(); // terminate() only asks windows, which a server has none of
#else
    m_process.terminate();
    QTimer::singleShot(5000, &m_process, [this] {
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });
#endif
}

void ServerProcess::check()
{
    if (m_asking)
        return;
    m_asking = true;
    QUrl url(serverUrl());
    url.setPath(QStringLiteral("/system_stats"));
    QNetworkRequest request(url);
    request.setTransferTimeout(2000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_asking = false;
        onAnswer(reply->error() == QNetworkReply::NoError);
    });
}

void ServerProcess::onAnswer(bool answered)
{
    if (answered)
        setState(State::Running);
    else
        setState(startedHere() && !m_stopping ? State::Starting : State::Stopped);
    if (m_state != State::Starting && !m_watching)
        m_poll.stop();
}

void ServerProcess::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    if (state == State::Running)
        m_problem.clear();
    emit changed();
}

} // namespace mothion::ai
