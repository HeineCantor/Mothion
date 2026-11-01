#include "io/FrameExport.h"

#include "canvas/FrameRenderer.h"

#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QObject>

#include <algorithm>

namespace mothion::io {

namespace {

constexpr int kMinDigits = 4;

QByteArray writerFormat(ImageFormat format)
{
    return format == ImageFormat::Jpeg ? QByteArrayLiteral("jpeg") : QByteArrayLiteral("png");
}

bool writeImage(const QImage &frame, const QString &path, const ExportOptions &options, QString *error)
{
    QImageWriter writer(path, writerFormat(options.format));
    QImage image = frame;
    if (options.format == ImageFormat::Jpeg) {
        // JPEG has no alpha; the frame is already over the paper.
        image = frame.convertToFormat(QImage::Format_RGB32);
        writer.setQuality(std::clamp(options.quality, 0, 100));
    } else {
        image = frame.convertToFormat(QImage::Format_ARGB32); // straight alpha, as PNG stores it
    }
    if (writer.write(image))
        return true;
    if (error)
        *error = QObject::tr("Cannot write %1: %2").arg(QFileInfo(path).fileName(), writer.errorString());
    return false;
}

} // namespace

std::optional<ImageFormat> formatForFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png"))
        return ImageFormat::Png;
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return ImageFormat::Jpeg;
    return std::nullopt;
}

QString suffixOf(ImageFormat format)
{
    return format == ImageFormat::Jpeg ? QStringLiteral("jpg") : QStringLiteral("png");
}

QImage renderFrame(const doc::Document &document, int frame, bool transparent)
{
    canvas::FrameRenderer renderer;
    renderer.setPaperVisible(!transparent);
    renderer.renderAll(document, frame);
    return renderer.image();
}

bool exportFrame(const doc::Document &document, int frame, const QString &path, const ExportOptions &options,
                 QString *error)
{
    const bool transparent = options.transparent && options.format == ImageFormat::Png;
    return writeImage(renderFrame(document, frame, transparent), path, options, error);
}

QString sequenceFileName(const QString &path, int frame, int lastFrame)
{
    const QFileInfo info(path);
    const int digits = std::max(kMinDigits, int(QString::number(lastFrame + 1).size()));
    const QString number = QStringLiteral("%1").arg(frame + 1, digits, 10, QLatin1Char('0'));
    const QString name = info.completeBaseName() + QLatin1Char('_') + number
                         + (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix());
    return info.dir().filePath(name);
}

bool exportSequence(const doc::Document &document, int first, int last, const QString &path,
                    const ExportOptions &options, const std::function<bool(int, int)> &progress, QString *error)
{
    const bool transparent = options.transparent && options.format == ImageFormat::Png;
    canvas::FrameRenderer renderer;
    renderer.setPaperVisible(!transparent);
    const int total = last - first + 1;
    for (int frame = first; frame <= last; ++frame) {
        renderer.renderAll(document, frame);
        if (!writeImage(renderer.image(), sequenceFileName(path, frame, last), options, error))
            return false;
        if (progress && !progress(frame - first + 1, total))
            break;
    }
    return true;
}

} // namespace mothion::io
