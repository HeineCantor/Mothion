#include "core/document/Blend.h"

#include <QtTest>

#include <cmath>

using namespace mothion::doc;

namespace {

// A premultiplied pixel from straight 0..1 channels.
Pixel px(double r, double g, double b, double a = 1.0)
{
    auto byte = [](double v) { return Pixel(std::lround(v * 255.0)); };
    return (byte(a) << 24) | (byte(r * a) << 16) | (byte(g * a) << 8) | byte(b * a);
}

// Channel (0 red, 1 green, 2 blue) of a premultiplied pixel, straight, 0..1.
double channel(Pixel p, int k)
{
    const double a = double(p >> 24) / 255.0;
    return a <= 0.0 ? 0.0 : double((p >> (16 - 8 * k)) & 0xff) / 255.0 / a;
}

double alpha(Pixel p) { return double(p >> 24) / 255.0; }

// `top` over `below`, the result's first channel.
double gray(BlendMode mode, double top, double below, double topAlpha = 1.0, double opacity = 1.0)
{
    Pixel dst = px(below, below, below);
    const Pixel src = px(top, top, top, topAlpha);
    blendSpan(&dst, &src, 1, mode, float(opacity));
    return channel(dst, 0);
}

} // namespace

class BlendTest : public QObject
{
    Q_OBJECT
private slots:
    void theSoftwareModesAreTheOnesQPainterLacks()
    {
        QVERIFY(!needsSoftwareBlend(BlendMode::Normal));
        QVERIFY(!needsSoftwareBlend(BlendMode::Exclusion));
        QVERIFY(needsSoftwareBlend(BlendMode::LinearBurn));
        QVERIFY(needsSoftwareBlend(BlendMode::AddGlow));
    }

    void separableModesOverAnOpaqueBelow()
    {
        const double tol = 0.01;
        QVERIFY(std::abs(gray(BlendMode::LinearBurn, 0.8, 0.3) - 0.1) < tol);
        QVERIFY(std::abs(gray(BlendMode::Subtract, 0.8, 0.3) - 0.0) < tol);
        QVERIFY(std::abs(gray(BlendMode::Subtract, 0.2, 0.7) - 0.5) < tol);
        QVERIFY(std::abs(gray(BlendMode::Divide, 0.8, 0.3) - 0.375) < tol);
        QVERIFY(std::abs(gray(BlendMode::LinearLight, 0.8, 0.3) - 0.9) < tol);
        QVERIFY(std::abs(gray(BlendMode::VividLight, 0.8, 0.3) - 0.75) < tol);
        QVERIFY(std::abs(gray(BlendMode::VividLight, 0.2, 0.7) - 0.25) < tol);
        QVERIFY(std::abs(gray(BlendMode::PinLight, 0.8, 0.3) - 0.6) < tol);
        QVERIFY(std::abs(gray(BlendMode::HardMix, 0.8, 0.3) - 1.0) < tol);
        QVERIFY(std::abs(gray(BlendMode::HardMix, 0.2, 0.7) - 0.0) < tol);
    }

    void opacityMixesTheBlendWithTheBelow()
    {
        // Half opacity: halfway between the below and the full result.
        QVERIFY(std::abs(gray(BlendMode::LinearBurn, 0.8, 0.9, 1.0, 0.5) - (0.9 + (0.7 - 0.9) * 0.5)) < 0.01);
        // The source's own alpha does the same.
        QVERIFY(std::abs(gray(BlendMode::LinearBurn, 0.8, 0.9, 0.5, 1.0) - 0.8) < 0.01);
    }

    void overTransparencyTheSourceLandsAsItIs()
    {
        Pixel dst = 0;
        const Pixel src = px(0.2, 0.4, 0.6, 0.5);
        blendSpan(&dst, &src, 1, BlendMode::Hue, 1.0f);
        QVERIFY(std::abs(alpha(dst) - 0.5) < 0.01);
        QVERIFY(std::abs(channel(dst, 2) - 0.6) < 0.02);
        // Nothing to put: the below stays.
        Pixel kept = px(0.1, 0.2, 0.3);
        const Pixel none = 0;
        blendSpan(&kept, &none, 1, BlendMode::Divide, 1.0f);
        QCOMPARE(kept, px(0.1, 0.2, 0.3));
    }

    void glowDodgeLetsTheAlphaWeighIntoTheDodge()
    {
        // A grey 0.5 at half alpha over 0.4: dodge by 0.25, not a half mix of the full dodge.
        const double glow = gray(BlendMode::GlowDodge, 0.5, 0.4, 0.5);
        QVERIFY(std::abs(glow - 0.4 / 0.75) < 0.01); // 0.533
        // At full alpha it is the plain color dodge.
        QVERIFY(std::abs(gray(BlendMode::GlowDodge, 0.5, 0.4) - 0.8) < 0.01);
        // And it differs from Color Dodge's mix at 0.65 for f = 0.8, a = 0.5, b = 0.3.
        QVERIFY(std::abs(gray(BlendMode::GlowDodge, 0.8, 0.3, 0.5) - 0.5) < 0.01);
    }

    void addGlowClipsAfterTheMix()
    {
        QVERIFY(std::abs(gray(BlendMode::AddGlow, 0.5, 0.4, 0.5) - 0.65) < 0.01);
        // 0.3 + 0.8 passes 1: the mix is 0.3 + 0.8 * 0.5 = 0.7, which a clip before the mix would make 0.65.
        QVERIFY(std::abs(gray(BlendMode::AddGlow, 0.8, 0.3, 0.5) - 0.7) < 0.01);
        QVERIFY(std::abs(gray(BlendMode::AddGlow, 0.8, 0.9, 1.0) - 1.0) < 0.01);
    }

    void lighterAndDarkerColorTakeTheWholeColor()
    {
        Pixel dst = px(0.9, 0.1, 0.1); // lum 0.34
        const Pixel src = px(0.1, 0.6, 0.1); // lum 0.45
        Pixel d = dst;
        blendSpan(&d, &src, 1, BlendMode::LighterColor, 1.0f);
        QCOMPARE(d, src);
        d = dst;
        blendSpan(&d, &src, 1, BlendMode::DarkerColor, 1.0f);
        QCOMPARE(d, dst);
    }

    void hslModesKeepWhatTheirNamesSay()
    {
        const Pixel red = px(0.9, 0.2, 0.2);
        const Pixel blue = px(0.2, 0.2, 0.8);
        // Luminosity: the below's color with the source's brightness; over itself nothing changes.
        Pixel d = blue;
        blendSpan(&d, &blue, 1, BlendMode::Luminosity, 1.0f);
        for (int k = 0; k < 3; ++k)
            QVERIFY(std::abs(channel(d, k) - channel(blue, k)) < 0.02);
        // Color: the source's tint at the below's luminosity: red on a mid grey stays red, as bright as the grey.
        d = px(0.5, 0.5, 0.5);
        blendSpan(&d, &red, 1, BlendMode::Color, 1.0f);
        QVERIFY(channel(d, 0) > channel(d, 1) + 0.1);
        const double lum = 0.3 * channel(d, 0) + 0.59 * channel(d, 1) + 0.11 * channel(d, 2);
        QVERIFY(std::abs(lum - 0.5) < 0.02);
        // Hue: the source's hue with the below's saturation and brightness: a grey below stays grey.
        d = px(0.5, 0.5, 0.5);
        blendSpan(&d, &red, 1, BlendMode::Hue, 1.0f);
        QVERIFY(std::abs(channel(d, 0) - channel(d, 2)) < 0.02);
        // Saturation: a grey source takes the color out.
        d = blue;
        const Pixel grey = px(0.5, 0.5, 0.5);
        blendSpan(&d, &grey, 1, BlendMode::Saturation, 1.0f);
        QVERIFY(std::abs(channel(d, 0) - channel(d, 2)) < 0.02);
    }

    void aSpanBlendsEveryPixel()
    {
        Pixel dst[3] = {px(0.9, 0.9, 0.9), px(0.5, 0.5, 0.5), px(0.1, 0.1, 0.1)};
        const Pixel src[3] = {px(0.8, 0.8, 0.8), px(0.8, 0.8, 0.8), px(0.8, 0.8, 0.8)};
        blendSpan(dst, src, 3, BlendMode::LinearBurn, 1.0f);
        QVERIFY(std::abs(channel(dst[0], 0) - 0.7) < 0.01);
        QVERIFY(std::abs(channel(dst[1], 0) - 0.3) < 0.01);
        QVERIFY(std::abs(channel(dst[2], 0) - 0.0) < 0.01);
    }
};

QTEST_APPLESS_MAIN(BlendTest)
#include "tst_blend.moc"
