#pragma once

#include "core/document/Document.h"
#include "core/document/History.h"

#include <QObject>
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

    doc::LayerId currentLayer() const { return m_currentLayer; }
    int currentFrame() const { return m_currentFrame; }

    // --- commands -------------------------------------------------------------
    // Runs a command and records it for undo (nullptr does nothing).
    void execute(std::unique_ptr<doc::Command> command);
    // Mutable document access for building commands that allocate ids.
    doc::Document &documentForCommands() { return *m_document; }

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
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
    void endPaint(const doc::TiledImage &before);
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
    // Emitted before anything that must not happen mid-stroke (undo, frame
    // change...): whoever is painting should cancel its stroke now.
    void strokeInterrupted();

private:
    void interruptStroke();
    void publish(const doc::ChangeSet &changes);
    void ensureValidCurrentLayer();

    std::unique_ptr<doc::Document> m_document;
    doc::History m_history;
    doc::LayerId m_currentLayer = doc::kNoId;
    int m_currentFrame = 0;

    struct PaintState
    {
        PaintTarget target;
        std::unique_ptr<doc::Command> createdDrawing; // applied, not yet recorded
    };
    std::optional<PaintState> m_paint;
};

} // namespace mothion::document
