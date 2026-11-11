#pragma once

#include "core/document/BlenderLink.h"
#include "core/document/CelMotion.h"
#include "core/document/Drawing.h"
#include "core/document/Timing.h"

#include <map>
#include <optional>
#include <string>

namespace mothion::doc {

struct CelProperties
{
    std::string name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f; // 0..1
    BlendMode blend = BlendMode::Normal;
    // A trajectory: a cel with no drawings that is never drawn, only moved, turned and scaled by its keys (what the cels
    // that follow it will do, see CelMotion). Fixed when it is made.
    bool trajectory = false;
    // For a trajectory: the trajectory it follows (kNoId: none, or one that is not there any more). Its own keys come on top of
    // what the parent does. For a trajectory `visible` is "active": an inactive one does nothing of its own.
    CelId parent = kNoId;

    bool operator==(const CelProperties &) const = default;
};

// A cel, a row of the timeline ("element" in Harmony terms): its own set of
// drawings plus the timing that says which of them is shown on each frame.
class Cel
{
public:
    Cel(CelId id, CelProperties properties);

    CelId id() const { return m_id; }

    const CelProperties &properties() const { return m_properties; }
    void setProperties(CelProperties properties) { m_properties = std::move(properties); }

    // Drawings, ordered by id (creation order).
    const std::map<DrawingId, Drawing> &drawings() const { return m_drawings; }
    Drawing *drawing(DrawingId id);
    const Drawing *drawing(DrawingId id) const;
    Drawing &addDrawing(Drawing drawing); // id must be new
    std::optional<Drawing> takeDrawing(DrawingId id);

    // Drawing shown at `frame`, or nullptr on an empty frame.
    const Drawing *drawingAt(int frame) const;

    Timing &timing() { return m_timing; }
    const Timing &timing() const { return m_timing; }

    // What the animation does to the cel as a whole: its pivot and the keys of its pose (see CelMotion). Empty: still.
    const CelMotion &motion() const { return m_motion; }
    void setMotion(CelMotion motion) { m_motion = std::move(motion); }
    // The Blender scene the cel was rendered from, if it was (see BlenderLink).
    const std::optional<BlenderLink> &blenderLink() const { return m_blenderLink; }
    void setBlenderLink(std::optional<BlenderLink> link) { m_blenderLink = std::move(link); }

    // Smallest positive number not already used as a name: "1", "2", ...
    std::string nextDrawingName() const;

private:
    CelId m_id;
    CelProperties m_properties;
    std::map<DrawingId, Drawing> m_drawings;
    Timing m_timing;
    CelMotion m_motion;
    std::optional<BlenderLink> m_blenderLink;
};

} // namespace mothion::doc
