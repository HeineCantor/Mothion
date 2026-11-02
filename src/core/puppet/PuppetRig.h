#pragma once

#include "core/puppet/ArapDeformer.h"
#include "core/puppet/Contour.h"
#include "core/puppet/Triangulation.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace mothion::puppet {

struct RigSettings
{
    int density = 1; // 0 coarse, 1 normal, 2 fine
    int expand = 3;  // px of mesh around the shape
};

// The puppet warp of one shape: a mesh per separate part of it, the pins
// placed on them and the deformed position of every mesh vertex.
//
// A pin is a vertex of its mesh: adding or removing one rebuilds that mesh
// with the pins as extra points (the deformation so far is carried over by
// barycentric mapping), then moving a pin solves ARAP with all the mesh's
// pins held. Parts are independent: a pin only moves its own mesh.
class PuppetRig
{
public:
    struct Pin
    {
        int mesh = -1;
        int vertex = -1; // in that mesh
        Vec2 rest;       // on the undeformed shape
        Vec2 position;   // where it is now
    };
    struct Mesh
    {
        std::vector<Vec2> outline; // simplified, undeformed
        TriMesh rest;
        std::vector<Vec2> deformed; // one per rest vertex
        std::unique_ptr<ArapDeformer> deformer;
    };

    PuppetRig(const Bitmask &shape, RigSettings settings);

    const std::vector<Mesh> &meshes() const { return m_meshes; }
    const std::vector<Pin> &pins() const { return m_pins; }
    bool isEmpty() const { return m_meshes.empty(); }
    // Whether any vertex is away from its rest position (by more than 1/1000 px).
    bool isDeformed() const;

    // Adds a pin where the deformed shape is at `at`; nothing if no mesh is there.
    std::optional<int> addPin(Vec2 at);
    void removePin(int pin);
    // Moves a pin and solves its mesh (`iterations` ARAP steps from the current state).
    void movePin(int pin, Vec2 to, int iterations = 8);
    // The pin within `radius` of `at` (nearest), if any.
    std::optional<int> pinAt(Vec2 at, double radius) const;

    // Local warp: the whole shape held except a disc of `radius` around the
    // mesh vertex nearest `at`, which follows the pen; the disc bends with
    // it (ARAP), pins stay put. Starts a drag; false if no mesh is at `at`.
    bool beginLocalDrag(Vec2 at, double radius);
    // Where the pen is now; the grabbed point keeps its offset from it.
    void localDragTo(Vec2 to, int iterations = 8);
    // Ends the drag, the pins hold their mesh again.
    void endLocalDrag();
    bool isLocalDragging() const { return m_local.has_value(); }
    // The point a local drag at `at` would take (where it is now), if any.
    std::optional<Vec2> localGrabPoint(Vec2 at) const;
    // Where the point taken by the local drag is now.
    std::optional<Vec2> localDragPoint() const;

    // Rebuilds the meshes with other settings, keeping pins and deformation.
    void setSettings(RigSettings settings);

    // Everything a warp step changes (meshes, deformation, pins), to go back
    // to it: the undo steps inside an open warp.
    struct State
    {
        struct MeshState
        {
            std::vector<Vec2> outline;
            TriMesh rest;
            std::vector<Vec2> deformed;
        };
        std::vector<MeshState> meshes;
        std::vector<Pin> pins;
    };
    State state() const;
    // Back to `state` (a local drag in progress ends).
    void restore(const State &state);

private:
    void buildMeshes();
    void remesh(int mesh);
    std::optional<std::pair<int, Vec2>> restPointUnder(Vec2 at) const; // (mesh, rest point)
    // The pins' vertices of a mesh, once each, and where they are.
    void pinHandles(int mesh, std::vector<int> &handles, std::vector<Vec2> &targets) const;
    // The deformed vertex of a mesh nearest `at`.
    std::size_t nearestVertex(int mesh, Vec2 at) const;

    Bitmask m_shape;
    RigSettings m_settings;
    std::vector<Mesh> m_meshes;
    std::vector<Pin> m_pins;

    struct LocalDrag
    {
        int mesh = -1;
        int vertex = -1;           // the grabbed vertex
        std::vector<Vec2> targets; // of the mesh's handles, set at the start
        std::size_t grabbed = 0;   // the moving one among them
        Vec2 offset;               // grabbed vertex − pen, at the start
    };
    std::optional<LocalDrag> m_local;
};

// Barycentric coordinates of p in triangle (a, b, c).
std::optional<std::array<double, 3>> barycentric(Vec2 a, Vec2 b, Vec2 c, Vec2 p, double tolerance = 1e-9);

} // namespace mothion::puppet
