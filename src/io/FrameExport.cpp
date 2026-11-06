#include "io/FrameExport.h"

#include "canvas/FrameRenderer.h"
#include "io/VideoExport.h"

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
    renderer.setGuidesVisible(false); // perspective layers are guides, not the picture
    renderer.renderAll(document, frame);
    return renderer.image();
}

namespace {

// The frames of an export: plain, or through the document's compositing network when it is asked for and there
// is one. Holds the network and its cache across the frames of a sequence.
class FrameSource
{
public:
    FrameSource(const doc::Document &document, const ExportOptions &options)
        : m_document(document)
        , m_transparent(options.transparent && options.format == ImageFormat::Png)
        , m_composited(options.compositing && !document.compositing().nodes.empty())
    {
        if (m_composited)
            m_graph.load(document.compositing());
    }

    QImage frame(int frame)
    {
        if (!m_composited)
            return renderFrame(m_document, frame, m_transparent);
        return renderFinalFrame(m_document, frame, m_evaluator, &m_graph, m_transparent);
    }
    bool transparent() const { return m_transparent; }

private:
    const doc::Document &m_document;
    bool m_transparent;
    bool m_composited;
    compositing::Graph m_graph;
    compositing::Evaluator m_evaluator;
};

} // namespace

bool exportFrame(const doc::Document &document, int frame, const QString &path, const ExportOptions &options,
                 QString *error)
{
    FrameSource source(document, options);
    return writeImage(source.frame(frame), path, options, error);
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
    FrameSource source(document, options);
    const int total = last - first + 1;
    for (int frame = first; frame <= last; ++frame) {
        if (!writeImage(source.frame(frame), sequenceFileName(path, frame, last), options, error))
            return false;
        if (progress && !progress(frame - first + 1, total))
            break;
    }
    return true;
}

} // namespace mothion::io
