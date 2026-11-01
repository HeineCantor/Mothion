// Tests for .sut import and the brush library.
//
// The .sut fixture is built here: a small SQLite database with the tables and
// columns the importer reads, one embedded tar archive holding a thumbnail,
// and a real Effector blob. No brush files live in the repository.

#include "tools/BrushLibrary.h"
#include "tools/SutImporter.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace mothion;

namespace {

QByteArray fromHex(const char *hex) { return QByteArray::fromHex(hex); }

// BrushSizeEffector of a real brush: pressure (linear) and velocity 100% → 300%.
const char *kSizeEffector = "00000030000001f0000000300000000000000064000000000000000000000000"
                            "0000002c0000005c0000012c000000000000000c000000020000001000000000"
                            "0000000000000000000000003ff00000000000003ff00000000000000000000c"
                            "000000050000001000000000000000003ff00000000000003fc3c8253c8253c8"
                            "3fd4f2094f2094f23fe000000000000000000000000000003fe6666666666666"
                            "00000000000000003ff00000000000000000000000000000";

QByteArray int32BE(qint32 v)
{
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i)
        b[i] = char((quint32(v) >> (24 - 8 * i)) & 0xFF);
    return b;
}

QByteArray utf16le(const QString &s)
{
    QByteArray b;
    for (QChar c : s) {
        b.append(char(c.unicode() & 0xFF));
        b.append(char(c.unicode() >> 8));
    }
    return b;
}

// BrushPatternImageArray with one entry pointing at `path`.
QByteArray patternArray(const QString &path)
{
    const QByteArray p = utf16le(path);
    const QByteArray name = utf16le(QStringLiteral("tip"));
    QByteArray entry = int32BE(0) + int32BE(qint32(p.size())) + p + int32BE(3) + int32BE(qint32(name.size())) + name;
    entry.replace(0, 4, int32BE(qint32(entry.size())));
    return int32BE(8) + int32BE(1) + entry;
}

// A one-file ustar archive.
QByteArray tarWith(const QString &name, const QByteArray &data)
{
    QByteArray header(512, '\0');
    header.replace(0, name.toUtf8().size(), name.toUtf8());
    const QByteArray size = QByteArray::number(data.size(), 8).rightJustified(11, '0') + '\0';
    header.replace(124, 12, size);
    header[156] = '0';
    header.replace(257, 5, "ustar");
    QByteArray tar = header + data;
    tar.append(QByteArray((512 - data.size() % 512) % 512, '\0'));
    tar.append(QByteArray(1024, '\0'));
    return tar;
}

QByteArray png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A filled-shape thumbnail: transparent disc ringed by a soft dark glow.
QImage glowThumbnail()
{
    QImage img(100, 100, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) {
            const double r = std::hypot(x - 49.5, y - 49.5);
            const double glow = std::max(0.0, 1.0 - std::abs(r - 38.0) / 8.0); // peak at r = 38
            img.setPixel(x, y, qRgba(0, 0, 0, int(glow * 180)));
        }
    return img;
}

// A line-art thumbnail: one crisp opaque stroke, open shape.
QImage lineThumbnail()
{
    QImage img(100, 100, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setPen(QPen(Qt::black, 3));
    p.drawLine(10, 50, 90, 50);
    return img;
}

void writeSut(const QString &file, const QImage &thumbnail)
{
    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(file);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("create table Node(_PW_ID integer primary key, NodeName text, "
                                      "NodeVariantID integer)")));
        QVERIFY(q.exec(QStringLiteral("insert into Node values(1, 'Test brush', 2)")));
        QVERIFY(q.exec(QStringLiteral(
            "create table Variant(_PW_ID integer primary key, VariantID integer, BrushSize real, BrushSizeUnit integer, "
            "BrushSizeEffector blob, BrushFlow integer, BrushHardness integer, BrushInterval real, Opacity integer, "
            "BrushUsePatternImage integer, BrushPatternImageArray blob, BrushPatternOrderType integer, "
            "BrushRotation real, BrushRotationEffector integer, BrushWaterColor integer, BrushUseIn integer, "
            "BrushUseOut integer, BrushInLength real, BrushInOutTarget blob)")));
        q.prepare(QStringLiteral(
            "insert into Variant values(1, 2, 300.0, 0, ?, 20, 80, 50.0, 90, 1, ?, 3, 90.0, 67, 1, 1, 0, 25.0, ?)"));
        q.addBindValue(fromHex(kSizeEffector));
        q.addBindValue(patternArray(QStringLiteral(".:Paint001:abc:data:material.layer")));
        // Start/end targets: size enabled, flow listed but off.
        q.addBindValue(int32BE(12) + int32BE(2) + int32BE(12) + int32BE(1001) + int32BE(1) + int32BE(0) + int32BE(1021)
                       + int32BE(0) + int32BE(0));
        QVERIFY(q.exec());
        QVERIFY(q.exec(QStringLiteral("create table MaterialFile(_PW_ID integer primary key, OriginalPath text, "
                                      "FileData blob)")));
        q.prepare(QStringLiteral("insert into MaterialFile values(1, ?, ?)"));
        q.addBindValue(QStringLiteral(".:Paint001:abc:data:material.layer"));
        q.addBindValue(tarWith(QStringLiteral("thumbnail/thumbnail.png"), png(thumbnail)));
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

} // namespace

class TestBrushImport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void importsParametersDynamicsAndTips()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("test.sut"));
        writeSut(file, glowThumbnail());

        QString error;
        const auto imported = tools::importSut(file, &error);
        QVERIFY2(imported.has_value(), qPrintable(error));
        const tools::BrushPreset &preset = imported->preset;
        QCOMPARE(preset.name, QStringLiteral("Test brush"));
        QCOMPARE(preset.size, 300.0);
        QCOMPARE(preset.opacity, 0.9);

        const brush::BrushStyle &s = preset.brush->style;
        QCOMPARE(s.flow, 0.2);
        QCOMPARE(s.hardness, 0.8);
        QCOMPARE(s.spacing, 0.5);
        QCOMPARE(s.sizeEffector.inputs(),
                 std::uint32_t(brush::InputPressure | brush::InputVelocity | brush::InputInOut));
        QCOMPARE(s.sizeEffector.velocityTo, 3.0);
        QCOMPARE(s.tipCount, 1);
        QCOMPARE(s.tipOrder, brush::TipOrder::Random);
        QCOMPARE(s.rotationMode, 67u);
        QCOMPARE(preset.brush->tips.size(), 1);
        QVERIFY(s.mix.on); // watercolor mode 1
        QVERIFY(s.sizeEffector.uses(brush::InputInOut)); // a start/end target
        QVERIFY(!s.flowEffector.uses(brush::InputInOut)); // listed but not enabled
        QVERIFY(s.inOut.in && !s.inOut.out);
        QCOMPARE(s.inOut.inLength, 25.0);
    }

    void glowThumbnailBecomesAFilledShape()
    {
        const QImage mask = tools::tipCoverageFromThumbnail(glowThumbnail());
        QCOMPARE(qAlpha(mask.pixel(50, 50)), 255); // centre: inside the glow
        QCOMPARE(qAlpha(mask.pixel(2, 2)), 0);     // corner: outside
        QCOMPARE(qAlpha(mask.pixel(50, 50 - 44)), 0); // outer half of the glow dropped
    }

    void lineArtThumbnailIsUsedAsDrawn()
    {
        const QImage mask = tools::tipCoverageFromThumbnail(lineThumbnail());
        QCOMPARE(qAlpha(mask.pixel(50, 50)), 255);
        QCOMPARE(qAlpha(mask.pixel(50, 20)), 0);
    }

    void whiteInsideALineArtShapeIsPaper()
    {
        QImage img(100, 100, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setPen(QPen(Qt::black, 4));
        p.setBrush(Qt::white);
        p.drawEllipse(10, 10, 80, 80);
        p.end();
        const QImage mask = tools::tipCoverageFromThumbnail(img);
        QCOMPARE(qAlpha(mask.pixel(50, 50)), 0);   // opaque white: no ink
        QCOMPARE(qAlpha(mask.pixel(50, 10)), 255); // black outline: full ink
    }

    void rejectsFilesThatAreNotSubTools()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("junk.sut"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a database");
        f.close();
        QString error;
        QVERIFY(!tools::importSut(file, &error).has_value());
        QVERIFY(!error.isEmpty());
        QVERIFY(!tools::importSut(dir.filePath(QStringLiteral("missing.sut")), &error).has_value());
    }

    void libraryKeepsImportsAcrossRestarts()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("lib.sut"));
        writeSut(file, lineThumbnail());

        tools::BrushLibrary library;
        QDir(library.storageFolder()).removeRecursively();
        tools::BrushLibrary first;
        const qsizetype builtIn = first.presets(tools::ToolId::Brush).size();
        QSignalSpy changed(&first, &tools::BrushLibrary::presetsChanged);
        QVERIFY(first.importSut(file));
        QVERIFY(first.importSut(file)); // same file again: no duplicate
        QCOMPARE(changed.count(), 1);
        QCOMPARE(first.presets(tools::ToolId::Brush).size(), builtIn + 1);

        tools::BrushLibrary second; // a new start reads the stored copy
        QCOMPARE(second.presets(tools::ToolId::Brush).size(), builtIn + 1);
        QCOMPARE(second.presets(tools::ToolId::Brush).last().name, QStringLiteral("Test brush"));
        QDir(library.storageFolder()).removeRecursively();
    }
};

QTEST_MAIN(TestBrushImport)
#include "tst_brushimport.moc"
