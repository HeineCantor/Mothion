#pragma once

#include "core/document/Document.h"
#include "core/document/History.h"
#include "document/Tween.h"

#include <QImage>
#include <functional>
#include <QObject>
#include <QPainterPath>
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
    // Adds an empty layer of `kind` to the current drawing, above the current
    // layer (on top if `onTop`), named "Layer N" (or "Perspective N") with the
    // first free N; it becomes current. A perspective layer starts with a grid
    // fit to the document. One undo step; false on an empty frame.
    // Merges layers of the current drawing into the lowest of them (it keeps
    // its name and place; the rest go): what they show together, each with
    // its own opacity and blend mode, as one undo step. Only raster layers
    // that are shown and not locked are taken; false, with `reason`, when
    // fewer than two are left. The result becomes the current layer.
    bool mergeLayers(const std::vector<doc::LayerId> &layers, QString *reason = nullptr);
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
    // The scene's length, in frames (1 or more; exposures past it stay, they are just not part of the scene).
    void setSceneLength(int frames);
    int sceneLength() const;
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
    int importImageSequence(const QList<QImage> &images, const QString &name, int startFrame, int hold,
                            const std::function<void(int)> &progress = {});

    // Erases every pixel of the current layer, as one undo step. Returns
    // false, with `reason` saying why, when there is nothing to clear or the
    // layer (or its cel) is hidden or locked.
    bool clearCurrentLayer(QString *reason = nullptr);
    // Whether clearCurrentLayer() would do something.
    bool canClearCurrentLayer() const;

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

    // Work on the pixels of the current layer under the selection; each is
    // one undo step and keeps the selection. They return false, with
    // `reason` saying why, when there is no selection, nothing under it, or
    // the layer (or its cel) is hidden (or locked, for the ones that change it).
    // Erases what is selected.
    bool eraseSelection(QString *reason = nullptr);
    // Moves (cut) or copies what is selected to a new layer above the
    // current one, in the same drawing; the new layer becomes current.
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
    std::optional<PaintTarget> beginPaint(bool createIfEmpty, QString *reason = nullptr, bool forDrawing = true);
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
    // Puts `image` (its top-left corner at `origin` on the page) as a new layer on top of everything the frame
    // `frame` shows: in the drawing the topmost cel that is not locked has there. With a non-empty `area` only what
    // is inside that path is kept. One undo step; the choice of layer and cel stays. False, with `reason`, if
    // no cel has a drawing at that frame, or all are locked.
    bool addImageLayerOnTop(const QImage &image, const QPoint &origin, const QPainterPath &area, int frame,
                            const QString &name, QString *reason = nullptr);
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
    std::vector<PaintTarget> beginPaintLayers(QString *reason = nullptr);
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
    // The camera was selected, or another row was (see cameraSelected()).
    void cameraSelectionChanged();
    // Pixels of (a layer of) `drawing` changed in `rect` (document coordinates).
    void pixelsChanged(doc::CelId cel, doc::DrawingId drawing, const QRect &rect);
    // Cels, drawings, layers, timing or document properties changed.
    void structureChanged();
    void currentFrameChanged(int frame);
    void currentCelChanged(doc::CelId cel);
    void currentLayerChanged(doc::LayerId layer);
    // The layers chosen in the Layers panel (layerSelection()) changed.
    void layerSelectionChanged();
    // Drawing on an empty frame of the selected cel made the frame and selected its layer.
    void layerChosenByDrawing();
    // Whether the selection is a layer changed (see layerChosen()).
    void layerChosenChanged();
    // Which layer's mask is being edited changed (see setMaskEditing()), or a mask was made or taken away.
    void maskEditingChanged();
    // Undo/redo availability or labels changed.
    void historyChanged();
    void selectionChanged();
    // Emitted before anything that must not happen mid-stroke (undo, frame
    // change...): whoever is painting should cancel its stroke now.
    void strokeInterrupted();
    // A new or loaded document took the place of the old one (size and frame
    // rate may differ); structureChanged and the current-item signals follow.
    void documentReplaced();

private:
    void interruptStroke();
    // The current layer, if it can be worked on, or nullptr with `reason`
    // set: no cel, it or the layer hidden (or locked, if `changesIt`), or
    // no layer because the frame is empty (`nothing` then says why).
    doc::Layer *workLayer(bool changesIt, const QString &nothing, QString *reason);
    bool selectionToLayer(bool cut, QString *reason);
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
    bool m_layerChosen = true;
    bool m_cameraSelected = false;
    doc::LayerId m_currentLayer = doc::kNoId;
    std::string m_currentLayerName; // looked for when the drawing changes
    QPainterPath m_selection;
    std::vector<LayerRef> m_layerSelection;
    std::vector<doc::Layer> m_copiedLayers; // bottom to top
    // The layer the chosen ones merge down into: the one under the lowest of them (kNoId if there is none).
    doc::LayerId layerBelow(const std::vector<doc::LayerId> &ids) const;
    std::vector<const doc::Layer *> mergeableLayers(const std::vector<doc::LayerId> &ids) const;

    struct PaintState
    {
        PaintTarget target;
        std::vector<PaintTarget> others; // painted along with `target` (beginPaintLayers)
        std::unique_ptr<doc::Command> createdDrawing; // applied, not yet recorded
        QPainterPath selectionBefore;
    };
    std::optional<PaintState> m_paint;
    doc::LayerId m_maskLayer = doc::kNoId;
};

} // namespace mothion::document
