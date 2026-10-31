// Tests for the Qt-free document model (QtTest is only the runner).

#include "core/document/TiledImage.h"

#include <QTest>

#include <vector>

using namespace mothion::doc;

namespace {
constexpr Pixel kRed = 0xffff0000;
constexpr Pixel kHalfBlue = 0x80000080; // premultiplied 50 % blue
} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    // --- TiledImage -----------------------------------------------------------

    void emptyImageIsTransparent()
    {
        TiledImage img;
        QVERIFY(img.isEmpty());
        QCOMPARE(img.pixel(10, 10), Pixel(0));
        QCOMPARE(img.pixel(-500, 7000), Pixel(0));
    }

    void setPixelCreatesOnlyOneTile()
    {
        TiledImage img;
        img.setPixel(100, 200, kRed);
        QCOMPARE(img.tileCount(), std::size_t(1));
        QCOMPARE(img.pixel(100, 200), kRed);
        QVERIFY(img.tile({1, 3}) != nullptr);
        QCOMPARE(img.pixel(101, 200), Pixel(0));
    }

    void negativeCoordinates()
    {
        QCOMPARE(TiledImage::tileAt(-1, -64), (TileCoord{-1, -1}));
        QCOMPARE(TiledImage::tileAt(-65, 63), (TileCoord{-2, 0}));
        TiledImage img;
        img.setPixel(-1, -1, kRed);
        QCOMPARE(img.pixel(-1, -1), kRed);
        QVERIFY(img.tile({-1, -1}) != nullptr);
    }

    void transparentWritesDropTiles()
    {
        TiledImage img;
        img.setPixel(5, 5, kRed);
        img.setPixel(5, 5, 0);
        QVERIFY(img.isEmpty());
        img.setPixel(500, 500, 0); // never creates a tile
        QVERIFY(img.isEmpty());
    }

    void copiesShareTilesAndCopyOnWrite()
    {
        TiledImage a;
        a.setPixel(3, 3, kRed);
        TiledImage b = a;
        QCOMPARE(a.tile({0, 0}).get(), b.tile({0, 0}).get()); // shared, not copied

        b.setPixel(4, 4, kHalfBlue);
        QVERIFY(a.tile({0, 0}).get() != b.tile({0, 0}).get());
        QCOMPARE(a.pixel(4, 4), Pixel(0)); // original untouched
        QCOMPARE(b.pixel(3, 3), kRed);     // clone kept the old pixels
    }

    void heldTilePointerIsNeverModified()
    {
        TiledImage img;
        img.setPixel(0, 0, kRed);
        const TilePtr before = img.tile({0, 0}); // what an undo record keeps
        img.setPixel(0, 0, kHalfBlue);
        QCOMPARE(before->pixels[0], kRed);
        img.setTile({0, 0}, before);
        QCOMPARE(img.pixel(0, 0), kRed);
    }

    void rectReadWriteAcrossTiles()
    {
        // 100×90 block at negative x: columns -1, 0, 1 × rows 0, 1 = six tiles.
        const int x = -30, y = 20, w = 100, h = 90;
        std::vector<Pixel> src(std::size_t(w * h));
        for (int i = 0; i < w * h; ++i)
            src[std::size_t(i)] = 0xff000000u | Pixel(i);
        TiledImage img;
        img.write(x, y, w, h, src.data(), w);
        QCOMPARE(img.tileCount(), std::size_t(6));

        std::vector<Pixel> back(std::size_t(w * h));
        img.read(x, y, w, h, back.data(), w);
        QVERIFY(back == src);
        QCOMPARE(img.pixel(x + 7, y + 3), src[std::size_t(3 * w + 7)]);

        // Reading outside gives transparency.
        Pixel outside = 1;
        img.read(1000, 1000, 1, 1, &outside, 1);
        QCOMPARE(outside, Pixel(0));
    }
};

QTEST_APPLESS_MAIN(TestDocument)
#include "tst_document.moc"
