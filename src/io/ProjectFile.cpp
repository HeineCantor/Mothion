#include "io/ProjectFile.h"

#include "io/FrameExport.h"
#include "io/ProjectThumbnail.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
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
    " opacity real not null, blend text not null)",
    "create table exposures (cel integer not null references cels(id), start integer not null,"
    " length integer not null, drawing integer not null references drawings(id))",
    "create table tiles (layer integer not null references layers(id), x integer not null,"
    " y integer not null, pixels blob not null, primary key (layer, x, y))",
};

// Stored by name, so reordering the enum cannot change old files.
constexpr std::array<std::pair<doc::BlendMode, const char *>, 5> kBlendNames{{
    {doc::BlendMode::Normal, "normal"},
    {doc::BlendMode::Multiply, "multiply"},
    {doc::BlendMode::Screen, "screen"},
    {doc::BlendMode::Overlay, "overlay"},
    {doc::BlendMode::Add, "add"},
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

// Ids are uint64 in the model, signed 64-bit integers in SQLite.
QVariant idValue(std::uint64_t id) { return QVariant(qlonglong(id)); }
std::uint64_t idFrom(const QVariant &v) { return std::uint64_t(v.toLongLong()); }

QByteArray packTile(const doc::Tile &tile)
{
    QByteArray raw(kTileBytes, Qt::Uninitialized);
    qToLittleEndian<doc::Pixel>(tile.pixels.data(), doc::kTilePixels, raw.data());
    return qCompress(raw);
}

std::shared_ptr<doc::Tile> unpackTile(const QByteArray &blob)
{
    const QByteArray raw = qUncompress(blob);
    if (raw.size() != kTileBytes)
        return nullptr;
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

    QSqlQuery cel(db), drawing(db), layer(db), exposure(db), tile(db);
    cel.prepare(QStringLiteral("insert into cels values (?, ?, ?, ?, ?, ?, ?)"));
    drawing.prepare(QStringLiteral("insert into drawings values (?, ?, ?)"));
    layer.prepare(QStringLiteral("insert into layers values (?, ?, ?, ?, ?, ?, ?, ?)"));
    exposure.prepare(QStringLiteral("insert into exposures values (?, ?, ?, ?)"));
    tile.prepare(QStringLiteral("insert into tiles values (?, ?, ?, ?)"));
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
                if (!exec(layer, {idValue(ly.id), idValue(id), int(position), QString::fromStdString(lp.name),
                                  lp.visible, lp.locked, double(lp.opacity), blendName(lp.blend)}))
                    return false;
                for (const doc::TileCoord &c : ly.image.tileCoords())
                    if (const doc::TilePtr t = ly.image.tile(c))
                        if (!exec(tile, {idValue(ly.id), c.x, c.y, packTile(*t)}))
                            return false;
            }
        }
        for (const doc::Exposure &e : l.timing().exposures())
            if (!exec(exposure, {idValue(l.id()), e.start, e.length, idValue(e.drawing)}))
                return false;
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
        if (!q.exec(QStringLiteral("select id, drawing, name, visible, locked, opacity, blend from layers"
                                   " order by drawing, position"))) {
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
            layers[id] = &it->second->layers.emplace_back(doc::Layer{id, std::move(p), {}});
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
        auto tile = unpackTile(q.value(3).toByteArray());
        if (!tile) {
            failure = QObject::tr("The project's pixels are damaged.");
            return nullptr;
        }
        layer->image.setTile({q.value(1).toInt(), q.value(2).toInt()}, std::move(tile));
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
