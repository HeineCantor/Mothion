#include "io/ProjectFile.h"

#include "core/Parallel.h"
#include "io/FrameExport.h"
#include "io/ProjectThumbnail.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <system_error>

namespace mothion::io {

namespace {

constexpr qint64 kApplicationId = 0x4D4F5448; // 'MOTH'
constexpr qsizetype kTileBytes = qsizetype(doc::kTilePixels) * qsizetype(sizeof(doc::Pixel));

const char *const kSchema[] = {
    // First, so that its page comes right after the schema's (ProjectThumbnail.h).
    "create table thumbnail (png blob not null)",
    "create table document (width integer not null, height integer not null,"
    " fps_numerator integer not null, fps_denominator integer not null, frame_count integer not null,"
    " paper_color integer not null, next_cel_id integer not null, next_drawing_id integer not null,"
    " next_layer_id integer not null, saved_by text)",
    "create table cels (id integer primary key, position integer not null, name text not null,"
    " visible integer not null, locked integer not null, opacity real not null, blend text not null)",
    "create table drawings (id integer primary key, cel integer not null references cels(id), name text not null)",
    "create table layers (id integer primary key, drawing integer not null references drawings(id),"
    " position integer not null, name text not null, visible integer not null, locked integer not null,"
    " opacity real not null, blend text not null, kind text not null, perspective text not null)",
    "create table exposures (cel integer not null references cels(id), start integer not null,"
    " length integer not null, drawing integer not null references drawings(id))",
    "create table tiles (layer integer not null references layers(id), x integer not null,"
    " y integer not null, pixels blob not null, primary key (layer, x, y))",
    // Format 5: the audio track, the sound file whole.
    "create table audio (name text not null, format text not null, offset integer not null,"
    " volume real not null, muted integer not null, duration real not null, data blob not null,"
    " scrub integer not null default 1)",
    // Format 7: the compositing network, as JSON.
    "create table compositing (graph text not null)",
    // Format 8: the camera, as JSON.
    "create table camera (data text not null)",
    // Format 9: the layers' masks: which layers have one, and its tiles (packed like the layers' own).
    "create table masks (layer integer primary key references layers(id))",
    "create table masktiles (layer integer not null references masks(layer), x integer not null,"
    " y integer not null, pixels blob not null, primary key (layer, x, y))",
    // Format 10: the timeline's markers.
    "create table markers (frame integer primary key, name text not null)",
};

// Stored by name, so reordering the enum cannot change old files.
constexpr std::array<std::pair<doc::BlendMode, const char *>, 28> kBlendNames{{
    {doc::BlendMode::Normal, "normal"},
    {doc::BlendMode::Multiply, "multiply"},
    {doc::BlendMode::Screen, "screen"},
    {doc::BlendMode::Overlay, "overlay"},
    {doc::BlendMode::Add, "add"},
    {doc::BlendMode::Darken, "darken"},
    {doc::BlendMode::Lighten, "lighten"},
    {doc::BlendMode::ColorDodge, "colordodge"},
    {doc::BlendMode::ColorBurn, "colorburn"},
    {doc::BlendMode::HardLight, "hardlight"},
    {doc::BlendMode::SoftLight, "softlight"},
    {doc::BlendMode::Difference, "difference"},
    {doc::BlendMode::Exclusion, "exclusion"},
    {doc::BlendMode::LinearBurn, "linearburn"},
    {doc::BlendMode::Subtract, "subtract"},
    {doc::BlendMode::Divide, "divide"},
    {doc::BlendMode::LinearLight, "linearlight"},
    {doc::BlendMode::VividLight, "vividlight"},
    {doc::BlendMode::PinLight, "pinlight"},
    {doc::BlendMode::HardMix, "hardmix"},
    {doc::BlendMode::LighterColor, "lightercolor"},
    {doc::BlendMode::DarkerColor, "darkercolor"},
    {doc::BlendMode::Hue, "hue"},
    {doc::BlendMode::Saturation, "saturation"},
    {doc::BlendMode::Color, "color"},
    {doc::BlendMode::Luminosity, "luminosity"},
    {doc::BlendMode::GlowDodge, "glowdodge"},
    {doc::BlendMode::AddGlow, "addglow"},
}};

QString blendName(doc::BlendMode blend)
{
    for (const auto &[mode, name] : kBlendNames)
        if (mode == blend)
            return QString::fromLatin1(name);
    return QStringLiteral("normal");
}

doc::BlendMode blendFromName(const QString &text)
{
    for (const auto &[mode, name] : kBlendNames)
        if (text == QLatin1String(name))
            return mode;
    return doc::BlendMode::Normal; // a mode from a newer version: the closest we have
}

// A perspective layer's grid as JSON, names as in perspective::Grid (the
// orientation as its quaternion, the position as x, y, z).
QString gridToJson(const perspective::Grid &g)
{
    const QJsonObject o{
        {QStringLiteral("orientation"),
         QJsonArray{g.orientation.w, g.orientation.x, g.orientation.y, g.orientation.z}},
        {QStringLiteral("position"), QJsonArray{g.position.x, g.position.y, g.position.z}},
        {QStringLiteral("fov"), g.fov},
        {QStringLiteral("fisheye"), g.fisheye},
        {QStringLiteral("frameWidth"), g.frameWidth},
        {QStringLiteral("originX"), g.originX},
        {QStringLiteral("originY"), g.originY},
        {QStringLiteral("cell"), g.cell},
        {QStringLiteral("cells"), g.cells},
        {QStringLiteral("color"), QColor::fromRgba(g.color).name(QColor::HexArgb)},
        {QStringLiteral("horizon"), g.horizon},
    };
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// Missing or damaged values keep the defaults. The first perspective layers
// had turn / tilt / roll angles and their center on the camera's axis.
perspective::Grid gridFromJson(const QString &text)
{
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    perspective::Grid g;
    auto number = [&o](const char *key, double &value) {
        if (o.value(QLatin1String(key)).isDouble())
            value = o.value(QLatin1String(key)).toDouble();
    };
    if (const QJsonArray q = o.value(QStringLiteral("orientation")).toArray(); q.size() == 4) {
        g.orientation =
            perspective::normalized({q[0].toDouble(1.0), q[1].toDouble(), q[2].toDouble(), q[3].toDouble()});
    } else if (o.contains(QStringLiteral("tilt"))) {
        double turn = 30.0, tilt = 60.0, roll = 0.0;
        number("turn", turn);
        number("tilt", tilt);
        number("roll", roll);
        g.orientation = perspective::fromAngles(turn, tilt, roll);
    }
    if (const QJsonArray p = o.value(QStringLiteral("position")).toArray(); p.size() == 3)
        g.position = {p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
    number("fov", g.fov);
    number("fisheye", g.fisheye);
    number("frameWidth", g.frameWidth);
    number("centerX", g.originX); // the first ones
    number("centerY", g.originY);
    number("originX", g.originX);
    number("originY", g.originY);
    number("cell", g.cell);
    g.cells = std::max(1, o.value(QStringLiteral("cells")).toInt(g.cells));
    if (const QColor color = QColor::fromString(o.value(QStringLiteral("color")).toString()); color.isValid())
        g.color = color.rgba();
    g.horizon = o.value(QStringLiteral("horizon")).toBool(g.horizon);
    return g;
}

// Ids are uint64 in the model, signed 64-bit integers in SQLite.
QVariant idValue(std::uint64_t id) { return QVariant(qlonglong(id)); }
std::uint64_t idFrom(const QVariant &v) { return std::uint64_t(v.toLongLong()); }

// Format 4: each byte of a tile, rows of 64 pixels of 4 bytes, minus what the PNG Paeth predictor guesses it
// is from the byte before it (a pixel to the left), the one above and the one above that.
constexpr int kTileRowBytes = doc::kTileSize * int(sizeof(doc::Pixel));

inline int paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

void filterPaeth(const unsigned char *raw, unsigned char *out)
{
    for (int y = 0; y < doc::kTileSize; ++y) {
        const unsigned char *row = raw + y * kTileRowBytes;
        const unsigned char *up = y ? row - kTileRowBytes : nullptr;
        unsigned char *o = out + y * kTileRowBytes;
        for (int x = 0; x < kTileRowBytes; ++x) {
            const int a = x >= 4 ? row[x - 4] : 0;
            const int b = up ? up[x] : 0;
            const int c = (up && x >= 4) ? up[x - 4] : 0;
            o[x] = static_cast<unsigned char>(row[x] - paeth(a, b, c));
        }
    }
}

void unfilterPaeth(const unsigned char *filtered, unsigned char *raw)
{
    for (int y = 0; y < doc::kTileSize; ++y) {
        const unsigned char *f = filtered + y * kTileRowBytes;
        unsigned char *row = raw + y * kTileRowBytes;
        const unsigned char *up = y ? row - kTileRowBytes : nullptr;
        for (int x = 0; x < kTileRowBytes; ++x) {
            const int a = x >= 4 ? row[x - 4] : 0;
            const int b = up ? up[x] : 0;
            const int c = (up && x >= 4) ? up[x - 4] : 0;
            row[x] = static_cast<unsigned char>(f[x] + paeth(a, b, c));
        }
    }
}

QByteArray packTile(const doc::Tile &tile)
{
    QByteArray raw(kTileBytes, Qt::Uninitialized);
    qToLittleEndian<doc::Pixel>(tile.pixels.data(), doc::kTilePixels, raw.data());
    QByteArray filtered(kTileBytes, Qt::Uninitialized);
    filterPaeth(reinterpret_cast<const unsigned char *>(raw.constData()),
                reinterpret_cast<unsigned char *>(filtered.data()));
    return qCompress(filtered);
}

// `filtered`: from a format 4 file (3 and older stored the pixels as they are).
std::shared_ptr<doc::Tile> unpackTile(const QByteArray &blob, bool filtered)
{
    QByteArray raw = qUncompress(blob);
    if (raw.size() != kTileBytes)
        return nullptr;
    if (filtered) {
        QByteArray plain(kTileBytes, Qt::Uninitialized);
        unfilterPaeth(reinterpret_cast<const unsigned char *>(raw.constData()),
                      reinterpret_cast<unsigned char *>(plain.data()));
        raw = std::move(plain);
    }
    auto tile = std::make_shared<doc::Tile>();
    qFromLittleEndian<doc::Pixel>(raw.constData(), doc::kTilePixels, tile->pixels.data());
    return tile;
}

// A named connection that is removed when the scope ends (after every
// QSqlDatabase and QSqlQuery using it is gone).
class Connection
{
public:
    Connection()
        : m_name(QStringLiteral("mothion-project-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
    }
    ~Connection() { QSqlDatabase::removeDatabase(m_name); }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    const QString &name() const { return m_name; }

private:
    QString m_name;
};

QString sqlError(const QSqlQuery &q) { return q.lastError().text(); }

// The preview for file managers (ProjectThumbnail.h): the first frame that
// shows something, scaled down, the Mothion icon in its bottom-right corner,
// after the magic. Smaller until it fits one page; empty if it never does.
QByteArray thumbnailBlob(const doc::Document &d)
{
    int frame = -1;
    for (int i = 0; i < d.celCount(); ++i)
        if (!d.celAt(i).timing().isEmpty()) {
            const int start = d.celAt(i).timing().exposures().front().start;
            frame = frame < 0 ? start : std::min(frame, start);
        }
    const QImage full = renderFrame(d, std::max(frame, 0), false).convertToFormat(QImage::Format_RGB32);
    const QImage icon(QStringLiteral(":/icon/mothion.png"));
    for (int side = kThumbnailSize; side >= 64; side = side * 3 / 4) {
        QImage image = full.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!icon.isNull()) {
            const int badge = std::max(16, std::min(image.width(), image.height()) * 3 / 10);
            const int margin = std::max(2, badge / 12);
            QPainter p(&image);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRect(image.width() - badge - margin, image.height() - badge - margin, badge, badge), icon);
        }
        QByteArray blob(kThumbnailMagic, qsizetype(kThumbnailMagicSize));
        QBuffer buffer(&blob);
        buffer.open(QIODevice::Append);
        image.save(&buffer, "PNG", 0); // quality 0: the smallest PNG
        if (std::size_t(blob.size()) <= kThumbnailMaxBytes)
            return blob;
    }
    return {};
}

// The compositing network as JSON text.
QString graphToJson(const doc::GraphData &graph, const doc::GraphView &view)
{
    QJsonArray nodes, links;
    for (const doc::GraphNodeData &n : graph.nodes) {
        QJsonObject params;
        for (const auto &[key, value] : n.params)
            params[QString::fromStdString(key)] = value;
        QJsonObject node{{"id", n.id}, {"type", QString::fromStdString(n.type)}, {"x", n.x}, {"y", n.y},
                         {"params", params}};
        if (!n.data.empty())
            node["data"] = QString::fromStdString(n.data);
        nodes.append(node);
    }
    for (const doc::GraphLinkData &l : graph.links)
        links.append(QJsonArray{l.from, l.fromPort, l.to, l.toPort});
    QJsonObject root{{"next", graph.nextId}, {"nodes", nodes}, {"links", links}};
    if (view.valid)
        root["view"] = QJsonArray{view.zoom, view.x, view.y};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// The camera as JSON text: whether it is on, and its keys (frame, centre, zoom, rotation, and the easing to the
// next key: "h" held, "l" straight, or the four numbers of a Bezier).
QString cameraToJson(const doc::Camera &camera)
{
    QJsonArray keys;
    for (const doc::CameraKey &k : camera.keys()) {
        QJsonObject key{{"f", k.frame}, {"x", k.pose.centerX}, {"y", k.pose.centerY}, {"z", k.pose.zoom},
                        {"r", k.pose.rotation}};
        switch (k.easing.kind) {
        case doc::Easing::Kind::Hold: key["e"] = QStringLiteral("h"); break;
        case doc::Easing::Kind::Linear: key["e"] = QStringLiteral("l"); break;
        case doc::Easing::Kind::Bezier: key["e"] = QJsonArray{k.easing.x1, k.easing.y1, k.easing.x2, k.easing.y2}; break;
        }
        keys.append(key);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"visible", camera.visible}, {"keys", keys}})
                                 .toJson(QJsonDocument::Compact));
}

doc::Camera cameraFromJson(const QString &text)
{
    doc::Camera camera;
    const QJsonObject root = QJsonDocument::fromJson(text.toUtf8()).object();
    camera.visible = root.value("visible").toBool(true);
    for (const QJsonValue &value : root.value("keys").toArray()) {
        const QJsonObject o = value.toObject();
        doc::CameraPose pose;
        pose.centerX = o.value("x").toDouble();
        pose.centerY = o.value("y").toDouble();
        pose.zoom = o.value("z").toDouble(1.0);
        if (!(pose.zoom > 0))
            pose.zoom = 1.0; // a zoom of nothing would be no picture
        pose.rotation = o.value("r").toDouble();
        doc::Easing easing = doc::Easing::linear();
        const QJsonValue e = o.value("e");
        if (e.isString() && e.toString() == QLatin1String("h")) {
            easing = doc::Easing::hold();
        } else if (e.isArray() && e.toArray().size() == 4) {
            const QJsonArray a = e.toArray();
            easing = doc::Easing::bezier(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
        }
        camera.setKey(o.value("f").toInt(), pose, easing);
    }
    return camera;
}

doc::GraphView viewFromJson(const QString &text)
{
    doc::GraphView view;
    const QJsonArray a = QJsonDocument::fromJson(text.toUtf8()).object().value("view").toArray();
    if (a.size() == 3 && a[0].toDouble() > 0) {
        view.valid = true;
        view.zoom = a[0].toDouble();
        view.x = a[1].toDouble();
        view.y = a[2].toDouble();
    }
    return view;
}

doc::GraphData graphFromJson(const QString &text)
{
    doc::GraphData graph;
    const QJsonObject root = QJsonDocument::fromJson(text.toUtf8()).object();
    graph.nextId = std::max(1, root.value("next").toInt(1));
    for (const QJsonValue &value : root.value("nodes").toArray()) {
        const QJsonObject o = value.toObject();
        doc::GraphNodeData n;
        n.id = o.value("id").toInt();
        n.type = o.value("type").toString().toStdString();
        n.x = o.value("x").toDouble();
        n.y = o.value("y").toDouble();
        n.data = o.value("data").toString().toStdString();
        const QJsonObject params = o.value("params").toObject();
        for (auto it = params.begin(); it != params.end(); ++it)
            n.params.emplace_back(it.key().toStdString(), it.value().toInt());
        if (n.id > 0 && !n.type.empty()) {
            graph.nextId = std::max(graph.nextId, n.id + 1);
            graph.nodes.push_back(std::move(n));
        }
    }
    for (const QJsonValue &value : root.value("links").toArray()) {
        const QJsonArray a = value.toArray();
        if (a.size() == 4)
            graph.links.push_back({a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt()});
    }
    return graph;
}

bool writeDocument(QSqlDatabase &db, const doc::Document &d, QString &failure)
{
    QSqlQuery q(db);
    auto run = [&](const QString &sql) {
        if (q.exec(sql))
            return true;
        failure = sqlError(q);
        return false;
    };
    // 64 KiB pages: the thumbnail fits in one, in one piece (ProjectThumbnail.h).
    if (!run(QStringLiteral("pragma page_size = 65536"))
        || !run(QStringLiteral("pragma application_id = %1").arg(kApplicationId))
        || !run(QStringLiteral("pragma user_version = %1").arg(kProjectFormatVersion)))
        return false;
    if (!db.transaction()) {
        failure = db.lastError().text();
        return false;
    }
    for (const char *statement : kSchema)
        if (!run(QString::fromLatin1(statement)))
            return false;

    // The thumbnail's row first, so it is written into the first table page.
    if (const QByteArray thumbnail = thumbnailBlob(d); !thumbnail.isEmpty()) {
        q.prepare(QStringLiteral("insert into thumbnail values (?)"));
        q.addBindValue(thumbnail);
        if (!q.exec()) {
            failure = sqlError(q);
            return false;
        }
    }

    q.prepare(QStringLiteral("insert into document values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const QVariant &v : {QVariant(d.width()), QVariant(d.height()), QVariant(d.frameRate().numerator),
                              QVariant(d.frameRate().denominator), QVariant(d.frameCount()),
                              QVariant(qlonglong(d.paperColor())), idValue(d.nextCelId()),
                              idValue(d.nextDrawingId()), idValue(d.nextLayerId()),
                              QVariant(QCoreApplication::applicationVersion())})
        q.addBindValue(v);
    if (!q.exec()) {
        failure = sqlError(q);
        return false;
    }

    QSqlQuery cel(db), drawing(db), layer(db), exposure(db), tile(db), mask(db), maskTile(db);
    cel.prepare(QStringLiteral("insert into cels values (?, ?, ?, ?, ?, ?, ?)"));
    drawing.prepare(QStringLiteral("insert into drawings values (?, ?, ?)"));
    layer.prepare(QStringLiteral("insert into layers values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    exposure.prepare(QStringLiteral("insert into exposures values (?, ?, ?, ?)"));
    tile.prepare(QStringLiteral("insert into tiles values (?, ?, ?, ?)"));
    mask.prepare(QStringLiteral("insert into masks values (?)"));
    maskTile.prepare(QStringLiteral("insert into masktiles values (?, ?, ?, ?)"));
    auto exec = [&](QSqlQuery &query, std::initializer_list<QVariant> values) {
        for (const QVariant &v : values)
            query.addBindValue(v);
        if (query.exec())
            return true;
        failure = sqlError(query);
        return false;
    };

    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &l = d.celAt(i);
        const doc::CelProperties &p = l.properties();
        if (!exec(cel, {idValue(l.id()), i, QString::fromStdString(p.name), p.visible, p.locked,
                          double(p.opacity), blendName(p.blend)}))
            return false;
        for (const auto &[id, dr] : l.drawings()) {
            if (!exec(drawing, {idValue(id), idValue(l.id()), QString::fromStdString(dr.name)}))
                return false;
            for (std::size_t position = 0; position < dr.layers.size(); ++position) {
                const doc::Layer &ly = dr.layers[position];
                const doc::LayerProperties &lp = ly.properties;
                const bool perspective = ly.kind == doc::LayerKind::Perspective;
                if (!exec(layer, {idValue(ly.id), idValue(id), int(position), QString::fromStdString(lp.name),
                                  lp.visible, lp.locked, double(lp.opacity), blendName(lp.blend),
                                  perspective ? QStringLiteral("perspective") : QStringLiteral("raster"),
                                  perspective ? gridToJson(lp.perspective) : QStringLiteral("")}))
                    return false;
                // The tiles are packed (the slow part: deflating them) on all the cores, a batch at a time,
                // and written in order.
                const std::vector<doc::TileCoord> coords = ly.image.tileCoords();
                std::vector<doc::TilePtr> tiles;
                tiles.reserve(coords.size());
                for (const doc::TileCoord &c : coords)
                    tiles.push_back(ly.image.tile(c));
                std::vector<QByteArray> packed(tiles.size());
                parallelFor(int(tiles.size()), [&](int k) {
                    if (tiles[std::size_t(k)])
                        packed[std::size_t(k)] = packTile(*tiles[std::size_t(k)]);
                });
                for (std::size_t n = 0; n < coords.size(); ++n)
                    if (tiles[n] && !exec(tile, {idValue(ly.id), coords[n].x, coords[n].y, packed[n]}))
                        return false;
                if (ly.mask) { // its mask, the same way
                    if (!exec(mask, {idValue(ly.id)}))
                        return false;
                    for (const doc::TileCoord &c : ly.mask->tileCoords())
                        if (const doc::TilePtr t = ly.mask->tile(c);
                            t && !exec(maskTile, {idValue(ly.id), c.x, c.y, packTile(*t)}))
                            return false;
                }
            }
        }
        for (const doc::Exposure &e : l.timing().exposures())
            if (!exec(exposure, {idValue(l.id()), e.start, e.length, idValue(e.drawing)}))
                return false;
    }
    if (const auto &track = d.audio(); track && track->data) {
        QSqlQuery audio(db);
        audio.prepare(QStringLiteral("insert into audio values (?, ?, ?, ?, ?, ?, ?, ?)"));
        audio.addBindValue(QString::fromStdString(track->name));
        audio.addBindValue(QString::fromStdString(track->format));
        audio.addBindValue(track->offset);
        audio.addBindValue(double(track->volume));
        audio.addBindValue(track->muted);
        audio.addBindValue(track->duration);
        audio.addBindValue(QByteArray(reinterpret_cast<const char *>(track->data->data()), qsizetype(track->data->size())));
        audio.addBindValue(track->scrub);
        if (!audio.exec()) {
            failure = sqlError(audio);
            return false;
        }
    }
    for (const auto &[frame, name] : d.markers()) {
        QSqlQuery marker(db);
        marker.prepare(QStringLiteral("insert into markers values (?, ?)"));
        marker.addBindValue(frame);
        marker.addBindValue(QString::fromStdString(name));
        if (!marker.exec()) {
            failure = sqlError(marker);
            return false;
        }
    }
    if (d.camera()) {
        QSqlQuery camera(db);
        camera.prepare(QStringLiteral("insert into camera values (?)"));
        camera.addBindValue(cameraToJson(*d.camera()));
        if (!camera.exec()) {
            failure = sqlError(camera);
            return false;
        }
    }
    if (!d.compositing().nodes.empty() || d.compositingView().valid) {
        QSqlQuery graph(db);
        graph.prepare(QStringLiteral("insert into compositing values (?)"));
        graph.addBindValue(graphToJson(d.compositing(), d.compositingView()));
        if (!graph.exec()) {
            failure = sqlError(graph);
            return false;
        }
    }
    if (!db.commit()) {
        failure = db.lastError().text();
        return false;
    }
    return true;
}

std::unique_ptr<doc::Document> readDocument(QSqlDatabase &db, QString &failure)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("pragma application_id")) || !q.next() || q.value(0).toLongLong() != kApplicationId) {
        failure = QObject::tr("This is not a Mothion project.");
        return nullptr;
    }
    if (!q.exec(QStringLiteral("pragma user_version")) || !q.next()) {
        failure = sqlError(q);
        return nullptr;
    }
    const int version = q.value(0).toInt();
    if (version > kProjectFormatVersion) {
        failure = QObject::tr("This project was saved by a newer version of Mothion (format %1).").arg(version);
        return nullptr;
    }
    const bool v1 = version < 2; // cels in "layers", tiles of drawings

    if (!q.exec(v1 ? QStringLiteral("select width, height, fps_numerator, fps_denominator, frame_count, paper_color,"
                                    " next_layer_id, next_drawing_id, 1 from document")
                   : QStringLiteral("select width, height, fps_numerator, fps_denominator, frame_count, paper_color,"
                                    " next_cel_id, next_drawing_id, next_layer_id from document"))
        || !q.next()) {
        failure = QObject::tr("The project has no document settings.");
        return nullptr;
    }
    const int width = q.value(0).toInt(), height = q.value(1).toInt();
    const doc::FrameRate rate{q.value(2).toInt(), q.value(3).toInt()};
    if (width <= 0 || height <= 0 || rate.numerator <= 0 || rate.denominator <= 0) {
        failure = QObject::tr("The project's canvas size or frame rate is not valid.");
        return nullptr;
    }
    auto d = std::make_unique<doc::Document>(width, height, rate);
    d->setFrameCount(q.value(4).toInt());
    d->setPaperColor(doc::Pixel(q.value(5).toLongLong()));
    const doc::CelId nextCel = idFrom(q.value(6));
    const doc::DrawingId nextDrawing = idFrom(q.value(7));
    const doc::LayerId nextLayer = idFrom(q.value(8));

    if (!q.exec(v1 ? QStringLiteral("select id, name, visible, locked, opacity, blend from layers order by position")
                   : QStringLiteral("select id, name, visible, locked, opacity, blend from cels order by position"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        doc::CelProperties p;
        p.name = q.value(1).toString().toStdString();
        p.visible = q.value(2).toBool();
        p.locked = q.value(3).toBool();
        p.opacity = std::clamp(q.value(4).toFloat(), 0.0f, 1.0f);
        p.blend = blendFromName(q.value(5).toString());
        d->insertCel(d->celCount(), std::make_unique<doc::Cel>(idFrom(q.value(0)), std::move(p)));
    }

    std::map<doc::DrawingId, doc::Drawing *> drawings;
    if (!q.exec(v1 ? QStringLiteral("select id, layer, name from drawings order by id")
                   : QStringLiteral("select id, cel, name from drawings order by id"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        doc::Cel *cel = d->cel(idFrom(q.value(1)));
        const doc::DrawingId id = idFrom(q.value(0));
        if (!cel || id == doc::kNoId || drawings.count(id))
            continue;
        drawings[id] = &cel->addDrawing({id, q.value(2).toString().toStdString(), {}});
    }

    // Tiles go to layers; a format 1 drawing gets one layer for its tiles,
    // with ids above every id the file uses (given out once all is read).
    std::map<doc::LayerId, doc::Layer *> layers;
    std::map<doc::DrawingId, doc::Layer *> v1Layers;
    if (v1) {
        for (auto &[id, drawing] : drawings)
            v1Layers[id] = &drawing->layers.emplace_back(doc::Layer{doc::kNoId, {"Layer 1"}, {}});
    } else {
        const bool kinds = version >= 3; // before, all raster
        QString columns = QStringLiteral("id, drawing, name, visible, locked, opacity, blend");
        if (kinds)
            columns += QStringLiteral(", kind, perspective");
        if (!q.exec(QStringLiteral("select %1 from layers order by drawing, position").arg(columns))) {
            failure = sqlError(q);
            return nullptr;
        }
        while (q.next()) {
            const auto it = drawings.find(idFrom(q.value(1)));
            const doc::LayerId id = idFrom(q.value(0));
            if (it == drawings.end() || id == doc::kNoId || layers.count(id))
                continue;
            doc::LayerProperties p;
            p.name = q.value(2).toString().toStdString();
            p.visible = q.value(3).toBool();
            p.locked = q.value(4).toBool();
            p.opacity = std::clamp(q.value(5).toFloat(), 0.0f, 1.0f);
            p.blend = blendFromName(q.value(6).toString());
            doc::LayerKind kind = doc::LayerKind::Raster;
            if (kinds && q.value(7).toString() == QLatin1String("perspective")) {
                kind = doc::LayerKind::Perspective;
                p.perspective = gridFromJson(q.value(8).toString());
            }
            layers[id] = &it->second->layers.emplace_back(doc::Layer{id, std::move(p), {}, kind});
        }
        // Pointers into the layer vectors: taken again now that they are complete.
        layers.clear();
        for (auto &[id, drawing] : drawings)
            for (doc::Layer &layer : drawing->layers)
                layers[layer.id] = &layer;
    }

    if (!q.exec(v1 ? QStringLiteral("select drawing, x, y, pixels from tiles")
                   : QStringLiteral("select layer, x, y, pixels from tiles"))) {
        failure = sqlError(q);
        return nullptr;
    }
    // The tiles are unpacked (the slow part: inflating them) on all the cores, a batch of them at a time.
    struct PendingTile
    {
        doc::Layer *layer = nullptr;
        int x = 0, y = 0;
        QByteArray blob;
        std::shared_ptr<doc::Tile> tile;
    };
    const bool filtered = version >= 4;
    std::vector<PendingTile> batch;
    constexpr std::size_t kBatch = 2048;
    auto flush = [&]() {
        parallelFor(int(batch.size()), [&](int i) { batch[std::size_t(i)].tile = unpackTile(batch[std::size_t(i)].blob, filtered); });
        for (PendingTile &t : batch) {
            if (!t.tile) {
                failure = QObject::tr("The project's pixels are damaged.");
                return false;
            }
            t.layer->image.setTile({t.x, t.y}, std::move(t.tile));
        }
        batch.clear();
        return true;
    };
    while (q.next()) {
        doc::Layer *layer = nullptr;
        if (v1) {
            const auto it = v1Layers.find(idFrom(q.value(0)));
            layer = it == v1Layers.end() ? nullptr : it->second;
        } else {
            const auto it = layers.find(idFrom(q.value(0)));
            layer = it == layers.end() ? nullptr : it->second;
        }
        if (!layer)
            continue;
        batch.push_back({layer, q.value(1).toInt(), q.value(2).toInt(), q.value(3).toByteArray(), nullptr});
        if (batch.size() >= kBatch && !flush())
            return nullptr;
    }
    if (!flush())
        return nullptr;

    if (version >= 9) { // the masks
        if (!q.exec(QStringLiteral("select layer from masks"))) {
            failure = sqlError(q);
            return nullptr;
        }
        while (q.next()) {
            const auto it = layers.find(idFrom(q.value(0)));
            if (it != layers.end() && it->second->kind == doc::LayerKind::Raster)
                it->second->mask.emplace();
        }
        if (!q.exec(QStringLiteral("select layer, x, y, pixels from masktiles"))) {
            failure = sqlError(q);
            return nullptr;
        }
        while (q.next()) {
            const auto it = layers.find(idFrom(q.value(0)));
            if (it == layers.end() || !it->second->mask)
                continue;
            std::shared_ptr<doc::Tile> tile = unpackTile(q.value(3).toByteArray(), true);
            if (!tile) {
                failure = QObject::tr("The project's pixels are damaged.");
                return nullptr;
            }
            it->second->mask->setTile({q.value(1).toInt(), q.value(2).toInt()}, std::move(tile));
        }
    }

    if (!q.exec(v1 ? QStringLiteral("select layer, start, length, drawing from exposures order by layer, start")
                   : QStringLiteral("select cel, start, length, drawing from exposures order by cel, start"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        doc::Cel *cel = d->cel(idFrom(q.value(0)));
        const doc::DrawingId drawing = idFrom(q.value(3));
        const int start = q.value(1).toInt(), length = q.value(2).toInt();
        if (cel && cel->drawing(drawing) && start >= 0 && length >= 1)
            cel->timing().setExposure(start, length, drawing);
    }

    if (version >= 5 && q.exec(version >= 6 ? QStringLiteral("select name, format, offset, volume, muted, duration, data, scrub from audio")
                                            : QStringLiteral("select name, format, offset, volume, muted, duration, data, 1 from audio"))
        && q.next()) {
        doc::AudioTrack track;
        track.name = q.value(0).toString().toStdString();
        track.format = q.value(1).toString().toStdString();
        track.offset = q.value(2).toInt();
        track.volume = std::clamp(q.value(3).toFloat(), 0.0f, 1.0f);
        track.muted = q.value(4).toBool();
        track.duration = std::max(0.0, q.value(5).toDouble());
        const QByteArray bytes = q.value(6).toByteArray();
        track.data = std::make_shared<const std::vector<unsigned char>>(bytes.cbegin(), bytes.cend());
        track.scrub = q.value(7).toBool();
        d->setAudio(std::move(track));
    }

    if (version >= 10 && q.exec(QStringLiteral("select frame, name from markers"))) {
        std::map<int, std::string> markers;
        while (q.next())
            if (q.value(0).toInt() >= 0)
                markers[q.value(0).toInt()] = q.value(1).toString().toStdString();
        d->setMarkers(std::move(markers));
    }

    if (version >= 8 && q.exec(QStringLiteral("select data from camera")) && q.next())
        d->setCamera(cameraFromJson(q.value(0).toString()));

    if (version >= 7 && q.exec(QStringLiteral("select graph from compositing")) && q.next())
    {
        d->setCompositing(graphFromJson(q.value(0).toString()));
        d->setCompositingView(viewFromJson(q.value(0).toString()));
    }

    d->restoreIdCounters(nextCel, nextDrawing, nextLayer);
    for (auto &[id, layer] : v1Layers)
        layer->id = d->allocateLayerId();
    return d;
}

std::filesystem::path fsPath(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

} // namespace

bool saveProject(const doc::Document &document, const QString &path, QString *error)
{
    const QString temporary = path + QStringLiteral(".saving");
    QFile::remove(temporary);
    QString failure;
    {
        Connection connection;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection.name());
            db.setDatabaseName(temporary);
            if (!db.open())
                failure = db.lastError().text();
            else
                writeDocument(db, document, failure);
            db.close();
        }
    }
    if (failure.isEmpty()) {
        // Atomic on POSIX; on Windows a replacing MoveFileEx.
        std::error_code ec;
        std::filesystem::rename(fsPath(temporary), fsPath(path), ec);
        if (ec)
            failure = QString::fromStdString(ec.message());
    }
    if (!failure.isEmpty()) {
        QFile::remove(temporary);
        if (error)
            *error = QObject::tr("Cannot save %1: %2").arg(QFileInfo(path).fileName(), failure);
        return false;
    }
    return true;
}

std::unique_ptr<doc::Document> loadProject(const QString &path, QString *error)
{
    auto fail = [&](const QString &why) -> std::unique_ptr<doc::Document> {
        if (error)
            *error = QObject::tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), why);
        return nullptr;
    };
    if (!QFileInfo(path).isFile()) // SQLite would create it
        return fail(QObject::tr("the file does not exist."));

    std::unique_ptr<doc::Document> document;
    QString failure;
    {
        Connection connection;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection.name());
            db.setDatabaseName(path);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (!db.open())
                failure = db.lastError().text();
            else
                document = readDocument(db, failure);
            db.close();
        }
    }
    if (!document)
        return fail(failure);
    return document;
}

} // namespace mothion::io
