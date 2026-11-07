#pragma once

#include "core/document/Drawing.h"

#include <QImage>
#include <QPainter>

namespace mothion::canvas {

// The composition mode of QPainter that does a blend mode (source over for the ones it has none for: those are done
// by hand, see doc::needsSoftwareBlend()).
QPainter::CompositionMode compositionFor(doc::BlendMode blend);

// `image` (premultiplied, drawn at the top left, clipped to the target) over `target` with the blend mode and an
// opacity (0..1), whichever way the mode is done.
void blendImageOver(QImage &target, const QImage &image, double opacity, doc::BlendMode blend);

} // namespace mothion::canvas
