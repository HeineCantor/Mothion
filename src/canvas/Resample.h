#pragma once

#include <QImage>
#include <QString>
#include <QTransform>

namespace mothion::canvas {

// How a picture is read when it is scaled, turned or moved by a fraction of a pixel (the choices of the filter of
// Krita's transform):
//  - Nearest neighbor: each pixel of the result takes the closest one of the picture: hard edges, for pixel art;
//  - Bilinear: the four around the point, weighted: smooth but soft, the fastest;
//  - Bicubic: sixteen (Catmull-Rom): sharper than bilinear, with no halo to speak of;
//  - Lanczos3: thirty-six: the sharpest, for making a small picture bigger, with a faint halo at hard contrasts.
// Made smaller, the bicubic and Lanczos read a wider area (as much as the picture is shrunk), so thin lines do not break.
enum class ResampleFilter { Nearest = 0, Bilinear = 1, Bicubic = 2, Lanczos3 = 3 };
constexpr int kResampleFilters = 4;

QString filterName(ResampleFilter filter);

// `source` (premultiplied ARGB32) drawn through `sourceToResult` (the picture's pixels to the result's) into a new
// transparent image of `size`. The edges of the picture fade over the width of one pixel of the result, and what is
// outside it is empty.
QImage resampleTransformed(const QImage &source, const QTransform &sourceToResult, const QSize &size,
                           ResampleFilter filter);

} // namespace mothion::canvas
