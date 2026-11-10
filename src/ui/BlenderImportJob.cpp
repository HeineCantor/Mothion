#include "ui/BlenderImportJob.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>

namespace mothion::ui {

BlenderImportJob::BlenderImportJob(document::DocumentController *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    connect(&m_render, &io::BlenderRender::range, this, &BlenderImportJob::onRange);
    connect(&m_render, &io::BlenderRender::frameSaved, this, &BlenderImportJob::onFrame);
    connect(&m_render, &io::BlenderRender::finished, this,
            [this](bool ok, const QString &error, const QStringList &) { onFinished(ok, error); });
    // Another project opened: the cel is not there any more.
    connect(m_document, &document::DocumentController::documentReplaced, this, [this] {
        m_stopped = true;
        m_render.cancel();
    });
}

void BlenderImportJob::start(const QString &blendFile, const QString &blender, int startFrame, int lookingAt, int percent,
                             bool lineArt)
{
    if (isRunning())
        return;
    if (!m_pictures.isValid()) {
        emit finished(false, tr("There is no room for the frames: no temporary folder."), 0);
        return;
    }
    m_name = QFileInfo(blendFile).completeBaseName();
    m_startFrame = std::max(0, startFrame);
    m_first = m_count = m_done = 0;
    m_sequence.reset();
    m_stopped = false;
    io::BlenderOptions options;
    options.blender = blender;
    options.width = m_document->document().width();
    options.height = m_document->document().height();
    options.percent = percent;
    options.lineArt = lineArt;
    options.startOffset = std::max(0, lookingAt - m_startFrame);
    emit status(tr("Blender: reading “%1”…").arg(m_name));
    m_render.start(blendFile, m_pictures.path(), options);
}

void BlenderImportJob::cancel()
{
    m_render.cancel();
}

void BlenderImportJob::onRange(int first, int last)
{
    m_first = first;
    m_count = last - first + 1;
    m_sequence = m_document->beginSequenceCel(m_name, m_startFrame, m_count);
    if (!m_sequence) {
        m_stopped = true;
        m_render.cancel();
        return;
    }
    emit status(tr("Blender: 0 of %1 frames").arg(m_count));
}

void BlenderImportJob::onFrame(int frame, const QString &file)
{
    if (!m_sequence || m_stopped)
        return;
    const QImage image(file);
    QFile::remove(file); // in the document now: the disk has no use for it
    if (!m_document->fillSequenceFrame(*m_sequence, frame - m_first, image)) {
        // The cel is gone (undone): nothing to fill any more.
        m_stopped = true;
        m_render.cancel();
        return;
    }
    emit status(tr("Blender: %1 of %2 frames").arg(++m_done).arg(m_count));
}

void BlenderImportJob::onFinished(bool ok, const QString &error)
{
    if (m_sequence && m_done > 0 && !m_stopped)
        m_document->finishSequence(*m_sequence); // what was made is one more undo step
    emit finished(ok, error, m_done);
}

} // namespace mothion::ui
