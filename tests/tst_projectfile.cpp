// Tests for project files (.moth), replacing the controller's document
// and exporting frames as PNG/JPEG.

#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"
#include "io/ProjectThumbnail.h"

#include <QElapsedTimer>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <algorithm>

using namespace mothion;

namespace {

constexpr doc::Pixel kRed = 0xffff0000;
constexpr doc::Pixel kHalfBlue = 0x80000080; // premultiplied 50 % blue

// Two cels, three drawings (one of two layers), holds and gaps, tiles at
// negative coordinates.
std::unique_ptr<doc::Document> sampleDocument()
{
    auto d = std::make_unique<doc::Document>(320, 240, doc::FrameRate{24000, 1001});
    d->setFrameCount(48);
    d->setPaperColor(0xfff0e8d0);

    const doc::CelId bottomId = d->allocateCelId();
    doc::Cel &bottom = d->insertCel(0, std::make_unique<doc::Cel>(bottomId, doc::CelProperties{"Background"}));
    doc::Drawing &bg = bottom.addDrawing({d->allocateDrawingId(), "1", {{d->allocateLayerId(), {"Paint"}, {}}}});
    bg.layers[0].image.setPixel(10, 20, kRed);
    bg.layers[0].image.setPixel(-70, -3, kHalfBlue);
    bottom.timing().setExposure(0, 48, bg.id);

    doc::CelProperties top{"Ink è ✓", false, true, 0.5f, doc::BlendMode::Multiply};
    doc::Cel &ink = d->insertCel(1, std::make_unique<doc::Cel>(d->allocateCelId(), top));
    doc::Drawing &a = ink.addDrawing({d->allocateDrawingId(), "A", {}});
    a.layers.push_back({d->allocateLayerId(), {"Line"}, {}});
    a.layers.push_back({d->allocateLayerId(), {"Color ✓", false, true, 0.25f, doc::BlendMode::Screen}, {}});
    a.layers[0].image.setPixel(100, 100, kHalfBlue);
    a.layers[0].mask.emplace().setPixel(100, 100, 0xff000000); // a mask with a black pixel
    a.layers[1].mask.emplace(); // and one with nothing in it
    a.layers[1].image.setPixel(-1, 300, kRed);
    doc::Drawing &b = ink.addDrawing({d->allocateDrawingId(), "B", {{d->allocateLayerId(), {"Line"}, {}}}});
    doc::LayerProperties grid{"Perspective ✓"};
    grid.perspective = perspective::defaultGrid(320, 240);
    grid.perspective.orientation = perspective::fromAngles(-12.5, 71.0, 3.0);
    grid.perspective.position = {15.0, -30.0, 400.0};
    grid.perspective.fov = 45.0;
    grid.perspective.fisheye = 0.6;
    grid.perspective.cells = 5;
    grid.perspective.color = 0x80ff8000;
    grid.perspective.horizon = false;
    b.layers.push_back({d->allocateLayerId(), grid, {}, doc::LayerKind::Perspective});
    b.layers[0].image.setPixel(200, 150, kRed);
    ink.timing().setExposure(0, 3, a.id);
    ink.timing().setExposure(5, 2, b.id);
    ink.timing().setExposure(7, 4, a.id);
    doc::AudioTrack song;
    song.name = "Canção ✓";
    song.format = "wav";
    song.data = std::make_shared<const std::vector<unsigned char>>(std::vector<unsigned char>{'R', 'I', 'F', 'F', 0, 1, 2, 255});
    song.offset = -12;
    song.volume = 0.4f;
    song.muted = true;
    song.scrub = false;
    song.duration = 187.25;
    d->setAudio(song);
    doc::Camera camera;
    camera.visible = false;
    camera.setKey(0, {160, 120, 1, 0}, doc::Easing::hold());
    camera.setKey(12, {200.5, 90.25, 2.5, -45}, doc::Easing::bezier(0.42, 0, 0.58, 1));
    camera.setKey(30, {160, 120, 1, 720}, doc::Easing::linear());
    d->setCamera(camera);
    doc::GraphData graph;
    graph.nodes.push_back({1, "frame", -330.5, 12, {}});
    graph.nodes.push_back({4, "blur", 20, -7.25, {{"radius", 17}}});
    graph.nodes.push_back({7, "output", 400, 0, {}});
    graph.nodes.push_back({9, "sensornoise", 3, 4, {{"animate", 1}, {"luminance", 20}}}); // toggles come back
    graph.nodes.push_back({8, "mask", 1, 2, {}, R"([{"t":"rect","x":1,"y":2,"w":3,"h":4}])"});
    graph.links.push_back({1, 0, 4, 0});
    graph.links.push_back({4, 0, 7, 0});
    graph.nextId = 10;
    d->setCompositing(graph);
    d->setCompositingView({true, 1.5, -120.25, 40});
    d->allocateCelId(); // ids used by a cel and a layer since deleted: must not come back
    d->allocateLayerId();
    return d;
}

void compareDocuments(const doc::Document &x, const doc::Document &y)
{
    QCOMPARE(x.width(), y.width());
    QCOMPARE(x.height(), y.height());
    QVERIFY(x.frameRate() == y.frameRate());
    QCOMPARE(x.frameCount(), y.frameCount());
    QCOMPARE(x.paperColor(), y.paperColor());
    QCOMPARE(x.nextCelId(), y.nextCelId());
    QCOMPARE(x.nextDrawingId(), y.nextDrawingId());
    QCOMPARE(x.nextLayerId(), y.nextLayerId());
    QVERIFY(x.compositing() == y.compositing());
    QVERIFY(x.camera() == y.camera());
    QVERIFY(x.compositingView() == y.compositingView());
    QCOMPARE(x.audio().has_value(), y.audio().has_value());
    if (x.audio() && y.audio()) {
        const doc::AudioTrack &ax = *x.audio(), &ay = *y.audio();
        QCOMPARE(ay.name, ax.name);
        QCOMPARE(ay.format, ax.format);
        QCOMPARE(ay.offset, ax.offset);
        QCOMPARE(ay.volume, ax.volume);
        QCOMPARE(ay.muted, ax.muted);
        QCOMPARE(ay.scrub, ax.scrub);
        QCOMPARE(ay.duration, ax.duration);
        QVERIFY(ax.data && ay.data && *ax.data == *ay.data); // the sound, byte for byte
    }
    QCOMPARE(x.celCount(), y.celCount());
    for (int i = 0; i < x.celCount(); ++i) {
        const doc::Cel &lx = x.celAt(i), &ly = y.celAt(i);
        QCOMPARE(lx.id(), ly.id());
        QVERIFY(lx.properties() == ly.properties());
        QVERIFY(lx.timing() == ly.timing());
        QCOMPARE(lx.drawings().size(), ly.drawings().size());
        for (const auto &[id, dx] : lx.drawings()) {
            const doc::Drawing *dy = ly.drawing(id);
            QVERIFY(dy);
            QCOMPARE(dy->name, dx.name);
            QCOMPARE(dy->layers.size(), dx.layers.size());
            for (std::size_t k = 0; k < dx.layers.size(); ++k) {
                const doc::Layer &kx = dx.layers[k], &ky = dy->layers[k];
                QCOMPARE(ky.id, kx.id);
                QVERIFY(ky.kind == kx.kind);
                QVERIFY(ky.properties == kx.properties);
                QCOMPARE(ky.mask.has_value(), kx.mask.has_value());
                if (kx.mask) {
                    auto mx = kx.mask->tileCoords(), my = ky.mask->tileCoords();
                    std::sort(mx.begin(), mx.end());
                    std::sort(my.begin(), my.end());
                    QVERIFY(mx == my);
                    for (const doc::TileCoord &c : mx)
                        QVERIFY(kx.mask->tile(c)->pixels == ky.mask->tile(c)->pixels);
                }
                auto cx = kx.image.tileCoords(), cy = ky.image.tileCoords();
                std::sort(cx.begin(), cx.end());
                std::sort(cy.begin(), cy.end());
                QVERIFY(cx == cy);
                for (const doc::TileCoord &c : cx)
                    QVERIFY(kx.image.tile(c)->pixels == ky.image.tile(c)->pixels);
            }
        }
    }
}

} // namespace

class TestProjectFile : public QObject
{
    Q_OBJECT

private slots:
    // Many tiles of noise and gradients: packed on all the cores, and filtered, they come back exactly (more
    // tiles than one batch of the reader).
    void manyTilesSurviveTheFilterAndTheCores()
    {
        auto d = std::make_unique<doc::Document>(4096, 4096, doc::FrameRate{24, 1});
        const doc::CelId id = d->allocateCelId();
        doc::Cel &cel = d->insertCel(0, std::make_unique<doc::Cel>(id, doc::CelProperties{"Noise"}));
        doc::Drawing &dr = cel.addDrawing({d->allocateDrawingId(), "1", {{d->allocateLayerId(), {"Paint"}, {}}}});
        cel.timing().setExposure(0, 1, dr.id);
        quint32 state = 12345;
        auto next = [&state] {
            state = state * 1664525u + 1013904223u;
            return state;
        };
        for (int ty = 0; ty < 48; ++ty)
            for (int tx = 0; tx < 48; ++tx) { // 2304 tiles
                auto tile = std::make_shared<doc::Tile>();
                for (int i = 0; i < doc::kTilePixels; ++i) {
                    const int x = i % doc::kTileSize, y = i / doc::kTileSize;
                    tile->pixels[std::size_t(i)] = (tx + ty) % 3 == 0 ? next() // noise
                                                   : (tx + ty) % 3 == 1 ? 0xff000000u | (x * 4) << 16 | (y * 4) << 8 | 77 // gradient
                                                                        : 0;                                              // blank
                }
                dr.layers[0].image.setTile({tx, ty}, std::move(tile));
            }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("noise.moth"));
        QString error;
        QVERIFY2(io::saveProject(*d, path, &error), qPrintable(error));
        const auto loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        compareDocuments(*d, *loaded);
    }

    // A project of format 3, its tiles stored as they are, still opens.
    void projectsOfFormat3StillOpen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto original = sampleDocument();
        original->setAudio(std::nullopt); // the audio table came with format 5
        original->setCompositing({});     // and the compositing one with format 7
        original->setCompositingView({});
        original->setCamera(std::nullopt); // and the camera with format 8
        for (int i = 0; i < original->celCount(); ++i) // and the masks with format 9
            for (const auto &[id, drawing] : original->celAt(i).drawings())
                for (doc::Layer &layer : original->celAt(i).drawing(id)->layers)
                    layer.mask.reset();
        const QString path = dir.filePath(QStringLiteral("old.moth"));
        QString error;
        QVERIFY2(io::saveProject(*original, path, &error), qPrintable(error));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("to-format-3"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("begin")));
            q.prepare(QStringLiteral("update tiles set pixels = ? where layer = ? and x = ? and y = ?"));
            for (int i = 0; i < original->celCount(); ++i)
                for (const auto &[id, drawing] : original->celAt(i).drawings())
                    for (const doc::Layer &layer : drawing.layers)
                        for (const doc::TileCoord &c : layer.image.tileCoords()) {
                            const doc::TilePtr tile = layer.image.tile(c);
                            QByteArray raw(doc::kTilePixels * 4, Qt::Uninitialized);
                            qToLittleEndian<doc::Pixel>(tile->pixels.data(), doc::kTilePixels, raw.data());
                            q.addBindValue(qCompress(raw));
                            q.addBindValue(qlonglong(layer.id));
                            q.addBindValue(c.x);
                            q.addBindValue(c.y);
                            QVERIFY(q.exec());
                        }
            QVERIFY(q.exec(QStringLiteral("commit")));
            QVERIFY(q.exec(QStringLiteral("pragma user_version = 3")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("to-format-3"));
        const auto loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        compareDocuments(*original, *loaded);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("scene.moth"));
        const auto original = sampleDocument();
        QString error;
        QVERIFY2(io::saveProject(*original, path, &error), qPrintable(error));
        QVERIFY(!QFile::exists(path + QStringLiteral(".saving")));

        const auto loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        compareDocuments(*original, *loaded);
    }

    void everyBlendModeSurvivesTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("blend.moth"));
        const auto original = sampleDocument();
        doc::Drawing &drawing = *original->celAt(0).drawing(original->celAt(0).drawings().begin()->first);
        for (int i = int(doc::BlendMode::Normal); i <= int(doc::BlendMode::Exclusion); ++i) {
            drawing.layers.push_back({original->allocateLayerId(), {"Mode", true, false, 1.0f, doc::BlendMode(i)}, {}});
        }
        QString error;
        QVERIFY2(io::saveProject(*original, path, &error), qPrintable(error));
        const auto loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        compareDocuments(*original, *loaded);
    }

    // The preview for file managers, found the way the Windows thumbnail
    // provider does: in the first bytes of the file, without SQLite.
    void savedProjectsCarryAThumbnail()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("scene.moth"));
        const auto original = sampleDocument();
        QString error;
        QVERIFY2(io::saveProject(*original, path, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray start = file.read(qint64(io::kThumbnailSearchBytes));
        const auto span =
            io::findThumbnail(reinterpret_cast<const unsigned char *>(start.constData()), std::size_t(start.size()));
        QVERIFY(span);
        const QImage thumbnail = QImage::fromData(start.mid(qsizetype(span->offset), qsizetype(span->size)), "PNG");
        QVERIFY(!thumbnail.isNull());
        QCOMPARE(thumbnail.size(), QSize(256, 192)); // the document's 4:3, scaled to fit
        QCOMPARE(thumbnail.pixel(2, 2), QRgb(original->paperColor())); // the paper
        // The icon in the bottom-right corner: something other than the paper there.
        bool icon = false;
        for (int y = 140; y < 190 && !icon; ++y)
            for (int x = 200; x < 254 && !icon; ++x)
                icon = thumbnail.pixel(x, y) != QRgb(original->paperColor());
        QVERIFY(icon);
        // The project itself still reads back the same.
        const auto loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        compareDocuments(*original, *loaded);

        // Cut short or without the magic: nothing.
        QVERIFY(!io::findThumbnail(reinterpret_cast<const unsigned char *>(start.constData()), span->offset + 20));
        const QByteArray png = start.mid(qsizetype(span->offset), qsizetype(span->size));
        QVERIFY(!io::findThumbnail(reinterpret_cast<const unsigned char *>(png.constData()), std::size_t(png.size())));
    }

    void saveReplacesAnExistingFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("scene.moth"));
        QString error;
        QVERIFY(io::saveProject(*sampleDocument(), path, &error));
        doc::Document small(64, 64);
        QVERIFY2(io::saveProject(small, path, &error), qPrintable(error));
        const auto loaded = io::loadProject(path, &error);
        QVERIFY(loaded);
        QCOMPARE(loaded->width(), 64);
        QCOMPARE(loaded->celCount(), 0);
    }

    void loadedIdsAreNeverReused()
    {
        doc::Document d(10, 10);
        d.insertCel(0, std::make_unique<doc::Cel>(7, doc::CelProperties{"L"}));
        d.celAt(0).addDrawing({12, "1", {{30, {"Line"}, {}}}});
        d.restoreIdCounters(1, 1, 1); // a file claiming counters below the ids in use
        QCOMPARE(d.allocateCelId(), doc::CelId(8));
        QCOMPARE(d.allocateDrawingId(), doc::DrawingId(13));
        QCOMPARE(d.allocateLayerId(), doc::LayerId(31));
    }

    // A file of format 1, before drawings had layers: each drawing opens with
    // one layer holding its pixels, with an id of its own.
    void formatOneOpensWithALayerPerDrawing()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("old.moth"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            const QStringList sql{
                QStringLiteral("pragma application_id = %1").arg(0x4D4F5448),
                QStringLiteral("pragma user_version = 1"),
                QStringLiteral("create table document (width integer, height integer, fps_numerator integer,"
                               " fps_denominator integer, frame_count integer, paper_color integer,"
                               " next_layer_id integer, next_drawing_id integer, saved_by text)"),
                QStringLiteral("create table layers (id integer primary key, position integer, name text,"
                               " visible integer, locked integer, opacity real, blend text)"),
                QStringLiteral("create table drawings (id integer primary key, layer integer, name text)"),
                QStringLiteral("create table exposures (layer integer, start integer, length integer,"
                               " drawing integer)"),
                QStringLiteral("create table tiles (drawing integer, x integer, y integer, pixels blob,"
                               " primary key (drawing, x, y))"),
                QStringLiteral("insert into document values (64, 64, 24, 1, 10, %1, 3, 5, '0.1')")
                    .arg(qlonglong(0xffffffff)),
                QStringLiteral("insert into layers values (2, 0, 'Ink', 1, 0, 0.5, 'multiply')"),
                QStringLiteral("insert into drawings values (3, 2, '1')"),
                QStringLiteral("insert into drawings values (4, 2, '2')"),
                QStringLiteral("insert into exposures values (2, 0, 2, 3)"),
                QStringLiteral("insert into exposures values (2, 2, 3, 4)"),
            };
            for (const QString &s : sql)
                QVERIFY2(q.exec(s), qPrintable(q.lastError().text()));
            std::vector<doc::Pixel> pixels(doc::kTilePixels, 0);
            pixels[0] = kRed;
            QByteArray raw(reinterpret_cast<const char *>(pixels.data()), qsizetype(pixels.size() * 4));
            q.prepare(QStringLiteral("insert into tiles values (4, 0, 0, ?)"));
            q.addBindValue(qCompress(raw));
            QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));

        QString error;
        const auto d = io::loadProject(path, &error);
        QVERIFY2(d, qPrintable(error));
        QCOMPARE(d->celCount(), 1);
        const doc::Cel &cel = d->celAt(0);
        QCOMPARE(cel.properties().name, std::string("Ink"));
        QCOMPARE(cel.properties().blend, doc::BlendMode::Multiply);
        QCOMPARE(cel.timing().drawingAt(3), doc::DrawingId(4));
        const doc::Drawing *first = cel.drawing(3), *second = cel.drawing(4);
        QVERIFY(first && second);
        QCOMPARE(first->layers.size(), std::size_t(1));
        QCOMPARE(second->layers.size(), std::size_t(1));
        QVERIFY(first->layers[0].image.isEmpty());
        QCOMPARE(second->layers[0].image.pixel(0, 0), kRed);
        QVERIFY(first->layers[0].id != doc::kNoId);
        QVERIFY(first->layers[0].id != second->layers[0].id);
        QVERIFY(d->nextLayerId() > std::max(first->layers[0].id, second->layers[0].id));
        QCOMPARE(d->nextCelId(), doc::CelId(3));
        QCOMPARE(d->nextDrawingId(), doc::DrawingId(5));
    }

    void missingFileIsAnError()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("nothing.moth"));
        QString error;
        QVERIFY(!io::loadProject(path, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(path)); // not created by opening it
    }

    void otherFilesAreRefused()
    {
        QTemporaryDir dir;
        const QString text = dir.filePath(QStringLiteral("notes.moth"));
        QFile f(text);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a database at all, just some text that is long enough");
        f.close();
        QString error;
        QVERIFY(!io::loadProject(text, &error));
        QVERIFY(!error.isEmpty());

        // An SQLite database that is not a Mothion project.
        const QString other = dir.filePath(QStringLiteral("other.moth"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
            db.setDatabaseName(other);
            QVERIFY(db.open());
            QSqlQuery(db).exec(QStringLiteral("create table t (x)"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("other"));
        error.clear();
        QVERIFY(!io::loadProject(other, &error));
        QVERIFY(error.contains(QStringLiteral("not a Mothion project")));
    }

    void newerFormatIsRefused()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("future.moth"));
        QString error;
        QVERIFY(io::saveProject(doc::Document(8, 8), path, &error));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("future"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery(db).exec(QStringLiteral("pragma user_version = %1").arg(io::kProjectFormatVersion + 1));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("future"));
        QVERIFY(!io::loadProject(path, &error));
        QVERIFY(error.contains(QStringLiteral("newer version")));
    }

    // --- DocumentController ----------------------------------------------------

    void replacingTheDocumentResetsState()
    {
        document::DocumentController controller;
        controller.setCurrentFrame(5);
        controller.selectAll();
        controller.importImage(QImage(4, 4, QImage::Format_ARGB32_Premultiplied), QStringLiteral("pic"));
        QVERIFY(controller.isModified());
        QVERIFY(controller.canUndo());

        QSignalSpy replaced(&controller, &document::DocumentController::documentReplaced);
        controller.replaceDocument(sampleDocument());
        QCOMPARE(replaced.count(), 1);
        QCOMPARE(controller.document().width(), 320);
        QVERIFY(!controller.isModified());
        QVERIFY(!controller.canUndo());
        QCOMPARE(controller.currentFrame(), 0);
        QVERIFY(!controller.hasSelection());
        QCOMPARE(controller.currentCel(), controller.document().celAt(1).id()); // the top one

        controller.newDocument();
        QCOMPARE(controller.document().width(), 1920);
        QCOMPARE(controller.document().celCount(), 1);
        QVERIFY(!controller.isModified());
    }

    void markSavedClearsModified()
    {
        document::DocumentController controller;
        QVERIFY(!controller.isModified());
        controller.importImage(QImage(4, 4, QImage::Format_ARGB32_Premultiplied), QStringLiteral("pic"));
        QVERIFY(controller.isModified());
        controller.markSaved();
        QVERIFY(!controller.isModified());
        controller.undo();
        QVERIFY(controller.isModified());
        controller.redo();
        QVERIFY(!controller.isModified());
    }

    // --- export ----------------------------------------------------------------

    void renderFrameFollowsTiming()
    {
        const auto d = sampleDocument();
        // Frame 0: drawing A; the ink layer is hidden, so only the background shows.
        QImage frame = io::renderFrame(*d, 0, false);
        QCOMPARE(frame.size(), QSize(320, 240));
        QCOMPARE(frame.pixel(10, 20), QRgb(kRed));
        QCOMPARE(frame.pixel(0, 0), QRgb(d->paperColor()));

        QImage transparent = io::renderFrame(*d, 0, true);
        QCOMPARE(qAlpha(transparent.pixel(0, 0)), 0);
        QCOMPARE(transparent.pixel(10, 20), QRgb(kRed));
    }

    void sequenceFileNames()
    {
        QCOMPARE(QFileInfo(io::sequenceFileName(QStringLiteral("/tmp/walk.png"), 0, 23)).fileName(),
                 QStringLiteral("walk_0001.png"));
        QCOMPARE(QFileInfo(io::sequenceFileName(QStringLiteral("/tmp/walk.png"), 23, 23)).fileName(),
                 QStringLiteral("walk_0024.png"));
        QCOMPARE(QFileInfo(io::sequenceFileName(QStringLiteral("/tmp/run.v2.jpg"), 4, 99999)).fileName(),
                 QStringLiteral("run.v2_000005.jpg"));
        QVERIFY(io::formatForFile(QStringLiteral("a.JPEG")) == io::ImageFormat::Jpeg);
        QVERIFY(!io::formatForFile(QStringLiteral("a.gif")));
    }

    void exportPngAndJpeg()
    {
        QTemporaryDir dir;
        const auto d = sampleDocument();
        QString error;

        const QString png = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY2(io::exportFrame(*d, 0, png, {io::ImageFormat::Png, true, 90}, &error), qPrintable(error));
        const QImage p(png);
        QCOMPARE(p.size(), QSize(320, 240));
        QVERIFY(p.hasAlphaChannel());
        QCOMPARE(qAlpha(p.pixel(0, 0)), 0);
        QCOMPARE(p.pixel(10, 20), QRgb(kRed));

        const QString jpg = dir.filePath(QStringLiteral("frame.jpg"));
        QVERIFY2(io::exportFrame(*d, 0, jpg, {io::ImageFormat::Jpeg, true, 95}, &error), qPrintable(error));
        const QImage j(jpg);
        QCOMPARE(j.size(), QSize(320, 240));
        QCOMPARE(qAlpha(j.pixel(0, 0)), 255); // no transparency in JPEG: the paper is there
        QVERIFY(qAbs(qRed(j.pixel(0, 0)) - qRed(d->paperColor())) < 6);
    }

    // The document's compositing network can be applied to the exported images, when it is asked for.
    void exportCanApplyTheCompositing()
    {
        QTemporaryDir dir;
        const auto d = sampleDocument(); // frame -> blur -> output
        QString error;
        auto read = [&](const QString &name, const io::ExportOptions &options) {
            const QString path = dir.filePath(name);
            return io::exportFrame(*d, 0, path, options, &error) ? QImage(path) : QImage();
        };
        io::ExportOptions plain{io::ImageFormat::Png, true, 90};
        io::ExportOptions composed = plain;
        composed.compositing = true;

        const QImage a = read(QStringLiteral("plain.png"), plain);
        const QImage b = read(QStringLiteral("composed.png"), composed);
        QVERIFY2(!a.isNull() && !b.isNull(), qPrintable(error));
        QCOMPARE(b.size(), a.size());
        QCOMPARE(a.pixel(10, 20), QRgb(kRed));
        QVERIFY(b.pixel(10, 20) != QRgb(kRed)); // blurred
        QVERIFY(b != a);
        QVERIFY(b.hasAlphaChannel()); // transparent stays transparent where the nodes leave nothing
        QCOMPARE(qAlpha(b.pixel(319, 239)), 0);

        // Over the paper (not transparent): the result is opaque, and still not the plain frame.
        io::ExportOptions opaque{io::ImageFormat::Png, false, 90};
        opaque.compositing = true;
        const QImage c = read(QStringLiteral("opaque.png"), opaque);
        QCOMPARE(qAlpha(c.pixel(0, 0)), 255);
        QVERIFY(c.pixel(10, 20) != QRgb(kRed));

        // A sequence, and JPEG, through the same frames.
        const QString seq = dir.filePath(QStringLiteral("walk.png"));
        QVERIFY2(io::exportSequence(*d, 0, 1, seq, composed, {}, &error), qPrintable(error));
        QCOMPARE(QImage(io::sequenceFileName(seq, 0, 1)).pixel(10, 20), b.pixel(10, 20));
        io::ExportOptions jpeg{io::ImageFormat::Jpeg, false, 95};
        jpeg.compositing = true;
        const QImage j = read(QStringLiteral("composed.jpg"), jpeg);
        QVERIFY(qAbs(qRed(j.pixel(10, 20)) - qRed(kRed)) > 8 || qAbs(qGreen(j.pixel(10, 20))) > 8); // blurred too

        // No network in the project: the option changes nothing.
        d->setCompositing({});
        const QImage none = read(QStringLiteral("none.png"), composed);
        QCOMPARE(none.pixel(10, 20), QRgb(kRed));
    }

    // The picture of a frame is the sheet seen through the project's camera, when it has one and it is on.
    void outputIsSeenThroughTheCamera()
    {
        doc::Document d(100, 100);
        d.setPaperColor(0xffffffff);
        d.setFrameCount(10);
        doc::Cel &cel = d.insertCel(0, std::make_unique<doc::Cel>(d.allocateCelId(), doc::CelProperties{"Art"}));
        doc::Drawing &drawing = cel.addDrawing({d.allocateDrawingId(), "1", {{d.allocateLayerId(), {"Paint"}, {}}}});
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 50; ++x)
                drawing.layers[0].image.setPixel(x, y, kRed); // the left half
        cel.timing().setExposure(0, 10, drawing.id);
        auto pixel = [](const QImage &image, int x, int y) { return QColor::fromRgba(image.pixel(x, y)); };
        auto isRed = [&](const QImage &image, int x, int y) { return pixel(image, x, y).red() > 200 && pixel(image, x, y).green() < 60; };
        auto isPaper = [&](const QImage &image, int x, int y) { return pixel(image, x, y).green() > 240; };

        // No camera: the sheet itself.
        QCOMPARE(io::renderOutputFrame(d, 0, false), io::renderFrame(d, 0, false));

        doc::Camera camera;
        camera.setKey(0, doc::CameraPose::wholeSheet(100, 100), doc::Easing::linear());
        camera.setKey(9, {25, 50, 2, 0}); // twice as close, looking at the left half
        d.setCamera(camera);
        QCOMPARE(io::renderOutputFrame(d, 0, false), io::renderFrame(d, 0, false)); // looking at all of it
        const QImage close = io::renderOutputFrame(d, 9, false);
        QCOMPARE(close.size(), QSize(100, 100));
        QVERIFY(isRed(close, 50, 50) && isRed(close, 5, 5) && isRed(close, 60, 90)); // all in the red half
        const QImage midway = io::renderOutputFrame(d, 5, false);
        QVERIFY(midway != io::renderFrame(d, 5, false)); // the frames in between are moved too
        QVERIFY(isRed(midway, 10, 50) && isPaper(midway, 95, 50)); // the red half's edge, nearer the middle

        // A camera that is off shows the whole sheet.
        camera.visible = false;
        d.setCamera(camera);
        QCOMPARE(io::renderOutputFrame(d, 9, false), io::renderFrame(d, 9, false));

        // Past the sheet's edge: the paper, or transparency.
        doc::Camera away;
        away.setKey(0, {-50, 50, 1, 0});
        d.setCamera(away);
        const QImage empty = io::renderOutputFrame(d, 0, false);
        QVERIFY(isPaper(empty, 50, 50) && isPaper(empty, 5, 5));
        QCOMPARE(qAlpha(io::renderOutputFrame(d, 0, true).pixel(50, 50)), 0);

        // Turned: the camera a quarter clockwise puts the left half of the sheet at the bottom of the picture.
        doc::Camera turned;
        turned.setKey(0, {50, 50, 1, 90});
        d.setCamera(turned);
        const QImage quarter = io::renderOutputFrame(d, 0, false);
        QVERIFY(isRed(quarter, 50, 90) && isPaper(quarter, 50, 10));

        // Far out: the whole sheet small in the middle, the paper around it.
        doc::Camera far;
        far.setKey(0, {50, 50, 0.5, 0});
        d.setCamera(far);
        const QImage small = io::renderOutputFrame(d, 0, false);
        QVERIFY(isRed(small, 35, 50) && isPaper(small, 65, 50) && isPaper(small, 5, 50));

        // The exports show the same picture.
        QTemporaryDir dir;
        QString error;
        const QString path = dir.filePath(QStringLiteral("camera.png"));
        QVERIFY2(io::exportFrame(d, 0, path, {io::ImageFormat::Png, false, 90}, &error), qPrintable(error));
        const QImage written(path);
        QVERIFY(isRed(written, 35, 50) && isPaper(written, 65, 50));
    }

    void exportSequenceWritesEveryFrame()
    {
        QTemporaryDir dir;
        const auto d = sampleDocument();
        const QString path = dir.filePath(QStringLiteral("walk.png"));
        QString error;
        QList<int> steps;
        QVERIFY2(io::exportSequence(*d, 2, 5, path, {}, [&](int done, int total) {
                     steps << done;
                     return total == 4;
                 }, &error),
                 qPrintable(error));
        QCOMPARE(steps, (QList<int>{1, 2, 3, 4}));
        for (int f = 2; f <= 5; ++f)
            QVERIFY(QFile::exists(io::sequenceFileName(path, f, 5)));
        QVERIFY(!QFile::exists(io::sequenceFileName(path, 6, 5)));

        // Stopping from the progress callback leaves the rest unwritten, without error.
        const QString stopped = dir.filePath(QStringLiteral("stop.png"));
        QVERIFY(io::exportSequence(*d, 0, 9, stopped, {}, [](int done, int) { return done < 2; }, &error));
        QVERIFY(QFile::exists(io::sequenceFileName(stopped, 1, 9)));
        QVERIFY(!QFile::exists(io::sequenceFileName(stopped, 2, 9)));
    }
};

QTEST_MAIN(TestProjectFile)
#include "tst_projectfile.moc"
