#pragma once

#include "core/document/Ids.h"
#include "core/document/TiledImage.h"

#include <string>

namespace mothion::doc {

// One drawing (a "cel") of a layer. It exists independently of time:
// the layer's Timing decides on which frames it is shown, possibly several
// times (holds, cycles).
struct Drawing
{
    DrawingId id = kNoId;
    std::string name; // shown in the timeline cells: "1", "2", "A"...
    TiledImage image;
};

} // namespace mothion::doc
