#include "ai/ServerProcess.h"

#include "ai/AiSettings.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#ifndef Q_OS_WIN
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

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

// The scripts of scripts/comfyui, which are in the program (resources), written to a folder to run them from.
bool ServerProcess::extractScripts(const QString &folder)
{
    const QStringList files{QStringLiteral("install.sh"), QStringLiteral("install.ps1"),
                            QStringLiteral("workflows/mothion_sketch_refine.json"),
                            QStringLiteral("workflows/mothion_tag_image.json")};
    for (const QString &name : files) {
        QFile in(QStringLiteral(":/comfyui/") + name);
        if (!in.open(QIODevice::ReadOnly))
            return false;
        const QString target = QDir(folder).filePath(name);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            return false;
        out.write(in.readAll());
    }
    return true;
}

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
    m_installer.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_installer, &QProcess::readyReadStandardOutput, this, &ServerProcess::readInstaller);
    connect(&m_installer, &QProcess::finished, this, &ServerProcess::installFinished);
    connect(&m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_problem = tr("The installation could not be started: %1").arg(m_installer.errorString());
            setState(State::Stopped);
        }
    });
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &ServerProcess::check);
}

ServerProcess::~ServerProcess()
{
    if (m_installer.state() != QProcess::NotRunning) {
        m_installCancelled = true;
        killInstaller();
        m_installer.waitForFinished(3000);
    }
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
    if (!isThisComputer(QUrl(serverUrl())))
        return no(tr("The server is at %1, not on this computer: start it there.").arg(serverUrl()));
    return true;
}

bool ServerProcess::installed() const
{
    QFile marker(QDir(comfyFolder()).filePath(QStringLiteral(".mothion-install")));
    if (!marker.open(QIODevice::ReadOnly) || marker.readAll().trimmed().toInt() < kInstallStamp)
        return false;
    return QFileInfo::exists(program()) && QFileInfo::exists(script());
}

void ServerProcess::setInstallCommand(const QString &program, const QStringList &arguments)
{
    m_installProgram = program;
    m_installArguments = arguments;
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
    if (m_process.state() != QProcess::NotRunning || m_state == State::Running
        || m_installer.state() != QProcess::NotRunning)
        return;
    QString why;
    if (!canStart(&why)) {
        m_problem = why;
        emit changed();
        return;
    }
    if (!installed()) {
        install(); // then it starts
        return;
    }
    launch();
}

void ServerProcess::install()
{
    if (m_installer.state() != QProcess::NotRunning || m_process.state() != QProcess::NotRunning)
        return;
    m_problem.clear();
    m_installOutput.clear();
    m_installStatus = tr("Preparing…");
    m_installPercent = -1;
    m_installCancelled = false;
    QString program = m_installProgram;
    QStringList arguments = m_installArguments;
    if (program.isEmpty()) {
        // The scripts of the program, written out for the run.
        static QTemporaryDir scripts; // for as long as the program runs
        if (!scripts.isValid() || !ServerProcess::extractScripts(scripts.path())) {
            m_problem = tr("The install script could not be prepared.");
            emit changed();
            return;
        }
        const QString folder = comfyFolder();
#ifdef Q_OS_WIN
        program = QStringLiteral("powershell.exe");
        arguments = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                     QStringLiteral("-File"), QDir(scripts.path()).filePath(QStringLiteral("install.ps1")),
                     QStringLiteral("-Dir"), folder, QStringLiteral("-Models"), QStringLiteral("-Stamp"),
                     QString::number(kInstallStamp)};
#else
        program = QStringLiteral("bash");
        arguments = {QDir(scripts.path()).filePath(QStringLiteral("install.sh")), QStringLiteral("--dir"), folder,
                     QStringLiteral("--models"), QStringLiteral("--stamp"), QString::number(kInstallStamp)};
#endif
    }
#ifndef Q_OS_WIN
    // Its own process group: stopping it stops the downloads it started too.
    m_installer.setChildProcessModifier([] { ::setsid(); });
#endif
    m_installer.start(program, arguments);
    setState(State::Installing);
    emit changed();
}

void ServerProcess::readInstaller()
{
    const QByteArray chunk = m_installer.readAllStandardOutput();
    m_installOutput = (m_installOutput + chunk).right(kOutputKept);
    // Lines end with \n; a download's bar is redrawn with \r.
    const QStringList parts = QString::fromLocal8Bit(chunk).split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
    static const QRegularExpression percent(QStringLiteral("(\\d{1,3}(?:\\.\\d+)?)%"));
    for (const QString &part : parts) {
        const QString line = part.trimmed();
        if (line.startsWith(QLatin1String("==> "))) {
            m_installStatus = line.mid(4);
            m_installPercent = -1;
        } else if (line.startsWith(QLatin1String("Downloading "))) {
            m_installStatus = line;
            m_installPercent = 0;
        } else if (const auto m = percent.match(line); m.hasMatch() && m_installPercent >= 0) {
            m_installPercent = std::clamp(int(m.captured(1).toDouble()), 0, 100);
        }
    }
    emit changed();
}

void ServerProcess::installFinished(int code, QProcess::ExitStatus status)
{
    readInstaller();
    if (m_installCancelled) {
        m_problem = tr("The installation was stopped. Start again to go on (what was downloaded is kept).");
        setState(State::Stopped);
        emit changed();
        return;
    }
    if (status != QProcess::NormalExit || code != 0) {
        const QString said = QString::fromLocal8Bit(m_installOutput).trimmed();
        m_problem = tr("The installation failed (exit code %1).").arg(code)
                    + (said.isEmpty() ? QString() : QStringLiteral("\n") + said.section(QLatin1Char('\n'), -6));
        setState(State::Stopped);
        emit changed();
        return;
    }
    m_installStatus.clear();
    m_installPercent = -1;
    setState(State::Stopped);
    if (installed())
        launch();
    else {
        m_problem = tr("The installation finished, but ComfyUI is not where Mothion looks for it (%1).")
                        .arg(QDir::toNativeSeparators(comfyFolder()));
        emit changed();
    }
}

void ServerProcess::killInstaller()
{
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill"), {QStringLiteral("/T"), QStringLiteral("/F"), QStringLiteral("/PID"),
                                                     QString::number(m_installer.processId())});
#else
    if (m_installer.processId() > 0)
        ::kill(-pid_t(m_installer.processId()), SIGTERM);
#endif
    if (!m_installer.waitForFinished(100) && m_installer.state() != QProcess::NotRunning)
        QTimer::singleShot(4000, &m_installer, [this] {
            if (m_installer.state() != QProcess::NotRunning)
                m_installer.kill();
        });
}

void ServerProcess::launch()
{
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
    if (m_installer.state() != QProcess::NotRunning) {
        m_installCancelled = true; // the install is what stops
        killInstaller();
        return;
    }
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
    if (m_state == State::Installing)
        return; // the install is what is going on
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
