#include "io/FrameExport.h"

#include "canvas/FrameRenderer.h"
#include "io/VideoExport.h"

#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QTransform>
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
        // JPEG has no alpha: over the paper, and over white where the paper is see-through.
        image = flattenedOnWhite(frame);
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

QImage flattenedOnWhite(const QImage &image)
{
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter p(&flat);
    p.drawImage(0, 0, image);
    return flat;
}

QImage renderFrame(const doc::Document &document, int frame, bool transparent)
{
    canvas::FrameRenderer renderer;
    renderer.setPaperVisible(!transparent);
    renderer.setGuidesVisible(false); // perspective layers are guides, not the picture
    renderer.setResampleFilter(canvas::ResampleFilter::Bicubic); // a cel moved by its keys: read with care
    renderer.renderAll(document, frame);
    return renderer.image();
}

QImage applyCamera(const QImage &sheet, const doc::CameraPose &pose, QRgb background)
{
    return applyCamera(sheet, QRect(QPoint(0, 0), sheet.size()), 1, sheet.size(), pose, background);
}

QImage applyCamera(const QImage &sheet, const QRect &area, int step, QSize pictureSize, const doc::CameraPose &pose,
                   QRgb background)
{
    QImage out(pictureSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor::fromRgba(background)); // as a color: the alpha of the paper is its own, not premultiplied
    const doc::Matrix2x3 m = pose.sheetToPicture(pictureSize.width(), pictureSize.height());
    // A pixel of the image to the sheet, then the sheet to the picture.
    QTransform toPicture = QTransform::fromScale(step, step) * QTransform::fromTranslate(area.left(), area.top())
                           * QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
    // Far out the image is brought down to about the picture's size first (an average of the pixels), or the
    // resampling would pick some of them and the picture would shimmer.
    QImage source = sheet;
    const double zoom = pose.zoom * step; // picture pixels for a pixel of the image
    if (zoom > 0 && zoom < 0.75) {
        source = sheet.scaled(QSize(std::max(1, int(sheet.width() * zoom)), std::max(1, int(sheet.height() * zoom))),
                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        toPicture = QTransform::fromScale(sheet.width() / double(source.width()), sheet.height() / double(source.height()))
                    * toPicture;
    }
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(toPicture);
    p.drawImage(0, 0, source);
    return out;
}

QRect cameraArea(const doc::CameraPose &pose, int width, int height)
{
    double left = 1e18, top = 1e18, right = -1e18, bottom = -1e18;
    for (const auto &corner : pose.frame(width, height)) {
        left = std::min(left, corner[0]);
        top = std::min(top, corner[1]);
        right = std::max(right, corner[0]);
        bottom = std::max(bottom, corner[1]);
    }
    const int margin = 2 * cameraStep(pose);
    return QRectF(QPointF(left, top), QPointF(right, bottom)).toAlignedRect().adjusted(-margin, -margin, margin, margin);
}

int cameraStep(const doc::CameraPose &pose)
{
    int step = 1;
    while (step < 8 && pose.zoom > 0 && pose.zoom * step * 2 <= 1.0)
        step *= 2;
    return step;
}

QImage renderOutputFrame(const doc::Document &document, int frame, bool transparent)
{
    const auto &camera = document.camera();
    if (!camera || !camera->visible || camera->isEmpty())
        return renderFrame(document, frame, transparent);
    const doc::CameraPose pose = camera->poseAt(frame);
    if (pose == doc::CameraPose::wholeSheet(document.width(), document.height()))
        return renderFrame(document, frame, transparent); // looking at all of it: nothing to resample
    // What the camera sees, which may lie out of the page (the canvas has no end).
    canvas::FrameRenderer renderer;
    renderer.setPaperVisible(!transparent);
    renderer.setGuidesVisible(false);
    renderer.setResampleFilter(canvas::ResampleFilter::Bicubic);
    renderer.setArea(document, frame, cameraArea(pose, document.width(), document.height()), cameraStep(pose));
    return applyCamera(renderer.image(), renderer.area(), renderer.step(), QSize(document.width(), document.height()),
                       pose, transparent ? QRgb(0) : document.paperColor());
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
            return renderOutputFrame(m_document, frame, m_transparent);
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
