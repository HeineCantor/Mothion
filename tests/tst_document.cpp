// Tests for the Qt-free document model (QtTest is only the runner).

#include "core/document/Document.h"
#include "core/document/TiledImage.h"

#include <QTest>

#include <vector>

using namespace mothion::doc;

namespace {

// Compact expected-timing builder: {{start, length, drawing}, ...}.
std::vector<Exposure> runs(std::initializer_list<Exposure> list) { return list; }

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

    void translatedMovesPixelsAndSharesWholeTiles()
    {
        TiledImage image;
        image.setPixel(10, 20, 0xff112233);
        const TiledImage moved = image.translated(5, -30);
        QCOMPARE(moved.pixel(15, -10), Pixel(0xff112233));
        QCOMPARE(moved.pixel(10, 20), Pixel(0));
        QCOMPARE(moved.tileCount(), std::size_t(1));

        const TiledImage byTiles = image.translated(kTileSize, 2 * kTileSize);
        QCOMPARE(byTiles.pixel(10 + kTileSize, 20 + 2 * kTileSize), Pixel(0xff112233));
        QCOMPARE(byTiles.tile({1, 2}).get(), image.tile({0, 0}).get()); // shared, not copied
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

    // --- Timing ----------------------------------------------------------------

    void timingLookup()
    {
        Timing t;
        t.setExposure(0, 3, 1);
        t.setExposure(5, 2, 2);
        QCOMPARE(t.drawingAt(0), DrawingId(1));
        QCOMPARE(t.drawingAt(2), DrawingId(1));
        QCOMPARE(t.drawingAt(3), kNoId); // gap
        QCOMPARE(t.drawingAt(6), DrawingId(2));
        QCOMPARE(t.drawingAt(7), kNoId);
        QCOMPARE(t.drawingAt(-1), kNoId);
        QCOMPARE(t.lastFrame(), 7);
    }

    void setExposureSplitsAHold()
    {
        Timing t;
        t.setExposure(0, 10, 1);
        t.setExposure(4, 2, 2); // in the middle of the hold of drawing 1
        QCOMPARE(t.exposures(), runs({{0, 4, 1}, {4, 2, 2}, {6, 4, 1}}));
    }

    void setExposureTrimsAndClears()
    {
        Timing t;
        t.setExposure(0, 4, 1);
        t.setExposure(4, 4, 2);
        t.clear(2, 4); // end of 1 and start of 2
        QCOMPARE(t.exposures(), runs({{0, 2, 1}, {6, 2, 2}}));
    }

    void touchingRunsOfSameDrawingMerge()
    {
        Timing t;
        t.setExposure(0, 2, 1);
        t.setExposure(2, 3, 1);
        QCOMPARE(t.exposures(), runs({{0, 5, 1}}));
        t.setExposure(1, 1, 2);
        t.setExposure(1, 1, 1); // healed back into one hold
        QCOMPARE(t.exposures(), runs({{0, 5, 1}}));
    }

    void resizeExposureRipples()
    {
        Timing t;
        t.setExposure(0, 2, 1);
        t.setExposure(2, 2, 2);
        t.setExposure(6, 2, 3); // gap at 4-5 is kept
        QVERIFY(t.resizeExposure(3, 5)); // hold of 2 goes from 2 to 5 frames
        QCOMPARE(t.exposures(), runs({{0, 2, 1}, {2, 5, 2}, {9, 2, 3}}));
        QVERIFY(t.resizeExposure(0, 1)); // shorten: everything after moves left
        QCOMPARE(t.exposures(), runs({{0, 1, 1}, {1, 5, 2}, {8, 2, 3}}));
        QVERIFY(!t.resizeExposure(7, 3)); // empty frame
        QVERIFY(!t.resizeExposure(0, 0));
    }

    void insertAndRemoveFrames()
    {
        Timing t;
        t.setExposure(0, 4, 1);
        t.setExposure(4, 2, 2);
        t.insertEmptyFrames(2, 3);
        QCOMPARE(t.exposures(), runs({{0, 2, 1}, {5, 2, 1}, {7, 2, 2}}));
        t.removeFrames(2, 3);
        QCOMPARE(t.exposures(), runs({{0, 4, 1}, {4, 2, 2}})); // merged again
        t.removeFrames(3, 2);
        QCOMPARE(t.exposures(), runs({{0, 3, 1}, {3, 1, 2}}));
    }

    void removeDrawingClearsItsFrames()
    {
        Timing t;
        t.setExposure(0, 2, 1);
        t.setExposure(2, 2, 2);
        t.setExposure(4, 2, 1); // drawing 1 reused (cycle)
        QVERIFY(t.usesDrawing(1));
        t.removeDrawing(1);
        QCOMPARE(t.exposures(), runs({{2, 2, 2}}));
        QVERIFY(!t.usesDrawing(1));
    }

    // --- Layer / Document -------------------------------------------------------

    void layerDrawingsAndNames()
    {
        Layer layer(1, {"Linee"});
        QCOMPARE(layer.nextDrawingName(), std::string("1"));
        layer.addDrawing({10, "1", {}});
        layer.addDrawing({11, "3", {}});
        QCOMPARE(layer.nextDrawingName(), std::string("2"));
        layer.timing().setExposure(0, 2, 11);
        QCOMPARE(layer.drawingAt(1)->name, std::string("3"));
        QVERIFY(layer.drawingAt(2) == nullptr);

        auto taken = layer.takeDrawing(10);
        QVERIFY(taken && taken->name == "1");
        QVERIFY(layer.drawing(10) == nullptr);
        QVERIFY(!layer.takeDrawing(10));
    }

    void documentLayerStack()
    {
        Document doc(1920, 1080, {60, 1});
        QCOMPARE(doc.frameCount(), 60);
        const LayerId a = doc.allocateLayerId();
        const LayerId b = doc.allocateLayerId();
        const LayerId c = doc.allocateLayerId();
        QVERIFY(a != b && b != c && a != kNoId);
        Layer &la = doc.insertLayer(0, std::make_unique<Layer>(a, LayerProperties{"A"}));
        doc.insertLayer(1, std::make_unique<Layer>(b, LayerProperties{"B"}));
        doc.insertLayer(99, std::make_unique<Layer>(c, LayerProperties{"C"})); // clamped to top
        QCOMPARE(doc.indexOf(c), 2);

        doc.moveLayer(a, 2);
        QCOMPARE(doc.indexOf(a), 2);
        QCOMPARE(doc.indexOf(b), 0);
        QCOMPARE(&la, doc.layer(a)); // references survive reordering

        auto taken = doc.takeLayer(b);
        QVERIFY(taken && taken->properties().name == "B");
        QCOMPARE(doc.layerCount(), 2);
        QCOMPARE(doc.indexOf(b), -1);
    }

    void ntscFrameRate()
    {
        const FrameRate ntsc{24000, 1001};
        QVERIFY(qAbs(ntsc.fps() - 23.976) < 0.001);
        QCOMPARE(Document(100, 100, ntsc).frameCount(), 24);
    }
};

QTEST_APPLESS_MAIN(TestDocument)
#include "tst_document.moc"
