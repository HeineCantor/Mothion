#pragma once

#include "core/document/Document.h"
#include "core/document/History.h"

#include <QImage>
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
    // Puts a loaded document in place of the current one: history cleared,
    // the top cel and frame 1 current, nothing selected.
    void replaceDocument(std::unique_ptr<doc::Document> document);
    // Changes since the last save (or since the document was created or loaded).
    bool isModified() const { return !m_history.isClean(); }
    // The document now matches the saved file.
    void markSaved();
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
    void setSelection(const QPainterPath &area);
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
        doc::TiledImage *image = nullptr;
    };
    // Resolves the current layer. On an empty frame a new drawing (with one
    // layer) is created if `createIfEmpty` (it becomes part of the stroke's
    // undo step). Returns nothing if painting is not possible; `reason` then
    // says why (empty when there is nothing to tell, e.g. erasing on an empty frame).
    std::optional<PaintTarget> beginPaint(bool createIfEmpty, QString *reason = nullptr);
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

signals:
    // Pixels of (a layer of) `drawing` changed in `rect` (document coordinates).
    void pixelsChanged(doc::CelId cel, doc::DrawingId drawing, const QRect &rect);
    // Cels, drawings, layers, timing or document properties changed.
    void structureChanged();
    void currentFrameChanged(int frame);
    void currentCelChanged(doc::CelId cel);
    void currentLayerChanged(doc::LayerId layer);
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

    std::unique_ptr<doc::Document> m_document;
    doc::History m_history;
    doc::CelId m_currentCel = doc::kNoId;
    int m_currentFrame = 0;
    doc::LayerId m_currentLayer = doc::kNoId;
    std::string m_currentLayerName; // looked for when the drawing changes
    QPainterPath m_selection;
    std::vector<LayerRef> m_layerSelection;
    std::vector<doc::Layer> m_copiedLayers; // bottom to top
    std::vector<const doc::Layer *> mergeableLayers(const std::vector<doc::LayerId> &ids) const;

    struct PaintState
    {
        PaintTarget target;
        std::vector<PaintTarget> others; // painted along with `target` (beginPaintLayers)
        std::unique_ptr<doc::Command> createdDrawing; // applied, not yet recorded
        QPainterPath selectionBefore;
    };
    std::optional<PaintState> m_paint;
};

} // namespace mothion::document
