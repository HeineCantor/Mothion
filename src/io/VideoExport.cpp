#include "io/VideoExport.h"

#include "core/document/Document.h"
#include "io/FrameExport.h"

#include <QCoreApplication>
#include <QFile>
#include <QObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace mothion::io {

QString findFfmpeg()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("ffmpeg.exe");
#else
    const QString name = QStringLiteral("ffmpeg");
#endif
    const QString beside = QStandardPaths::findExecutable(name, {QCoreApplication::applicationDirPath()});
    return beside.isEmpty() ? QStandardPaths::findExecutable(name) : beside;
}

QImage renderFinalFrame(const doc::Document &document, int frame, compositing::Evaluator &evaluator,
                        const compositing::Graph *graph, bool transparent)
{
    QImage plain = renderOutputFrame(document, frame, transparent); // through the camera, if there is one
    if (!graph)
        return plain;
    compositing::EvalContext context;
    context.frame = plain;
    context.frameIndex = frame;
    context.paper = document.paperColor();
    const QImage result = evaluator.evaluate(*graph, context);
    if (result.isNull())
        return plain; // nothing wired to Output: the picture as it is
    if (transparent)
        return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Blurs and tints leave partly transparent pixels: over the paper, like the plain frame (which stays see-through
    // where the paper is).
    QImage covered(result.size(), QImage::Format_ARGB32_Premultiplied);
    covered.fill(QColor::fromRgba(document.paperColor())); // see-through, if the paper is
    QPainter p(&covered);
    p.drawImage(0, 0, result);
    p.end();
    // Over an opaque paper the frame is opaque: no alpha in the image then.
    return qAlpha(document.paperColor()) == 255 ? covered.convertToFormat(QImage::Format_RGB32) : covered;
}

QString videoSuffix(VideoCodec codec)
{
    switch (codec) {
    case VideoCodec::Vp9: return QStringLiteral("webm");
    case VideoCodec::ProRes4444: return QStringLiteral("mov");
    case VideoCodec::H264: break;
    }
    return QStringLiteral("mp4");
}

bool videoHasAlpha(VideoCodec codec) { return codec != VideoCodec::H264; }

bool exportVideo(const doc::Document &document, int first, int last, const QString &path, const VideoOptions &options,
                 const std::function<bool(int, int)> &progress, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty())
        return fail(QObject::tr("ffmpeg was not found. Install it and make sure it is on the PATH."));
    const int total = last - first + 1;
    if (total < 1 || first < 0)
        return fail(QObject::tr("There are no frames to export."));

    compositing::Graph graph;
    const bool useGraph = options.compositing && !document.compositing().nodes.empty();
    if (useGraph)
        graph.load(document.compositing());
    compositing::Evaluator evaluator;

    // The frames go in as raw pixels: Format_RGB32 is B, G, R, X in memory on the machines we run on.
    const doc::FrameRate rate = document.frameRate();
    const bool alpha = videoHasAlpha(options.codec); // the frames go in with their alpha, and come out with it
    const bool withoutPaper = alpha && options.withoutPaper;
    QStringList args{QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                     QStringLiteral("-s"), QStringLiteral("%1x%2").arg(document.width()).arg(document.height()),
                     QStringLiteral("-r"), QStringLiteral("%1/%2").arg(rate.numerator).arg(rate.denominator),
                     QStringLiteral("-i"), QStringLiteral("-"), QStringLiteral("-an")};
    const QString crf = QString::number(std::clamp(options.crf, 0, 51));
    switch (options.codec) {
    case VideoCodec::H264:
        args << QStringLiteral("-vf") << QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2") << QStringLiteral("-c:v")
             << QStringLiteral("libx264") << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << crf << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        break;
    case VideoCodec::Vp9: // the alpha rides in a second plane, which asks for no alternate reference frames
        args << QStringLiteral("-vf") << QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2") << QStringLiteral("-c:v")
             << QStringLiteral("libvpx-vp9") << QStringLiteral("-b:v") << QStringLiteral("0") << QStringLiteral("-crf")
             << crf << QStringLiteral("-pix_fmt") << QStringLiteral("yuva420p") << QStringLiteral("-auto-alt-ref")
             << QStringLiteral("0") << QStringLiteral("-metadata:s:v:0") << QStringLiteral("alpha_mode=1");
        break;
    case VideoCodec::ProRes4444:
        args << QStringLiteral("-c:v") << QStringLiteral("prores_ks") << QStringLiteral("-profile:v")
             << QStringLiteral("4444") << QStringLiteral("-pix_fmt") << QStringLiteral("yuva444p10le")
             << QStringLiteral("-vendor") << QStringLiteral("apl0");
        break;
    }
    args << path;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels); // ffmpeg's complaints, read at the end
    process.start(ffmpeg, args);
    if (!process.waitForStarted(5000))
        return fail(QObject::tr("Cannot start ffmpeg: %1").arg(process.errorString()));

    QByteArray messages;
    bool cancelled = false;
    for (int frame = first; frame <= last; ++frame) {
        QImage image = renderFinalFrame(document, frame, evaluator, useGraph ? &graph : nullptr, withoutPaper);
        if (image.size() != QSize(document.width(), document.height()))
            image = image.scaled(document.width(), document.height());
        // ffmpeg's bgra is straight alpha. Where the codec has none, the frame is flattened (white behind what is
        // see-through).
        image = alpha ? image.convertToFormat(QImage::Format_ARGB32) : flattenedOnWhite(image);
        // Scan lines are padded to 4 bytes, which 32-bit pixels already are: the image is one block.
        const qint64 size = qint64(image.sizeInBytes());
        if (process.write(reinterpret_cast<const char *>(image.constBits()), size) != size
            || !process.waitForBytesWritten(-1) || process.state() != QProcess::Running) {
            break; // ffmpeg stopped: what it said is below
        }
        messages += process.readAll();
        if (progress && !progress(frame - first + 1, total)) {
            cancelled = true;
            break;
        }
    }

    if (cancelled) {
        process.kill();
        process.waitForFinished(3000);
        QFile::remove(path);
        return true; // stopped by the user: not a failure
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(-1) && process.state() != QProcess::NotRunning)
        process.kill();
    messages += process.readAll();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QFile::remove(path);
        const QString said = QString::fromLocal8Bit(messages).trimmed();
        return fail(QObject::tr("ffmpeg failed: %1").arg(said.isEmpty() ? process.errorString() : said));
    }
    return true;
}

} // namespace mothion::io
