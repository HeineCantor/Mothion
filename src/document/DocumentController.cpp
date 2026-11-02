#include "document/DocumentController.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"

#include <algorithm>
#include <functional>
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

// The selection as it was before and after a step: made, cleared, or carried
// by a move. The selection lives in the controller, not in the document, so
// the command sets it there through `assign`; it changes no pixels.
class SelectionCommand : public doc::Command
{
public:
    using Assign = std::function<void(const QPainterPath &)>;

    SelectionCommand(doc::CommandKind kind, Assign assign, QPainterPath before, QPainterPath after)
        : m_kind(kind)
        , m_assign(std::move(assign))
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    doc::CommandKind kind() const override { return m_kind; }
    doc::ChangeSet apply(doc::Document &) override
    {
        m_assign(m_after);
        return {};
    }
    doc::ChangeSet revert(doc::Document &) override
    {
        m_assign(m_before);
        return {};
    }

private:
    doc::CommandKind m_kind;
    Assign m_assign;
    QPainterPath m_before;
    QPainterPath m_after;
};

} // namespace

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_document(makeDefaultDocument())
{
    m_currentLayer = m_document->layerAt(0).id();
}

DocumentController::~DocumentController() = default;

std::unique_ptr<doc::Document> DocumentController::makeDefaultDocument()
{
    auto d = std::make_unique<doc::Document>(kDefaultWidth, kDefaultHeight, doc::FrameRate{24, 1});
    d->setFrameCount(kDefaultFrames);
    // The first layer is part of the new document, not an undoable step.
    doc::AddLayerCommand first(*d, 0, {tr("Layer 1").toStdString()});
    first.apply(*d);
    return d;
}

// --- whole document ----------------------------------------------------------------

void DocumentController::newDocument()
{
    replaceDocument(makeDefaultDocument());
}

void DocumentController::replaceDocument(std::unique_ptr<doc::Document> document)
{
    if (!document)
        return;
    interruptStroke(); // open edits land in the old document before it goes
    m_document = std::move(document);
    m_history.clear();
    m_history.markClean();
    m_currentLayer = m_document->layerCount() > 0 ? m_document->layerAt(m_document->layerCount() - 1).id()
                                                  : doc::kNoId;
    m_currentFrame = 0;
    m_selection = QPainterPath();

    emit documentReplaced();
    emit structureChanged();
    emit currentLayerChanged(m_currentLayer);
    emit currentFrameChanged(m_currentFrame);
    emit selectionChanged();
    emit historyChanged();
}

void DocumentController::markSaved()
{
    m_history.markClean();
    emit historyChanged();
}

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

bool DocumentController::canClearCurrentDrawing() const
{
    const doc::Layer *layer = m_document->layer(m_currentLayer);
    if (!layer || !layer->properties().visible || layer->properties().locked)
        return false;
    const doc::Drawing *drawing = layer->drawingAt(m_currentFrame);
    return drawing && !drawing->image.isEmpty();
}

bool DocumentController::clearCurrentDrawing(QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    interruptStroke(); // an open edit is applied first, then cleared with the rest
    doc::Layer *layer = m_document->layer(m_currentLayer);
    if (!layer)
        return fail(tr("No layer selected"));
    const QString name = QString::fromStdString(layer->properties().name);
    if (!layer->properties().visible)
        return fail(tr("Layer “%1” is hidden").arg(name));
    if (layer->properties().locked)
        return fail(tr("Layer “%1” is locked").arg(name));
    const doc::DrawingId drawingId = layer->timing().drawingAt(m_currentFrame);
    doc::Drawing *drawing = layer->drawing(drawingId);
    if (!drawing || drawing->image.isEmpty())
        return fail(tr("Nothing to clear on this frame"));

    // The drawing stays (its exposures too), only its pixels go.
    const doc::TiledImage before = drawing->image;
    const QRect area = boundsOf(before.tileCoords());
    drawing->image = doc::TiledImage();
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(doc::PaintCommand::capture(m_currentLayer, drawingId, before, drawing->image));
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearDrawing, std::move(steps)));
    emit pixelsChanged(m_currentLayer, drawingId, area);
    emit historyChanged();
    return true;
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
    if (m_paint) { // recorded with the paint, by endPaint()
        assignSelection(clipped);
        return;
    }
    QPainterPath before = m_selection;
    assignSelection(clipped);
    m_history.pushApplied(std::make_unique<SelectionCommand>(
        doc::CommandKind::Select, [this](const QPainterPath &a) { assignSelection(a); }, std::move(before),
        m_selection));
    emit historyChanged();
}

void DocumentController::assignSelection(const QPainterPath &area)
{
    m_selection = area;
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

void DocumentController::invertSelection()
{
    QPainterPath page;
    page.addRect(0, 0, m_document->width(), m_document->height());
    setSelection(page.subtracted(m_selection));
}

doc::Drawing *DocumentController::selectedDrawing(bool changesIt, QString *reason)
{
    auto fail = [reason](const QString &why) -> doc::Drawing * {
        if (reason)
            *reason = why;
        return nullptr;
    };
    if (!hasSelection())
        return fail(tr("Nothing is selected"));
    doc::Layer *layer = m_document->layer(m_currentLayer);
    if (!layer)
        return fail(tr("No layer selected"));
    const QString name = QString::fromStdString(layer->properties().name);
    if (!layer->properties().visible)
        return fail(tr("Layer “%1” is hidden").arg(name));
    if (changesIt && layer->properties().locked)
        return fail(tr("Layer “%1” is locked").arg(name));
    doc::Drawing *drawing = layer->drawing(layer->timing().drawingAt(m_currentFrame));
    if (!drawing || drawing->image.isEmpty())
        return fail(tr("Nothing in the selection on this frame"));
    return drawing;
}

bool DocumentController::eraseSelection(QString *reason)
{
    interruptStroke();
    doc::Drawing *drawing = selectedDrawing(true, reason);
    if (!drawing)
        return false;
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    doc::TiledImage inside, outside;
    canvas::splitByMask(drawing->image, mask, inside, outside);
    if (inside.isEmpty()) {
        if (reason)
            *reason = tr("Nothing in the selection on this frame");
        return false;
    }
    const doc::DrawingId drawingId = m_document->layer(m_currentLayer)->timing().drawingAt(m_currentFrame);
    const doc::TiledImage before = drawing->image;
    drawing->image = std::move(outside);
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(doc::PaintCommand::capture(m_currentLayer, drawingId, before, drawing->image));
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearDrawing, std::move(steps)));
    emit pixelsChanged(m_currentLayer, drawingId, mask.bounds);
    emit historyChanged();
    return true;
}

bool DocumentController::cutSelectionToLayer(QString *reason)
{
    return selectionToLayer(true, reason);
}

bool DocumentController::copySelectionToLayer(QString *reason)
{
    return selectionToLayer(false, reason);
}

bool DocumentController::selectionToLayer(bool cut, QString *reason)
{
    interruptStroke();
    doc::Drawing *drawing = selectedDrawing(cut, reason);
    if (!drawing)
        return false;
    doc::Document &d = *m_document;
    const doc::Layer &source = *d.layer(m_currentLayer);
    const doc::DrawingId sourceDrawing = source.timing().drawingAt(m_currentFrame);
    const doc::Exposure exposure = *source.timing().exposureAt(m_currentFrame);
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    doc::TiledImage inside, outside;
    canvas::splitByMask(drawing->image, mask, inside, outside);
    if (inside.isEmpty()) {
        if (reason)
            *reason = tr("Nothing in the selection on this frame");
        return false;
    }

    // The source loses its part (cut), then a new layer above it gets the
    // part in a drawing exposed on the same frames: applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (cut) {
        const doc::TiledImage before = drawing->image;
        drawing->image = std::move(outside);
        steps.push_back(doc::PaintCommand::capture(m_currentLayer, sourceDrawing, before, drawing->image));
        emit pixelsChanged(m_currentLayer, sourceDrawing, mask.bounds);
    }
    const QString name = QString::fromStdString(source.properties().name);
    auto layer = std::make_unique<doc::AddLayerCommand>(
        d, d.indexOf(m_currentLayer) + 1,
        doc::LayerProperties{(cut ? tr("%1 (cut)") : tr("%1 (copy)")).arg(name).toStdString()});
    const doc::LayerId layerId = layer->layerId();
    publish(layer->apply(d));
    steps.push_back(std::move(layer));

    doc::NewDrawing copy = doc::makeNewDrawing(d, layerId, exposure.start, exposure.length);
    publish(copy.command->apply(d));
    steps.push_back(std::move(copy.command));
    doc::TiledImage &target = d.layer(layerId)->drawing(copy.id)->image;
    const doc::TiledImage empty = target;
    target = std::move(inside);
    steps.push_back(doc::PaintCommand::capture(layerId, copy.id, empty, target));
    emit pixelsChanged(layerId, copy.id, mask.bounds);

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(
        cut ? doc::CommandKind::CutToLayer : doc::CommandKind::CopyToLayer, std::move(steps)));
    emit historyChanged();
    setCurrentLayer(layerId);
    return true;
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
    case doc::CommandKind::ClearDrawing: return tr("clear");
    case doc::CommandKind::Move: return tr("move");
    case doc::CommandKind::Select: return tr("selection");
    case doc::CommandKind::CutToLayer: return tr("cut to layer");
    case doc::CommandKind::CopyToLayer: return tr("copy to layer");
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
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return m_paint->target;
}

void DocumentController::notifyPainted(const QRect &docRect)
{
    if (m_paint && !docRect.isEmpty())
        emit pixelsChanged(m_paint->target.layer, m_paint->target.drawing, docRect);
}

void DocumentController::endPaint(const doc::TiledImage &before, doc::CommandKind kind)
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();

    // New drawing + stroke + selection change: one undo step.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto paint = doc::PaintCommand::capture(state.target.layer, state.target.drawing, before, *state.target.image);
    if (paint) {
        if (state.createdDrawing)
            steps.push_back(std::move(state.createdDrawing));
        steps.push_back(std::move(paint));
    } else if (state.createdDrawing) { // nothing drawn: the drawing we created is not wanted
        publish(state.createdDrawing->revert(*m_document));
    }
    if (m_selection != state.selectionBefore)
        steps.push_back(std::make_unique<SelectionCommand>(
            doc::CommandKind::Move, [this](const QPainterPath &a) { assignSelection(a); }, state.selectionBefore,
            m_selection));
    if (steps.empty())
        return;
    if (steps.size() == 1 && steps.front()->kind() == kind)
        m_history.pushApplied(std::move(steps.front()));
    else
        m_history.pushApplied(std::make_unique<doc::GroupCommand>(kind, std::move(steps)));
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
