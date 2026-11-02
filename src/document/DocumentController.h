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
// It owns the Document and its History, keeps the current selection
// (layer, frame) and turns command ChangeSets into Qt signals. Views read
// through document() and change things only through this class, so every
// change is undoable and every view hears about it.
class DocumentController : public QObject
{
    Q_OBJECT

public:
    // Starts with a default document: 1920×1080, 24 fps, 120 frames, one layer.
    explicit DocumentController(QObject *parent = nullptr);
    ~DocumentController() override;

    const doc::Document &document() const { return *m_document; }

    // --- whole document ---------------------------------------------------------
    // Starts over with a new default document.
    void newDocument();
    // Puts a loaded document in place of the current one: history cleared,
    // the top layer and frame 1 current, nothing selected.
    void replaceDocument(std::unique_ptr<doc::Document> document);
    // Changes since the last save (or since the document was created or loaded).
    bool isModified() const { return !m_history.isClean(); }
    // The document now matches the saved file.
    void markSaved();
    // Applies edits left open (a puppet warp) and cancels a stroke in
    // progress, so that the document is complete, e.g. before saving.
    void finishEdits() { interruptStroke(); }

    doc::LayerId currentLayer() const { return m_currentLayer; }
    int currentFrame() const { return m_currentFrame; }

    // --- commands -------------------------------------------------------------
    // Runs a command and records it for undo (nullptr does nothing).
    void execute(std::unique_ptr<doc::Command> command);
    // Mutable document access for building commands that allocate ids.
    doc::Document &documentForCommands() { return *m_document; }

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    // Puts `image` on a new layer above the current one, named `name`, as one
    // drawing held for the whole scene: centered, scaled down to fit the
    // document if larger. One undo step; the new layer becomes current.
    void importImage(const QImage &image, const QString &name);

    // Erases every pixel of the drawing at the current layer and frame, as one
    // undo step. Returns false, with `reason` saying why, when there is
    // nothing to clear or the layer is hidden or locked.
    bool clearCurrentDrawing(QString *reason = nullptr);
    // Whether clearCurrentDrawing() would do something.
    bool canClearCurrentDrawing() const;

    // --- selection ------------------------------------------------------------------
    // The selected area in document coordinates; empty = nothing selected
    // (tools then act on the whole drawing). Every change is an undo step;
    // one made during a paint (a move carrying it) is part of the paint's
    // step: see endPaint().
    const QPainterPath &selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    void setSelection(const QPainterPath &area);
    void selectAll();
    void clearSelection();
    // Selects what was not selected (all if nothing was).
    void invertSelection();

    // Work on the pixels under the selection in the drawing at the current
    // layer and frame; each is one undo step and keeps the selection. They
    // return false, with `reason` saying why, when there is no selection,
    // nothing under it, or the layer is hidden (or locked, for the ones that
    // change it).
    // Erases what is selected.
    bool eraseSelection(QString *reason = nullptr);
    // Moves (cut) or copies what is selected to a new layer above, exposed
    // like the drawing it came from; the new layer becomes current.
    bool cutSelectionToLayer(QString *reason = nullptr);
    bool copySelectionToLayer(QString *reason = nullptr);

    // --- clipboard ------------------------------------------------------------------
    // The pixels of the drawing at the current layer and frame under the
    // selection (all of it when nothing is selected), cropped to what is
    // drawn; `origin` gets where their top-left corner is. A null image, with
    // `reason`, when there is nothing to copy.
    QImage copyPixels(QPoint *origin, QString *reason = nullptr);
    // Pastes `image` over the drawing at the current layer and frame (made if
    // the frame is empty), at `origin` (centered in the document when there
    // is none); the pasted area becomes the selection. One undo step; false,
    // with `reason`, if the layer cannot be painted (hidden, locked).
    bool pasteImage(const QImage &image, std::optional<QPoint> origin, QString *reason = nullptr);

    QString undoText() const; // "Undo stroke", or plain "Undo"
    QString redoText() const;
    static QString commandName(doc::CommandKind kind);

    // --- painting ---------------------------------------------------------------
    // A stroke paints live into a drawing, then is recorded in one go.
    struct PaintTarget
    {
        doc::LayerId layer = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::TiledImage *image = nullptr;
    };
    // Resolves the drawing at the current layer/frame. On an empty frame a new
    // drawing is created if `createIfEmpty` (it becomes part of the stroke's
    // undo step). Returns nothing if painting is not possible; `reason` then
    // says why (empty when there is nothing to tell, e.g. erasing on an empty frame).
    std::optional<PaintTarget> beginPaint(bool createIfEmpty, QString *reason = nullptr);
    // Live pixel updates during the stroke.
    void notifyPainted(const QRect &docRect);
    // Records the stroke; `before` is the drawing image as it was at beginPaint().
    // If the selection changed meanwhile, that change is part of the same
    // undo step. `kind` names the step ("Undo move").
    void endPaint(const doc::TiledImage &before, doc::CommandKind kind = doc::CommandKind::Paint);
    // Abandons the stroke (the caller has already restored the pixels).
    void cancelPaint();
    bool isPainting() const { return m_paint.has_value(); }

public slots:
    void undo();
    void redo();
    void setCurrentFrame(int frame);
    void setCurrentLayer(doc::LayerId layer);

signals:
    // Pixels of `drawing` changed in `rect` (document coordinates).
    void pixelsChanged(doc::LayerId layer, doc::DrawingId drawing, const QRect &rect);
    // Layers, drawings, timing or document properties changed.
    void structureChanged();
    void currentFrameChanged(int frame);
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
    // The drawing under the selection operations, or nullptr with `reason` set.
    doc::Drawing *selectedDrawing(bool changesIt, QString *reason);
    bool selectionToLayer(bool cut, QString *reason);
    // Sets the selection as is, recording nothing.
    void assignSelection(const QPainterPath &area);
    void publish(const doc::ChangeSet &changes);
    void ensureValidCurrentLayer();
    static std::unique_ptr<doc::Document> makeDefaultDocument();

    std::unique_ptr<doc::Document> m_document;
    doc::History m_history;
    doc::LayerId m_currentLayer = doc::kNoId;
    int m_currentFrame = 0;
    QPainterPath m_selection;

    struct PaintState
    {
        PaintTarget target;
        std::unique_ptr<doc::Command> createdDrawing; // applied, not yet recorded
        QPainterPath selectionBefore;
    };
    std::optional<PaintState> m_paint;
};

} // namespace mothion::document
