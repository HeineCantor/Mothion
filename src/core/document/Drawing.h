#pragma once

#include "core/document/Ids.h"
#include "core/document/TiledImage.h"

#include <string>

namespace mothion::doc {

// One drawing of a cel, a "frame" in the interface. It exists independently
// of time: the cel's Timing decides on which frames it is shown, possibly several
// times (holds, cycles).
struct Drawing
{
    DrawingId id = kNoId;
    std::string name; // shown in the timeline cells: "1", "2", "A"...
    TiledImage image;
};

} // namespace mothion::doc
