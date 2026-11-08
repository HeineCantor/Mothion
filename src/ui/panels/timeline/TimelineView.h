#pragma once

#include "core/document/Timing.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"
#include "audio/AudioAnalyzer.h"
#include "ui/panels/timeline/TimelineMetrics.h"

#include <QAbstractScrollArea>

#include <map>
#include <optional>
#include <vector>

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Exposure timeline (Harmony style) of the document:
// one row per cel (top cel first), frames horizontally, each exposure a
// block spanning its hold with the frame's (drawing's) name. A ruler on top
// shows frame numbers and the current-frame cursor.
//
// Interaction:
//  - click / drag in the ruler or cells: current frame (and cel, in cells);
//  - in a row's header: the eye and the lock show / hide and lock the cel,
//    a click elsewhere makes it current, a double-click renames it;
//  - click a block: selects it (Ctrl+click adds or takes away one, Shift+click
//    the blocks of the row from the last one clicked); click an empty cell:
//    none selected;
//  - drag a selected block: moves all the selected ones by as many frames,
//    one undo step on release; only onto free frames (where they would land
//    on another frame they are shown there striped, and the drop is refused);
//  - copy (right click, or Ctrl+C here) the selected blocks; paste them on
//    their cels from the current frame, keeping their distances, as the same
//    drawings (Ctrl+V) or as independent copies (Ctrl+Shift+V), onto free
//    frames only;
//  - drag the right edge of a block: change the hold length (ripple), one
//    undo step on release;
//  - right click: new frame, clear frame, split frame, insert inbetween,
//    insert/remove frames, delete frame;
//  - Left/Right: frame, Up/Down: cel.
//
// Everything is custom-painted in one widget so the ruler, names and cells
// scroll together without synchronising several widgets.
class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    // The color of a cel's blocks in the timeline (the Layers panel colors the cel's row with it too).
    static QColor colorForCel(int celIndex);

    explicit TimelineView(document::DocumentController *document, QWidget *parent = nullptr);

    int frameWidth() const { return m_metrics.frameWidth; }
    void setFrameWidth(int width);

    // Frames shown: the scene length, extended past the last exposure.
    int displayedFrames() const;

    // The selected blocks (exposures), by cel and first frame.
    const std::vector<doc::ExposureRef> &selectedFrames() const { return m_selected; }

    // Copies the selected blocks; pastes them at the current frame (see
    // above). Paste returns false when there is no room or nothing copied.
    void copyFrames();
    bool pasteFrames(bool asCopies);
    bool canPasteFrames() const;

signals:
    void frameWidthChanged(int width);
    // "Import audio…" was chosen in a menu of the timeline (the window asks for the file).
    void audioImportRequested();
    // The current frame was moved by the person, with the pointer or the arrow keys (the sound is heard).
    void frameScrubbed(int frame);
    // The sound of the track could not be decoded (so there is no waveform, and the length is unknown).
    void audioFailed(const QString &reason);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override; // Ctrl+C / V here are the timeline's, for blocks

private:
    // Hold-length drag in progress.
    struct Resize
    {
        int celIndex = -1;
        int start = 0;     // first frame of the exposure being resized
        int newLength = 1;
    };

    int rowCount() const;
    int celIndexForRow(int row) const; // rows are top-first, cels bottom-first
    int rowAt(int y) const;              // -1 outside the rows
    int frameAt(int x) const;
    // The eye and lock toggles in a row's header.
    QRect eyeRect(int row) const;
    QRect lockRect(int row) const;
    void renameCel(int celIndex);
    // Exposure whose right edge is under (x, row), as {cel index, start}.
    std::optional<Resize> edgeAt(const QPoint &pos) const;
    const doc::Timing &timingForPainting(int celIndex) const; // preview while resizing or moving
    bool isSelected(doc::CelId cel, int start) const;
    // Press on the block `ref` at `frame` (viewport x `x`): the selection,
    // then maybe a drag.
    void pressFrame(const doc::ExposureRef &ref, int frame, int x, Qt::KeyboardModifiers modifiers);
    void dragFrames(int x);
    void dropFrames();
    void pruneSelection(); // blocks gone (undo, other edits) leave the selection

    void onStructureChanged();
    void updateScrollBars();
    void ensureFrameVisible(int frame);
    void scrubTo(const QPoint &pos, bool selectCel);

    void paintCells(QPainter &p) const;
    void paintExposure(QPainter &p, const doc::Exposure &e, const QString &name, const QColor &color, int y,
                       bool selected) const;
    // A dragged block where it cannot be dropped: striped, over what is there.
    void paintBlockedExposure(QPainter &p, const doc::Exposure &e, int y) const;
    void paintRuler(QPainter &p) const;
    void paintHeaders(QPainter &p) const;
    void paintCursor(QPainter &p) const;

    // The camera: a row under the cels (and over the audio's), one cell tall, with a diamond at each keyframe and a
    // line between two keys that are not held. In its header an eye turns the camera on and off, and a click on the
    // name selects it (the canvas then moves it). A click on a diamond goes to its frame and, dragged, moves the
    // key to another frame (one undo step on release); a double click on an empty cell makes a key there at the pose
    // the camera has; right click: how the camera goes from that key to the next, and delete.
    bool hasCamera() const;
    int cameraHeight() const; // 0 without a camera
    int cameraTop() const;
    QRect cameraEyeRect() const;
    bool inCameraRow(int y) const;
    void paintCamera(QPainter &p) const;
    void paintMarkerLines(QPainter &p) const;
    void paintMarkerFlags(QPainter &p) const;
    // The frame of the marker whose flag is under `pos` (in the strip at the top of the ruler).
    std::optional<int> markerAt(const QPoint &pos) const;
    int flagWidth() const; // the pennant's, from the pole
    void renameMarker(int frame);
    void showMarkerMenu(const QPoint &global, int frame);
    void paintCameraHeader(QPainter &p) const;
    void showCameraMenu(const QPoint &globalPos, int frame);

    // The audio track: a row at the bottom, under the cels, taller than theirs for its waveform. The header has a
    // mute button and the name; the block, from where the sound begins for as long as it lasts, can be dragged
    // along the frames (one undo step on release). Without a track there is no row.
    bool hasAudio() const;
    int audioTop() const;
    QRect audioHeaderRect() const;
    QRect audioMuteRect() const;
    QRect audioScrubRect() const; // beside it, where a cel has its lock
    QRect audioVolumeRect() const; // the slider under them
    float audioVolumeAt(int x) const;
    // The block at `offset` frames, as long as the sound (a few frames wide until it is known).
    QRect audioBlockRect(int offset) const;
    bool inAudioRow(int y) const;
    double audioSeconds() const;
    void paintAudio(QPainter &p) const;
    void paintAudioHeader(QPainter &p) const;
    void paintSelection(QPainter &p) const;
    // Decodes the track's sound when it is a new one (for the waveform and its length), or forgets it.
    void syncAudio();
    void showAudioMenu(const QPoint &globalPos);
    document::DocumentController *m_document;
    TimelineMetrics m_metrics;
    bool m_scrubbing = false;
    audio::AudioAnalyzer m_analyzer;
    audio::Waveform m_wave;
    const void *m_waveKey = nullptr; // the sound the waveform is of (the address of its bytes)
    struct AudioDrag
    {
        int pressFrame = 0;
        int startOffset = 0;
        int offset = 0;
        bool moved = false;
    };
    std::optional<AudioDrag> m_audioDrag;
    struct CameraDrag
    {
        int from = 0; // the key's frame
        int to = 0;   // where it would go
    };
    std::optional<CameraDrag> m_cameraDrag;
    struct MarkerDrag
    {
        int from = 0; // the marker's frame
        int to = 0;   // where it would go
        int grabbed = 0; // the frame under the pointer when the flag was taken
    };
    std::optional<MarkerDrag> m_markerDrag;
    std::optional<float> m_volumeBefore; // the volume slider is being dragged: what it was when it began
    std::optional<Resize> m_resize;
    doc::Timing m_previewTiming; // the resized cel's timing during a drag

    std::vector<doc::ExposureRef> m_selected;
    std::optional<doc::ExposureRef> m_anchor; // the last block clicked, for Shift
    // Blocks being dragged along the time.
    struct FrameDrag
    {
        int pressX = 0;
        int pressFrame = 0;
        bool moved = false;
        int offset = 0;              // frames, as far as the earliest can go
        doc::ExposureRef grabbed;
        bool selectOnlyIt = false;   // a click (no drag) on one of several selected: it alone
        bool blocked = false;        // some would land on another frame: no drop
    };
    std::optional<FrameDrag> m_frameDrag;
    std::map<int, doc::Timing> m_movePreview; // by cel index, while dragging
    std::vector<doc::ExposureCopy> m_copied;  // the blocks copied
    // The blocks were the last thing copied (the system clipboard has not changed since): Ctrl+V pastes them.
    // After a copy of pixels (from the canvas), or of anything else, Ctrl+V is the window's paste.
    bool m_blocksNewer = false;
};

} // namespace mothion::ui
