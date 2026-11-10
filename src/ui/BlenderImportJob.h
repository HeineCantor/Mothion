#pragma once

#include "document/DocumentController.h"
#include "io/BlenderRender.h"

#include <QObject>
#include <QTemporaryDir>

#include <optional>

namespace mothion::ui {

// A Blender scene being imported in the background as a cel: as soon as Blender has read the scene the cel is there, with an
// empty frame for each frame of the scene, and each picture goes into its frame as it is rendered, the one being looked at
// first (see io::BlenderRender). The work goes on while the person does other things; undoing the cel stops it.
class BlenderImportJob : public QObject
{
    Q_OBJECT

public:
    BlenderImportJob(document::DocumentController *document, QObject *parent = nullptr);

    // Starts it: the cel is named after the file and its first frame goes at `startFrame` (0-based). `lookingAt` is the frame
    // of the timeline now: its picture is made first.
    void start(const QString &blendFile, const QString &blender, int startFrame, int lookingAt, int percent, bool lineArt);
    void cancel();
    bool isRunning() const { return m_render.isRunning(); }

signals:
    // What to tell the person: where it is.
    void status(const QString &text);
    // Over: `frames` were made; `error` says why not, and is empty when it was cancelled.
    void finished(bool ok, const QString &error, int frames);

private:
    void onRange(int first, int last);
    void onFrame(int frame, const QString &file);
    void onFinished(bool ok, const QString &error);

    document::DocumentController *m_document;
    io::BlenderRender m_render;
    QTemporaryDir m_pictures;
    QString m_name;
    doc::BlenderLink m_link; // recorded on the cel
    int m_startFrame = 0;
    int m_first = 0;
    int m_count = 0;
    int m_done = 0;
    std::optional<document::DocumentController::PendingSequence> m_sequence;
    bool m_stopped = false; // it was stopped by us: the cel was undone, or the document replaced
};

} // namespace mothion::ui
