#pragma once

#include "core/rig3d/Model.h"

#include <vector>

namespace mothion::rig3d {

// What the model is seen through: a camera turning round a point, looking at it.
struct OrbitCamera
{
    double yaw = 0;      // degrees, round the up axis (+y): 0 looks from +z
    double pitch = 0;    // degrees, up from the horizontal
    double distance = 5; // from the point looked at
    Vec3 target;
    double fov = 30;     // the vertical field of view, degrees

    Vec3 eye() const;
    // The model's space to the camera's (the camera at the origin, looking down −z, +y up).
    Mat4 view() const;
    // The camera's space to clip space, for a picture `aspect` wide for 1 high; near and far round what is `radius`
    // from the target.
    Mat4 projection(double aspect, double radius) const;
};

// A pose of a model: where each node is in its parent (its turn and its place; its scale stays as at rest), how much of
// each blend shape there is, and the camera. Saved as plain numbers (see toNumbers()), the Deform keys of a 3D model cel.
struct Pose
{
    std::vector<Quat> rotations;    // one for each node
    std::vector<Vec3> translations; // one for each node
    std::vector<double> morphs;     // one for each MorphSlot
    OrbitCamera camera;

    // The model as it was made, seen whole from the front.
    static Pose rest(const Model &model);

    // The numbers of the pose, in this order: the camera's yaw, pitch, distance, target x y z, fov; for each node its
    // turn (w x y z) and its place (x y z); the weight of each blend shape. Numbers between two keys go straight from
    // one to the other: a turn in between is the quaternion so found made a unit long again.
    std::vector<double> toNumbers() const;
    // From the numbers: what is missing (a model with more nodes now than the key had) is as at rest.
    static Pose fromNumbers(const Model &model, const std::vector<double> &numbers);
};

constexpr int kCameraNumbers = 7;
constexpr int kNodeNumbers = 7;

// The global matrix of each node in the pose (the model's space).
std::vector<Mat4> globalMatrices(const Model &model, const Pose &pose);
// The camera that frames the model at rest from the front: looking at the middle of what its meshes cover, from where
// it all fits.
OrbitCamera framingCamera(const Model &model);
// The middle and the half size (the radius) of what the meshes of the model cover at rest (see computeRestBounds()).
void restBounds(const Model &model, Vec3 &center, double &radius);
// Works out and keeps in the model what its meshes cover in the rest pose, skinned (GltfLoader does it once).
void computeRestBounds(Model &model);

// The model's space to the page: through the camera, onto a picture `width` × `height` pixels (y down, the page's).
struct Projection
{
    Mat4 viewProjection;
    double width = 1, height = 1;

    Vec3 toPage(const Vec3 &p) const; // x, y on the page; z the depth (−1 near to 1 far)
    // The point of the model's space on the page at (x, y), as deep as `depthOf` is from the camera.
    Vec3 fromPage(double x, double y, const Vec3 &depthOf) const;
};
Projection projectionOf(const Model &model, const OrbitCamera &camera, double width, double height);

} // namespace mothion::rig3d
