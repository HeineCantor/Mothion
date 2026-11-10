#pragma once

#include <string>

namespace mothion::doc {

// Where a cel that came from a Blender scene comes from: the .blend file and the scene in it (what a Blender that has it
// open tells, see io::BlenderSession), and how the frames were asked for, so they can be asked for again when the scene
// changes.
struct BlenderLink
{
    std::string path;        // the .blend file
    std::string scene;       // the scene in it
    int startFrame = 0;      // where the first frame of the scene goes on the timeline
    int percent = 50;        // how much of the size of the page the pictures are drawn at (they are made as big as the page)

    bool operator==(const BlenderLink &) const = default;
};

} // namespace mothion::doc
