#include "document/DocumentController.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/CelCommands.h"
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

// The smallest rect holding the image's visible pixels, in its own coordinates.
QRect opaqueBounds(const QImage &image)
{
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const doc::Pixel *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (doc::alphaOf(line[x]) == 0)
                continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
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
    m_currentCel = m_document->celAt(0).id();
}

DocumentController::~DocumentController() = default;

std::unique_ptr<doc::Document> DocumentController::makeDefaultDocument()
{
    auto d = std::make_unique<doc::Document>(kDefaultWidth, kDefaultHeight, doc::FrameRate{24, 1});
    d->setFrameCount(kDefaultFrames);
    // The first layer is part of the new document, not an undoable step.
    doc::AddCelCommand first(*d, 0, {tr("Cel 1").toStdString()});
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
    m_currentCel = m_document->celCount() > 0 ? m_document->celAt(m_document->celCount() - 1).id()
                                                  : doc::kNoId;
    m_currentFrame = 0;
    m_currentLayer = doc::kNoId;
    m_currentLayerName.clear();
    m_selection = QPainterPath();
    if (const doc::Drawing *drawing = currentDrawing(); drawing && !drawing->layers.empty()) {
        m_currentLayer = drawing->layers.back().id;
        m_currentLayerName = drawing->layers.back().properties.name;
    }

    emit documentReplaced();
    emit structureChanged();
    emit currentCelChanged(m_currentCel);
    emit currentFrameChanged(m_currentFrame);
    emit currentLayerChanged(m_currentLayer);
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
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1,
                                                    doc::CelProperties{name.toStdString()});
    const doc::CelId celId = cel->celId();
    publish(cel->apply(d));
    steps.push_back(std::move(cel));

    doc::NewDrawing drawing = doc::makeNewDrawing(d, celId, 0, d.frameCount(), tr("Layer 1").toStdString());
    publish(drawing.command->apply(d));
    steps.push_back(std::move(drawing.command));

    doc::TiledImage &target = d.layer(celId, drawing.id, drawing.layer)->image;
    const doc::TiledImage before = target;
    target.write(at.x(), at.y(), pixels.width(), pixels.height(),
                 reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    if (auto paint = doc::PaintCommand::capture(celId, drawing.id, drawing.layer, before, target))
        steps.push_back(std::move(paint));
    emit pixelsChanged(celId, drawing.id, QRect(at, pixels.size()));

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportImage, std::move(steps)));
    emit historyChanged();
    setCurrentCel(celId);
}

const doc::Drawing *DocumentController::currentDrawing() const
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    return cel ? cel->drawingAt(m_currentFrame) : nullptr;
}

doc::DrawingId DocumentController::currentDrawingId() const
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    return cel ? cel->timing().drawingAt(m_currentFrame) : doc::kNoId;
}

doc::Layer *DocumentController::workLayer(bool changesIt, const QString &nothing, QString *reason)
{
    auto fail = [reason](const QString &why) -> doc::Layer * {
        if (reason)
            *reason = why;
        return nullptr;
    };
    doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    const QString celName = QString::fromStdString(cel->properties().name);
    if (!cel->properties().visible)
        return fail(tr("Cel “%1” is hidden").arg(celName));
    if (changesIt && cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(celName));
    doc::Layer *layer = m_document->layer(m_currentCel, currentDrawingId(), m_currentLayer);
    if (!layer)
        return fail(nothing);
    const QString name = QString::fromStdString(layer->properties.name);
    if (!layer->properties.visible)
        return fail(tr("Layer “%1” is hidden").arg(name));
    if (changesIt && layer->properties.locked)
        return fail(tr("Layer “%1” is locked").arg(name));
    return layer;
}

bool DocumentController::canClearCurrentLayer() const
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel || !cel->properties().visible || cel->properties().locked)
        return false;
    const doc::Layer *layer = m_document->layer(m_currentCel, currentDrawingId(), m_currentLayer);
    return layer && layer->properties.visible && !layer->properties.locked && !layer->image.isEmpty();
}

bool DocumentController::clearCurrentLayer(QString *reason)
{
    interruptStroke(); // an open edit is applied first, then cleared with the rest
    const QString nothing = tr("Nothing to clear on this frame");
    doc::Layer *layer = workLayer(true, nothing, reason);
    if (!layer)
        return false;
    if (layer->image.isEmpty()) {
        if (reason)
            *reason = nothing;
        return false;
    }

    // The layer stays (and its drawing, exposures too), only its pixels go.
    const doc::DrawingId drawingId = currentDrawingId();
    const doc::TiledImage before = layer->image;
    const QRect area = boundsOf(before.tileCoords());
    layer->image = doc::TiledImage();
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, layer->image));
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearLayer, std::move(steps)));
    emit pixelsChanged(m_currentCel, drawingId, area);
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

bool DocumentController::eraseSelection(QString *reason)
{
    interruptStroke();
    const QString nothing = tr("Nothing in the selection on this frame");
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    doc::Layer *layer = workLayer(true, nothing, reason);
    if (!layer)
        return false;
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    doc::TiledImage inside, outside;
    canvas::splitByMask(layer->image, mask, inside, outside);
    if (inside.isEmpty()) {
        if (reason)
            *reason = nothing;
        return false;
    }
    const doc::DrawingId drawingId = currentDrawingId();
    const doc::TiledImage before = layer->image;
    layer->image = std::move(outside);
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, layer->image));
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearLayer, std::move(steps)));
    emit pixelsChanged(m_currentCel, drawingId, mask.bounds);
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
    const QString nothing = tr("Nothing in the selection on this frame");
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    doc::Layer *layer = workLayer(cut, nothing, reason);
    if (!layer)
        return false;
    doc::Document &d = *m_document;
    const doc::DrawingId drawingId = currentDrawingId();
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    doc::TiledImage inside, outside;
    canvas::splitByMask(layer->image, mask, inside, outside);
    if (inside.isEmpty()) {
        if (reason)
            *reason = nothing;
        return false;
    }

    // The source loses its part (cut), then a new layer above it in the same
    // drawing gets the part: applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (cut) {
        const doc::TiledImage before = layer->image;
        layer->image = std::move(outside);
        steps.push_back(doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, layer->image));
    }
    const QString name = QString::fromStdString(layer->properties.name);
    auto added = std::make_unique<doc::AddLayerCommand>(
        d, m_currentCel, drawingId, currentDrawing()->indexOf(m_currentLayer) + 1,
        doc::LayerProperties{(cut ? tr("%1 (cut)") : tr("%1 (copy)")).arg(name).toStdString()}, std::move(inside));
    const doc::LayerId layerId = added->layerId();
    publish(added->apply(d)); // `layer` may have moved: not used from here on
    steps.push_back(std::move(added));
    emit pixelsChanged(m_currentCel, drawingId, mask.bounds);

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(
        cut ? doc::CommandKind::CutToLayer : doc::CommandKind::CopyToLayer, std::move(steps)));
    emit historyChanged();
    setCurrentLayer(layerId);
    return true;
}

QImage DocumentController::copyPixels(QPoint *origin, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return QImage();
    };
    interruptStroke(); // an open edit is part of what is copied
    const QString nothing = tr("Nothing to copy on this frame");
    QString why;
    const doc::Layer *layer = workLayer(false, nothing, &why);
    if (!layer)
        return fail(why);
    if (layer->image.isEmpty())
        return fail(nothing);

    doc::TiledImage copied = layer->image;
    if (hasSelection()) {
        doc::TiledImage outside;
        canvas::splitByMask(layer->image, canvas::SelectionMask::fromPath(m_selection), copied, outside);
    }
    const QRect tiles = boundsOf(copied.tileCoords());
    QRect crop;
    QImage area;
    if (!tiles.isEmpty()) {
        area = QImage(tiles.size(), QImage::Format_ARGB32_Premultiplied);
        copied.read(tiles.x(), tiles.y(), tiles.width(), tiles.height(), reinterpret_cast<doc::Pixel *>(area.bits()),
                    area.bytesPerLine() / 4);
        crop = opaqueBounds(area);
    }
    if (crop.isEmpty())
        return fail(tr("Nothing in the selection on this frame"));
    if (origin)
        *origin = tiles.topLeft() + crop.topLeft();
    return area.copy(crop);
}

bool DocumentController::pasteImage(const QImage &image, std::optional<QPoint> origin, QString *reason)
{
    if (image.isNull()) {
        if (reason)
            *reason = tr("Nothing to paste");
        return false;
    }
    interruptStroke();
    doc::Document &d = *m_document;
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QPoint at = origin.value_or(QPoint((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2));
    const QRect area = QRect(at, pixels.size()) & QRect(0, 0, d.width(), d.height());
    if (area.isEmpty()) {
        if (reason)
            *reason = tr("The pasted image is outside the sheet");
        return false;
    }

    // Over what is drawn there, as a paint step: the drawing it may need, the
    // pixels and the selection around them are recorded together.
    const auto target = beginPaint(true, reason);
    if (!target)
        return false;
    const doc::TiledImage before = *target->image;
    doc::TiledImage pasted;
    const QPoint from = area.topLeft() - at;
    pasted.write(area.x(), area.y(), area.width(), area.height(),
                 reinterpret_cast<const doc::Pixel *>(pixels.constScanLine(from.y())) + from.x(),
                 pixels.bytesPerLine() / 4);
    canvas::compositeOver(*target->image, pasted);
    notifyPainted(area);
    QPainterPath selected;
    selected.addRect(area);
    setSelection(selected);
    endPaint(before, doc::CommandKind::Paste);
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
    case doc::CommandKind::AddDrawing: return tr("new frame");
    case doc::CommandKind::RemoveDrawing: return tr("delete frame");
    case doc::CommandKind::ChangeTiming: return tr("timing change");
    case doc::CommandKind::AddCel: return tr("new cel");
    case doc::CommandKind::RemoveCel: return tr("delete cel");
    case doc::CommandKind::MoveCel: return tr("move cel");
    case doc::CommandKind::ChangeCelProperties: return tr("cel properties");
    case doc::CommandKind::AddLayer: return tr("new layer");
    case doc::CommandKind::RemoveLayer: return tr("delete layer");
    case doc::CommandKind::MoveLayer: return tr("move layer");
    case doc::CommandKind::ChangeLayerProperties: return tr("layer properties");
    case doc::CommandKind::ImportImage: return tr("import image");
    case doc::CommandKind::ClearLayer: return tr("clear");
    case doc::CommandKind::Move: return tr("move");
    case doc::CommandKind::Select: return tr("selection");
    case doc::CommandKind::CutToLayer: return tr("cut to layer");
    case doc::CommandKind::CopyToLayer: return tr("copy to layer");
    case doc::CommandKind::Fill: return tr("fill");
    case doc::CommandKind::Transform: return tr("transform");
    case doc::CommandKind::Paste: return tr("paste");
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
        ensureValidCurrentCel();
        resolveCurrentLayer();
        emit structureChanged();
    }
    for (const doc::ChangeSet::Pixels &p : changes.pixels)
        emit pixelsChanged(p.cel, p.drawing, boundsOf(p.tiles));
}

void DocumentController::ensureValidCurrentCel()
{
    if (m_document->cel(m_currentCel))
        return;
    // The current cel is gone (removed or undone): pick the top one, if any.
    const doc::CelId fallback = m_document->celCount() > 0
        ? m_document->celAt(m_document->celCount() - 1).id()
        : doc::kNoId;
    m_currentCel = fallback;
    emit currentCelChanged(fallback);
}

void DocumentController::resolveCurrentLayer()
{
    const doc::Drawing *drawing = currentDrawing();
    doc::LayerId layer = doc::kNoId;
    if (drawing && !drawing->layers.empty()) {
        if (drawing->layer(m_currentLayer)) {
            layer = m_currentLayer;
        } else {
            layer = drawing->layers.back().id; // the top one, unless one has the name
            for (auto it = drawing->layers.rbegin(); it != drawing->layers.rend(); ++it)
                if (it->properties.name == m_currentLayerName) {
                    layer = it->id;
                    break;
                }
        }
        m_currentLayerName = drawing->layer(layer)->properties.name; // renamed, perhaps
    }
    if (layer == m_currentLayer)
        return;
    m_currentLayer = layer;
    emit currentLayerChanged(layer);
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
    resolveCurrentLayer();
}

void DocumentController::setCurrentCel(doc::CelId cel)
{
    if (cel == m_currentCel || !m_document->cel(cel))
        return;
    interruptStroke();
    m_currentCel = cel;
    emit currentCelChanged(cel);
    resolveCurrentLayer();
}

void DocumentController::setCurrentLayer(doc::LayerId layer)
{
    const doc::Drawing *drawing = currentDrawing();
    if (layer == m_currentLayer || !drawing || !drawing->layer(layer))
        return;
    interruptStroke();
    m_currentLayer = layer;
    m_currentLayerName = drawing->layer(layer)->properties.name;
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
    doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    if (!cel->properties().visible)
        return fail(tr("Cel “%1” is hidden").arg(QString::fromStdString(cel->properties().name)));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));

    PaintState state;
    doc::DrawingId drawing = cel->timing().drawingAt(m_currentFrame);
    if (drawing == doc::kNoId) {
        if (!createIfEmpty)
            return fail(QString());
        // Harmony-like: drawing on an empty frame creates a drawing there,
        // its layer named like the one worked on before.
        const std::string layerName = m_currentLayerName.empty() ? tr("Layer 1").toStdString() : m_currentLayerName;
        doc::NewDrawing created = doc::makeNewDrawing(*m_document, m_currentCel, m_currentFrame, 1, layerName);
        publish(created.command->apply(*m_document)); // the new layer becomes current
        state.createdDrawing = std::move(created.command);
        drawing = created.id;
    }
    doc::Layer *layer = cel->drawing(drawing)->layer(m_currentLayer);
    if (!layer)
        return fail(tr("No layer selected"));
    const QString name = QString::fromStdString(layer->properties.name);
    if (!layer->properties.visible)
        return fail(tr("Layer “%1” is hidden").arg(name));
    if (layer->properties.locked)
        return fail(tr("Layer “%1” is locked").arg(name));
    state.target = {m_currentCel, drawing, m_currentLayer, &layer->image};
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return m_paint->target;
}

void DocumentController::notifyPainted(const QRect &docRect)
{
    if (m_paint && !docRect.isEmpty())
        emit pixelsChanged(m_paint->target.cel, m_paint->target.drawing, docRect);
}

void DocumentController::endPaint(const doc::TiledImage &before, doc::CommandKind kind)
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();

    // New drawing + stroke + selection change: one undo step.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto paint = doc::PaintCommand::capture(state.target.cel, state.target.drawing, state.target.layer, before,
                                            *state.target.image);
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
