#pragma once

#include "core/document/TiledImage.h"

#include <cstdint>

namespace mothion::canvas {

// Hue / Saturation / Value of a layer (Edit > Color): the hue turned round the wheel (degrees, -180..180); the
// saturation and the value from -100 (none: gray, black) through 0 (as they are) to +100 (full), each pixel's own
// moved that far towards the end. The alpha stays as it is.
struct HsvShift
{
    int hue = 0;
    int saturation = 0;
    int value = 0;

    bool isNone() const { return hue == 0 && saturation == 0 && value == 0; }
    bool operator==(const HsvShift &) const = default;
};

// A color as straight (not premultiplied) ARGB, shifted.
std::uint32_t shiftedColor(std::uint32_t argb, const HsvShift &shift);
// A pixel of a layer (premultiplied ARGB), shifted.
doc::Pixel shiftedPixel(doc::Pixel pixel, const HsvShift &shift);
// Every pixel of `image` shifted (the tiles done on all the cores).
doc::TiledImage shiftedImage(const doc::TiledImage &image, const HsvShift &shift);

} // namespace mothion::canvas
