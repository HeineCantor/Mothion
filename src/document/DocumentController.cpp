#include "document/DocumentController.h"

#include "canvas/FrameRenderer.h"
#include "tools/Preferences.h"
#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace mothion::document {

namespace {


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
    applyUndoLimits();
    connect(&tools::Preferences::instance(), &tools::Preferences::changed, this, &DocumentController::applyUndoLimits);
}

void DocumentController::applyUndoLimits()
{
    const tools::Preferences &prefs = tools::Preferences::instance();
    doc::HistoryLimits limits; // the memory limit stays at its default
    limits.maxSteps = std::size_t(prefs.undoSteps());
    m_history.setLimits(limits);
}

DocumentController::~DocumentController() = default;

std::unique_ptr<doc::Document> DocumentController::makeDefaultDocument()
{
    const tools::Preferences &prefs = tools::Preferences::instance();
    auto d = std::make_unique<doc::Document>(prefs.newWidth(), prefs.newHeight(), doc::FrameRate{prefs.newFrameRate(), 1});
    d->setFrameCount(prefs.newFrames());
    d->setPaperColor(prefs.paperColor().rgba());
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
    m_layerSelection.clear();
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

void DocumentController::addCel()
{
    doc::Document &d = *m_document;
    QString name; // the first free number
    for (int n = d.celCount() + 1;; ++n) {
        name = tr("Cel %1").arg(n);
        bool used = false;
        for (int i = 0; i < d.celCount() && !used; ++i)
            used = d.celAt(i).properties().name == name.toStdString();
        if (!used)
            break;
    }
    auto command =
        std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, doc::CelProperties{name.toStdString()});
    const doc::CelId id = command->celId();
    execute(std::move(command));
    setCurrentCel(id);
}

bool DocumentController::addLayer(bool onTop, doc::LayerKind kind)
{
    const doc::Drawing *drawing = currentDrawing();
    if (!drawing)
        return false;
    const bool perspective = kind == doc::LayerKind::Perspective;
    QString name; // the first free number
    for (int n = perspective ? 1 : int(drawing->layers.size()) + 1;; ++n) {
        name = perspective ? tr("Perspective %1").arg(n) : tr("Layer %1").arg(n);
        if (std::none_of(drawing->layers.begin(), drawing->layers.end(),
                         [&](const doc::Layer &l) { return l.properties.name == name.toStdString(); }))
            break;
    }
    const int index = onTop ? int(drawing->layers.size()) : drawing->indexOf(m_currentLayer) + 1;
    doc::LayerProperties properties{name.toStdString()};
    if (perspective)
        properties.perspective = perspective::defaultGrid(m_document->width(), m_document->height());
    auto command = std::make_unique<doc::AddLayerCommand>(*m_document, m_currentCel, drawing->id, index,
                                                          std::move(properties), doc::TiledImage(), kind);
    const doc::LayerId id = command->layerId();
    execute(std::move(command));
    setCurrentLayer(id);
    return true;
}

std::vector<const doc::Layer *> DocumentController::mergeableLayers(const std::vector<doc::LayerId> &ids) const
{
    // Raster layers, shown and not locked, of the current drawing, bottom to top.
    std::vector<const doc::Layer *> found;
    const doc::Cel *cel = m_document->cel(m_currentCel);
    const doc::Drawing *drawing = currentDrawing();
    if (!cel || !drawing || !cel->properties().visible || cel->properties().locked)
        return found;
    for (const doc::Layer &layer : drawing->layers)
        if (std::find(ids.begin(), ids.end(), layer.id) != ids.end() && layer.kind == doc::LayerKind::Raster
            && layer.properties.visible && !layer.properties.locked)
            found.push_back(&layer);
    return found;
}

bool DocumentController::canMergeLayers(const std::vector<doc::LayerId> &ids) const
{
    return mergeableLayers(ids).size() >= 2;
}

bool DocumentController::mergeLayers(const std::vector<doc::LayerId> &ids, QString *reason)
{
    interruptStroke();
    const auto layers = mergeableLayers(ids);
    if (layers.size() < 2) {
        if (reason)
            *reason = tr("Choose at least two shown, unlocked layers to merge");
        return false;
    }
    const doc::Drawing *drawing = currentDrawing();
    // What they show together, as one image (each with its own opacity and blend).
    doc::Drawing together;
    for (const doc::Layer *layer : layers)
        together.layers.push_back(*layer);
    doc::TiledImage merged;
    if (const QRect bounds = canvas::drawingBounds(together, false); !bounds.isEmpty()) {
        const QImage image = canvas::composeDrawing(together, bounds, false);
        merged.write(bounds.x(), bounds.y(), bounds.width(), bounds.height(),
                     reinterpret_cast<const doc::Pixel *>(image.constBits()), image.bytesPerLine() / 4);
    }
    // The lowest of them keeps the result, drawn plainly (its look is in the pixels now).
    const doc::Layer &bottom = *layers.front();
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (auto paint = doc::PaintCommand::capture(m_currentCel, drawing->id, bottom.id, bottom.image, merged))
        steps.push_back(std::move(paint));
    doc::LayerProperties plain = bottom.properties;
    plain.opacity = 1.0f;
    plain.blend = doc::BlendMode::Normal;
    if (auto look = doc::makeSetLayerProperties(*m_document, m_currentCel, drawing->id, bottom.id, plain))
        steps.push_back(std::move(look));
    for (std::size_t i = 1; i < layers.size(); ++i)
        steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_currentCel, drawing->id, layers[i]->id));
    const doc::LayerId result = bottom.id;
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MergeLayers, std::move(steps)));
    setCurrentLayer(result);
    return true;
}

bool DocumentController::copyLayers(const std::vector<LayerRef> &layers, QString *reason)
{
    std::vector<doc::Layer> copied;
    // In the order the document draws them: cels bottom to top, layers bottom to top.
    for (int i = 0; i < m_document->celCount(); ++i) {
        const doc::Cel &cel = m_document->celAt(i);
        std::vector<const doc::Drawing *> seen;
        for (const LayerRef &ref : layers) {
            if (ref.cel != cel.id())
                continue;
            const doc::Drawing *drawing = cel.drawing(ref.drawing);
            if (!drawing || std::find(seen.begin(), seen.end(), drawing) != seen.end())
                continue;
            seen.push_back(drawing);
            for (const doc::Layer &layer : drawing->layers)
                if (std::find(layers.begin(), layers.end(), LayerRef{cel.id(), drawing->id, layer.id}) != layers.end())
                    copied.push_back(layer);
        }
    }
    if (copied.empty()) {
        if (reason)
            *reason = tr("No layers to copy");
        return false;
    }
    m_copiedLayers = std::move(copied); // the pixels shared with the originals until one is painted
    return true;
}

bool DocumentController::pasteLayers(bool merged, const std::vector<doc::LayerId> &replace, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    if (m_copiedLayers.empty())
        return fail(tr("No layers copied"));
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));
    interruptStroke();

    // The new layers' pixels: each copied layer, or all of them as one.
    struct Piece
    {
        doc::LayerProperties properties;
        doc::TiledImage image;
        doc::LayerKind kind;
    };
    std::vector<Piece> pieces;
    if (merged) {
        doc::Drawing together;
        for (const doc::Layer &layer : m_copiedLayers)
            together.layers.push_back(layer);
        const QRect bounds = canvas::drawingBounds(together, false);
        if (bounds.isEmpty())
            return fail(tr("The copied layers show nothing to paste"));
        const QImage image = canvas::composeDrawing(together, bounds, false);
        Piece piece{doc::LayerProperties{m_copiedLayers.back().properties.name}, {}, doc::LayerKind::Raster};
        piece.properties.name = tr("Merged layers").toStdString();
        piece.image.write(bounds.x(), bounds.y(), bounds.width(), bounds.height(),
                          reinterpret_cast<const doc::Pixel *>(image.constBits()), image.bytesPerLine() / 4);
        pieces.push_back(std::move(piece));
    } else {
        for (const doc::Layer &layer : m_copiedLayers)
            pieces.push_back({layer.properties, layer.image, layer.kind});
    }

    std::vector<std::unique_ptr<doc::Command>> steps;
    doc::DrawingId drawingId = currentDrawingId();
    int index = 0;
    doc::LayerId unwanted = doc::kNoId;
    if (drawingId == doc::kNoId) { // nothing here yet: a frame is made, without its empty layer
        doc::NewDrawing created = doc::makeNewDrawing(*m_document, m_currentCel, m_currentFrame);
        drawingId = created.id;
        unwanted = created.layer;
        index = 1;
        steps.push_back(std::move(created.command));
    } else {
        index = int(currentDrawing()->layers.size());
        // Layers chosen to be overwritten: the new ones go where the lowest of them was.
        const doc::Drawing *drawing = currentDrawing();
        const bool theCopied =
            replace.size() == m_copiedLayers.size()
            && std::all_of(replace.begin(), replace.end(), [this](doc::LayerId id) {
                   return std::any_of(m_copiedLayers.begin(), m_copiedLayers.end(),
                                      [id](const doc::Layer &l) { return l.id == id; });
               });
        int lowest = index;
        std::vector<doc::LayerId> going;
        for (const doc::Layer &layer : drawing->layers)
            if (!theCopied && !layer.properties.locked
                && std::find(replace.begin(), replace.end(), layer.id) != replace.end()) {
                going.push_back(layer.id);
                lowest = std::min(lowest, drawing->indexOf(layer.id));
            }
        if (!going.empty()) {
            for (auto it = going.rbegin(); it != going.rend(); ++it) // top first
                steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_currentCel, drawingId, *it));
            index = lowest;
        }
    }
    doc::LayerId last = doc::kNoId;
    for (Piece &piece : pieces) {
        auto add = std::make_unique<doc::AddLayerCommand>(*m_document, m_currentCel, drawingId, index++,
                                                          std::move(piece.properties), std::move(piece.image), piece.kind);
        last = add->layerId();
        steps.push_back(std::move(add));
    }
    if (unwanted != doc::kNoId)
        steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_currentCel, drawingId, unwanted));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::PasteLayers, std::move(steps)));
    setCurrentLayer(last);
    return true;
}

bool DocumentController::previewLayerProperties(const LayerRef &ref, const doc::LayerProperties &properties)
{
    doc::Layer *layer = m_document->layer(ref.cel, ref.drawing, ref.layer);
    if (!layer)
        return false;
    layer->properties = properties;
    emit pixelsChanged(ref.cel, ref.drawing, QRect(0, 0, m_document->width(), m_document->height()));
    return true;
}

bool DocumentController::previewCelProperties(doc::CelId id, const doc::CelProperties &properties)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return false;
    cel->setProperties(properties);
    // The cel's looks change what the whole frame shows.
    emit pixelsChanged(id, doc::kNoId, QRect(0, 0, m_document->width(), m_document->height()));
    return true;
}

void DocumentController::commitLayerProperties(const LayerRef &ref, const doc::LayerProperties &before,
                                               const doc::LayerProperties &after)
{
    if (!previewLayerProperties(ref, before))
        return;
    execute(doc::makeSetLayerProperties(*m_document, ref.cel, ref.drawing, ref.layer, after));
}

void DocumentController::insertFrame(doc::CelId cel, int frame, bool after)
{
    if (!m_document->cel(cel))
        return;
    const int at = std::max(0, frame + (after ? 1 : 0));
    const std::string layerName = m_currentLayerName.empty() ? tr("Layer 1").toStdString() : m_currentLayerName;
    doc::NewDrawing made = doc::makeInbetween(*m_document, cel, at, layerName,
                                              after ? doc::CommandKind::InsertInbetween : doc::CommandKind::AddDrawing);
    execute(std::move(made.command));
    setCurrentCel(cel);
    setCurrentFrame(at);
}

bool DocumentController::stepToFrame(int direction)
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return false;
    const auto &exposures = cel->timing().exposures();
    std::optional<int> to;
    if (direction > 0) {
        for (const doc::Exposure &e : exposures)
            if (e.start > m_currentFrame) {
                to = e.start;
                break;
            }
    } else {
        // Before the start of the frame shown now (or of the point, when empty).
        const doc::Exposure *here = cel->timing().exposureAt(m_currentFrame);
        const int from = here ? here->start : m_currentFrame;
        for (auto it = exposures.rbegin(); it != exposures.rend(); ++it)
            if (it->start < from) {
                to = it->start;
                break;
            }
    }
    if (!to)
        return false;
    setCurrentFrame(*to);
    return true;
}

bool DocumentController::changeCurrentHold(int delta)
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    const doc::Exposure *hold = cel ? cel->timing().exposureAt(m_currentFrame) : nullptr;
    if (!hold)
        return false;
    const doc::Exposure before = *hold;
    auto command = doc::makeResizeExposure(*m_document, m_currentCel, before.start, std::max(1, before.length + delta));
    if (!command)
        return false;
    execute(std::move(command));
    const doc::Exposure *after = m_document->cel(m_currentCel)->timing().exposureAt(before.start);
    if (after && m_currentFrame >= after->end())
        setCurrentFrame(after->end() - 1);
    return true;
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

int DocumentController::importImageSequence(const QList<QImage> &images, const QString &name, int startFrame,
                                            int hold, const std::function<void(int)> &progress)
{
    QList<QImage> usable;
    for (const QImage &image : images)
        if (!image.isNull())
            usable << image;
    if (usable.isEmpty())
        return 0;
    interruptStroke();
    doc::Document &d = *m_document;
    startFrame = std::max(0, startFrame);
    hold = std::max(1, hold);

    // One new cel above the current one; each image a drawing of its own, shown for `hold` frames from the
    // one after the last. Applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1,
                                                    doc::CelProperties{name.toStdString()});
    const doc::CelId celId = cel->celId();
    // The views hear of it once, when all is in: one refresh, not one for each of the frames.
    doc::ChangeSet changes = cel->apply(d);
    steps.push_back(std::move(cel));
    std::vector<std::pair<doc::DrawingId, QRect>> painted;

    for (int i = 0; i < usable.size(); ++i) {
        // Scaled down to fit the document if larger, centered, as a single imported image is.
        QImage pixels = usable[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (pixels.width() > d.width() || pixels.height() > d.height())
            pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);

        doc::NewDrawing drawing = doc::makeNewDrawing(d, celId, startFrame + i * hold, hold, tr("Layer 1").toStdString());
        changes.merge(drawing.command->apply(d));
        steps.push_back(std::move(drawing.command));
        doc::TiledImage &target = d.layer(celId, drawing.id, drawing.layer)->image;
        const doc::TiledImage before = target;
        target.write(at.x(), at.y(), pixels.width(), pixels.height(),
                     reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
        if (auto paint = doc::PaintCommand::capture(celId, drawing.id, drawing.layer, before, target))
            steps.push_back(std::move(paint));
        painted.emplace_back(drawing.id, QRect(at, pixels.size()));
        if (progress)
            progress(i + 1);
    }
    // The scene is as long as the sequence needs (this is not one of the undo steps).
    const int end = startFrame + int(usable.size()) * hold;
    if (end > d.frameCount())
        d.setFrameCount(end);

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    publish(changes);
    for (const auto &[drawingId, area] : painted)
        emit pixelsChanged(celId, drawingId, area);
    emit historyChanged();
    setCurrentCel(celId);
    setCurrentFrame(startFrame);
    return int(usable.size());
}

std::optional<DocumentController::PerspectiveLayerInfo> DocumentController::perspectiveLayerById(doc::LayerId id) const
{
    if (id == doc::kNoId)
        return std::nullopt;
    for (int i = 0; i < m_document->celCount(); ++i) {
        const doc::Cel &cel = m_document->celAt(i);
        for (const auto &[drawingId, drawing] : cel.drawings())
            for (const doc::Layer &layer : drawing.layers)
                if (layer.id == id && layer.kind == doc::LayerKind::Perspective)
                    return PerspectiveLayerInfo{{cel.id(), drawingId, layer.id}, layer.properties.perspective};
    }
    return std::nullopt;
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
    if (layer->kind == doc::LayerKind::Perspective)
        return fail(tr("“%1” is a perspective layer: it has no pixels to work on").arg(name));
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
    case doc::CommandKind::ImportSequence: return tr("import image sequence");
    case doc::CommandKind::ClearLayer: return tr("clear");
    case doc::CommandKind::Move: return tr("move");
    case doc::CommandKind::Select: return tr("selection");
    case doc::CommandKind::CutToLayer: return tr("cut to layer");
    case doc::CommandKind::CopyToLayer: return tr("copy to layer");
    case doc::CommandKind::Fill: return tr("fill");
    case doc::CommandKind::Transform: return tr("transform");
    case doc::CommandKind::Paste: return tr("paste");
    case doc::CommandKind::SplitFrame: return tr("split frame");
    case doc::CommandKind::InsertInbetween: return tr("insert inbetween");
    case doc::CommandKind::MoveFrames: return tr("move frames");
    case doc::CommandKind::PasteFrames: return tr("paste frames");
    case doc::CommandKind::MergeLayers: return tr("merge layers");
    case doc::CommandKind::PasteLayers: return tr("paste layers");
    case doc::CommandKind::Blend: return tr("blend");
    case doc::CommandKind::Liquify: return tr("liquify");
    case doc::CommandKind::Shape: return tr("shape");
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
    if (layer->kind == doc::LayerKind::Perspective)
        return fail(tr("“%1” is a perspective layer: it can't be drawn on").arg(name));
    state.target = {m_currentCel, drawing, m_currentLayer, &layer->image};
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return m_paint->target;
}

void DocumentController::setLayerSelection(std::vector<LayerRef> layers)
{
    if (layers.size() == 1 && layers.front().layer == m_currentLayer)
        layers.clear(); // just the current one
    m_layerSelection = std::move(layers);
}

std::vector<DocumentController::PaintTarget> DocumentController::beginPaintLayers(QString *reason)
{
    if (m_layerSelection.empty()) {
        const auto target = beginPaint(false, reason);
        return target ? std::vector<PaintTarget>{*target} : std::vector<PaintTarget>{};
    }
    interruptStroke(); // a previous stroke left open
    std::vector<PaintTarget> targets;
    for (const LayerRef &ref : m_layerSelection) {
        const doc::Cel *cel = m_document->cel(ref.cel);
        doc::Layer *layer = m_document->layer(ref.cel, ref.drawing, ref.layer);
        if (!layer || !cel->properties().visible || cel->properties().locked || !layer->properties.visible
            || layer->properties.locked || layer->kind == doc::LayerKind::Perspective)
            continue;
        const PaintTarget target{ref.cel, ref.drawing, ref.layer, &layer->image};
        if (ref.layer == m_currentLayer)
            targets.insert(targets.begin(), target);
        else
            targets.push_back(target);
    }
    if (targets.empty()) {
        if (reason)
            *reason = tr("The selected layers are hidden or locked");
        return targets;
    }
    PaintState state;
    state.target = targets.front();
    state.others.assign(targets.begin() + 1, targets.end());
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return targets;
}

void DocumentController::notifyPainted(const QRect &docRect)
{
    if (!m_paint || docRect.isEmpty())
        return;
    emit pixelsChanged(m_paint->target.cel, m_paint->target.drawing, docRect);
    std::vector<std::pair<doc::CelId, doc::DrawingId>> told{{m_paint->target.cel, m_paint->target.drawing}};
    for (const PaintTarget &t : m_paint->others)
        if (std::find(told.begin(), told.end(), std::pair{t.cel, t.drawing}) == told.end()) {
            told.emplace_back(t.cel, t.drawing);
            emit pixelsChanged(t.cel, t.drawing, docRect);
        }
}

void DocumentController::endPaint(const doc::TiledImage &before, doc::CommandKind kind)
{
    endPaint(std::vector<doc::TiledImage>{before}, kind);
}

void DocumentController::endPaint(const std::vector<doc::TiledImage> &before, doc::CommandKind kind)
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();

    // New drawing + stroke(s) + selection change: one undo step.
    std::vector<std::unique_ptr<doc::Command>> steps;
    std::vector<PaintTarget> targets{state.target};
    targets.insert(targets.end(), state.others.begin(), state.others.end());
    for (std::size_t i = 0; i < targets.size() && i < before.size(); ++i)
        if (auto paint = doc::PaintCommand::capture(targets[i].cel, targets[i].drawing, targets[i].layer, before[i],
                                                    *targets[i].image))
            steps.push_back(std::move(paint));
    if (!steps.empty()) {
        if (state.createdDrawing)
            steps.insert(steps.begin(), std::move(state.createdDrawing));
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
