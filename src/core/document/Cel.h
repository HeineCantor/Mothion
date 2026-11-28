#pragma once

#include "core/document/BlenderLink.h"
#include "core/document/CelMotion.h"
#include "core/document/Deformer.h"
#include "core/document/Drawing.h"
#include "core/document/ModelCel.h"
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
    // The trajectory it follows (kNoId: none, or one that is not there any more; a cel that is no trajectory can follow one
    // too), or the deformation cel that bends it (which may follow a trajectory). Its own keys come on top of what the
    // trajectory does. For a trajectory `visible` is "active": an inactive one does nothing of its own.
    CelId parent = kNoId;
    // A deformation cel: like a trajectory, a cel with no drawings that is never drawn; it bends the drawings of the cels
    // that follow it (their parent is it) as its pose on the frame says (see Deformer, and its Deform keys). It can follow
    // a trajectory, which then moves what it bent. Fixed when it is made; None for any other cel.
    DeformerKind deformer = DeformerKind::None;
    // A drawn cel whose parent is a deformation cel: -1, it is bent by it; else the bone (or node) it is carried by, whole
    // and not bent, as that bone moves and turns from its rest (Harmony's kinematic output: a hand on an arm).
    int parentPart = -1;
    // A mouth chart, for the lip sync (see LipSync.h): a drawing for each mouth shape, named after it, shown frame by
    // frame as the sound says. The Timeline's mouth bar and the Mouth chart panel are for these cels only.
    bool mouthChart = false;
    // A 3D model cel: no drawings; it shows its model (see Cel::model()) as its Deform keys pose it (see rig3d::Pose),
    // drawn on the page through the pose's camera, then moved, turned, scaled and faded by its other keys as any cel.
    // Fixed when it is made.
    bool model3d = false;

    // A trajectory or a deformation cel: a cel with no drawings, never drawn.
    bool drawsNothing() const { return trajectory || deformer != DeformerKind::None; }

    bool operator==(const CelProperties &) const = default;
};

// Whether a cel has keys of `channel`: a deformation cel only of Deform (its pose), a 3D model cel of all (Deform: its
// model's pose), any other of all but Deform.
inline bool hasChannel(const CelProperties &properties, Channel channel)
{
    if (properties.model3d)
        return true;
    return (properties.deformer != DeformerKind::None) == (channel == Channel::Deform);
}

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
    // A deformation cel's bones and their rest (see CelProperties::deformer); empty for any other.
    const Deformer &deformer() const { return m_deformer; }
    void setDeformer(Deformer deformer) { m_deformer = std::move(deformer); }
    // A 3D model cel's model and the settings of its bones (see CelProperties::model3d); empty for any other.
    const ModelRig &model() const { return m_model; }
    void setModel(ModelRig model) { m_model = std::move(model); }
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
    Deformer m_deformer;
    ModelRig m_model;
    std::optional<BlenderLink> m_blenderLink;
};

} // namespace mothion::doc
