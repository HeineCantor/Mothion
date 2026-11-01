#include "io/ProjectFile.h"

#include <QCoreApplication>
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
    "create table document (width integer not null, height integer not null,"
    " fps_numerator integer not null, fps_denominator integer not null, frame_count integer not null,"
    " paper_color integer not null, next_layer_id integer not null, next_drawing_id integer not null,"
    " saved_by text)",
    "create table layers (id integer primary key, position integer not null, name text not null,"
    " visible integer not null, locked integer not null, opacity real not null, blend text not null)",
    "create table drawings (id integer primary key, layer integer not null references layers(id), name text not null)",
    "create table exposures (layer integer not null references layers(id), start integer not null,"
    " length integer not null, drawing integer not null references drawings(id))",
    "create table tiles (drawing integer not null references drawings(id), x integer not null,"
    " y integer not null, pixels blob not null, primary key (drawing, x, y))",
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

bool writeDocument(QSqlDatabase &db, const doc::Document &d, QString &failure)
{
    QSqlQuery q(db);
    auto run = [&](const QString &sql) {
        if (q.exec(sql))
            return true;
        failure = sqlError(q);
        return false;
    };
    if (!run(QStringLiteral("pragma application_id = %1").arg(kApplicationId))
        || !run(QStringLiteral("pragma user_version = %1").arg(kProjectFormatVersion)))
        return false;
    if (!db.transaction()) {
        failure = db.lastError().text();
        return false;
    }
    for (const char *statement : kSchema)
        if (!run(QString::fromLatin1(statement)))
            return false;

    q.prepare(QStringLiteral("insert into document values (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const QVariant &v : {QVariant(d.width()), QVariant(d.height()), QVariant(d.frameRate().numerator),
                              QVariant(d.frameRate().denominator), QVariant(d.frameCount()),
                              QVariant(qlonglong(d.paperColor())), idValue(d.nextLayerId()),
                              idValue(d.nextDrawingId()), QVariant(QCoreApplication::applicationVersion())})
        q.addBindValue(v);
    if (!q.exec()) {
        failure = sqlError(q);
        return false;
    }

    QSqlQuery layer(db), drawing(db), exposure(db), tile(db);
    layer.prepare(QStringLiteral("insert into layers values (?, ?, ?, ?, ?, ?, ?)"));
    drawing.prepare(QStringLiteral("insert into drawings values (?, ?, ?)"));
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

    for (int i = 0; i < d.layerCount(); ++i) {
        const doc::Layer &l = d.layerAt(i);
        const doc::LayerProperties &p = l.properties();
        if (!exec(layer, {idValue(l.id()), i, QString::fromStdString(p.name), p.visible, p.locked,
                          double(p.opacity), blendName(p.blend)}))
            return false;
        for (const auto &[id, dr] : l.drawings()) {
            if (!exec(drawing, {idValue(id), idValue(l.id()), QString::fromStdString(dr.name)}))
                return false;
            for (const doc::TileCoord &c : dr.image.tileCoords())
                if (const doc::TilePtr t = dr.image.tile(c))
                    if (!exec(tile, {idValue(id), c.x, c.y, packTile(*t)}))
                        return false;
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
    if (const int version = q.value(0).toInt(); version > kProjectFormatVersion) {
        failure = QObject::tr("This project was saved by a newer version of Mothion (format %1).").arg(version);
        return nullptr;
    }

    if (!q.exec(QStringLiteral("select width, height, fps_numerator, fps_denominator, frame_count, paper_color,"
                               " next_layer_id, next_drawing_id from document"))
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
    const doc::LayerId nextLayer = idFrom(q.value(6));
    const doc::DrawingId nextDrawing = idFrom(q.value(7));

    if (!q.exec(QStringLiteral("select id, name, visible, locked, opacity, blend from layers order by position"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        doc::LayerProperties p;
        p.name = q.value(1).toString().toStdString();
        p.visible = q.value(2).toBool();
        p.locked = q.value(3).toBool();
        p.opacity = std::clamp(q.value(4).toFloat(), 0.0f, 1.0f);
        p.blend = blendFromName(q.value(5).toString());
        d->insertLayer(d->layerCount(), std::make_unique<doc::Layer>(idFrom(q.value(0)), std::move(p)));
    }

    std::map<doc::DrawingId, doc::Drawing *> drawings;
    if (!q.exec(QStringLiteral("select id, layer, name from drawings order by id"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        doc::Layer *layer = d->layer(idFrom(q.value(1)));
        const doc::DrawingId id = idFrom(q.value(0));
        if (!layer || id == doc::kNoId || drawings.count(id))
            continue;
        drawings[id] = &layer->addDrawing({id, q.value(2).toString().toStdString(), {}});
    }

    if (!q.exec(QStringLiteral("select drawing, x, y, pixels from tiles"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        const auto it = drawings.find(idFrom(q.value(0)));
        if (it == drawings.end())
            continue;
        auto tile = unpackTile(q.value(3).toByteArray());
        if (!tile) {
            failure = QObject::tr("The project's pixels are damaged.");
            return nullptr;
        }
        it->second->image.setTile({q.value(1).toInt(), q.value(2).toInt()}, std::move(tile));
    }

    if (!q.exec(QStringLiteral("select layer, start, length, drawing from exposures order by layer, start"))) {
        failure = sqlError(q);
        return nullptr;
    }
    while (q.next()) {
        doc::Layer *layer = d->layer(idFrom(q.value(0)));
        const doc::DrawingId drawing = idFrom(q.value(3));
        const int start = q.value(1).toInt(), length = q.value(2).toInt();
        if (layer && layer->drawing(drawing) && start >= 0 && length >= 1)
            layer->timing().setExposure(start, length, drawing);
    }

    d->restoreIdCounters(nextLayer, nextDrawing);
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
