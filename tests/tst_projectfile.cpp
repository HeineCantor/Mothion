// Tests for project files (.moth), replacing the controller's document
// and exporting frames as PNG/JPEG.

#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

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
    a.layers[1].image.setPixel(-1, 300, kRed);
    doc::Drawing &b = ink.addDrawing({d->allocateDrawingId(), "B", {{d->allocateLayerId(), {"Line"}, {}}}});
    b.layers[0].image.setPixel(200, 150, kRed);
    ink.timing().setExposure(0, 3, a.id);
    ink.timing().setExposure(5, 2, b.id);
    ink.timing().setExposure(7, 4, a.id);
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
                QVERIFY(ky.properties == kx.properties);
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
