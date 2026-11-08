#include "sync/DocumentSync.h"

#include "core/Parallel.h"
#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/ProjectCodec.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <set>

namespace mothion::sync {

namespace {

constexpr int kTilesPerOp = 24; // a message of tiles stays around 100 KB

QByteArray compact(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QByteArray hashOf(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1); }
QJsonObject parse(const QByteArray &json) { return QJsonDocument::fromJson(json).object(); }

QJsonObject layerObject(const doc::Layer &layer)
{
    const doc::LayerProperties &p = layer.properties;
    const bool perspective = layer.kind == doc::LayerKind::Perspective;
    return {{"id", qint64(layer.id)},
            {"name", QString::fromStdString(p.name)},
            {"visible", p.visible},
            {"locked", p.locked},
            {"opacity", double(p.opacity)},
            {"blend", io::codec::blendName(p.blend)},
            {"kind", perspective ? QStringLiteral("perspective") : QStringLiteral("raster")},
            {"perspective", perspective ? io::codec::gridToJson(p.perspective) : QString()},
            {"mask", layer.mask.has_value()}};
}

QJsonObject drawingObject(doc::CelId cel, const doc::Drawing &drawing)
{
    QJsonArray layers;
    for (const doc::Layer &layer : drawing.layers)
        layers.append(layerObject(layer));
    return {{"cel", qint64(cel)}, {"id", qint64(drawing.id)}, {"name", QString::fromStdString(drawing.name)},
            {"layers", layers}};
}

QJsonObject celObject(const doc::Cel &cel)
{
    const doc::CelProperties &p = cel.properties();
    QJsonArray exposures, drawings;
    for (const doc::Exposure &e : cel.timing().exposures())
        exposures.append(QJsonArray{e.start, e.length, qint64(e.drawing)});
    for (const auto &[id, drawing] : cel.drawings())
        drawings.append(qint64(id));
    return {{"id", qint64(cel.id())},
            {"name", QString::fromStdString(p.name)},
            {"visible", p.visible},
            {"locked", p.locked},
            {"opacity", double(p.opacity)},
            {"blend", io::codec::blendName(p.blend)},
            {"drawings", drawings},
            {"exposures", exposures}};
}

QJsonObject globalObject(const doc::Document &d)
{
    return {{"fpsNumerator", d.frameRate().numerator},
            {"fpsDenominator", d.frameRate().denominator},
            {"frames", d.frameCount()},
            {"paper", double(d.paperColor())},
            {"camera", d.camera() ? io::codec::cameraToJson(*d.camera()) : QString()},
            {"compositing", io::codec::graphToJson(d.compositing(), d.compositingView())}};
}

// The cel list Op: the order of the cels now, and the cels that are gone since `known` (a full one replaces).
QByteArray celListJson(const doc::Document &d, const std::vector<doc::CelId> *known)
{
    QJsonArray order, removed;
    std::set<doc::CelId> now;
    for (int i = 0; i < d.celCount(); ++i) {
        order.append(qint64(d.celAt(i).id()));
        now.insert(d.celAt(i).id());
    }
    if (known)
        for (const doc::CelId id : *known)
            if (!now.count(id))
                removed.append(qint64(id));
    return compact({{"order", order}, {"removed", removed}, {"full", known == nullptr}});
}

// The cel Op: its properties, and what changed in its drawings and timing since `known` (a full one carries all).
QByteArray celOpJson(const doc::Cel &cel, const std::set<doc::DrawingId> *knownDrawings,
                     const std::vector<doc::Exposure> *knownExposures)
{
    QJsonObject o = celObject(cel);
    o.remove("exposures");
    o.remove("drawings");
    QJsonArray drawings, removed, set, cleared;
    std::set<doc::DrawingId> now;
    for (const auto &[id, drawing] : cel.drawings()) {
        drawings.append(qint64(id));
        now.insert(id);
    }
    if (knownDrawings)
        for (const doc::DrawingId id : *knownDrawings)
            if (!now.count(id))
                removed.append(qint64(id));
    const std::vector<doc::Exposure> &exposures = cel.timing().exposures();
    for (const doc::Exposure &e : exposures)
        if (!knownExposures || std::find(knownExposures->begin(), knownExposures->end(), e) == knownExposures->end())
            set.append(QJsonArray{e.start, e.length, qint64(e.drawing)});
    if (knownExposures)
        for (const doc::Exposure &e : *knownExposures)
            if (std::find(exposures.begin(), exposures.end(), e) == exposures.end())
                cleared.append(QJsonArray{e.start, e.length});
    o["drawings"] = drawings; // all of them, for a full one to prune by
    o["removed"] = removed;
    o["set"] = set;
    o["cleared"] = cleared;
    o["full"] = knownDrawings == nullptr;
    return compact(o);
}

std::vector<doc::CelId> celIdsOf(const doc::Document &d)
{
    std::vector<doc::CelId> ids;
    for (int i = 0; i < d.celCount(); ++i)
        ids.push_back(d.celAt(i).id());
    return ids;
}

// The tiles at `coords` of an image, as Ops of a few tiles each (packed on all the cores).
void tilesOps(const doc::TiledImage &image, const std::vector<doc::TileCoord> &coords, doc::CelId cel,
              doc::DrawingId drawing, doc::LayerId layer, bool mask, std::vector<Op> &ops)
{
    std::vector<TileData> data(coords.size());
    parallelFor(int(coords.size()), [&](int i) {
        const doc::TileCoord c = coords[std::size_t(i)];
        const doc::TilePtr tile = image.tile(c);
        data[std::size_t(i)] = {c.x, c.y, tile ? io::codec::packTile(*tile) : QByteArray()};
    });
    for (std::size_t from = 0; from < data.size(); from += kTilesPerOp) {
        Op op;
        op.kind = Op::Kind::Tiles;
        op.cel = qint64(cel);
        op.drawing = qint64(drawing);
        op.layer = qint64(layer);
        op.mask = mask;
        for (std::size_t i = from; i < std::min(data.size(), from + kTilesPerOp); ++i)
            op.tiles.append(data[i]);
        ops.push_back(std::move(op));
    }
}

// What is different in `now` from `known` (the tile objects differ: they are shared when nothing was painted).
std::vector<doc::TileCoord> changedTiles(const doc::TiledImage &now, const doc::TiledImage &known)
{
    std::vector<doc::TileCoord> changed;
    for (const doc::TileCoord &c : now.tileCoords())
        if (now.tile(c) != known.tile(c))
            changed.push_back(c);
    for (const doc::TileCoord &c : known.tileCoords())
        if (!now.tile(c))
            changed.push_back(c);
    return changed;
}

doc::Cel &celFor(doc::Document &d, doc::CelId id)
{
    if (doc::Cel *cel = d.cel(id))
        return *cel;
    return d.insertCel(d.celCount(), std::make_unique<doc::Cel>(id, doc::CelProperties{})); // its Cel Op fills it in
}

} // namespace

DocumentSync::DocumentSync(document::DocumentController &controller)
    : m_controller(controller)
{
}

void DocumentSync::resetBaseline()
{
    m_globalHash.clear();
    m_celList.clear();
    m_celHash.clear();
    m_celDrawings.clear();
    m_celExposures.clear();
    m_drawingHash.clear();
    m_images.clear();
    m_masks.clear();
    scan(false);
}

std::vector<Op> DocumentSync::collectChanges() { return scan(true); }

std::vector<Op> DocumentSync::scan(bool emitOps)
{
    std::vector<Op> ops;
    const doc::Document &d = m_controller.document();
    std::set<doc::CelId> cels;
    std::set<doc::DrawingId> drawings;
    std::set<doc::LayerId> images, masks;

    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &cel = d.celAt(i);
        cels.insert(cel.id());
        for (const auto &[id, drawing] : cel.drawings()) {
            drawings.insert(id);
            const QByteArray json = compact(drawingObject(cel.id(), drawing));
            const QByteArray hash = hashOf(json);
            QByteArray &known = m_drawingHash[id];
            if (known != hash) {
                known = hash;
                if (emitOps) {
                    Op op;
                    op.kind = Op::Kind::Drawing;
                    op.cel = qint64(cel.id());
                    op.drawing = qint64(id);
                    op.json = json;
                    ops.push_back(std::move(op));
                }
            }
            for (const doc::Layer &layer : drawing.layers) {
                if (layer.kind != doc::LayerKind::Raster)
                    continue;
                images.insert(layer.id);
                auto sync = [&](const doc::TiledImage &now, doc::TiledImage &was, bool mask) {
                    if (!emitOps) {
                        was = now;
                        return;
                    }
                    const std::vector<doc::TileCoord> changed = changedTiles(now, was);
                    if (changed.empty())
                        return;
                    tilesOps(now, changed, cel.id(), id, layer.id, mask, ops);
                    for (const doc::TileCoord &c : changed)
                        was.setTile(c, now.tile(c));
                };
                sync(layer.image, m_images[layer.id], false);
                if (layer.mask) {
                    masks.insert(layer.id);
                    sync(*layer.mask, m_masks[layer.id], true);
                }
            }
        }
    }
    std::erase_if(m_drawingHash, [&](const auto &e) { return !drawings.count(e.first); });
    std::erase_if(m_images, [&](const auto &e) { return !images.count(e.first); });
    std::erase_if(m_masks, [&](const auto &e) { return !masks.count(e.first); });

    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &cel = d.celAt(i);
        const QByteArray hash = hashOf(compact(celObject(cel)));
        QByteArray &known = m_celHash[cel.id()];
        if (known != hash) {
            known = hash;
            if (emitOps) {
                Op op;
                op.kind = Op::Kind::Cel;
                op.cel = qint64(cel.id());
                const auto drawingsKnown = m_celDrawings.find(cel.id());
                const auto exposuresKnown = m_celExposures.find(cel.id());
                const bool isNew = drawingsKnown == m_celDrawings.end();
                static const std::set<doc::DrawingId> noDrawings;
                static const std::vector<doc::Exposure> noExposures;
                op.json = celOpJson(cel, isNew ? &noDrawings : &drawingsKnown->second,
                                    isNew ? &noExposures : &exposuresKnown->second);
                ops.push_back(std::move(op));
            }
        }
        std::set<doc::DrawingId> ids;
        for (const auto &[id, drawing] : cel.drawings())
            ids.insert(id);
        m_celDrawings[cel.id()] = std::move(ids);
        m_celExposures[cel.id()] = cel.timing().exposures();
    }
    std::erase_if(m_celHash, [&](const auto &e) { return !cels.count(e.first); });
    std::erase_if(m_celDrawings, [&](const auto &e) { return !cels.count(e.first); });
    std::erase_if(m_celExposures, [&](const auto &e) { return !cels.count(e.first); });

    const std::vector<doc::CelId> list = celIdsOf(d);
    if (list != m_celList) {
        if (emitOps) {
            Op op;
            op.kind = Op::Kind::CelList;
            op.json = celListJson(d, &m_celList);
            ops.push_back(std::move(op));
        }
        m_celList = list;
    }

    const QByteArray globalJson = compact(globalObject(d));
    const QByteArray globalHash = hashOf(globalJson);
    if (globalHash != m_globalHash) {
        m_globalHash = globalHash;
        if (emitOps) {
            Op op;
            op.kind = Op::Kind::Global;
            op.json = globalJson;
            ops.push_back(std::move(op));
        }
    }
    return ops;
}

void DocumentSync::rebaseDrawing(doc::CelId celId, doc::DrawingId drawingId)
{
    const doc::Document &d = m_controller.document();
    const doc::Cel *cel = d.cel(celId);
    const doc::Drawing *drawing = cel ? cel->drawing(drawingId) : nullptr;
    if (!drawing)
        return;
    m_drawingHash[drawingId] = hashOf(compact(drawingObject(celId, *drawing)));
    for (const doc::Layer &layer : drawing->layers) {
        if (layer.kind != doc::LayerKind::Raster)
            continue;
        m_images[layer.id] = layer.image;
        if (layer.mask)
            m_masks[layer.id] = *layer.mask;
        else
            m_masks.erase(layer.id);
    }
}

void DocumentSync::apply(const Op &op)
{
    doc::Document &d = m_controller.remoteDocument();
    doc::ChangeSet changes;

    switch (op.kind) {
    case Op::Kind::Global: {
        const QJsonObject o = parse(op.json);
        d.setFrameRate({o.value("fpsNumerator").toInt(24), std::max(1, o.value("fpsDenominator").toInt(1))});
        d.setFrameCount(o.value("frames").toInt(1));
        d.setPaperColor(doc::Pixel(o.value("paper").toDouble()));
        const QString camera = o.value("camera").toString();
        d.setCamera(camera.isEmpty() ? std::nullopt : std::optional<doc::Camera>(io::codec::cameraFromJson(camera)));
        const QString graph = o.value("compositing").toString();
        d.setCompositing(io::codec::graphFromJson(graph));
        d.setCompositingView(io::codec::viewFromJson(graph));
        m_globalHash = hashOf(compact(globalObject(d)));
        changes.structure = changes.camera = true;
        break;
    }
    case Op::Kind::CelList: {
        const QJsonObject o = parse(op.json);
        std::vector<doc::CelId> wanted;
        for (const QJsonValue &v : o.value("order").toArray())
            wanted.push_back(doc::CelId(v.toInteger()));
        if (o.value("full").toBool()) { // a restoring one: the cels not in it go
            for (const doc::CelId id : celIdsOf(d))
                if (std::find(wanted.begin(), wanted.end(), id) == wanted.end())
                    d.takeCel(id);
        } else { // the cels it says were taken out
            for (const QJsonValue &v : o.value("removed").toArray())
                d.takeCel(doc::CelId(v.toInteger()));
        }
        // Their order; a cel it does not know (somebody else's, new) keeps its place after them.
        int position = 0;
        for (const doc::CelId id : wanted)
            if (d.cel(id)) {
                if (d.indexOf(id) != position)
                    d.moveCel(id, position);
                ++position;
            }
        m_celList = celIdsOf(d);
        std::erase_if(m_celHash, [&](const auto &e) { return !d.cel(e.first); });
        std::erase_if(m_celDrawings, [&](const auto &e) { return !d.cel(e.first); });
        std::erase_if(m_celExposures, [&](const auto &e) { return !d.cel(e.first); });
        changes.structure = true;
        break;
    }
    case Op::Kind::Cel: {
        const QJsonObject o = parse(op.json);
        doc::Cel &cel = celFor(d, doc::CelId(op.cel));
        doc::CelProperties p;
        p.name = o.value("name").toString().toStdString();
        p.visible = o.value("visible").toBool(true);
        p.locked = o.value("locked").toBool(false);
        p.opacity = float(o.value("opacity").toDouble(1.0));
        p.blend = io::codec::blendFromName(o.value("blend").toString());
        cel.setProperties(p);
        std::vector<doc::DrawingId> gone;
        if (o.value("full").toBool()) { // a restoring one: what it does not list goes
            std::set<doc::DrawingId> wanted;
            for (const QJsonValue &v : o.value("drawings").toArray())
                wanted.insert(doc::DrawingId(v.toInteger()));
            for (const auto &[id, drawing] : cel.drawings())
                if (!wanted.count(id))
                    gone.push_back(id);
            cel.timing() = doc::Timing();
        } else {
            for (const QJsonValue &v : o.value("removed").toArray())
                gone.push_back(doc::DrawingId(v.toInteger()));
        }
        for (const doc::DrawingId id : gone) {
            cel.takeDrawing(id);
            m_drawingHash.erase(id);
        }
        for (const QJsonValue &v : o.value("cleared").toArray()) { // where it stopped showing something
            const QJsonArray e = v.toArray();
            cel.timing().clear(e.at(0).toInt(), e.at(1).toInt());
        }
        for (const QJsonValue &v : o.value("set").toArray()) { // and where it started
            const QJsonArray e = v.toArray();
            cel.timing().setExposure(e.at(0).toInt(), e.at(1).toInt(), doc::DrawingId(e.at(2).toInteger()));
        }
        m_celHash[cel.id()] = hashOf(compact(celObject(cel)));
        std::set<doc::DrawingId> ids;
        for (const auto &[id, drawing] : cel.drawings())
            ids.insert(id);
        m_celDrawings[cel.id()] = std::move(ids);
        m_celExposures[cel.id()] = cel.timing().exposures();
        changes.structure = true;
        break;
    }
    case Op::Kind::Drawing: {
        const QJsonObject o = parse(op.json);
        doc::Cel &cel = celFor(d, doc::CelId(op.cel));
        const doc::DrawingId id = doc::DrawingId(op.drawing);
        doc::Drawing *drawing = cel.drawing(id);
        if (!drawing)
            drawing = &cel.addDrawing(doc::Drawing{id, std::string(), {}});
        drawing->name = o.value("name").toString().toStdString();
        std::vector<doc::Layer> old = std::move(drawing->layers);
        std::vector<doc::Layer> layers;
        for (const QJsonValue &v : o.value("layers").toArray()) {
            const QJsonObject l = v.toObject();
            const doc::LayerId layerId = doc::LayerId(l.value("id").toInteger());
            doc::Layer layer;
            for (doc::Layer &existing : old)
                if (existing.id == layerId)
                    layer = std::move(existing); // its tiles stay
            layer.id = layerId;
            layer.properties.name = l.value("name").toString().toStdString();
            layer.properties.visible = l.value("visible").toBool(true);
            layer.properties.locked = l.value("locked").toBool(false);
            layer.properties.opacity = float(l.value("opacity").toDouble(1.0));
            layer.properties.blend = io::codec::blendFromName(l.value("blend").toString());
            layer.kind = l.value("kind").toString() == QLatin1String("perspective") ? doc::LayerKind::Perspective
                                                                                    : doc::LayerKind::Raster;
            if (layer.kind == doc::LayerKind::Perspective)
                layer.properties.perspective = io::codec::gridFromJson(l.value("perspective").toString());
            if (l.value("mask").toBool() && !layer.mask)
                layer.mask.emplace();
            else if (!l.value("mask").toBool())
                layer.mask.reset();
            layers.push_back(std::move(layer));
        }
        drawing->layers = std::move(layers);
        rebaseDrawing(cel.id(), id);
        changes.structure = true;
        break;
    }
    case Op::Kind::Tiles: {
        doc::Layer *layer = d.layer(doc::CelId(op.cel), doc::DrawingId(op.drawing), doc::LayerId(op.layer));
        if (!layer)
            return;
        if (op.mask && !layer->mask)
            layer->mask.emplace();
        doc::TiledImage &image = op.mask ? *layer->mask : layer->image;
        doc::TiledImage &known = op.mask ? m_masks[layer->id] : m_images[layer->id];
        doc::ChangeSet::Pixels pixels{doc::CelId(op.cel), doc::DrawingId(op.drawing), layer->id, {}};
        for (const TileData &t : op.tiles) {
            const doc::TileCoord coord{t.x, t.y};
            doc::TilePtr tile;
            if (!t.packed.isEmpty())
                tile = io::codec::unpackTile(t.packed);
            image.setTile(coord, tile);
            known.setTile(coord, image.tile(coord));
            pixels.tiles.push_back(coord);
        }
        changes.pixels.push_back(std::move(pixels));
        break;
    }
    }
    m_controller.publishRemote(changes);
}

Op DocumentSync::celOrder() const
{
    const doc::Document &d = m_controller.document();
    const std::vector<doc::CelId> ids = celIdsOf(d);
    Op op;
    op.kind = Op::Kind::CelList;
    op.json = celListJson(d, &ids); // the cels are all still there: nothing removed, nothing replaced
    return op;
}

std::vector<Op> DocumentSync::current(const Op &like) const
{
    std::vector<Op> ops;
    const doc::Document &d = m_controller.document();
    switch (like.kind) {
    case Op::Kind::Global: {
        Op op;
        op.kind = Op::Kind::Global;
        op.json = compact(globalObject(d));
        ops.push_back(std::move(op));
        break;
    }
    case Op::Kind::CelList: {
        Op op;
        op.kind = Op::Kind::CelList;
        op.json = celListJson(d, nullptr);
        ops.push_back(std::move(op));
        break;
    }
    case Op::Kind::Cel: {
        if (const doc::Cel *cel = d.cel(doc::CelId(like.cel))) {
            Op op;
            op.kind = Op::Kind::Cel;
            op.cel = like.cel;
            op.json = celOpJson(*cel, nullptr, nullptr);
            ops.push_back(std::move(op));
        }
        break;
    }
    case Op::Kind::Drawing: {
        const doc::Cel *cel = d.cel(doc::CelId(like.cel));
        const doc::Drawing *drawing = cel ? cel->drawing(doc::DrawingId(like.drawing)) : nullptr;
        if (!drawing)
            break;
        Op op;
        op.kind = Op::Kind::Drawing;
        op.cel = like.cel;
        op.drawing = like.drawing;
        op.json = compact(drawingObject(cel->id(), *drawing));
        ops.push_back(std::move(op));
        for (const doc::Layer &layer : drawing->layers) { // and what is on its layers: all of it
            if (layer.kind != doc::LayerKind::Raster)
                continue;
            tilesOps(layer.image, layer.image.tileCoords(), cel->id(), drawing->id, layer.id, false, ops);
            if (layer.mask)
                tilesOps(*layer.mask, layer.mask->tileCoords(), cel->id(), drawing->id, layer.id, true, ops);
        }
        break;
    }
    case Op::Kind::Tiles: {
        const doc::Layer *layer = d.layer(doc::CelId(like.cel), doc::DrawingId(like.drawing), doc::LayerId(like.layer));
        if (!layer)
            break;
        const doc::TiledImage *image = like.mask ? (layer->mask ? &*layer->mask : nullptr) : &layer->image;
        if (!image)
            break;
        std::vector<doc::TileCoord> coords;
        for (const TileData &t : like.tiles)
            coords.push_back({t.x, t.y});
        tilesOps(*image, coords, doc::CelId(like.cel), doc::DrawingId(like.drawing), layer->id, like.mask, ops);
        break;
    }
    }
    return ops;
}

} // namespace mothion::sync
