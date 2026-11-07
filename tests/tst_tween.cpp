#include "core/tween/Flow.h"
#include "core/tween/Morph.h"

#include <QtTest>

#include <cmath>

using namespace mothion::tween;

namespace {

// A picture of thick strokes (a curve and a loop), `shift` px to the side and down, and `bend` px of a gentle wave.
Image strokes(int w, int h, double shiftX, double shiftY, double bend = 0.0)
{
    Image image(w, h);
    auto stamp = [&](double cx, double cy, double radius) {
        for (int y = int(cy - radius - 1); y <= int(cy + radius + 1); ++y)
            for (int x = int(cx - radius - 1); x <= int(cx + radius + 1); ++x)
                if (x >= 0 && y >= 0 && x < w && y < h && std::hypot(x - cx, y - cy) <= radius)
                    image.at(x, y) = 1.0f;
    };
    auto place = [&](double x, double y) {
        const double wave = bend * std::sin(y / 18.0);
        stamp(x + shiftX + wave, y + shiftY, 2.0);
    };
    for (double t = 0; t < 1.0; t += 0.002) { // a long S
        place(w * 0.25 + w * 0.3 * std::sin(t * 6.0), h * 0.15 + t * h * 0.7);
    }
    for (double a = 0; a < 6.3; a += 0.01) // a ring
        place(w * 0.6 + w * 0.12 * std::cos(a), h * 0.4 + w * 0.12 * std::sin(a));
    return image;
}

// The mean distance between the field at the ink points of `a` and `want`.
double meanError(const Image &a, const Field &s, double wantX, double wantY)
{
    double sum = 0;
    long n = 0;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x)
            if (a.at(x, y) > 0.5f) {
                const std::size_t i = std::size_t(y) * std::size_t(a.width) + std::size_t(x);
                sum += std::hypot(s.dx[i] - wantX, s.dy[i] - wantY);
                ++n;
            }
    return n ? sum / double(n) : 1e9;
}

} // namespace

class TestTween : public QObject
{
    Q_OBJECT

private slots:
    void imagesAreSampledAndShrunk()
    {
        Image image(4, 4);
        image.at(1, 1) = 1.0f;
        QVERIFY(std::abs(image.sample(1, 1) - 1.0f) < 1e-6f);
        QVERIFY(std::abs(image.sample(1.5, 1) - 0.5f) < 1e-6f);
        QCOMPARE(image.sample(-3, 0), 0.0f); // outside: nothing
        const Image half = downscaled(image, 2);
        QCOMPARE(half.width, 2);
        QVERIFY(std::abs(half.at(0, 0) - 0.25f) < 1e-6f); // the mean of a block
        Image dot(21, 21); // away from the edges
        dot.at(10, 10) = 1.0f;
        const Image soft = blurred(dot, 1.0);
        float sum = 0;
        for (float v : soft.v)
            sum += v;
        QVERIFY(std::abs(sum - 1.0f) < 0.02f); // the blur keeps what there is
        QVERIFY(soft.at(10, 10) < 1.0f && soft.at(10, 10) > 0.1f);
    }

    void aFieldKeepsItsDisplacementsInTheNewPixelsWhenResized()
    {
        Field field(10, 10);
        std::fill(field.dx.begin(), field.dx.end(), 3.0f);
        const Field big = resized(field, 40, 40); // four times as many pixels across
        QCOMPARE(big.width, 40);
        QVERIFY(std::abs(big.dx[100] - 12.0f) < 1e-3f); // 3 px was 12 now
    }

    void identicalImagesNeedNoMovement()
    {
        const Image a = strokes(160, 200, 0, 0);
        const Field s = registerImages(a, a);
        QVERIFY(meanError(a, s, 0, 0) < 0.05);
    }

    void aShiftIsFound()
    {
        const Image a = strokes(160, 200, 0, 0);
        const Image b = strokes(160, 200, 7, -5);
        const Field s = registerImages(a, b);
        // b(x + s) is a(x): a point of a is found 7 to the right and 5 up in b.
        QVERIFY2(meanError(a, s, 7, -5) < 1.2, qPrintable(QString::number(meanError(a, s, 7, -5))));
    }

    void aGentleBendingIsFollowed()
    {
        const Image a = strokes(160, 200, 0, 0, 0.0);
        const Image b = strokes(160, 200, 0, 0, 9.0);
        const Field s = registerImages(a, b);
        // The ink of a lands near the ink of b: its distance to it falls to a pixel or so.
        auto distanceToInk = [&](const Image &from, const Field &field, const Image &to) {
            double sum = 0;
            long n = 0;
            for (int y = 0; y < from.height; ++y)
                for (int x = 0; x < from.width; ++x)
                    if (from.at(x, y) > 0.5f) {
                        const std::size_t i = std::size_t(y) * std::size_t(from.width) + std::size_t(x);
                        const double px = x + field.dx[i], py = y + field.dy[i];
                        double best = 1e9;
                        for (int j = std::max(0, int(py) - 12); j < std::min(to.height, int(py) + 13); ++j)
                            for (int i2 = std::max(0, int(px) - 12); i2 < std::min(to.width, int(px) + 13); ++i2)
                                if (to.at(i2, j) > 0.5f)
                                    best = std::min(best, std::hypot(i2 - px, j - py));
                        sum += std::min(best, 12.0);
                        ++n;
                    }
            return sum / double(n);
        };
        const double before = distanceToInk(a, Field(a.width, a.height), b);
        const double after = distanceToInk(a, s, b);
        QVERIFY2(before > 1.5, qPrintable(QString::number(before)));
        QVERIFY2(after < 0.8 && after < before * 0.5, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(after)));
    }

    void emptyOrMismatchedImagesGiveNothing()
    {
        const Image a = strokes(80, 80, 0, 0);
        QVERIFY(registerImages(a, Image(40, 40)).isEmpty()); // not the same size
        const Field none = registerImages(Image(64, 64), Image(64, 64)); // nothing to see
        QVERIFY(!none.isEmpty());
        for (float v : none.dx)
            QCOMPARE(v, 0.0f);
        QVERIFY(registerImages(Image(), Image()).isEmpty());
    }

    void aMorphStartsAtOneEndsAtTheOther()
    {
        using mothion::puppet::RasterImage;
        RasterImage a(10, 20, 64, 64), b(10, 20, 64, 64);
        auto blob = [](RasterImage &image, int cx, int cy, std::uint32_t color) {
            for (int y = cy - 6; y <= cy + 6; ++y)
                for (int x = cx - 6; x <= cx + 6; ++x)
                    image.pixels[std::size_t(y) * 64 + std::size_t(x)] = color;
        };
        blob(a, 20, 32, 0xff000000);
        blob(b, 36, 32, 0xff000000); // 16 px to the right
        Field s(64, 64);
        std::fill(s.dx.begin(), s.dx.end(), 16.0f); // each point of a is 16 px right in b
        auto alphaAt = [](const RasterImage &image, int x, int y) { return int(image.pixels[std::size_t(y) * 64 + std::size_t(x)] >> 24); };
        const RasterImage start = morph(a, b, s, 0.0);
        const RasterImage end = morph(a, b, s, 1.0);
        const RasterImage mid = morph(a, b, s, 0.5);
        QCOMPARE(start.x, 10);
        QCOMPARE(alphaAt(start, 20, 32), 255);
        QCOMPARE(alphaAt(start, 36, 32), 0);
        QCOMPARE(alphaAt(end, 36, 32), 255);
        QCOMPARE(alphaAt(end, 20, 32), 0);
        QVERIFY(alphaAt(mid, 28, 32) > 250); // halfway: the shape is half way, whole (both pictures agree there)
        QVERIFY(alphaAt(mid, 20, 32) < 5 && alphaAt(mid, 36, 32) < 5);
    }

    void whatHasNoCounterpartFades()
    {
        using mothion::puppet::RasterImage;
        RasterImage a(0, 0, 32, 32), b(0, 0, 32, 32);
        for (int y = 10; y < 20; ++y)
            for (int x = 10; x < 20; ++x)
                b.pixels[std::size_t(y) * 32 + std::size_t(x)] = 0xff000000; // only in b
        const Field none(32, 32); // nothing moves
        const RasterImage mid = morph(a, b, none, 0.5);
        const int alpha = int(mid.pixels[std::size_t(15) * 32 + 15] >> 24);
        QVERIFY(alpha > 120 && alpha < 135); // half
        QCOMPARE(int(morph(a, b, none, 1.0).pixels[std::size_t(15) * 32 + 15] >> 24), 255);
        QCOMPARE(int(morph(a, b, none, 0.0).pixels[std::size_t(15) * 32 + 15] >> 24), 0);
    }

    void mismatchedBlocksGiveAnEmptyPicture()
    {
        using mothion::puppet::RasterImage;
        RasterImage a(0, 0, 8, 8), b(0, 0, 9, 8);
        QVERIFY(morph(a, b, Field(8, 8), 0.5).pixels.size() == 64);
        for (std::uint32_t p : morph(a, b, Field(8, 8), 0.5).pixels)
            QCOMPARE(p, 0u);
    }
};

QTEST_APPLESS_MAIN(TestTween)
#include "tst_tween.moc"
