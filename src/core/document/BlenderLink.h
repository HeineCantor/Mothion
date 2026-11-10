#pragma once

#include <string>

namespace mothion::doc {

// Where a cel that came from a Blender scene comes from: the .blend file, and how it was rendered, so it can be made again
// when the file is saved (see io::BlenderRender).
struct BlenderLink
{
    std::string path;        // the .blend file, as it was chosen
    int startFrame = 0;      // where the first frame of the scene goes on the timeline
    int percent = 50;        // how much of the size of the page the draft is rendered at
    bool lineArt = false;    // the Line Art of the scene is kept

    bool operator==(const BlenderLink &) const = default;
};

} // namespace mothion::doc
