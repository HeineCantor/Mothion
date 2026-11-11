#include "document/DocumentController.h"

#include "canvas/FrameRenderer.h"
#include "tools/Preferences.h"
#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/Parallel.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/AudioCommands.h"
#include "core/document/commands/CameraCommands.h"
#include "core/document/commands/MarkerCommands.h"
#include "core/document/commands/BlenderLinkCommands.h"
#include "core/document/commands/MotionCommands.h"
#include "core/document/commands/CompositingCommands.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <set>
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
    return makeDocument(prefs.newWidth(), prefs.newHeight(), prefs.newFrameRate(), prefs.newFrames(), prefs.paperColor());
}

std::unique_ptr<doc::Document> DocumentController::makeDocument(int width, int height, int frameRate, int frames,
                                                                const QColor &paper)
{
    auto d = std::make_unique<doc::Document>(width, height, doc::FrameRate{frameRate, 1});
    d->setFrameCount(frames);
    d->setPaperColor(paper.rgba());
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

void DocumentController::newDocument(int width, int height, int frameRate, int frames, const QColor &paper)
{
    replaceDocument(makeDocument(width, height, frameRate, frames, paper));
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
    const bool cameraWasSelected = m_cameraSelected;
    m_cameraSelected = false;
    m_layerChosen = true;
    m_layerSelection.clear();
    m_selection = QPainterPath();
    if (const doc::Drawing *drawing = currentDrawing(); drawing && !drawing->layers.empty()) {
        m_currentLayer = drawing->layers.back().id;
        m_currentLayerName = drawing->layers.back().properties.name;
    }

    m_tempPoseCel = doc::kNoId;
    emit documentReplaced();
    if (cameraWasSelected)
        emit cameraSelectionChanged();
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

void DocumentController::markUnsaved()
{
    m_history.markUnsaved();
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

doc::LayerId DocumentController::layerBelow(const std::vector<doc::LayerId> &ids) const
{
    const doc::Drawing *drawing = currentDrawing();
    if (!drawing)
        return doc::kNoId;
    int lowest = -1;
    for (doc::LayerId id : ids) {
        const int i = drawing->indexOf(id);
        if (i >= 0 && (lowest < 0 || i < lowest))
            lowest = i;
    }
    return lowest > 0 ? drawing->layers[std::size_t(lowest - 1)].id : doc::kNoId;
}

bool DocumentController::canMergeDown(const std::vector<doc::LayerId> &ids) const
{
    const doc::LayerId below = layerBelow(ids);
    if (below == doc::kNoId)
        return false;
    std::vector<doc::LayerId> together = ids;
    together.push_back(below);
    // Everything chosen, and the layer below, must be takeable: none is left out silently.
    const auto taken = mergeableLayers(together);
    std::size_t wanted = 0;
    const doc::Drawing *drawing = currentDrawing();
    for (doc::LayerId id : together)
        wanted += drawing->indexOf(id) >= 0 ? 1 : 0;
    return taken.size() >= 2 && taken.size() == wanted;
}

bool DocumentController::mergeDown(const std::vector<doc::LayerId> &ids, QString *reason)
{
    if (layerBelow(ids) == doc::kNoId) {
        if (reason)
            *reason = tr("There is no layer below to merge with");
        return false;
    }
    if (!canMergeDown(ids)) {
        if (reason)
            *reason = tr("The layers to merge, and the one below, must be shown, unlocked drawing layers");
        return false;
    }
    std::vector<doc::LayerId> together = ids;
    together.push_back(layerBelow(ids));
    return mergeLayers(together, reason);
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

bool DocumentController::importAudio(const QString &path, QString *reason)
{
    constexpr qint64 kMaxBytes = qint64(512) << 20; // a song, not an album
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (reason)
            *reason = tr("Cannot read %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    if (file.size() > kMaxBytes || file.size() == 0) {
        if (reason)
            *reason = file.size() == 0 ? tr("%1 is empty.").arg(QFileInfo(path).fileName())
                                       : tr("%1 is too large for the project.").arg(QFileInfo(path).fileName());
        return false;
    }
    const QByteArray bytes = file.readAll();
    doc::AudioTrack track;
    track.name = QFileInfo(path).completeBaseName().toStdString();
    track.format = QFileInfo(path).suffix().toLower().toStdString();
    track.data = std::make_shared<const std::vector<unsigned char>>(bytes.cbegin(), bytes.cend());
    // A new song begins where the old one did, or at the start.
    if (const auto &old = m_document->audio())
        track.offset = old->offset;
    execute(std::make_unique<doc::SetAudioCommand>(std::move(track), doc::CommandKind::SetAudio));
    return true;
}

void DocumentController::removeAudio()
{
    if (m_document->audio())
        execute(std::make_unique<doc::SetAudioCommand>(std::nullopt, doc::CommandKind::SetAudio));
}

namespace {

// The same track, changed by `change`; none if there is no track or nothing changes.
template <class Change>
std::unique_ptr<doc::Command> audioChange(const std::optional<doc::AudioTrack> &track, Change change)
{
    if (!track)
        return nullptr;
    doc::AudioTrack changed = *track;
    change(changed);
    if (changed == *track)
        return nullptr;
    return std::make_unique<doc::SetAudioCommand>(std::move(changed), doc::CommandKind::ChangeAudio);
}

} // namespace

void DocumentController::setAudioOffset(int frames)
{
    execute(audioChange(m_document->audio(), [frames](doc::AudioTrack &t) { t.offset = frames; }));
}

void DocumentController::setAudioVolume(float volume)
{
    execute(audioChange(m_document->audio(), [volume](doc::AudioTrack &t) { t.volume = std::clamp(volume, 0.0f, 1.0f); }));
}

void DocumentController::setAudioMuted(bool muted)
{
    execute(audioChange(m_document->audio(), [muted](doc::AudioTrack &t) { t.muted = muted; }));
}

void DocumentController::previewAudioVolume(float volume)
{
    auto track = m_document->audio();
    if (!track)
        return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (track->volume == volume)
        return;
    track->volume = volume;
    m_document->setAudio(std::move(track));
    emit audioChanged();
}

void DocumentController::commitAudioVolume(float before)
{
    const auto track = m_document->audio();
    if (!track)
        return;
    const float after = track->volume;
    if (after == before)
        return;
    previewAudioVolume(before); // the step goes from where the drag began
    setAudioVolume(after);
}

int DocumentController::tweenFrames(doc::CelId celId, int from, int to, TweenEase ease, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return 0;
    };
    interruptStroke();
    doc::Cel *cel = m_document->cel(celId);
    if (!cel)
        return fail(tr("No cel selected"));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));
    if (from < 0 || to - from < 2)
        return fail(tr("There is no frame between the two poses"));
    const doc::DrawingId a = cel->timing().drawingAt(from), b = cel->timing().drawingAt(to);
    if (a == doc::kNoId || b == doc::kNoId)
        return fail(tr("Both ends of a tween need a drawing"));
    if (a == b)
        return fail(tr("The two frames show the same drawing"));
    const auto tween = DrawingTween::make(*cel->drawing(a), *cel->drawing(b), reason);
    if (!tween)
        return 0;

    // The pictures between: each layer of each frame on its own core.
    const int frames = to - from - 1, layers = tween->layerCount();
    std::vector<doc::Layer> made(std::size_t(frames) * std::size_t(layers));
    parallelFor(frames * layers, [&](int k) {
        const int frame = k / layers, layer = k % layers;
        const double u = double(frame + 1) / double(to - from);
        made[std::size_t(k)] = tween->layerAt(layer, easedTime(ease, u));
    });

    // New drawings, named with the next free numbers, shown one frame each from the first frame after `from`.
    std::set<std::string> used;
    for (const auto &[id, drawing] : cel->drawings())
        used.insert(drawing.name);
    doc::Timing after = cel->timing();
    std::vector<std::unique_ptr<doc::Command>> steps;
    int number = 0;
    for (int frame = 0; frame < frames; ++frame) {
        std::string name;
        do
            name = std::to_string(++number);
        while (used.contains(name));
        used.insert(name);
        doc::Drawing drawing{m_document->allocateDrawingId(), name, {}};
        for (int layer = 0; layer < layers; ++layer) {
            doc::Layer &l = made[std::size_t(frame) * std::size_t(layers) + std::size_t(layer)];
            l.id = m_document->allocateLayerId();
            drawing.layers.push_back(std::move(l));
        }
        after.setExposure(from + 1 + frame, 1, drawing.id);
        steps.push_back(std::make_unique<doc::AddDrawingCommand>(celId, std::move(drawing)));
    }
    steps.push_back(std::make_unique<doc::SetTimingCommand>(celId, cel->timing(), std::move(after)));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::Tween, std::move(steps)));
    return frames;
}

void DocumentController::selectCamera(bool selected)
{
    selected = selected && m_document->camera().has_value();
    if (selected == m_cameraSelected)
        return;
    m_cameraSelected = selected;
    emit cameraSelectionChanged();
}

void DocumentController::addCamera()
{
    if (m_document->camera())
        return; // one for now
    execute(std::make_unique<doc::SetCameraCommand>(
        doc::Camera::wholeSheet(m_document->width(), m_document->height())));
}

void DocumentController::removeCamera()
{
    if (m_document->camera())
        execute(std::make_unique<doc::SetCameraCommand>(std::nullopt));
}

std::vector<doc::CelId> DocumentController::celSelection() const
{
    std::vector<doc::CelId> chosen;
    bool hasCurrent = false;
    for (int i = 0; i < m_document->celCount(); ++i) { // bottom up, those still there
        const doc::CelId id = m_document->celAt(i).id();
        if (std::find(m_celSelection.begin(), m_celSelection.end(), id) != m_celSelection.end()) {
            chosen.push_back(id);
            hasCurrent = hasCurrent || id == m_currentCel;
        }
    }
    // Another cel became current some other way: it alone.
    if (!hasCurrent)
        return m_document->cel(m_currentCel) ? std::vector<doc::CelId>{m_currentCel} : std::vector<doc::CelId>{};
    return chosen;
}

void DocumentController::setCelSelection(const std::vector<doc::CelId> &cels, doc::CelId current)
{
    if (!m_document->cel(current))
        return;
    std::vector<doc::CelId> kept;
    for (doc::CelId id : cels)
        if (m_document->cel(id) && std::find(kept.begin(), kept.end(), id) == kept.end())
            kept.push_back(id);
    if (std::find(kept.begin(), kept.end(), current) == kept.end())
        kept.push_back(current);
    const std::vector<doc::CelId> before = celSelection();
    m_celSelection = kept;
    if (current != m_currentCel)
        setCurrentCel(current);
    if (kept.size() > 1) { // the tools act on the layers of all of them (a pixel move, a transform)
        std::vector<LayerRef> layers;
        for (doc::CelId id : kept)
            for (const auto &[drawingId, drawing] : m_document->cel(id)->drawings())
                for (const doc::Layer &layer : drawing.layers)
                    layers.push_back({id, drawingId, layer.id});
        setLayerSelection(std::move(layers));
    }
    if (celSelection() != before)
        emit celSelectionChanged();
}

bool DocumentController::moveChosenCels(int insertAt)
{
    auto step = doc::makeMoveCels(*m_document, celSelection(), insertAt);
    if (!step)
        return false;
    execute(std::move(step));
    return true;
}

bool DocumentController::shiftChosenCels(bool up)
{
    auto step = doc::makeShiftCels(*m_document, celSelection(), up);
    if (!step)
        return false;
    execute(std::move(step));
    return true;
}

void DocumentController::chooseCel(doc::CelId id)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return;
    selectCamera(false);
    setLayerChosen(false); // a cel is not a layer: the pixel tools wait for one to be chosen
    setCurrentCel(id);
    std::vector<LayerRef> layers;
    for (const auto &[drawingId, drawing] : cel->drawings())
        for (const doc::Layer &layer : drawing.layers)
            layers.push_back({id, drawingId, layer.id});
    setLayerSelection(std::move(layers));
    const std::vector<doc::CelId> before = celSelection();
    m_celSelection = {id}; // chosen alone
    if (celSelection() != before)
        emit celSelectionChanged();
    emit celChosen(id);
}

void DocumentController::setLayerChosen(bool chosen)
{
    if (chosen == m_layerChosen)
        return;
    m_layerChosen = chosen;
    emit layerChosenChanged();
}

bool DocumentController::drawingBlocked() const
{
    if (m_cameraSelected)
        return true;
    if (m_layerChosen)
        return false;
    const doc::Cel *cel = m_document->cel(m_currentCel);
    return cel && cel->timing().drawingAt(m_currentFrame) != doc::kNoId;
}

void DocumentController::setMaskEditing(doc::LayerId layer)
{
    if (layer == m_maskLayer)
        return;
    interruptStroke();
    m_maskLayer = layer;
    emit maskEditingChanged();
}

bool DocumentController::createLayerMask()
{
    const doc::DrawingId drawing = currentDrawingId();
    auto step = doc::makeAddLayerMask(*m_document, m_currentCel, drawing, m_currentLayer);
    if (!step)
        return false;
    execute(std::move(step));
    setMaskEditing(m_currentLayer); // the mask is what is drawn on now
    emit maskEditingChanged();
    return true;
}

bool DocumentController::deleteLayerMask()
{
    const doc::DrawingId drawing = currentDrawingId();
    auto step = doc::makeRemoveLayerMask(*m_document, m_currentCel, drawing, m_currentLayer);
    if (!step)
        return false;
    execute(std::move(step));
    if (m_maskLayer == m_currentLayer)
        setMaskEditing(doc::kNoId);
    emit maskEditingChanged();
    return true;
}

const doc::CelMotion *DocumentController::celMotion(doc::CelId id) const
{
    const doc::Cel *cel = m_document->cel(id);
    return cel ? &cel->motion() : nullptr;
}

doc::CelPose DocumentController::celPoseAt(doc::CelId id, int frame) const
{
    const doc::Cel *cel = m_document->cel(id);
    return cel ? cel->motion().poseAt(frame) : doc::CelPose{};
}

bool DocumentController::celKeyMotion(const doc::Cel &cel, int frame, const doc::CelPose &pose, doc::CelMotion &out) const
{
    doc::CelMotion motion = cel.motion();
    const bool first = motion.isEmpty();
    if (first)
        startMotion(motion, cel, frame);
    const doc::CelPose now = motion.interpolatedAt(frame);
    bool changed = false;
    for (doc::Channel c : doc::kChannels) {
        const bool wanted = motion.hasKeys(c) || doc::channelsDiffer(c, pose, now) || (first && c != doc::Channel::Opacity);
        if (!wanted)
            continue;
        if (const doc::CelKey *existing = motion.keyAt(c, frame); existing && existing->pose == doc::channelOf(c, pose))
            continue;
        motion.setKey(c, frame, pose);
        changed = true;
    }
    if (changed)
        out = std::move(motion);
    return changed;
}

bool DocumentController::setCelKey(doc::CelId id, int frame, const doc::CelPose &pose)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0)
        return false;
    clearTemporaryPose(); // what is made a key now is the pose that was shown (the caller has it already)
    doc::CelMotion motion;
    if (!celKeyMotion(*cel, frame, pose, motion))
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::setCelChannelKey(doc::CelId id, doc::Channel channel, int frame, const doc::CelPose &pose)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    if (motion.isEmpty())
        startMotion(motion, *cel, frame);
    if (const doc::CelKey *existing = motion.keyAt(channel, frame); existing && existing->pose == doc::channelOf(channel, pose))
        return false;
    motion.setKey(channel, frame, pose);
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

void DocumentController::startMotion(doc::CelMotion &motion, const doc::Cel &cel, int frame) const
{
    // The first key: the cel turns and scales around the middle of what its frame shows (the page's, if it shows nothing).
    QRect bounds;
    if (const doc::Drawing *drawing = cel.drawingAt(frame))
        bounds = canvas::visibleBounds(*drawing);
    const QPointF middle = QRectF(bounds).center();
    motion.pivotX = bounds.isEmpty() ? m_document->width() / 2.0 : middle.x();
    motion.pivotY = bounds.isEmpty() ? m_document->height() / 2.0 : middle.y();
}

void DocumentController::toggleCelKey(doc::CelId id)
{
    interruptStroke(); // a pose edit left open (Transform, Move) is applied first
    const doc::CelMotion *motion = celMotion(id);
    if (motion && motion->hasKeyAt(m_currentFrame))
        removeCelKey(id, m_currentFrame);
    else
        setCelKey(id, m_currentFrame, celPoseAt(id, m_currentFrame)); // the pose left on the frame, or the one it has
}

void DocumentController::toggleChosenCelKeys()
{
    interruptStroke(); // a pose edit left open (Transform, Move) is applied first
    const std::vector<doc::CelId> chosen = celSelection();
    if (chosen.size() < 2) {
        toggleCelKey(m_currentCel);
        return;
    }
    // As for one cel: the current one decides. It has a key here: the keys on this frame go from all of them; it has none:
    // every one that has none gets a key, at the pose it has (or the one left on the frame). One undo step.
    const doc::CelMotion *current = celMotion(m_currentCel);
    const bool remove = current && current->hasKeyAt(m_currentFrame);
    std::vector<doc::CelPose> shown; // as they are shown now, before the pose left on the frame is cleared
    for (doc::CelId id : chosen)
        shown.push_back(celPoseAt(id, m_currentFrame));
    clearTemporaryPose();
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const doc::Cel *cel = m_document->cel(chosen[i]);
        if (!cel)
            continue;
        doc::CelMotion motion;
        if (remove) {
            motion = cel->motion();
            if (!motion.removeKeysAt(m_currentFrame))
                continue;
        } else if (cel->motion().hasKeyAt(m_currentFrame) || !celKeyMotion(*cel, m_currentFrame, shown[i], motion)) {
            continue;
        }
        steps.push_back(std::make_unique<doc::SetCelMotionCommand>(chosen[i], std::move(motion)));
    }
    if (steps.empty())
        return;
    if (steps.size() == 1)
        execute(std::move(steps.front()));
    else
        execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditMotion, std::move(steps)));
}

bool DocumentController::applyCelPose(doc::CelId id, int frame, const doc::CelPose &pose,
                                      const std::vector<doc::Channel> &channels)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0)
        return false;
    doc::CelMotion motion = cel->motion();
    for (doc::Channel channel : channels) {
        if (motion.keyAt(channel, frame)) {
            motion.setKey(channel, frame, pose);
        } else {
            // No key of this channel here: the value stays only while this frame is the one shown (or until it is made a key).
            if (m_tempPoseCel != id)
                clearTemporaryPose();
            motion.setTemporary(channel, frame, pose);
            m_tempPoseCel = id;
        }
    }
    cel->setMotion(std::move(motion));
    return true;
}

void DocumentController::previewCelPose(doc::CelId id, int frame, const doc::CelPose &pose,
                                        const std::vector<doc::Channel> &channels)
{
    if (!applyCelPose(id, frame, pose, channels))
        return;
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

void DocumentController::previewCelPoses(const std::vector<CelPoseChange> &changes)
{
    // All of them, then the views hear of it once: the frame is drawn again once, not for each cel.
    bool any = false;
    for (const CelPoseChange &c : changes)
        any = applyCelPose(c.cel, c.frame, c.pose, c.channels) || any;
    if (!any)
        return;
    doc::ChangeSet published;
    published.motion = true;
    publish(published);
}

void DocumentController::setCelPose(doc::CelId id, int frame, const doc::CelPose &pose, doc::Channel channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0 || cel->motion().isEmpty())
        return;
    if (cel->motion().keyAt(channel, frame))
        setCelChannelKey(id, channel, frame, pose);
    else
        previewCelPose(id, frame, pose, {channel});
}

void DocumentController::clearTemporaryPose()
{
    if (m_tempPoseCel == doc::kNoId)
        return;
    const doc::CelId id = m_tempPoseCel;
    m_tempPoseCel = doc::kNoId;
    doc::Cel *cel = m_document->cel(id);
    if (!cel || !cel->motion().hasTemporary())
        return;
    doc::CelMotion motion = cel->motion();
    motion.clearTemporary();
    cel->setMotion(std::move(motion));
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

void DocumentController::finishCelPose(doc::CelId id, const doc::CelMotion &before)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || cel->motion() == before)
        return;
    m_history.pushApplied(std::make_unique<doc::SetCelMotionCommand>(id, before, cel->motion()));
    emit historyChanged();
}

void DocumentController::finishCelPoses(const std::vector<std::pair<doc::CelId, doc::CelMotion>> &before)
{
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (const auto &[id, motion] : before) {
        const doc::Cel *cel = m_document->cel(id);
        if (cel && !(cel->motion() == motion))
            steps.push_back(std::make_unique<doc::SetCelMotionCommand>(id, motion, cel->motion()));
    }
    if (steps.empty())
        return;
    if (steps.size() == 1)
        m_history.pushApplied(std::move(steps.front()));
    else
        m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditMotion, std::move(steps)));
    emit historyChanged();
}

void DocumentController::beginFirstKey(doc::CelId id, int frame)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0 || !cel->motion().isEmpty())
        return;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    startMotion(motion, *cel, frame);
    for (doc::Channel c : {doc::Channel::Position, doc::Channel::Rotation, doc::Channel::Scale})
        motion.setKey(c, frame, doc::CelPose{});
    cel->setMotion(std::move(motion));
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

void DocumentController::cancelCelPose(doc::CelId id, const doc::CelMotion &before)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return;
    cel->setMotion(before);
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

bool DocumentController::setCelKeyEasing(doc::CelId id, int frame, const doc::Easing &easing, std::optional<doc::Channel> channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    bool changed = false;
    for (doc::Channel c : doc::kChannels) {
        const doc::CelKey *key = motion.keyAt(c, frame);
        if ((channel && *channel != c) || !key || key->easing == easing)
            continue;
        motion.setEasing(c, frame, easing);
        changed = true;
    }
    if (!changed)
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::removeCelKey(doc::CelId id, int frame, std::optional<doc::Channel> channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    if (!(channel ? motion.removeKey(*channel, frame) : motion.removeKeysAt(frame)))
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::moveCelKey(doc::CelId id, int frame, int to, std::optional<doc::Channel> channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame == to)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    if (!(channel ? motion.moveKey(*channel, frame, to) : motion.moveKeysAt(frame, to)))
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::setCelPivot(doc::CelId id, double x, double y)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || (cel->motion().pivotX == x && cel->motion().pivotY == y))
        return false;
    doc::CelMotion motion = cel->motion();
    motion.pivotX = x;
    motion.pivotY = y;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::clearCelMotion(doc::CelId id)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || cel->motion().isEmpty())
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, doc::CelMotion{}));
    return true;
}

bool DocumentController::setMarker(int frame, const QString &name)
{
    if (frame < 0)
        return false;
    std::map<int, std::string> markers = m_document->markers();
    const std::string text = name.toStdString();
    if (const auto it = markers.find(frame); it != markers.end() && it->second == text)
        return false;
    markers[frame] = text;
    execute(std::make_unique<doc::SetMarkersCommand>(std::move(markers)));
    return true;
}

bool DocumentController::removeMarker(int frame)
{
    std::map<int, std::string> markers = m_document->markers();
    if (markers.erase(frame) == 0)
        return false;
    execute(std::make_unique<doc::SetMarkersCommand>(std::move(markers)));
    return true;
}

bool DocumentController::moveMarker(int from, int to)
{
    std::map<int, std::string> markers = m_document->markers();
    const auto it = markers.find(from);
    if (it == markers.end() || to < 0 || (to != from && markers.count(to)))
        return false;
    if (to == from)
        return false;
    markers[to] = it->second;
    markers.erase(from);
    execute(std::make_unique<doc::SetMarkersCommand>(std::move(markers)));
    return true;
}

void DocumentController::toggleMarker()
{
    if (!removeMarker(m_currentFrame))
        setMarker(m_currentFrame, QString());
}

bool DocumentController::goToMarker(int direction)
{
    const auto &markers = m_document->markers();
    if (direction > 0) {
        const auto it = markers.upper_bound(m_currentFrame);
        if (it == markers.end())
            return false;
        setCurrentFrame(it->first);
        return true;
    }
    auto it = markers.lower_bound(m_currentFrame);
    if (it == markers.begin())
        return false;
    setCurrentFrame(std::prev(it)->first);
    return true;
}

void DocumentController::setSceneLength(int frames)
{
    frames = std::max(1, frames);
    if (frames == m_document->frameCount())
        return;
    execute(std::make_unique<doc::SetFrameCountCommand>(frames));
}

int DocumentController::sceneLength() const { return m_document->frameCount(); }

int DocumentController::usedFrames() const
{
    int last = 0;
    for (int i = 0; i < m_document->celCount(); ++i)
        last = std::max(last, m_document->celAt(i).timing().lastFrame());
    return last;
}

void DocumentController::setCameraVisible(bool visible)
{
    if (!m_document->camera() || m_document->camera()->visible == visible)
        return;
    doc::Camera camera = *m_document->camera();
    camera.visible = visible;
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
}

void DocumentController::setCameraKey(int frame, const doc::CameraPose &pose)
{
    doc::Camera camera = m_document->camera() ? *m_document->camera() : doc::Camera();
    if (const doc::CameraKey *key = camera.keyAt(frame); key && key->pose == pose)
        return;
    camera.setKey(frame, pose);
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
}

bool DocumentController::removeCameraKey(int frame)
{
    if (!m_document->camera())
        return false;
    doc::Camera camera = *m_document->camera();
    if (!camera.removeKey(frame))
        return false;
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
    return true;
}

bool DocumentController::moveCameraKey(int from, int to)
{
    if (!m_document->camera() || from == to)
        return false;
    doc::Camera camera = *m_document->camera();
    if (!camera.moveKey(from, to))
        return false;
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
    return true;
}

bool DocumentController::setCameraEasing(int frame, const doc::Easing &easing)
{
    if (!m_document->camera())
        return false;
    doc::Camera camera = *m_document->camera();
    if (!camera.setEasing(frame, easing))
        return false;
    if (camera == *m_document->camera())
        return true; // already so
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
    return true;
}

doc::CameraPose DocumentController::cameraPoseAt(int frame) const
{
    const auto &camera = m_document->camera();
    if (camera && camera->visible && !camera->isEmpty())
        return camera->poseAt(frame);
    return doc::CameraPose::wholeSheet(m_document->width(), m_document->height());
}

void DocumentController::previewCamera(const std::optional<doc::Camera> &camera)
{
    m_document->setCamera(camera);
    emit cameraChanged();
}

void DocumentController::commitCamera(const std::optional<doc::Camera> &before, const std::optional<doc::Camera> &after)
{
    // Copies: either may be the document's own camera, which the preview below replaces.
    const std::optional<doc::Camera> was = before, now = after;
    previewCamera(was); // so the step records the whole change, not the part after the last preview
    if (was != now)
        execute(std::make_unique<doc::SetCameraCommand>(now));
}

void DocumentController::setCompositing(doc::GraphData graph)
{
    if (graph != m_document->compositing())
        execute(std::make_unique<doc::SetCompositingCommand>(std::move(graph)));
}

void DocumentController::setAudioScrub(bool scrub)
{
    execute(audioChange(m_document->audio(), [scrub](doc::AudioTrack &t) { t.scrub = scrub; }));
}

void DocumentController::setAudioDuration(double seconds)
{
    auto track = m_document->audio();
    if (!track || track->duration == seconds)
        return;
    track->duration = seconds;
    m_document->setAudio(std::move(track));
    emit structureChanged(); // not an undo step: the length of the block is a fact about the file
}

std::vector<std::pair<doc::CelId, doc::BlenderLink>> DocumentController::blenderLinks() const
{
    std::vector<std::pair<doc::CelId, doc::BlenderLink>> links;
    for (int i = 0; i < m_document->celCount(); ++i)
        if (const auto &link = m_document->celAt(i).blenderLink())
            links.emplace_back(m_document->celAt(i).id(), *link);
    return links;
}

std::optional<DocumentController::PendingSequence> DocumentController::beginSequenceCel(const QString &name, int startFrame,
                                                                                         int count,
                                                                                         const doc::BlenderLink *link)
{
    if (count <= 0)
        return std::nullopt;
    interruptStroke();
    doc::Document &d = *m_document;
    startFrame = std::max(0, startFrame);
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, doc::CelProperties{name.toStdString()});
    PendingSequence sequence;
    sequence.cel = cel->celId();
    doc::ChangeSet changes = cel->apply(d);
    steps.push_back(std::move(cel));
    if (link) { // where the cel comes from, as part of the same step
        auto linking = std::make_unique<doc::SetBlenderLinkCommand>(sequence.cel, *link);
        changes.merge(linking->apply(d));
        steps.push_back(std::move(linking));
    }
    for (int i = 0; i < count; ++i) {
        doc::NewDrawing drawing = doc::makeNewDrawing(d, sequence.cel, startFrame + i, 1, tr("Layer 1").toStdString());
        changes.merge(drawing.command->apply(d));
        steps.push_back(std::move(drawing.command));
        sequence.drawings.push_back(drawing.id);
        sequence.layers.push_back(drawing.layer);
    }
    if (startFrame + count > d.frameCount())
        d.setFrameCount(startFrame + count);
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    publish(changes);
    emit historyChanged();
    setCurrentCel(sequence.cel);
    setCurrentFrame(startFrame);
    return sequence;
}

bool DocumentController::fillSequenceFrame(const PendingSequence &sequence, int index, const QImage &image)
{
    if (index < 0 || index >= int(sequence.drawings.size()) || image.isNull())
        return false;
    doc::Document &d = *m_document;
    doc::Layer *layer = d.layer(sequence.cel, sequence.drawings[std::size_t(index)], sequence.layers[std::size_t(index)]);
    if (!layer)
        return false;
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixels.width() != d.width() || pixels.height() != d.height())
        pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);
    layer->image.write(at.x(), at.y(), pixels.width(), pixels.height(),
                       reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    emit pixelsChanged(sequence.cel, sequence.drawings[std::size_t(index)], QRect(at, pixels.size()));
    return true;
}

std::unique_ptr<DocumentController::SequenceUpdate> DocumentController::beginSequenceUpdate(doc::CelId cel, int startFrame)
{
    if (!m_document->cel(cel))
        return nullptr;
    auto update = std::make_unique<SequenceUpdate>();
    update->cel = cel;
    update->startFrame = std::max(0, startFrame);
    return update;
}

bool DocumentController::applySequenceFrames(SequenceUpdate &update, const std::map<int, QImage> &frames)
{
    doc::Document &d = *m_document;
    doc::Cel *cel = d.cel(update.cel);
    if (!cel)
        return false;
    doc::ChangeSet changes;
    std::vector<std::pair<doc::DrawingId, QRect>> painted;
    for (const auto &[index, image] : frames) {
        if (image.isNull())
            continue;
        const int frame = update.startFrame + index;
        doc::DrawingId drawingId = cel->timing().drawingAt(frame);
        doc::LayerId layerId = doc::kNoId;
        const bool isNew = drawingId == doc::kNoId || !cel->drawing(drawingId);
        if (isNew) { // the scene got longer: the frames after the last one are new
            doc::NewDrawing drawing = doc::makeNewDrawing(d, update.cel, frame, 1, tr("Layer 1").toStdString());
            changes.merge(drawing.command->apply(d));
            update.steps.push_back(std::move(drawing.command));
            drawingId = drawing.id;
            layerId = drawing.layer;
            if (frame + 1 > d.frameCount())
                d.setFrameCount(frame + 1);
        } else if (!cel->drawing(drawingId)->layers.empty()) {
            layerId = cel->drawing(drawingId)->layers.front().id; // the render is the first layer; others are the person's
        }
        doc::Layer *layer = layerId == doc::kNoId ? nullptr : d.layer(update.cel, drawingId, layerId);
        if (!layer)
            continue;
        const std::pair<doc::DrawingId, doc::LayerId> key{drawingId, layerId};
        if (!update.before.count(key))
            update.before.emplace(key, isNew ? doc::TiledImage() : layer->image); // as it was, for the undo
        QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (pixels.width() != d.width() || pixels.height() != d.height())
            pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);
        layer->image = doc::TiledImage(); // the new picture replaces the old one, all of it
        layer->image.write(at.x(), at.y(), pixels.width(), pixels.height(),
                           reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
        painted.emplace_back(drawingId, QRect(0, 0, d.width(), d.height()));
    }
    publish(changes);
    for (const auto &[drawingId, area] : painted)
        emit pixelsChanged(update.cel, drawingId, area);
    return true;
}

void DocumentController::finishSequenceUpdate(SequenceUpdate &update)
{
    doc::Document &d = *m_document;
    std::vector<std::unique_ptr<doc::Command>> steps = std::move(update.steps);
    update.steps.clear();
    for (const auto &[key, before] : update.before) {
        const doc::Layer *layer = d.layer(update.cel, key.first, key.second);
        if (!layer)
            continue;
        if (auto paint = doc::PaintCommand::capture(update.cel, key.first, key.second, before, layer->image))
            steps.push_back(std::move(paint));
    }
    update.before.clear();
    if (steps.empty())
        return;
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::UpdateSequence, std::move(steps)));
    emit historyChanged();
}

void DocumentController::finishSequence(const PendingSequence &sequence)
{
    doc::Document &d = *m_document;
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (std::size_t i = 0; i < sequence.drawings.size(); ++i) {
        const doc::Layer *layer = d.layer(sequence.cel, sequence.drawings[i], sequence.layers[i]);
        if (!layer)
            continue;
        if (auto paint = doc::PaintCommand::capture(sequence.cel, sequence.drawings[i], sequence.layers[i], doc::TiledImage(),
                                                    layer->image))
            steps.push_back(std::move(paint));
    }
    if (steps.empty())
        return;
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    emit historyChanged();
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
    startFrame = std::max(0, startFrame);
    hold = std::max(1, hold);
    std::vector<std::pair<int, int>> runs;
    for (int i = 0; i < usable.size(); ++i)
        runs.emplace_back(startFrame + i * hold, hold);
    importRuns(runs, [&usable](int i) { return usable[i]; }, name, m_currentCel, progress);
    return int(usable.size());
}

doc::CelId DocumentController::importRuns(const std::vector<std::pair<int, int>> &runs,
                                          const std::function<QImage(int)> &picture, const QString &name,
                                          doc::CelId above, const std::function<void(int)> &progress)
{
    if (runs.empty())
        return doc::kNoId;
    interruptStroke();
    doc::Document &d = *m_document;

    // One new cel above `above`; each picture a drawing of its own. Applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(above) + 1, doc::CelProperties{name.toStdString()});
    const doc::CelId celId = cel->celId();
    // The views hear of it once, when all is in: one refresh, not one for each of the frames.
    doc::ChangeSet changes = cel->apply(d);
    steps.push_back(std::move(cel));
    std::vector<std::pair<doc::DrawingId, QRect>> painted;
    int end = 0;

    for (int i = 0; i < int(runs.size()); ++i) {
        // Scaled down to fit the document if larger, centered, as a single imported image is.
        QImage pixels = picture(i).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (pixels.width() > d.width() || pixels.height() > d.height())
            pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);

        doc::NewDrawing drawing =
            doc::makeNewDrawing(d, celId, runs[std::size_t(i)].first, runs[std::size_t(i)].second, tr("Layer 1").toStdString());
        changes.merge(drawing.command->apply(d));
        steps.push_back(std::move(drawing.command));
        doc::TiledImage &target = d.layer(celId, drawing.id, drawing.layer)->image;
        const doc::TiledImage before = target;
        target.write(at.x(), at.y(), pixels.width(), pixels.height(),
                     reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
        if (auto paint = doc::PaintCommand::capture(celId, drawing.id, drawing.layer, before, target))
            steps.push_back(std::move(paint));
        painted.emplace_back(drawing.id, QRect(at, pixels.size()));
        end = std::max(end, runs[std::size_t(i)].first + runs[std::size_t(i)].second);
        if (progress)
            progress(i + 1);
    }
    // The scene is as long as the pictures need (this is not one of the undo steps).
    if (end > d.frameCount())
        d.setFrameCount(end);

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    publish(changes);
    for (const auto &[drawingId, area] : painted)
        emit pixelsChanged(celId, drawingId, area);
    emit historyChanged();
    setCurrentCel(celId);
    setCurrentFrame(runs.front().first);
    return celId;
}

doc::CelId DocumentController::rasterCel(doc::CelId celId, QString *reason)
{
    const doc::Cel *cel = m_document->cel(celId);
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return doc::kNoId;
    };
    if (!cel)
        return fail(tr("There is no cel."));
    int first = -1, last = -1;
    for (const doc::Exposure &e : cel->timing().exposures()) {
        first = first < 0 ? e.start : std::min(first, e.start);
        last = std::max(last, e.end());
    }
    if (first < 0)
        return fail(tr("The cel has no drawings."));

    // The frames with a drawing, in stretches that look the same: the same drawing under the same pose.
    std::vector<std::pair<int, int>> runs;
    auto poseAt = [cel](int frame) { return cel->motion().isEmpty() ? doc::CelPose{} : cel->motion().poseAt(frame); };
    for (int f = first; f < last; ++f) {
        if (!cel->drawingAt(f))
            continue;
        if (!runs.empty() && runs.back().first + runs.back().second == f && cel->drawingAt(f - 1) == cel->drawingAt(f) &&
            poseAt(f - 1) == poseAt(f))
            ++runs.back().second;
        else
            runs.emplace_back(f, 1);
    }
    if (runs.empty())
        return fail(tr("The cel shows nothing."));

    const doc::Document &d = *m_document;
    const QString name = tr("%1 (raster)").arg(QString::fromStdString(cel->properties().name));
    return importRuns(
        runs,
        [&d, celId, &runs](int i) {
            canvas::FrameRenderer renderer;
            renderer.setPaperVisible(false);
            renderer.setGuidesVisible(false);
            renderer.setResampleFilter(canvas::ResampleFilter::Bicubic); // moved by its keys: read with care
            renderer.setOnlyCel(celId);
            renderer.renderAll(d, runs[std::size_t(i)].first);
            return renderer.image();
        },
        name, celId, {});
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
    if (!layer || !layer->properties.visible || layer->properties.locked)
        return false;
    // What there is to clear: the mask when it is what is drawn on, the layer's pixels otherwise.
    return !(layer->mask && m_maskLayer == layer->id ? *layer->mask : layer->image).isEmpty();
}

bool DocumentController::clearCurrentLayer(QString *reason)
{
    interruptStroke(); // an open edit is applied first, then cleared with the rest
    const QString nothing = tr("Nothing to clear on this frame");
    doc::Layer *layer = workLayer(true, nothing, reason);
    if (!layer)
        return false;
    // With the mask being drawn on, it is the mask that is cleared (nothing hidden again), not the layer.
    const bool onMask = layer->mask && m_maskLayer == layer->id;
    doc::TiledImage &image = onMask ? *layer->mask : layer->image;
    if (image.isEmpty()) {
        if (reason)
            *reason = nothing;
        return false;
    }

    // The layer stays (and its drawing, exposures too), only its pixels go.
    const doc::DrawingId drawingId = currentDrawingId();
    const doc::TiledImage before = image;
    const QRect area = boundsOf(before.tileCoords());
    image = doc::TiledImage();
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, image, onMask));
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearLayer, std::move(steps)));
    emit pixelsChanged(m_currentCel, drawingId, area);
    emit historyChanged();
    return true;
}

void DocumentController::setSelection(const QPainterPath &area, bool clipToPage)
{
    // Kept inside the document: what lies outside is never drawn.
    QPainterPath page;
    page.addRect(0, 0, m_document->width(), m_document->height());
    QPainterPath clipped = area.isEmpty() ? QPainterPath() : clipToPage ? area.intersected(page) : area;
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

bool DocumentController::flipSelection(bool horizontal, QString *reason)
{
    interruptStroke();
    const QString nothing = tr("Nothing in the selection on this frame");
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    // The layers the tools work on: the one current, or every one chosen in the Layers panel (those of a chosen cel or
    // frame are all chosen together), the hidden and locked ones left out.
    const std::vector<PaintTarget> targets = beginPaintLayers(reason);
    if (targets.empty())
        return false;
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    const QRect box = mask.bounds;
    std::vector<doc::TiledImage> before;
    bool anything = false;
    for (const PaintTarget &target : targets) {
        before.push_back(*target.image);
        doc::TiledImage inside, outside;
        canvas::splitByMask(*target.image, mask, inside, outside);
        if (inside.isEmpty())
            continue;
        anything = true;
        // The part under the selection, mirrored in the box it was in, and laid over what stays.
        QImage area(box.size(), QImage::Format_ARGB32_Premultiplied);
        inside.read(box.x(), box.y(), box.width(), box.height(), reinterpret_cast<doc::Pixel *>(area.bits()),
                    area.bytesPerLine() / 4);
        const QImage mirrored = area.mirrored(horizontal, !horizontal);
        doc::TiledImage piece;
        piece.write(box.x(), box.y(), box.width(), box.height(),
                    reinterpret_cast<const doc::Pixel *>(mirrored.constBits()), mirrored.bytesPerLine() / 4);
        canvas::compositeOver(outside, piece);
        *target.image = std::move(outside);
    }
    if (!anything) {
        cancelPaint();
        if (reason)
            *reason = nothing;
        return false;
    }
    // The outline is mirrored across the same line (pixel x goes to left + right - x), and goes into the same undo step.
    const QTransform across = horizontal ? QTransform(-1, 0, 0, 1, 2.0 * box.left() + box.width(), 0)
                                         : QTransform(1, 0, 0, -1, 0, 2.0 * box.top() + box.height());
    setSelection(across.map(m_selection), false);
    notifyPainted(box);
    endPaint(before, doc::CommandKind::Flip);
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
    // The whole image, even what lies off the page: the layer keeps it and the selection takes all of it, so it
    // can be moved (the page shows only its own part).
    const QRect area(at, pixels.size());
    if ((area & QRect(0, 0, d.width(), d.height())).isEmpty()) {
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
    pasted.write(area.x(), area.y(), area.width(), area.height(),
                 reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    canvas::compositeOver(*target->image, pasted);
    notifyPainted(area & QRect(0, 0, d.width(), d.height()));
    QPainterPath selected;
    selected.addRect(area);
    setSelection(selected, false);
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
    case doc::CommandKind::UpdateSequence: return tr("update of the Blender scene");
    case doc::CommandKind::SetAudio: return tr("audio");
    case doc::CommandKind::ChangeAudio: return tr("audio change");
    case doc::CommandKind::EditCompositing: return tr("compositing");
    case doc::CommandKind::Tween: return tr("tween");
    case doc::CommandKind::EditCamera: return tr("camera");
    case doc::CommandKind::SceneLength: return tr("scene length");
    case doc::CommandKind::EditMarkers: return tr("markers");
    case doc::CommandKind::Flip: return tr("flip");
    case doc::CommandKind::EditMotion: return tr("keyframes");
    case doc::CommandKind::EditBlenderLink: return tr("Blender link");
    case doc::CommandKind::ChangeMask: return tr("mask");
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
    if (changes.camera) {
        if (m_cameraSelected && !m_document->camera()) {
            m_cameraSelected = false;
            emit cameraSelectionChanged();
        }
        emit cameraChanged();
    }
    if (changes.markers)
        emit markersChanged();
    if (changes.motion)
        emit motionChanged();
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
    clearTemporaryPose(); // a pose left on a frame that is not a key goes with the frame
    m_currentFrame = frame;
    emit currentFrameChanged(frame);
    resolveCurrentLayer();
}

void DocumentController::setCurrentCel(doc::CelId cel)
{
    if (cel == m_currentCel || !m_document->cel(cel))
        return;
    interruptStroke();
    clearTemporaryPose();
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

std::optional<DocumentController::PaintTarget> DocumentController::beginPaint(bool createIfEmpty, QString *reason,
                                                                              bool forDrawing)
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

    if (forDrawing && m_cameraSelected)
        return fail(tr("The camera is selected: choose a layer to draw on"));
    PaintState state;
    doc::DrawingId drawing = cel->timing().drawingAt(m_currentFrame);
    if (forDrawing && !m_layerChosen && drawing != doc::kNoId)
        return fail(tr("Choose a layer to draw on: a cel or a frame is selected"));
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
        if (!m_layerChosen) { // the new frame's layer is the selection now
            m_layerChosen = true;
            emit layerChosenByDrawing();
        }
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
    if (layer->mask && m_maskLayer == layer->id) { // the mask is what is drawn on
        state.target.image = &*layer->mask;
        state.target.mask = true;
    }
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return m_paint->target;
}

void DocumentController::setLayerSelection(std::vector<LayerRef> layers)
{
    if (layers.size() == 1 && layers.front().layer == m_currentLayer)
        layers.clear(); // just the current one
    if (layers == m_layerSelection)
        return;
    m_layerSelection = std::move(layers);
    emit layerSelectionChanged();
}

std::vector<DocumentController::PaintTarget> DocumentController::beginPaintLayers(QString *reason)
{
    if (m_cameraSelected) { // the camera is what the pen moves: not the layers
        if (reason)
            *reason = tr("The camera is selected: choose a cel or a layer first");
        return {};
    }
    if (m_layerSelection.empty()) {
        const auto target = beginPaint(false, reason, false);
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
        PaintTarget target{ref.cel, ref.drawing, ref.layer, &layer->image};
        if (layer->mask && m_maskLayer == layer->id) {
            target.image = &*layer->mask;
            target.mask = true;
        }
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
                                                    *targets[i].image, targets[i].mask))
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
    emit poseEditClosing(); // a pose edit left open (Transform, Move on a cel with keys) is applied as it is
    if (!m_paint)
        return;
    emit strokeInterrupted(); // the painter restores pixels and calls cancelPaint()
    cancelPaint();            // in case nobody listened
}

} // namespace mothion::document
