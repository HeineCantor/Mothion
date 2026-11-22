#pragma once

#include "core/document/CelMotion.h"
#include "core/document/Timing.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/TimingCommands.h"
#include "audio/AudioAnalyzer.h"
#include "document/DocumentController.h"
#include "ui/panels/timeline/TimelineMetrics.h"

#include <QAbstractScrollArea>

#include <map>
#include <set>
#include <optional>
#include <vector>

class QMenu;

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
//  - S with keys of motion chosen: they are spread out or drawn in from the current frame (the red cursor), as far
//    as the pointer goes from it or nearer to it, or as many times as a number typed says (2: twice as far); a click
//    or Enter makes it (one undo step), Esc or a right click drops it;
//  - Left/Right: frame, Up/Down: cel.
//
// Everything is custom-painted in one widget so the ruler, names and cells
// scroll together without synchronising several widgets.
class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    // The color of a cel's blocks in the timeline (the Layers panel colors the cel's row with it too).
    // The color of a cel belongs to the cel, not to its place among the others: it is taken from its id, so that moving the
    // cels (or adding, or removing others) leaves every one in its color. `colorSlot()` is what is asked of colorForCel().
    static int colorSlot(doc::CelId cel) { return int((cel - 1) % 1000000); }
    static QColor colorForCel(int slot);

    explicit TimelineView(document::DocumentController *document, QWidget *parent = nullptr);

    int frameWidth() const { return m_metrics.frameWidth; }
    void setFrameWidth(int width);
    // The frames that play (0-based, both included): shown as a band on the ruler and an edge on each side down the
    // cells, and the lines that mark the seconds count from the first of them.
    void setPlayRange(int first, int last);

    // Which kinds of rows the timeline shows (the filter of its bar): any of these together. A cel of a kind not shown is
    // not there, nor are the rows of it inside the cels that follow it (a trajectory's, a deformer's); a trajectory inside
    // another goes with its kind. Only the view: what is hidden is as it was, and still works (the canvas draws it). Kept
    // for the next time (in the settings).
    enum Shown : int
    {
        ShowDrawings = 1,     // cels with drawings
        ShowTrajectories = 2, // trajectory cels
        ShowDeformers = 4,    // deformation cels
        ShowCamera = 8,       // the camera's row
        ShowAudio = 16,       // the sound's row
        ShowAll = 31
    };
    int shownKinds() const { return m_shown; }
    void setShownKinds(int kinds);
    // Whether the timeline shows `cel` (its kind is shown).
    bool showsCel(const doc::Cel &cel) const;

    // Frames shown: the scene length, extended past the last exposure.
    int displayedFrames() const;

    // The selected blocks (exposures), by cel and first frame.
    const std::vector<doc::ExposureRef> &selectedFrames() const { return m_selected; }

    // Copies the selected blocks; pastes them at the current frame (see
    // above). Paste returns false when there is no room or nothing copied.
    void copyFrames();
    bool pasteFrames(bool asCopies);
    bool canPasteFrames() const;

    // The key last clicked in the rows of motion, by cel and frame, and its channel (none: the keys of the frame, all).
    const std::optional<std::pair<doc::CelId, int>> &selectedKey() const { return m_selectedKey; }
    std::optional<doc::Channel> selectedKeyChannel() const { return m_selectedChannel; }
    // The rows of the cel's channels are on view (it has keys and its "Keyframes" row is not folded).
    bool channelRowsShown(int celIndex) const;
    bool keyframesRowShown(int celIndex) const; // under the cel: its "Keyframes" row, unless the cel folds it away

    // The rows, top first: how many, the cel of each (for a row of a trajectory or deformer inside a cel, that one: its keys
    // are shown there), whether it is one of keys, whether it is such a row inside a cel.
    int rowCount() const;
    int celIndexForRow(int row) const; // (cels are bottom-first, rows top-first)
    bool isMotionRow(int row) const;
    bool isLinkRow(int row) const;

signals:
    void shownKindsChanged(int kinds);
    void frameWidthChanged(int width);
    // "Import audio…" was chosen in a menu of the timeline (the window asks for the file).
    void audioImportRequested();
    // The current frame was moved by the person, with the pointer or the arrow keys (the sound is heard).
    void frameScrubbed(int frame);
    // The pointer is dragging the current frame along the timeline (the canvas shows the animation as it is, not the cel
    // as drawn, while it does).
    void scrubbingChanged(bool scrubbing);
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
    void focusOutEvent(QFocusEvent *event) override;

private:
    int m_roomLeft = 0; // pixels of scroll before frame 1 and past the last frame, grown as the scroll nears them
    int m_roomRight = 0;
    int m_playFirst = 0;
    int m_playLast = -1; // -1: no range shown (the whole scene)
    // Hold-length drag in progress.
    struct Resize
    {
        int celIndex = -1;
        int start = 0;     // first frame of the exposure being resized
        int newLength = 1;
    };

    // The rows, top first: for each cel its own, and under it (when it has keys of motion) a row for them, a cell tall,
    // with a diamond at each key and a line between two keys that are not held. A click on a diamond goes to its frame
    // and, dragged, moves the key (one undo step on release); a double click on an empty cell makes a key there at the
    // pose the cel has; right click: how the cel goes from that key to the next, delete, and where to add one.
    struct RowRef
    {
        int celIndex = 0;
        bool motion = false;
        int channel = -1; // of a row of motion: the channel it shows, or -1 for the one with the keys of all of them
        // Of a row (a motion one) that names a trajectory or a deformer its cel follows: that one's index. `celIndex` is that
        // one too, so the row shows that cel's keys and works on them, as its own "Keyframes" row does (the same keys).
        int link = -1;
        int depth = 0;  // of such a row: how far down the chain it is (0: the one followed)
        int owner = -1; // of such a row: the cel that follows it, under which it is shown
    };
    // The row of keys of a cel folds away under it (an arrow in its header, and at the left of the keys' own header): it is
    // there for a cel that has keys unless it is folded. Only the view: the keys are as they were.
    bool hasKeys(int celIndex) const;
    // The trajectories shown inside others: a cel and how deep it is (0: not inside any). The arrow of a cel folds them away too.
    struct Placed
    {
        int celIndex;
        int depth;
    };
    std::vector<Placed> celOrder() const;
    bool hasFollowers(int celIndex) const;
    // The deformer that bends a drawn cel (or carries it by a part) and the trajectories it follows, shown inside it (under
    // its own row, unless it is folded): the deformer, the trajectory it follows, then those that one follows. Each row
    // shows that cel's keys, the very ones of its own row: moved, chosen or deleted there, they are on both. The cel stays
    // where it is in the stack.
    std::vector<int> linkChain(int celIndex) const;
    // Whether the cel follows a trajectory or a deformer (it has rows of them inside it).
    bool followsTrajectory(int celIndex) const;
    void paintLinkHeader(QPainter &p, int row) const;
    int depthOf(int celIndex) const;
    int indentOf(int row) const; // how far in the header of a row starts, for a trajectory inside another
    static constexpr int kIndent = 10;
    QRect foldRect(int row) const;
    // Under the "Keyframes" row, a row for each channel (position, rotation, scale, opacity) that has keys, with its own.
    std::optional<doc::Channel> channelOfRow(int row) const;
    int channelRowCount(int celIndex) const; // the arrow, in the header of a cel with keys or of its row of keys
    void toggleFold(doc::CelId cel, bool keyframesRow); // the arrow of the cel folds "Keyframes", the one of "Keyframes" the channels
    RowRef rowRef(int row) const;
    // A diamond of the motion row `row` under `pos`: its frame, or -1.
    int motionKeyAt(const QPoint &pos, int row) const;
    void paintMotionRow(QPainter &p, int celIndex, int row) const;
    void paintMotionHeader(QPainter &p, int celIndex, int row) const;
    void paintFoldArrow(QPainter &p, const QRect &r, bool folded) const;
    void showMotionMenu(const QPoint &globalPos, int celIndex, int frame, std::optional<doc::Channel> channel);
    // What is done to a whole cel: Raster into new Cel, Follow trajectory (for the menus of its frames and of the cel).
    void addCelActions(QMenu &menu, const doc::Cel &cel);
    // The menu of the cel itself (a right click on its name): those, and copy, cut, paste and delete of the cel.
    void showCelMenu(const QPoint &globalPos, const doc::Cel &cel);
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
    void setScrubbing(bool on);

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
    int m_shown = ShowAll;
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
    struct MotionDrag
    {
        doc::CelId cel = doc::kNoId;
        int from = 0; // the key's frame
        int to = 0;   // where it would go
        std::optional<doc::Channel> channel; // the key of this channel, or the keys of every one on that frame
        bool collapse = false; // pressed on one of several chosen keys without moving it: let go, it alone stays chosen
    };
    std::optional<MotionDrag> m_motionDrag;
    std::set<doc::CelId> m_channelsOpen; // the cels whose channel rows are open under "Keyframes" (closed to begin with)
    std::set<doc::CelId> m_foldedCel;   // the cels that fold "Keyframes" (and so the channels) away under them
    std::optional<std::pair<doc::CelId, int>> m_selectedKey; // the key last clicked, by cel and frame
    std::optional<doc::Channel> m_selectedChannel; // of that key: its channel's alone, or (none) all the keys of the frame
    // All the keys chosen (Ctrl adds or takes one away, Shift all of a row between the last and this one); the one last
    // clicked is also m_selectedKey. A key with no channel stands for all the keys of its frame.
    struct ChosenKey
    {
        doc::CelId cel = doc::kNoId;
        int frame = 0;
        std::optional<doc::Channel> channel;
        bool operator==(const ChosenKey &) const = default;
    };
    std::vector<ChosenKey> m_chosenKeys;
    bool keyChosen(doc::CelId cel, int frame, std::optional<doc::Channel> rowChannel) const;
    std::vector<document::DocumentController::CelKeyRef> chosenKeyRefs() const; // each of a channel
    void pressKey(doc::CelId cel, int frame, std::optional<doc::Channel> channel, int row, Qt::KeyboardModifiers modifiers);
    void clearKeySelection();
    std::vector<document::DocumentController::CelKeyCopy> m_copiedKeys;
    bool m_keysNewer = false; // the keys copied are what Paste puts, not the blocks
    void copyKeys();
    bool pasteKeys();
    // The chosen keys being scaled in time from the current frame (S; see the class's comment).
    struct KeyScale
    {
        std::vector<document::DocumentController::CelKeyRef> keys;
        int pivot = 0;
        double reference = 1; // how far from the cursor, in pixels (signed), the pointer is at for the keys as they are
        double pointer = 1;   // the factor the pointer gives
        QString typed;        // a number typed, which is the factor instead
        bool fits = true;     // false: two keys would land on one frame (or before the first): it would be refused
        double factor() const;
    };
    std::optional<KeyScale> m_keyScale;
    bool m_dropContextMenu = false; // the right click that dropped a scale opens no menu
    void startKeyScale();
    void updateKeyScale(); // the preview, after the factor changed
    void finishKeyScale(bool apply);
    double cursorX() const; // the middle of the current frame, in the viewport
    void paintKeyScale(QPainter &p) const;
    struct MarkerDrag
    {
        int from = 0; // the marker's frame
        int to = 0;   // where it would go
        int grabbed = 0; // the frame under the pointer when the flag was taken
    };
    std::optional<MarkerDrag> m_markerDrag;
    // A cel's name taken and moved up or down (with the cels chosen with it): where the pointer was pressed, and where the
    // cels would go (a position of the stack, bottom up) once the pointer has moved enough for it to be a drag.
    struct CelDrag
    {
        QPoint pressed;
        bool moving = false;
        int insertAt = 0;
        doc::CelId cel = doc::kNoId; // the one taken
        // A trajectory over the middle of another one: it would follow it (over itself: let go of its parent).
        doc::CelId parentTarget = doc::kNoId;
    };
    std::optional<CelDrag> m_celDrag;
    int celDropPosition(int y) const; // the position of the stack the pointer is at
    int celDropY(int position) const; // where the line of that position is drawn
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
