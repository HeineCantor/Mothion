#pragma once

#include <limits>
#include "core/document/Document.h"
#include "core/document/History.h"
#include "document/Tween.h"

#include <QImage>
#include <functional>
#include <QObject>
#include <QPainterPath>

#include <map>
#include <string>
#include <QRect>
#include <QString>

#include <memory>
#include <optional>

namespace mothion::document {

// The bridge between the Qt-free document model and the UI.
//
// It owns the Document and its History, keeps what is current (cel, frame,
// and the layer of the drawing shown there) and turns command ChangeSets into Qt signals. Views read
// through document() and change things only through this class, so every
// change is undoable and every view hears about it.
class DocumentController : public QObject
{
    Q_OBJECT

public:
    // Starts with a default document: 1920×1080, 24 fps, 120 frames, one cel.
    explicit DocumentController(QObject *parent = nullptr);
    ~DocumentController() override;

    const doc::Document &document() const { return *m_document; }

    // --- whole document ---------------------------------------------------------
    // Starts over with a new default document.
    void newDocument();
    // A new document of the given size (px), frame rate, length (frames) and paper color, with one cel.
    void newDocument(int width, int height, int frameRate, int frames, const QColor &paper);
    // Puts a loaded document in place of the current one: history cleared,
    // the top cel and frame 1 current, nothing selected.
    void replaceDocument(std::unique_ptr<doc::Document> document);
    // Changes since the last save (or since the document was created or loaded).
    // The undo history's limits, from the preferences (at the start and when they change).
    void applyUndoLimits();
    bool isModified() const { return !m_history.isClean(); }
    // The document now matches the saved file.
    void markSaved();
    // The document holds work that is not in its file (a recovered autosave).
    void markUnsaved();
    // Applies edits left open (a puppet warp) and cancels a stroke in
    // progress, so that the document is complete, e.g. before saving.
    void finishEdits() { interruptStroke(); }

    doc::CelId currentCel() const { return m_currentCel; }
    int currentFrame() const { return m_currentFrame; }
    // The drawing the current cel shows at the current frame (nullptr on an
    // empty frame), and the layer of it that tools work on (kNoId then).
    // Going to another drawing keeps a layer of the same name current, else
    // the top one.
    const doc::Drawing *currentDrawing() const;
    doc::DrawingId currentDrawingId() const;
    doc::LayerId currentLayer() const { return m_currentLayer; }

    // --- commands -------------------------------------------------------------
    // Runs a command and records it for undo (nullptr does nothing).
    void execute(std::unique_ptr<doc::Command> command);
    // Mutable document access for building commands that allocate ids.
    doc::Document &documentForCommands() { return *m_document; }

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    // Adds an empty cel above the current one, named "Cel N" with the first
    // free N; it becomes current. One undo step.
    void addCel();
    // A trajectory cel above the current one (see doc::CelProperties::trajectory): no drawings, its pivot at the middle of the
    // page, which is where its circle stands. One undo step.
    void addTrajectory();
    // A trajectory made from the marks of a spacing chart drawn on the current vector layer: each stroke (a dot or a short
    // dash), in the order it was drawn, is one frame, its middle (the mean of its points) the place the trajectory has
    // there (with a long stroke among them, the path, they go in the order they come along it instead): the keys of the position go one on each frame, from the current one. One undo step; the new trajectory is
    // chosen. False, with `reason`, if the layer is not a vector one or has fewer than two strokes.
    bool trajectoryFromMarks(QString *reason = nullptr);
    // `child` follows `parent` (a trajectory; kNoId lets it go); its own keys then come on top of the parent's way. The child
    // is a trajectory or any cel. One undo step. False if that would be a loop, or the parent is no trajectory, or nothing changes.
    bool setTrajectoryParent(doc::CelId child, doc::CelId parent);
    // Adds an empty layer of `kind` to the current drawing, above the current
    // layer (on top if `onTop`), named "Layer N" (or "Perspective N") with the
    // first free N; it becomes current. A perspective layer starts with a grid
    // fit to the document. One undo step; false on an empty frame.
    // Merges layers of the current drawing into the lowest of them (it keeps
    // its name and place; the rest go): what they show together, each with
    // its own opacity and blend mode, as one undo step. Only raster layers, or only vector
    // layers (their strokes joined, bottom to top), that are shown and not locked are taken; false, with `reason`, when
    // fewer than two are left or the kinds are mixed. The result becomes the current layer.
    bool mergeLayers(const std::vector<doc::LayerId> &layers, QString *reason = nullptr);
    // Makes the vector layers among `layers` raster layers: the pixels stay, the strokes go (undo brings them back).
    // The Fill and Gradient tools and the like work on them then.
    bool rasterizeLayers(const std::vector<doc::LayerId> &layers, QString *reason = nullptr);
    bool canRasterizeLayers(const std::vector<doc::LayerId> &layers) const;
    bool canMergeLayers(const std::vector<doc::LayerId> &layers) const;
    // Merges the layers into the one right below the lowest of them (it keeps its name and place; the result is drawn
    // plainly, as in mergeLayers()). One layer chosen is "merge down". False, with `reason`, when there is no layer
    // below, or it, or a chosen one, is hidden, locked or not a drawing layer.
    bool mergeDown(const std::vector<doc::LayerId> &layers, QString *reason = nullptr);
    bool canMergeDown(const std::vector<doc::LayerId> &layers) const;
    bool addLayer(bool onTop = false, doc::LayerKind kind = doc::LayerKind::Raster);
    // Puts a new frame (a drawing with one empty layer, named like the layer
    // worked on) at `frame` of `cel`, the frames from there on moving one
    // place to the right, and makes it current. "New frame" puts it at the
    // current point, "Insert inbetween" right after it (`after`).
    void insertFrame(doc::CelId cel, int frame, bool after);
    // To where the current cel's next frame starts (direction > 0) or the
    // previous one (the one before the frame shown now): over holds and
    // empty frames. False, staying put, when there is none that way.
    bool stepToFrame(int direction);
    // Lengthens (delta > 0) or shortens the hold of the frame shown at the
    // current point of the current cel, by `delta` frames and to at least
    // one; the frames after it move along. The current point stays on that
    // frame. One undo step; false on an empty point or when nothing changes.
    bool changeCurrentHold(int delta);
    // Puts `image` on a new cel above the current one, named `name`, as one
    // drawing held for the whole scene: centered, scaled down to fit the
    // document if larger. One undo step; the new cel becomes current.
    void importImage(const QImage &image, const QString &name);
    // --- audio track ------------------------------------------------------------------
    // The song under the animation, if there is one (one at most): the file is read into the project, so it
    // goes where the project goes. False, with `reason`, if it cannot be read. One undo step each.
    const std::optional<doc::AudioTrack> &audioTrack() const { return m_document->audio(); }
    bool importAudio(const QString &path, QString *reason = nullptr);
    void removeAudio();
    void setAudioOffset(int frames);
    void setAudioVolume(float volume);
    void setAudioMuted(bool muted);
    void setAudioScrub(bool scrub); // whether it is heard as the timeline is scrubbed
    // The volume while a slider is dragged: shown and heard at once, not an undo step; commitAudioVolume() then
    // records the drag as one step (`before` is the volume when it began).
    void previewAudioVolume(float volume);
    void commitAudioVolume(float before);
    // What decoding found out (not an undo step: it is a fact about the file).
    void setAudioDuration(double seconds);

    // Tweening: the drawings of `cel` shown at frames `from` and `to` are two poses, and each frame between them gets
    // a drawing of its own, the picture between the poses (see DrawingTween): the poses laid over each other
    // automatically, the pictures made from both, layer by layer. They replace what those frames showed. One undo step.
    // Returns how many frames were made; 0, with the reason, if there was no way (no poses, the same drawing, no frame
    // between, a locked cel).
    int tweenFrames(doc::CelId cel, int from, int to, TweenEase ease = TweenEase::Linear, QString *reason = nullptr);

    // The camera (see doc::Camera): at most one for now. Each change is one undo step.
    const std::optional<doc::Camera> &camera() const { return m_document->camera(); }
    void addCamera();                                      // one that shows the whole sheet, from the first frame
    void removeCamera();
    // The motion of a cel (see doc::CelMotion): keys of where it is, as a rigid piece, on the frames they stand for.
    // Each change is one undo step. The pose is a cel's as it is shown (as drawn where it has no key).
    const doc::CelMotion *celMotion(doc::CelId cel) const;
    doc::CelPose celPoseAt(doc::CelId cel, int frame) const;
    // The "Cel key": keys on `frame` from this pose, in the channels (position, rotation, scale, opacity) that have keys
    // already, those the pose changes there, and, for the first key of a cel, position, rotation and scale (its pivot is
    // put at the middle of what it shows). False if there is no such cel or nothing changes.
    bool setCelKey(doc::CelId cel, int frame, const doc::CelPose &pose);
    // A key of one channel only (the part of `pose` that is the channel's).
    bool setCelChannelKey(doc::CelId cel, doc::Channel channel, int frame, const doc::CelPose &pose);
    // On the keys of one channel, or (none given) of all the channels that have a key on `frame`.
    bool setCelKeyEasing(doc::CelId cel, int frame, const doc::Easing &easing, std::optional<doc::Channel> channel = {});
    bool removeCelKey(doc::CelId cel, int frame, std::optional<doc::Channel> channel = {});
    // Moves the key on `from` to `to`: refused (false) if `to` has a key (of the same channel) or `from` has none.
    bool moveCelKey(doc::CelId cel, int frame, int to, std::optional<doc::Channel> channel = {});
    // Several keys at once (each one of a channel of a cel on a frame), one undo step. False, and nothing done, if one would
    // not work (see each).
    struct CelKeyRef
    {
        doc::CelId cel = doc::kNoId;
        int frame = 0;
        doc::Channel channel = doc::Channel::Position;
        bool operator==(const CelKeyRef &) const = default;
    };
    bool removeCelKeys(const std::vector<CelKeyRef> &keys);
    // All of them `delta` frames on (or back): refused if one would go before the first frame or on a key that is not moved too.
    bool moveCelKeys(const std::vector<CelKeyRef> &keys, int delta);
    // While keys are dragged on the timeline: the motions the cels would have with `keys` moved `delta` frames (as
    // moveCelKeys() would leave them), for the views that draw a motion (the path of a trajectory) to show it live. No
    // edit is made. A move that would be refused (a place taken) shows nothing moved. clearKeyMovePreview() ends it.
    void previewKeyMove(const std::vector<CelKeyRef> &keys, int delta);
    void clearKeyMovePreview();
    // The motion of `cel` as it would be with the keys moved, or nullptr when none is being previewed.
    const doc::CelMotion *previewedMotion(doc::CelId cel) const;
    // What was copied: a key with its curve, where it was relative to the first copied (`offset`), and the cel it came from.
    struct CelKeyCopy
    {
        doc::CelId cel = doc::kNoId;
        int offset = 0;
        doc::Channel channel = doc::Channel::Position;
        doc::CelKey key;
    };
    std::vector<CelKeyCopy> copyCelKeys(const std::vector<CelKeyRef> &keys) const;
    // The copied keys with the first on `frame`: on `onto` if they all came from one cel, else each on its own (one that is
    // gone is left out). A key already there is replaced. False if nothing could be placed.
    bool pasteCelKeys(const std::vector<CelKeyCopy> &copied, int frame, doc::CelId onto);
    // Where the cel turns and scales around, in pixels of the page. The pose on every key stays as it was, so the cel is
    // not moved on the frames with a key; between keys the piece goes round a different point.
    bool setCelPivot(doc::CelId cel, double x, double y);
    // A key on the current frame of the cel, or none if there is one: for a button and a key.
    void toggleCelKey(doc::CelId cel);
    // The same for the cels chosen together: if the current one has a key on the frame, the keys there go from all of them,
    // else each one without a key gets one, at the pose it has there. One undo step. (One cel chosen: toggleCelKey().)
    void toggleChosenCelKeys();
    // While a tool drags the pose: the key of `cel` on `frame` is put at `pose` at once, with no undo step (the first key of
    // a cel is made, with its pivot). finishCelPose() then records the whole drag as one step, from `before` (the cel's
    // motion when the drag began); cancelCelPose() puts it back.
    // Only the `channels` of `pose` are the drag's: a key of a channel there is changed, with none its value goes with the
    // frame.
    void previewCelPose(doc::CelId cel, int frame, const doc::CelPose &pose, const std::vector<doc::Channel> &channels);
    // The same for several cels at once (cels moved together): the views are told once, so the frame is drawn once.
    struct CelPoseChange
    {
        doc::CelId cel = doc::kNoId;
        int frame = 0;
        doc::CelPose pose;
        std::vector<doc::Channel> channels;
    };
    void previewCelPoses(const std::vector<CelPoseChange> &changes);
    // The cel's pose on `frame` set by hand (the options' fields): changes the key there, or, with none, leaves a pose
    // that goes with the frame (see CelMotion::setTemporary) until the Cel key button makes it a key. A cel with no keys
    // is not moved this way.
    void setCelPose(doc::CelId cel, int frame, const doc::CelPose &pose, doc::Channel channel);
    void finishCelPose(doc::CelId cel, const doc::CelMotion &before);
    // The same for several cels moved together: one undo step for all (each with the motion it had when the drag began).
    void finishCelPoses(const std::vector<std::pair<doc::CelId, doc::CelMotion>> &before);
    // For a cel with no key: its first one on `frame` (position, rotation and scale, as it is drawn, the pivot as the first key
    // puts it) with no undo step, as previewCelPose() changes it; finishCelPose() from the motion it had then records both.
    void beginFirstKey(doc::CelId cel, int frame);
    void cancelCelPose(doc::CelId cel, const doc::CelMotion &before);
    // Takes all the keys of the cel away (it stands still, as drawn).
    bool clearCelMotion(doc::CelId cel);
    // The cel's keys played backwards over the frames they span (see doc::CelMotion::reversed()): one undo step. False
    // if it has fewer than two keyframes (nothing to turn around).
    bool reverseCelMotion(doc::CelId cel);
    // Markers on the timeline (frame -> name; see doc::Document::markers()). Each change is one undo step.
    const std::map<int, std::string> &markers() const { return m_document->markers(); }
    // A marker on `frame` (>= 0) with this name; renames the one that is there. False if nothing changes.
    bool setMarker(int frame, const QString &name);
    // Takes the marker on `frame` away. False if there is none.
    bool removeMarker(int frame);
    // Moves the marker on `from` to `to`: refused (false) if `to` has a marker or `from` has none.
    bool moveMarker(int from, int to);
    // A marker on the current frame, or none if there is one: for a key.
    void toggleMarker();
    // Goes to the next marker after the current frame (direction > 0) or the previous one; false, staying put, if there
    // is none that way.
    bool goToMarker(int direction);
    // The scene's length, in frames (1 or more; exposures past it stay, they are just not part of the scene).
    void setSceneLength(int frames);
    // Whole frames per second (one undo step); a rate the document already has changes nothing.
    void setFrameRate(int fps);
    int sceneLength() const;
    // The frames that play (the Timeline, Autoplay and the Compositing window all go through this one range): from
    // `first` to `last`, 0-based, both included, any whole numbers (before the scene or past it too). `last` of
    // kSceneEnd is the end of the scene, wherever it is put. Saved with the project (not an undo step); a new project
    // plays the whole scene.
    static constexpr int kSceneEnd = doc::Document::kSceneEnd;
    void setPlayRange(int first, int last);
    int playRangeFirst() const { return m_document->playFirst(); }
    int playRangeLast() const; // resolved, and never before the first
    bool playRangeFollowsSceneEnd() const { return m_document->playLast() == kSceneEnd; }
    // The frame after the last exposure of any cel: how short the scene can be without leaving some out.
    int usedFrames() const;
    void setCameraVisible(bool visible);

    // The layer's mask: painting tools work on it, not on the layer, while it is the one being edited (and the layer
    // is the current one with a mask). kNoId: none, the layers' own pixels.
    void setMaskEditing(doc::LayerId layer);
    doc::LayerId maskEditingLayer() const { return m_maskLayer; }
    // A mask for the current layer (everything shows), made the one edited; or take it away. Each one undo step.
    bool createLayerMask();
    bool deleteLayerMask();                   // an invisible camera is not applied
    void setCameraKey(int frame, const doc::CameraPose &pose); // a key at `frame` (a camera is made if there is none)
    bool removeCameraKey(int frame);
    bool moveCameraKey(int from, int to);
    bool setCameraEasing(int frame, const doc::Easing &easing); // from the key at `frame` to the next
    // The camera as it is at `frame` (the whole sheet if there is none or it is invisible).
    doc::CameraPose cameraPoseAt(int frame) const;
    // For a drag: shows `camera` without recording it, and commitCamera() then records the whole change as one
    // step, from what it was before the drag.
    void previewCamera(const std::optional<doc::Camera> &camera);
    void commitCamera(const std::optional<doc::Camera> &before, const std::optional<doc::Camera> &after);

    // Whether the camera is what the Layers panel has selected: drawing is refused then (see beginPaint()), and the
    // canvas moves the camera with the pen (canvas::CameraGizmo). Not kept in the project.
    bool cameraSelected() const { return m_cameraSelected; }
    void selectCamera(bool selected);

    // The compositing network kept in the project: one undo step puts `graph` in place of it.
    const doc::GraphData &compositing() const { return m_document->compositing(); }
    void setCompositing(doc::GraphData graph);
    // Where the node editor was left (no undo step; kept in the project).
    const doc::GraphView &compositingView() const { return m_document->compositingView(); }
    void setCompositingView(const doc::GraphView &view) { m_document->setCompositingView(view); }

    // An image sequence as one new cel: image i is a drawing of its own, shown for `hold` frames from
    // startFrame + i * hold (the scene gets longer if it needs). Images that are null are skipped; returns how
    // many were imported. One undo step.
    // `progress`, if given, is told how many are done after each one (the views refresh once, at the end).
    // A cel that fills in as the pictures come (a render in the background): made at once with an empty drawing for each of
    // `count` frames from `startFrame` (one undo step), then `fillSequenceFrame` puts a picture in the drawing of frame
    // `index`, scaled to fit the page (made bigger too if it is smaller: a draft is rendered small), centered; it is not an
    // undo step and no view refreshes but the canvas's. `finishSequence` records the pictures as one more step.
    struct PendingSequence
    {
        doc::CelId cel = doc::kNoId;
        std::vector<doc::DrawingId> drawings;
        std::vector<doc::LayerId> layers;
    };
    // `link`, if given, is recorded on the cel (see doc::BlenderLink) in the same step.
    std::optional<PendingSequence> beginSequenceCel(const QString &name, int startFrame, int count,
                                                    const doc::BlenderLink *link = nullptr);
    // The cels that came from a Blender scene, with where from.
    std::vector<std::pair<doc::CelId, doc::BlenderLink>> blenderLinks() const;
    // False if the cel (or that drawing) is gone: it was undone, say.
    bool fillSequenceFrame(const PendingSequence &sequence, int index, const QImage &image);
    void finishSequence(const PendingSequence &sequence);

    // The pictures of a cel that was made from a sequence (a Blender scene, saved again) replacing the old ones, frame by
    // frame as they come: picture `index` goes in the first layer of the drawing at frame `startFrame + index` (a frame the
    // cel does not reach yet gets a drawing of its own; the other layers of a drawing, the person's, are left alone). Nothing
    // is an undo step until `finishSequenceUpdate`, which records all that was done as one, from the pictures as they were.
    struct SequenceUpdate
    {
        doc::CelId cel = doc::kNoId;
        int startFrame = 0;
        std::map<std::pair<doc::DrawingId, doc::LayerId>, doc::TiledImage> before; // the layers touched, as they were
        std::vector<std::unique_ptr<doc::Command>> steps;                          // the drawings made
    };
    std::unique_ptr<SequenceUpdate> beginSequenceUpdate(doc::CelId cel, int startFrame);
    // False if the cel is gone.
    bool applySequenceFrames(SequenceUpdate &update, const std::map<int, QImage> &frames);
    void finishSequenceUpdate(SequenceUpdate &update);

    int importImageSequence(const QList<QImage> &images, const QString &name, int startFrame, int hold,
                            const std::function<void(int)> &progress = {});

    // What the cel shows, moved by its keys and all, drawn as pictures in a new cel above it without any keys: where the
    // cel has a drawing, a frame is a picture; a stretch of frames that look the same stays one picture held. Only what
    // is within the page is kept. One undo step. The new cel's id, or kNoId (with `reason` saying why).
    doc::CelId rasterCel(doc::CelId cel, QString *reason = nullptr);

    // Erases every pixel of the current layer, as one undo step. Returns
    // false, with `reason` saying why, when there is nothing to clear or the
    // layer (or its cel) is hidden or locked.
    bool clearCurrentLayer(QString *reason = nullptr);
    // Whether clearCurrentLayer() would do something.
    bool canClearCurrentLayer() const;
    // The current layer is a vector layer (not the mask drawn on it).
    bool currentLayerIsVector() const;

    // --- selection ------------------------------------------------------------------
    // The selected area in document coordinates; empty = nothing selected
    // (tools then act on the whole layer). Every change is an undo step;
    // one made during a paint (a move carrying it) is part of the paint's
    // step: see endPaint().
    const QPainterPath &selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    // By default the selection is kept inside the page (what lies outside is never drawn); a pasted image bigger than
    // the page is selected whole (`clipToPage` false), so it can be moved to bring any part of it onto the page.
    void setSelection(const QPainterPath &area, bool clipToPage = true);
    void selectAll();
    void clearSelection();
    // Selects what was not selected (all if nothing was).
    void invertSelection();
    // Moves the selection's edge out (`pixels` > 0) or in (< 0) by that many pixels, rounding the corners it goes
    // around; kept inside the page. One undo step. False, with `reason`, when nothing is selected or shrinking would
    // leave nothing.
    bool growSelection(int pixels, QString *reason = nullptr);

    // Work on the pixels of the current layer under the selection; each is
    // one undo step and keeps the selection. They return false, with
    // `reason` saying why, when there is no selection, nothing under it, or
    // the layer (or its cel) is hidden (or locked, for the ones that change it).
    // Erases what is selected; what is not selected.
    bool eraseSelection(QString *reason = nullptr) { return eraseBySelection(true, reason); }
    bool eraseOutsideSelection(QString *reason = nullptr) { return eraseBySelection(false, reason); }
    // Fills what is selected with `color` (on a mask: shows the layer there). Not on a vector layer.
    bool fillSelection(const QColor &color, QString *reason = nullptr);
    // Moves (cut) or copies what is selected to a new layer above the
    // current one, in the same drawing; the new layer becomes current.
    // Mirrors the selected pixels across the middle of the selection's box (the selection turns with them, so it still
    // outlines them); what is outside stays. It works on the layers the tools work on: the current one, or every layer
    // chosen in the Layers panel (a chosen cel or frame: all its layers). One undo step. False, with `reason`, if nothing
    // is selected or there is nothing under it.
    bool flipSelectionHorizontally(QString *reason = nullptr) { return flipSelection(true, reason); }
    bool flipSelectionVertically(QString *reason = nullptr) { return flipSelection(false, reason); }
    bool cutSelectionToLayer(QString *reason = nullptr);
    bool copySelectionToLayer(QString *reason = nullptr);

    // --- clipboard ------------------------------------------------------------------
    // The pixels of the current layer under the selection (all of it when nothing is selected), cropped to what is
    // drawn; `origin` gets where their top-left corner is. A null image, with
    // `reason`, when there is nothing to copy.
    QImage copyPixels(QPoint *origin, QString *reason = nullptr);
    // Pastes `image` over the current layer (a drawing is made if the frame
    // is empty), at `origin` (centered in the document when there
    // is none); the pasted area becomes the selection. One undo step; false,
    // with `reason`, if the layer cannot be painted (hidden, locked).
    bool pasteImage(const QImage &image, std::optional<QPoint> origin, QString *reason = nullptr);

    // What a paste puts on the canvas before it is part of the layer: it floats, in a Transform session, so it can be
    // moved or turned at once and does not move what lies under it. `strokes` (when copied from a vector layer onto
    // one) are the strokes, at their own places; the image is what they look like (or the pixels, for a raster layer).
    struct FloatingPaste
    {
        QImage image; // premultiplied ARGB32
        QPoint at;    // its top-left corner in the document
        std::vector<doc::VectorStroke> strokes;
        bool vector = false;
    };
    // Starts a floating paste for the Transform tool to take (see takeFloatingPaste(), and floatingPasteStarted()). Checks
    // what pasteImage() does (and that a vector layer gets strokes, a raster layer an image); nothing in the document
    // changes until the transform is applied. False, with `reason`, if it cannot be done.
    bool beginFloatingPaste(const QImage &image, std::optional<QPoint> origin,
                            const std::vector<doc::VectorStroke> *strokes, QString *reason = nullptr);
    std::optional<FloatingPaste> takeFloatingPaste();
    // The strokes the last copy took from a vector layer (empty after a copy of a raster layer).
    const std::vector<doc::VectorStroke> &copiedStrokes() const { return m_copiedStrokes; }

    QString undoText() const; // "Undo stroke", or plain "Undo"
    QString redoText() const;
    static QString commandName(doc::CommandKind kind);

    // --- painting ---------------------------------------------------------------
    // A stroke paints live into a layer, then is recorded in one go.
    struct PaintTarget
    {
        doc::CelId cel = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::LayerId layer = doc::kNoId;
        doc::TiledImage *image = nullptr; // the layer's pixels, or its mask's when `mask`
        bool mask = false;
        bool vector = false; // the layer is a vector layer: a stroke is kept with the pixels
        // What a painting tool puts on this target: on a mask, black. A mask holds what is hidden, in black (where it
        // has no pixel everything shows, white): a tool that paints puts the layer back (takes the black away) and the
        // eraser hides (paints it): see CanvasView::beginStroke(), FillTool and ShapeTool.
        QColor tint(const QColor &color) const
        {
            return mask ? QColor(0, 0, 0, color.alpha()) : color;
        }
    };
    // Resolves the current layer. On an empty frame a new drawing (with one
    // layer) is created if `createIfEmpty` (it becomes part of the stroke's
    // undo step). Returns nothing if painting is not possible; `reason` then
    // says why (empty when there is nothing to tell, e.g. erasing on an empty frame).
    // While a cel or a frame (and not one of its layers) is the selected row of the Layers panel, drawing is refused
    // where the cel has a frame; where it has none, drawing makes the frame and selects its layer (see
    // setLayerChosen()).
    // `forDrawing` false is for the tools that move whole layers: they are not refused.
    // A vector layer only takes the brush and the eraser, which keep what they draw as a stroke (attachStroke()):
    // the other tools are refused there, unless `vectorOk`.
    std::optional<PaintTarget> beginPaint(bool createIfEmpty, QString *reason = nullptr, bool forDrawing = true,
                                          bool vectorOk = false);
    // On a vector layer: the stroke that endPaint() records, in the same undo step as its pixels. `points` is
    // taken as the stroke is over. Dropped by cancelPaint().
    void attachStroke(doc::VectorStroke stroke);
    // Live pixel updates during the stroke.
    void notifyPainted(const QRect &docRect);
    // A layer of a drawing of a cel.
    struct LayerRef
    {
        doc::CelId cel = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::LayerId layer = doc::kNoId;
        bool operator==(const LayerRef &) const = default;
    };
    // A perspective layer, wherever it is (any cel, any frame): what the pen can snap to.
    struct PerspectiveLayerInfo
    {
        LayerRef ref;
        perspective::Grid grid;
    };
    // The perspective layer with this id; nothing if there is none (or it is a layer of pixels).
    std::optional<PerspectiveLayerInfo> perspectiveLayerById(doc::LayerId id) const;

    // The layers chosen in the Layers panel, for the edits that act on several
    // at once (a move): every layer of a chosen frame or cel is in it. Empty
    // when that is just the current layer.
    // Shows `properties` on the layer at once, as a preview, recording
    // nothing (a slider being dragged); the edit is then executed as a command
    // from the properties it had before. False if there is no such layer.
    bool previewLayerProperties(const LayerRef &layer, const doc::LayerProperties &properties);
    // The same for a cel (its opacity while the slider is dragged): shown at
    // once, recorded nowhere; the edit is then executed as a command from the
    // properties it had before. False if there is no such cel.
    bool previewCelProperties(doc::CelId cel, const doc::CelProperties &properties);
    // Records an edit shown with previewLayerProperties(): the layer back to
    // `before`, then `after` executed as one undo step (nothing if equal).
    void commitLayerProperties(const LayerRef &layer, const doc::LayerProperties &before,
                               const doc::LayerProperties &after);
    // --- layer clipboard ------------------------------------------------------------
    // Layers copied from the Layers panel (of any frames, in the order they
    // are drawn): their pixels and looks, kept apart from the picture clipboard.
    bool copyLayers(const std::vector<LayerRef> &layers, QString *reason = nullptr);
    bool hasCopiedLayers() const { return !m_copiedLayers.empty(); }
    // Puts the copied layers on top of the current frame (one is made when the cel shows
    // none there): each as a layer of its own, or, `merged`, as the one layer of what they
    // show together. One undo step; the pasted layer (the top one) becomes current.
    // With `replace` (layers of the current frame, as chosen in the panel) the pasted layers
    // take their place, at the lowest of them, and they go (locked ones stay) - unless these
    // are the very layers that were copied, which are not overwritten by their own copy.
    bool pasteLayers(bool merged, const std::vector<doc::LayerId> &replace = {}, QString *reason = nullptr);

    void setLayerSelection(std::vector<LayerRef> layers);
    const std::vector<LayerRef> &layerSelection() const { return m_layerSelection; }
    // Like beginPaint(false), for every layer of layerSelection() (or the
    // current layer when it is empty) that can be painted: hidden and locked
    // ones, or in hidden or locked cels, are left out. Empty, with `reason`,
    // when none is left. The first target is the current layer if it is in.
    // A vector layer is taken only with `vectorOk` (the tool draws its strokes again, see attachStrokeChange()).
    std::vector<PaintTarget> beginPaintLayers(QString *reason = nullptr, bool vectorOk = false);
    // After beginPaintLayers(): the strokes of the vector layer `target` are `after` now (they were `before`), in the
    // same undo step as its pixels.
    void attachStrokeChange(const PaintTarget &target, std::vector<doc::VectorStroke> before,
                            std::vector<doc::VectorStroke> after);
    // Records the stroke; `before` is the layer image as it was at beginPaint().
    // If the selection changed meanwhile, that change is part of the same
    // undo step. `kind` names the step ("Undo move").
    void endPaint(const doc::TiledImage &before, doc::CommandKind kind = doc::CommandKind::Paint);
    // The same after beginPaintLayers(): `before` holds each target's image
    // as it was, in the order of the targets.
    void endPaint(const std::vector<doc::TiledImage> &before, doc::CommandKind kind);
    // Abandons the stroke (the caller has already restored the pixels).
    void cancelPaint();
    bool isPainting() const { return m_paint.has_value(); }

public slots:
    void undo();
    void redo();
    void setCurrentFrame(int frame);
    void setCurrentCel(doc::CelId cel);
    // The cel as a whole is chosen (its name in the timeline, as its row in the Layers panel): it is current, no layer is, and
    // the layers of all its frames are the ones the tools act on together (a transform moves every frame of it).
    void chooseCel(doc::CelId cel);
    // The cels chosen together (Ctrl and Shift in the timeline and the Layers panel), bottom up: always the current cel, and
    // the others that were chosen with it and are still there. Only the current cel is acted on by the tools.
    std::vector<doc::CelId> celSelection() const;
    // Chooses `cels` together, `current` the one that is current (it is among them). One alone is chooseCel() without the
    // layers being the tools' (the selection only).
    void setCelSelection(const std::vector<doc::CelId> &cels, doc::CelId current);
    // The chosen cels moved together: as a block where `insertAt` is (a position of the stack as it is, bottom up, 0 to the
    // number of cels), or each one place up or down. One undo step. False when nothing moves.
    bool moveChosenCels(int insertAt);
    bool shiftChosenCels(bool up);
    // A layer of the current drawing.
    void setCurrentLayer(doc::LayerId layer);
    // Whether the Layers panel's selection is a layer (true) or a cel or a frame, which cannot be drawn on (see
    // beginPaint()).
    bool layerChosen() const { return m_layerChosen; }
    void setLayerChosen(bool chosen);
    // True when the pixel tools have nowhere to work right now, for the cursor: the camera is selected, or a cel or
    // a frame is while the current frame has a drawing (on an empty frame drawing makes one).
    bool drawingBlocked() const;

signals:
    // The audio track changed without a structure change (a volume being dragged): the views and the player take it up.
    void audioChanged();
    // The camera changed (a key, whether it is on, or it was added or taken away; also a preview).
    void cameraChanged();
    // A marker was added, renamed, moved or taken away.
    void markersChanged();
    // The keys or the pivot of a cel's motion changed (the frame shown is drawn again, and the timeline's keys).
    void motionChanged();
    // The preview of keys being dragged changed (see previewKeyMove()).
    void motionPreviewChanged();
    // The camera was selected, or another row was (see cameraSelected()).
    void cameraSelectionChanged();
    // Pixels of (a layer of) `drawing` changed in `rect` (document coordinates).
    void pixelsChanged(doc::CelId cel, doc::DrawingId drawing, const QRect &rect);
    // Cels, drawings, layers, timing or document properties changed.
    void structureChanged();
    // The play range was set (see setPlayRange()), or went back to the whole scene with a new project.
    void playRangeChanged();
    void currentFrameChanged(int frame);
    void currentCelChanged(doc::CelId cel);
    void currentLayerChanged(doc::LayerId layer);
    // Drawing on an empty frame of the selected cel made the frame and selected its layer.
    void layerChosenByDrawing();
    // Whether the selection is a layer changed (see layerChosen()).
    void layerChosenChanged();
    // The layers chosen in the Layers panel (layerSelection()) changed.
    void layerSelectionChanged();
    // chooseCel() was called: the Layers panel shows the cel's row as chosen.
    void celChosen(doc::CelId cel);
    // The cels chosen together (celSelection()) changed.
    void celSelectionChanged();
    // Which layer's mask is being edited changed (see setMaskEditing()), or a mask was made or taken away.
    void maskEditingChanged();
    // Undo/redo availability or labels changed.
    void historyChanged();
    void selectionChanged();
    // Emitted before anything that must not happen mid-stroke (undo, frame
    // change...): whoever is painting should cancel its stroke now.
    void strokeInterrupted();
    // The document is about to change under an open pose edit (another frame or cel, a command, undo): the canvas applies it.
    void poseEditClosing();
    // A new or loaded document took the place of the old one (size and frame
    // rate may differ); structureChanged and the current-item signals follow.
    void documentReplaced();
    // A floating paste is ready (beginFloatingPaste()): the canvas takes it up in the Transform tool.
    void floatingPasteStarted();

private:
    // A new cel above `above` of pictures: picture i is a drawing shown for runs[i].second frames from runs[i].first
    // (in order, apart). One undo step; the pictures are asked for one at a time, as they are put in.
    doc::CelId importRuns(const std::vector<std::pair<int, int>> &runs, const std::function<QImage(int)> &picture,
                          const QString &name, doc::CelId above, const std::function<void(int)> &progress);
    void interruptStroke();
    // The current layer, if it can be worked on, or nullptr with `reason`
    // set: no cel, it or the layer hidden (or locked, if `changesIt`), or
    // no layer because the frame is empty (`nothing` then says why).
    doc::Layer *workLayer(bool changesIt, const QString &nothing, QString *reason, bool vectorOk = false);
    bool selectionToLayer(bool cut, QString *reason);
    bool flipSelection(bool horizontal, QString *reason);
    bool eraseBySelection(bool inside, QString *reason);
    // The first key of a cel: where it turns around (the middle of what it shows).
    void clearTemporaryPose();
    doc::CelId m_tempPoseCel = doc::kNoId; // the cel that has a pose that is not a key (one at a time)
    void startMotion(doc::CelMotion &motion, const doc::Cel &cel, int frame) const;
    // The motion the cel would have with a key on `frame` from `pose` (set in `out`); false if that changes nothing.
    // Changes the cel's motion as previewCelPose() does, without telling the views; false if there is no such cel.
    bool applyCelPose(doc::CelId cel, int frame, const doc::CelPose &pose, const std::vector<doc::Channel> &channels);
    bool celKeyMotion(const doc::Cel &cel, int frame, const doc::CelPose &pose, doc::CelMotion &out) const;
    // Sets the selection as is, recording nothing.
    void assignSelection(const QPainterPath &area);
    void publish(const doc::ChangeSet &changes);
    void ensureValidCurrentCel();
    // Puts the current layer back in the current drawing (see currentLayer()).
    void resolveCurrentLayer();
    static std::unique_ptr<doc::Document> makeDefaultDocument();
    static std::unique_ptr<doc::Document> makeDocument(int width, int height, int frameRate, int frames,
                                                        const QColor &paper);

    std::unique_ptr<doc::Document> m_document;
    doc::History m_history;
    doc::CelId m_currentCel = doc::kNoId;
    int m_currentFrame = 0;
    std::optional<FloatingPaste> m_floating;
    std::map<doc::CelId, doc::CelMotion> m_keyPreview;
    // The motions of the cels with `keys` moved `delta` frames; false if that is refused.
    bool movedKeyMotions(const std::vector<CelKeyRef> &keys, int delta, std::map<doc::CelId, doc::CelMotion> &motions) const;
    std::vector<doc::VectorStroke> m_copiedStrokes;
    bool m_layerChosen = true;
    bool m_cameraSelected = false;
    doc::LayerId m_currentLayer = doc::kNoId;
    std::string m_currentLayerName; // looked for when the drawing changes
    QPainterPath m_selection;
    std::vector<LayerRef> m_layerSelection;
    std::vector<doc::CelId> m_celSelection; // (see celSelection())
    std::vector<doc::Layer> m_copiedLayers; // bottom to top
    // The layer the chosen ones merge down into: the one under the lowest of them (kNoId if there is none).
    doc::LayerId layerBelow(const std::vector<doc::LayerId> &ids) const;
    // `incompatible` is set when the kinds do not mix (or vector layers have a blend or clip): none is returned then.
    std::vector<const doc::Layer *> mergeableLayers(const std::vector<doc::LayerId> &ids, bool *incompatible = nullptr) const;

    struct PaintState
    {
        PaintTarget target;
        std::vector<PaintTarget> others; // painted along with `target` (beginPaintLayers)
        std::unique_ptr<doc::Command> createdDrawing; // applied, not yet recorded
        std::vector<doc::VectorStroke> strokes;       // the strokes of a vector layer (attachStroke())
        std::vector<std::unique_ptr<doc::Command>> strokeChanges; // applied, not yet recorded (attachStrokeChange())
        QPainterPath selectionBefore;
    };
    std::optional<PaintState> m_paint;
    doc::LayerId m_maskLayer = doc::kNoId;
};

} // namespace mothion::document
