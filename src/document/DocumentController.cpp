#include "document/DocumentController.h"

#include "canvas/TileQt.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"

#include <algorithm>
#include <vector>

namespace mothion::document {

namespace {

constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;
constexpr int kDefaultFrames = 120;

// Bounding rect of changed tiles, in document coordinates.
QRect boundsOf(const std::vector<doc::TileCoord> &tiles)
{
    QRect r;
    for (const doc::TileCoord &c : tiles)
        r |= canvas::tileRect(c);
    return r;
}

} // namespace

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_document(std::make_unique<doc::Document>(kDefaultWidth, kDefaultHeight, doc::FrameRate{24, 1}))
{
    m_document->setFrameCount(kDefaultFrames);
    // The first layer is part of the new document, not an undoable step.
    doc::AddLayerCommand first(*m_document, 0, {tr("Layer 1").toStdString()});
    first.apply(*m_document);
    m_currentLayer = first.layerId();
}

DocumentController::~DocumentController() = default;

// --- commands ------------------------------------------------------------------

void DocumentController::execute(std::unique_ptr<doc::Command> command)
{
    if (!command)
        return;
    interruptStroke();
    publish(m_history.execute(*m_document, std::move(command)));
    emit historyChanged();
}

void DocumentController::importImage(const QImage &image, const QString &name)
{
    if (image.isNull())
        return;
    interruptStroke();
    doc::Document &d = *m_document;

    // The pixels: scaled down to fit the document if larger, centered.
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixels.width() > d.width() || pixels.height() > d.height())
        pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);

    // New layer above the current one, one drawing held for the whole scene,
    // then the pixels: each step applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto layer = std::make_unique<doc::AddLayerCommand>(d, d.indexOf(m_currentLayer) + 1,
                                                        doc::LayerProperties{name.toStdString()});
    const doc::LayerId layerId = layer->layerId();
    publish(layer->apply(d));
    steps.push_back(std::move(layer));

    doc::NewDrawing drawing = doc::makeNewDrawing(d, layerId, 0, d.frameCount());
    publish(drawing.command->apply(d));
    steps.push_back(std::move(drawing.command));

    doc::TiledImage &target = d.layer(layerId)->drawing(drawing.id)->image;
    const doc::TiledImage before = target;
    target.write(at.x(), at.y(), pixels.width(), pixels.height(),
                 reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    if (auto paint = doc::PaintCommand::capture(layerId, drawing.id, before, target))
        steps.push_back(std::move(paint));
    emit pixelsChanged(layerId, drawing.id, QRect(at, pixels.size()));

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportImage, std::move(steps)));
    emit historyChanged();
    setCurrentLayer(layerId);
}

void DocumentController::setSelection(const QPainterPath &area)
{
    // Kept inside the document: what lies outside is never drawn.
    QPainterPath page;
    page.addRect(0, 0, m_document->width(), m_document->height());
    QPainterPath clipped = area.isEmpty() ? QPainterPath() : area.intersected(page);
    if (clipped.boundingRect().isEmpty())
        clipped = QPainterPath();
    if (clipped == m_selection)
        return;
    m_selection = clipped;
    emit selectionChanged();
}

void DocumentController::selectAll()
{
    QPainterPath all;
    all.addRect(0, 0, m_document->width(), m_document->height());
    setSelection(all);
}

void DocumentController::clearSelection()
{
    setSelection({});
}

void DocumentController::undo()
{
    interruptStroke();
    if (!m_history.canUndo())
        return;
    publish(m_history.undo(*m_document));
    emit historyChanged();
}

void DocumentController::redo()
{
    interruptStroke();
    if (!m_history.canRedo())
        return;
    publish(m_history.redo(*m_document));
    emit historyChanged();
}

QString DocumentController::commandName(doc::CommandKind kind)
{
    switch (kind) {
    case doc::CommandKind::Paint: return tr("stroke");
    case doc::CommandKind::AddDrawing: return tr("new drawing");
    case doc::CommandKind::RemoveDrawing: return tr("delete drawing");
    case doc::CommandKind::ChangeTiming: return tr("timing change");
    case doc::CommandKind::AddLayer: return tr("new layer");
    case doc::CommandKind::RemoveLayer: return tr("delete layer");
    case doc::CommandKind::MoveLayer: return tr("move layer");
    case doc::CommandKind::ChangeLayerProperties: return tr("layer properties");
    case doc::CommandKind::ImportImage: return tr("import image");
    }
    return {};
}

QString DocumentController::undoText() const
{
    const doc::Command *c = m_history.undoCommand();
    return c ? tr("Undo %1").arg(commandName(c->kind())) : tr("Undo");
}

QString DocumentController::redoText() const
{
    const doc::Command *c = m_history.redoCommand();
    return c ? tr("Redo %1").arg(commandName(c->kind())) : tr("Redo");
}

void DocumentController::publish(const doc::ChangeSet &changes)
{
    if (changes.structure) {
        ensureValidCurrentLayer();
        emit structureChanged();
    }
    for (const doc::ChangeSet::Pixels &p : changes.pixels)
        emit pixelsChanged(p.layer, p.drawing, boundsOf(p.tiles));
}

void DocumentController::ensureValidCurrentLayer()
{
    if (m_document->layer(m_currentLayer))
        return;
    // The current layer is gone (removed or undone): pick the top one, if any.
    const doc::LayerId fallback = m_document->layerCount() > 0
        ? m_document->layerAt(m_document->layerCount() - 1).id()
        : doc::kNoId;
    m_currentLayer = fallback;
    emit currentLayerChanged(fallback);
}

// --- selection -------------------------------------------------------------------

void DocumentController::setCurrentFrame(int frame)
{
    frame = std::max(0, frame);
    if (frame == m_currentFrame)
        return;
    interruptStroke();
    m_currentFrame = frame;
    emit currentFrameChanged(frame);
}

void DocumentController::setCurrentLayer(doc::LayerId layer)
{
    if (layer == m_currentLayer || !m_document->layer(layer))
        return;
    interruptStroke();
    m_currentLayer = layer;
    emit currentLayerChanged(layer);
}

// --- painting ----------------------------------------------------------------------

std::optional<DocumentController::PaintTarget> DocumentController::beginPaint(bool createIfEmpty, QString *reason)
{
    auto fail = [reason](const QString &why) -> std::optional<PaintTarget> {
        if (reason)
            *reason = why;
        return std::nullopt;
    };
    interruptStroke(); // a previous stroke left open
    doc::Layer *layer = m_document->layer(m_currentLayer);
    if (!layer)
        return fail(tr("No layer selected"));
    if (!layer->properties().visible)
        return fail(tr("Layer “%1” is hidden").arg(QString::fromStdString(layer->properties().name)));
    if (layer->properties().locked)
        return fail(tr("Layer “%1” is locked").arg(QString::fromStdString(layer->properties().name)));

    PaintState state;
    doc::DrawingId drawing = layer->timing().drawingAt(m_currentFrame);
    if (drawing == doc::kNoId) {
        if (!createIfEmpty)
            return fail(QString());
        // Harmony-like: drawing on an empty frame creates a drawing there.
        doc::NewDrawing created = doc::makeNewDrawing(*m_document, m_currentLayer, m_currentFrame);
        publish(created.command->apply(*m_document));
        state.createdDrawing = std::move(created.command);
        drawing = created.id;
    }
    state.target = {m_currentLayer, drawing, &layer->drawing(drawing)->image};
    m_paint = std::move(state);
    return m_paint->target;
}

void DocumentController::notifyPainted(const QRect &docRect)
{
    if (m_paint && !docRect.isEmpty())
        emit pixelsChanged(m_paint->target.layer, m_paint->target.drawing, docRect);
}

void DocumentController::endPaint(const doc::TiledImage &before)
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();

    auto paint = doc::PaintCommand::capture(state.target.layer, state.target.drawing, before, *state.target.image);
    if (state.createdDrawing) {
        if (!paint) { // nothing drawn: the drawing we created is not wanted
            publish(state.createdDrawing->revert(*m_document));
            return;
        }
        // New drawing + stroke: one undo step.
        std::vector<std::unique_ptr<doc::Command>> steps;
        steps.push_back(std::move(state.createdDrawing));
        steps.push_back(std::move(paint));
        m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::Paint, std::move(steps)));
    } else if (paint) {
        m_history.pushApplied(std::move(paint));
    } else {
        return;
    }
    emit historyChanged();
}

void DocumentController::cancelPaint()
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();
    if (state.createdDrawing)
        publish(state.createdDrawing->revert(*m_document));
}

void DocumentController::interruptStroke()
{
    if (!m_paint)
        return;
    emit strokeInterrupted(); // the painter restores pixels and calls cancelPaint()
    cancelPaint();            // in case nobody listened
}

} // namespace mothion::document
