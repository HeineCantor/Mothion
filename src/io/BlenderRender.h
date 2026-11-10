#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace mothion::io {

// The Blender program, if it can be found: the one asked for (when it exists), then the search path, then the places
// Blender installs itself to; empty if not.
QString findBlender(const QString &preferred = {});

struct BlenderOptions
{
    QString blender;                       // the program (see findBlender())
    int width = 1920, height = 1080;       // the size of the page: the document's
    int percent = 50;                      // the pictures are this much of it (a draft is small: it is scaled up when imported)
    bool lineArt = false;                  // keep the Line Art modifiers of the scene (they are most of the time a frame takes)
    int firstFrame = -1, lastFrame = -1;   // -1: the scene's own range
    int startOffset = 0;                   // the frames are made from this far into the range, then round to the ones before it
};

// What a line the render prints tells: a frame was saved (`done` counts them), the range that is being rendered
// (`first`, `last`), the program's own complaint, or nothing of interest.
struct BlenderLine
{
    enum class Kind { Nothing, Range, Saved, Error, Done } kind = Kind::Nothing;
    int first = 0, last = 0;
    QString text; // the file saved, or the complaint
};
BlenderLine parseBlenderLine(const QString &line);

// The Python the render runs inside Blender (the options go in after `--`, as JSON).
QString blenderScript();

// Renders a draft of the animation of a Blender scene, seen by the scene's active camera, to numbered PNG pictures with
// transparency (the background is left out), in a Blender of its own that has no window: the scene is read from the file,
// so what has not been saved is not in it. A draft is as light as it can be and keep the materials and the look: Eevee with
// one sample, the scene's compositing, no Line Art (unless asked), the data kept from one frame to the next, and a part of the
// size. One render at a time; the pictures are written in `outputDir`.
class BlenderRender : public QObject
{
    Q_OBJECT

public:
    explicit BlenderRender(QObject *parent = nullptr);
    ~BlenderRender() override;

    void start(const QString &blendFile, const QString &outputDir, const BlenderOptions &options);
    // Stops it (finished() is then told with `cancelled`).
    void cancel();
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }

signals:
    // The range that is rendered (known once the scene is read).
    void range(int first, int last);
    // `done` of `total` frames saved so far.
    void progressed(int done, int total);
    // A picture is saved: the frame of the scene it is of, and the file (the pictures come in the order the render makes
    // them, see BlenderOptions::startOffset).
    void frameSaved(int frame, const QString &file);
    // Over: the pictures (in frame order) when it went well, else why not (empty when it was cancelled).
    void finished(bool ok, const QString &error, const QStringList &files);

private:
    void readOutput();
    void processFinished(int code, QProcess::ExitStatus status);

    QProcess m_process;
    QString m_outputDir;
    QString m_buffer;       // an output line that is not complete yet
    QString m_error;        // what the render itself said went wrong
    QStringList m_tail;     // the last lines it printed, for an error that says nothing
    int m_first = 0, m_last = 0;
    int m_done = 0;
    bool m_cancelled = false;
};

} // namespace mothion::io
