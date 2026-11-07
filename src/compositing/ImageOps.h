#pragma once

#include <QImage>

#include <array>
#include <cstdint>

namespace mothion::compositing {

// What several nodes need. All the images are premultiplied ARGB, and what is given in another format is converted.

// A box blur of `radius` pixels (the edges go on outside the picture); radius 0 gives the picture as it is.
QImage boxBlur(const QImage &source, int radius);

// The picture smeared along a line of `length` pixels at `angle` degrees (0 is to the right, centered on each pixel).
QImage directionalBlur(const QImage &source, double length, double angleDegrees);

// Every red, green and blue of the picture, without its alpha (the color it would have on its own), replaced by
// the table's entry for it; the alpha stays. A see-through pixel stays see-through.
QImage mapChannels(const QImage &source, const std::array<std::uint8_t, 256> &table);

} // namespace mothion::compositing
