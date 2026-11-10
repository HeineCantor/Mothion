#pragma once

#include "document/DocumentController.h"
#include "io/BlenderFrameCache.h"
#include "io/BlenderLinkServer.h"

#include <QObject>

#include <deque>
#include <functional>
#include <memory>
#include <optional>

namespace mothion::ui {

// The work of bringing a Blender scene into the document through the live link, and keeping it up to date.
//
// An import makes a new cel from a Blender that is connected: it is there at once, with an empty frame for each frame of the
// scene, and the pictures the Blender draws go into their frames as they come, the one being looked at first.
//
// The cel remembers its scene (doc::BlenderLink). When that Blender says the scene changed, the cel is brought up to date: the
// frames are asked for again, and kept in the frame cache as they come (the buffer of the update). The picture looked at is put
// in the cel as soon as it is there, unless the animation is playing; the others are put in together when all are there, so what
// plays is never half of one scene and half of the other. The pictures go in the first layer of each frame, so what the person
// drew over them stays. A scene that changes again while it is being brought up to date starts over; several cels go one after
// the other.
//
// Each job is one undo step; undoing the cel of an import stops it.
class BlenderLiveLink : public QObject
{
    Q_OBJECT

public:
    BlenderLiveLink(document::DocumentController *document, io::BlenderLinkServer *server, io::BlenderFrameCache *cache,
                    QObject *parent = nullptr);

    // How the frames are asked for: the viewport's shading and compositor for the capture (see BlenderFrameRequest) and how
    // much of the size of the page they are drawn at (they are made as big as the page afterwards).
    struct Quality
    {
        QString shading = QStringLiteral("MATERIAL");
        QString compositor = QStringLiteral("KEEP");
        int percent = 50;
    };
    void setQuality(const Quality &quality) { m_quality = quality; }
    const Quality &quality() const { return m_quality; }
    // Whether the animation is playing now (see above); without it nothing is taken to be playing.
    void setPlayingProbe(std::function<bool()> playing) { m_playing = std::move(playing); }
    // Whether a change in the scene brings its cels up to date by itself (on by default).
    void setAutomatic(bool on) { m_automatic = on; }

    // An import from a connected Blender: the cel's first frame goes at `startFrame` (0-based); `lookingAt` is the frame of the
    // timeline now. False if the Blender is not there or something is being done.
    bool import(int sessionId, int startFrame, int lookingAt);
    // Brings the cel up to date from its scene (queued behind what is being done). False if it has no scene or its Blender is
    // not connected.
    bool update(doc::CelId cel);
    // Whether the cel has a Blender connected that has its scene.
    bool isConnected(doc::CelId cel) const;
    // Stops what is being done and forgets what was waiting.
    void cancel();
    bool isBusy() const { return m_mode != Mode::None || !m_queue.empty(); }

signals:
    void status(const QString &text);
    // A job is over: `frames` were made; `error` says why not, and is empty when it was cancelled. `update`: it was an update.
    void finished(bool ok, const QString &error, int frames, bool update);

private:
    enum class Mode { None, Import, Update };

    void begin(const io::BlenderSession &session, int startFrame, int lookingAt);
    void startNext();
    void onSceneChanged(int sessionId);
    void onFrame(int request, int frame, const QImage &image);
    void onFinished(int request, bool ok, bool cancelled, const QString &error);
    QList<int> order(int lookingAt) const;
    QImage pageSized(const QImage &image) const;

    document::DocumentController *m_document;
    io::BlenderLinkServer *m_server;
    io::BlenderFrameCache *m_cache;
    Quality m_quality;
    std::function<bool()> m_playing;
    bool m_automatic = true;

    Mode m_mode = Mode::None;
    std::deque<doc::CelId> m_queue; // cels waiting to be brought up to date
    int m_request = 0;
    int m_session = 0;
    QString m_key;        // of the scene (file and scene)
    QString m_name;
    int m_startFrame = 0;
    int m_first = 0;      // the scene's first frame
    int m_count = 0;
    int m_done = 0;
    int m_lookingAt = 0;
    int m_percent = 50;   // of the size of the page, for the job being done
    // An import:
    std::optional<document::DocumentController::PendingSequence> m_sequence;
    doc::BlenderLink m_link;
    // An update:
    doc::CelId m_cel = doc::kNoId;
    std::unique_ptr<document::DocumentController::SequenceUpdate> m_update;
    bool m_early = false;   // the picture looked at was put in as it came
    int m_earlyIndex = -1;
    bool m_restart = false; // the scene changed again while it was being brought up to date: it starts over
    bool m_stopped = false; // stopped by us: the cel was undone, or the document replaced
};

} // namespace mothion::ui
