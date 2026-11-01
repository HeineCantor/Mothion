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

    // Adds a pin where the deformed shape is at `at`; nothing if no mesh is there.
    std::optional<int> addPin(Vec2 at);
    void removePin(int pin);
    // Moves a pin and solves its mesh (`iterations` ARAP steps from the current state).
    void movePin(int pin, Vec2 to, int iterations = 8);
    // The pin within `radius` of `at` (nearest), if any.
    std::optional<int> pinAt(Vec2 at, double radius) const;

    // Rebuilds the meshes with other settings, keeping pins and deformation.
    void setSettings(RigSettings settings);

private:
    void buildMeshes();
    void remesh(int mesh);
    std::optional<std::pair<int, Vec2>> restPointUnder(Vec2 at) const; // (mesh, rest point)

    Bitmask m_shape;
    RigSettings m_settings;
    std::vector<Mesh> m_meshes;
    std::vector<Pin> m_pins;
};

// Barycentric coordinates of p in triangle (a, b, c).
std::optional<std::array<double, 3>> barycentric(Vec2 a, Vec2 b, Vec2 c, Vec2 p, double tolerance = 1e-9);

} // namespace mothion::puppet
