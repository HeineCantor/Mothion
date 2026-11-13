#pragma once

#include "core/brush/BrushStyle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mothion::doc {

// One stroke of a vector layer, kept as what the pen did instead of as pixels: the points the brush engine
// was fed, and what is needed to draw them again the same way (the brush, size, colors, seed). The layer's
// image holds what the strokes look like drawn now; the strokes are what it is drawn from, so that it can be
// drawn again at another size without losing sharpness.
//
// The brush is named, not stored: its definition (dynamics, tips) is looked up when the stroke is drawn
// again, as Clip Studio keeps a table of brush styles that its vector lines refer to.
struct VectorStroke
{
    std::string brush;       // name of the brush definition the stroke was drawn with
    double size = 6.0;       // the brush diameter, px (the tool setting; the engine's radius is half of it)
    double opacity = 1.0;    // the stroke opacity, a ceiling the dabs never go past
    std::uint32_t color = 0xFF000000u;          // ARGB, the tool's main color
    std::uint32_t secondaryColor = 0xFFFFFFFFu; // ARGB, the other color (for the brush's color variations)
    std::uint32_t seed = 0;  // the random seed of the stroke's dabs
    bool erase = false;      // takes the layer's pixels away instead of adding to them
    // How the tool was set, which changes how the same points are drawn.
    bool cornerTips = true;           // sharp corners keep a point
    bool startCorrectedAfter = false; // the start is corrected after the stroke, not tapered live
    std::vector<brush::BrushInput> points; // before brush::finalPoints()
};

} // namespace mothion::doc
