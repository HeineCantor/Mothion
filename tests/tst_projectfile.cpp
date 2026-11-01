// Tests for project files (.mothion), replacing the controller's document
// and exporting frames as PNG/JPEG.

#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace mothion;

namespace {

constexpr doc::Pixel kRed = 0xffff0000;
constexpr doc::Pixel kHalfBlue = 0x80000080; // premultiplied 50 % blue

// Two layers, three drawings, holds and gaps, tiles at negative coordinates.
std::unique_ptr<doc::Document> sampleDocument()
{
    auto d = std::make_unique<doc::Document>(320, 240, doc::FrameRate{24000, 1001});
    d->setFrameCount(48);
    d->setPaperColor(0xfff0e8d0);

    const doc::LayerId bottomId = d->allocateLayerId();
    doc::Layer &bottom = d->insertLayer(0, std::make_unique<doc::Layer>(bottomId, doc::LayerProperties{"Background"}));
    doc::Drawing &bg = bottom.addDrawing({d->allocateDrawingId(), "1", {}});
    bg.image.setPixel(10, 20, kRed);
    bg.image.setPixel(-70, -3, kHalfBlue);
    bottom.timing().setExposure(0, 48, bg.id);

    doc::LayerProperties top{"Ink è ✓", false, true, 0.5f, doc::BlendMode::Multiply};
    doc::Layer &ink = d->insertLayer(1, std::make_unique<doc::Layer>(d->allocateLayerId(), top));
    doc::Drawing &a = ink.addDrawing({d->allocateDrawingId(), "A", {}});
    a.image.setPixel(100, 100, kHalfBlue);
    doc::Drawing &b = ink.addDrawing({d->allocateDrawingId(), "B", {}});
    b.image.setPixel(200, 150, kRed);
    ink.timing().setExposure(0, 3, a.id);
    ink.timing().setExposure(5, 2, b.id);
    ink.timing().setExposure(7, 4, a.id);
    d->allocateLayerId(); // an id used by a layer since deleted: must not come back
    return d;
}

void compareDocuments(const doc::Document &x, const doc::Document &y)
{
    QCOMPARE(x.width(), y.width());
    QCOMPARE(x.height(), y.height());
    QVERIFY(x.frameRate() == y.frameRate());
    QCOMPARE(x.frameCount(), y.frameCount());
    QCOMPARE(x.paperColor(), y.paperColor());
    QCOMPARE(x.nextLayerId(), y.nextLayerId());
    QCOMPARE(x.nextDrawingId(), y.nextDrawingId());
    QCOMPARE(x.layerCount(), y.layerCount());
    for (int i = 0; i < x.layerCount(); ++i) {
        const doc::Layer &lx = x.layerAt(i), &ly = y.layerAt(i);
        QCOMPARE(lx.id(), ly.id());
        QVERIFY(lx.properties() == ly.properties());
        QVERIFY(lx.timing() == ly.timing());
        QCOMPARE(lx.drawings().size(), ly.drawings().size());
        for (const auto &[id, dx] : lx.drawings()) {
            const doc::Drawing *dy = ly.drawing(id);
            QVERIFY(dy);
            QCOMPARE(dy->name, dx.name);
            auto cx = dx.image.tileCoords(), cy = dy->image.tileCoords();
            std::sort(cx.begin(), cx.end());
            std::sort(cy.begin(), cy.end());
            QVERIFY(cx == cy);
            for (const doc::TileCoord &c : cx)
                QVERIFY(dx.image.tile(c)->pixels == dy->image.tile(c)->pixels);
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
        const QString path = dir.filePath(QStringLiteral("scene.mothion"));
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
        const QString path = dir.filePath(QStringLiteral("scene.mothion"));
        QString error;
        QVERIFY(io::saveProject(*sampleDocument(), path, &error));
        doc::Document small(64, 64);
        QVERIFY2(io::saveProject(small, path, &error), qPrintable(error));
        const auto loaded = io::loadProject(path, &error);
        QVERIFY(loaded);
        QCOMPARE(loaded->width(), 64);
        QCOMPARE(loaded->layerCount(), 0);
    }

    void loadedIdsAreNeverReused()
    {
        doc::Document d(10, 10);
        d.insertLayer(0, std::make_unique<doc::Layer>(7, doc::LayerProperties{"L"}));
        d.layerAt(0).addDrawing({12, "1", {}});
        d.restoreIdCounters(1, 1); // a file claiming counters below the ids in use
        QCOMPARE(d.allocateLayerId(), doc::LayerId(8));
        QCOMPARE(d.allocateDrawingId(), doc::DrawingId(13));
    }

    void missingFileIsAnError()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("nothing.mothion"));
        QString error;
        QVERIFY(!io::loadProject(path, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(path)); // not created by opening it
    }

    void otherFilesAreRefused()
    {
        QTemporaryDir dir;
        const QString text = dir.filePath(QStringLiteral("notes.mothion"));
        QFile f(text);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a database at all, just some text that is long enough");
        f.close();
        QString error;
        QVERIFY(!io::loadProject(text, &error));
        QVERIFY(!error.isEmpty());

        // An SQLite database that is not a Mothion project.
        const QString other = dir.filePath(QStringLiteral("other.mothion"));
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
        const QString path = dir.filePath(QStringLiteral("future.mothion"));
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
        QCOMPARE(controller.currentLayer(), controller.document().layerAt(1).id()); // the top one

        controller.newDocument();
        QCOMPARE(controller.document().width(), 1920);
        QCOMPARE(controller.document().layerCount(), 1);
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
