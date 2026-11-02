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
    RigSettings settings() const { return m_settings; }
    // Whether any vertex is away from its rest position (by more than 1/1000 px).
    bool isDeformed() const;

    // Adds a pin where the deformed shape is at `at`; nothing if no mesh is there.
    std::optional<int> addPin(Vec2 at);
    void removePin(int pin);
    // Moves a pin and solves its mesh (`iterations` ARAP steps from the current state).
    void movePin(int pin, Vec2 to, int iterations = 8);
    // Moves several pins, each to its place, and solves each of their meshes once.
    void movePins(const std::vector<int> &pins, const std::vector<Vec2> &to, int iterations = 8);
    // The pin within `radius` of `at` (nearest), if any.
    std::optional<int> pinAt(Vec2 at, double radius) const;

    // A vertex of one of the meshes.
    struct VertexRef
    {
        int mesh = -1;
        int vertex = -1;
        bool operator==(const VertexRef &) const = default;
    };
    // The vertex nearest `at` on the mesh the deformed shape has there, if any.
    std::optional<VertexRef> vertexAt(Vec2 at) const;
    Vec2 position(VertexRef v) const { return m_meshes[std::size_t(v.mesh)].deformed[std::size_t(v.vertex)]; }

    // Local warp: the whole shape held except a disc of `radius` around each
    // grabbed vertex; they follow the pen together, the discs bend with them
    // (ARAP) from the shape as it is now, not from its rest (a small move
    // bends a little, whatever was done before); pins stay put unless
    // grabbed. Starts a drag with the pen at `at`.
    bool beginLocalDrag(const std::vector<VertexRef> &grabbed, Vec2 at, double radius);
    // The same with the vertex under `at`; false if no mesh is there.
    bool beginLocalDrag(Vec2 at, double radius);
    // Where the pen is now: each grabbed vertex moved by the pen's travel.
    void localDragTo(Vec2 to, int iterations = 8);
    // Another radius during the drag: the discs are measured again on the
    // shape as it was at the start, and the bend redone as if it had been
    // that radius all along (what falls out goes back to where it was).
    void setLocalRadius(double radius, int iterations = 8);
    // Ends the drag, the pins hold their mesh again.
    void endLocalDrag();
    bool isLocalDragging() const { return m_local.has_value(); }
    // The point a local drag at `at` would take (where it is now), if any.
    std::optional<Vec2> localGrabPoint(Vec2 at) const;
    // Where the vertices taken by the local drag are now.
    std::vector<Vec2> localDragPoints() const;

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
        struct Grabbed
        {
            int vertex = -1;
            std::size_t handle = 0; // its place among the mesh's handles
            Vec2 start;             // where it was at the start
        };
        struct Part // one mesh with grabbed vertices
        {
            int mesh = -1;
            std::vector<Vec2> start;   // the shape at the start of the drag
            std::vector<int> vertices; // the grabbed ones
            // Solves from `start`, as its rest.
            std::unique_ptr<ArapDeformer> deformer;
            std::vector<Vec2> targets; // of the deformer's handles
            std::vector<Grabbed> grabbed;
        };
        std::vector<Part> parts;
        Vec2 pressedAt; // the pen at the start
        Vec2 pen;       // and now
    };
    // Holds the pins and what lies out of the discs of `radius` (on the
    // shape at the start) where they were then; the grabbed ones move.
    void holdOutside(LocalDrag::Part &part, double radius);
    std::optional<LocalDrag> m_local;
};

// Barycentric coordinates of p in triangle (a, b, c).
std::optional<std::array<double, 3>> barycentric(Vec2 a, Vec2 b, Vec2 c, Vec2 p, double tolerance = 1e-9);

} // namespace mothion::puppet
