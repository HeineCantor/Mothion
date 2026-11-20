#include "io/RhubarbLipSync.h"

#include "io/VideoExport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace mothion::io {

QString findRhubarb()
{
    if (const QString chosen = qEnvironmentVariable("MOTHION_RHUBARB"); !chosen.isEmpty() && QFileInfo(chosen).isExecutable())
        return chosen;
    const QString here = QCoreApplication::applicationDirPath();
    for (const QString &dir : {QDir(here).filePath(QStringLiteral("rhubarb")), here})
        if (const QString found = QStandardPaths::findExecutable(QStringLiteral("rhubarb"), {dir}); !found.isEmpty())
            return found;
    return QStandardPaths::findExecutable(QStringLiteral("rhubarb"));
}

std::optional<std::vector<doc::MouthCue>> parseRhubarbJson(const QByteArray &json, QString *error)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(QStringLiteral("mouthCues")).isArray()) {
        if (error)
            *error = QCoreApplication::translate("RhubarbJob", "Rhubarb gave no mouth shapes");
        return std::nullopt;
    }
    std::vector<doc::MouthCue> cues;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("mouthCues")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString value = o.value(QStringLiteral("value")).toString();
        if (value.size() != 1)
            continue;
        doc::MouthCue cue;
        cue.start = o.value(QStringLiteral("start")).toDouble();
        cue.end = o.value(QStringLiteral("end")).toDouble();
        cue.shape = value.at(0).toUpper().toLatin1();
        cues.push_back(cue);
    }
    return cues;
}

RhubarbStatus parseRhubarbStatus(const QByteArray &line)
{
    RhubarbStatus status;
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("progress"))
        status.progress = std::clamp(o.value(QStringLiteral("value")).toDouble(), 0.0, 1.0);
    else if (type == QLatin1String("failure"))
        status.failure = o.value(QStringLiteral("reason")).toString();
    return status;
}

RhubarbJob::RhubarbJob(QObject *parent)
    : LipSyncJob(parent)
{
}

RhubarbJob::~RhubarbJob() { cancel(); }

bool RhubarbJob::isRunning() const { return m_process != nullptr; }

bool RhubarbJob::start(const doc::AudioTrack &track, const LipSyncOptions &options, QString *reason, const QString &rhubarb)
{
    auto refuse = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    cancel();
    m_rhubarb = rhubarb.isEmpty() ? findRhubarb() : rhubarb;
    if (m_rhubarb.isEmpty())
        return refuse(tr("Rhubarb Lip Sync was not found. It comes with Mothion's releases; otherwise put it (with its "
                         "\"res\" folder) in a folder named \"rhubarb\" beside Mothion, or set MOTHION_RHUBARB to it."));
    if (!track.data || track.data->empty())
        return refuse(tr("The project has no audio to hear"));
    m_options = options;
    m_dir = std::make_unique<QTemporaryDir>();
    if (!m_dir->isValid())
        return refuse(tr("No temporary folder for the sound: %1").arg(m_dir->errorString()));
    // The sound as it is in the project, in a file of its kind.
    const QString format = QString::fromStdString(track.format).toLower();
    const QString original = m_dir->filePath(QStringLiteral("sound.") + (format.isEmpty() ? QStringLiteral("bin") : format));
    QFile file(original);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(reinterpret_cast<const char *>(track.data->data()), qint64(track.data->size())) != qint64(track.data->size()))
        return refuse(tr("The sound cannot be written for Rhubarb: %1").arg(file.errorString()));
    file.close();
    if (format == QLatin1String("wav") || format == QLatin1String("ogg")) {
        runRhubarb(original);
        return true;
    }
    // Another kind (mp3, flac...): a WAV first, which Rhubarb reads (mono at 16 kHz, what it hears with).
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty())
        return refuse(tr("Rhubarb reads WAV and Ogg sounds; this one (%1) needs ffmpeg to be made one, and ffmpeg was "
                         "not found. Import the audio as a WAV file instead.").arg(format));
    const QString wav = m_dir->filePath(QStringLiteral("sound.wav"));
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this, wav](int code, QProcess::ExitStatus status) {
        const QByteArray errors = m_process->readAllStandardError();
        m_process->deleteLater();
        m_process = nullptr;
        if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(wav)) {
            fail(tr("The sound could not be made a WAV file for Rhubarb:\n%1").arg(QString::fromLocal8Bit(errors.right(800))));
            return;
        }
        runRhubarb(wav);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("ffmpeg could not be started"));
    });
    m_process->start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                              QStringLiteral("error"), QStringLiteral("-i"), original, QStringLiteral("-ac"),
                              QStringLiteral("1"), QStringLiteral("-ar"), QStringLiteral("16000"), wav});
    return true;
}

void RhubarbJob::runRhubarb(const QString &soundFile)
{
    QStringList args{QStringLiteral("--machineReadable"), QStringLiteral("-f"), QStringLiteral("json"),
                     QStringLiteral("-r"), m_options.recognizer == LipSyncRecognizer::RhubarbEnglish ? QStringLiteral("pocketSphinx") : QStringLiteral("phonetic"),
                     QStringLiteral("--extendedShapes"), m_options.extendedShapes};
    if (!m_options.dialog.trimmed().isEmpty()) {
        const QString dialog = m_dir->filePath(QStringLiteral("dialog.txt"));
        QFile file(dialog);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_options.dialog.toUtf8());
            file.close();
            args << QStringLiteral("-d") << dialog;
        }
    }
    const QString output = m_dir->filePath(QStringLiteral("cues.json"));
    args << QStringLiteral("-o") << output << soundFile;
    m_status.clear();
    m_failure.clear();
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_dir->path());
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_status += m_process->readAllStandardError();
        for (qsizetype end = m_status.indexOf('\n'); end >= 0; end = m_status.indexOf('\n')) {
            const RhubarbStatus status = parseRhubarbStatus(m_status.left(end));
            m_status.remove(0, end + 1);
            if (status.progress)
                emit progress(*status.progress);
            if (status.failure)
                m_failure = *status.failure;
        }
    });
    connect(m_process, &QProcess::finished, this, [this, output](int code, QProcess::ExitStatus status) {
        m_process->deleteLater();
        m_process = nullptr;
        if (status != QProcess::NormalExit || code != 0) {
            fail(m_failure.isEmpty() ? tr("Rhubarb stopped with an error (%1)").arg(code)
                                     : tr("Rhubarb could not hear the sound:\n%1").arg(m_failure));
            return;
        }
        QFile file(output);
        QString error;
        const auto cues = file.open(QIODevice::ReadOnly) ? parseRhubarbJson(file.readAll(), &error) : std::nullopt;
        if (!cues) {
            fail(error.isEmpty() ? tr("Rhubarb gave no mouth shapes") : error);
            return;
        }
        m_dir.reset();
        emit finished(*cues);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Rhubarb could not be started (%1)").arg(m_rhubarb));
    });
    m_process->start(m_rhubarb, args);
}

void RhubarbJob::fail(const QString &reason)
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_dir.reset();
    emit failed(reason);
}

void RhubarbJob::cancel()
{
    if (!m_process)
        return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(2000);
    m_process->deleteLater();
    m_process = nullptr;
    m_dir.reset();
}

} // namespace mothion::io
