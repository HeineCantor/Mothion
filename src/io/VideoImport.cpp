#include "io/VideoImport.h"

#include "io/VideoExport.h"

#include <QDir>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>

namespace mothion::io {

QStringList videoPatterns()
{
    QStringList patterns;
    for (const char *suffix : {"mp4", "m4v", "mov", "mkv", "webm", "avi", "wmv", "flv", "mpg", "mpeg", "ts", "mts",
                               "m2ts", "3gp", "ogv", "gif", "apng"})
        patterns << QStringLiteral("*.") + QString::fromLatin1(suffix);
    return patterns;
}

QStringList extractVideoFrames(const QString &video, const QString &folder, double fps,
                               const std::function<bool(double, double)> &progress, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QStringList();
    };
    if (error)
        error->clear();
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty())
        return fail(QObject::tr("ffmpeg was not found: it reads the video (install it, or put it next to Mothion)"));
    if (fps <= 0)
        return fail(QObject::tr("The scene has no frame rate"));
    QDir out(folder);
    if (!out.exists() && !QDir().mkpath(folder))
        return fail(QObject::tr("Cannot make the folder %1").arg(folder));

    QProcess process;
    process.setProgram(ffmpeg);
    // Only the pictures (no sound, no subtitles), a frame for each frame of the scene, as PNG (it keeps an alpha).
    process.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                          QStringLiteral("-i"), video, QStringLiteral("-an"), QStringLiteral("-sn"),
                          QStringLiteral("-dn"), QStringLiteral("-vf"),
                          QStringLiteral("fps=%1").arg(QString::number(fps, 'g', 12)), QStringLiteral("-start_number"),
                          QStringLiteral("1"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
                          QStringLiteral("-nostats"), out.filePath(QStringLiteral("frame_%06d.png"))});
    process.start();
    if (!process.waitForStarted())
        return fail(QObject::tr("ffmpeg could not be started"));

    // How long the video is (from what ffmpeg tells of it first), and how far it got (its progress lines).
    static const QRegularExpression durationLine(QStringLiteral(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))"));
    QByteArray told, said;
    double total = 0, done = 0;
    bool stopped = false;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50);
        said += process.readAllStandardError();
        if (total <= 0) {
            const QRegularExpressionMatch m = durationLine.match(QString::fromUtf8(said));
            if (m.hasMatch())
                total = m.captured(1).toDouble() * 3600 + m.captured(2).toDouble() * 60 + m.captured(3).toDouble();
        }
        told += process.readAllStandardOutput();
        for (qsizetype end = told.indexOf('\n'); end >= 0; end = told.indexOf('\n')) {
            const QByteArray line = told.left(end).trimmed();
            told.remove(0, end + 1);
            if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) { // (both in microseconds)
                bool ok = false;
                const double us = line.mid(line.indexOf('=') + 1).toDouble(&ok);
                if (ok && us >= 0)
                    done = us / 1e6;
            }
        }
        if (progress && !progress(total > 0 ? std::min(done, total) : done, total)) {
            stopped = true;
            process.kill();
            process.waitForFinished();
            break;
        }
    }
    said += process.readAllStandardError();
    if (stopped)
        return {};
    QStringList frames;
    for (const QString &name : out.entryList({QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name))
        frames << out.filePath(name);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || frames.isEmpty()) {
        // ffmpeg's last words say why.
        QStringList lines = QString::fromUtf8(said).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString why = lines.isEmpty() ? QString() : lines.last().trimmed();
        return fail(why.isEmpty() ? QObject::tr("ffmpeg could not read the video") : why);
    }
    return frames;
}

} // namespace mothion::io
