#pragma once

#include "core/rig3d/Math3d.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A rigged 3D model as Mothion keeps it once read (see GltfLoader.h): a tree of nodes (the bones are nodes), the meshes
// some of them carry, the skins that bend a mesh by its bones, and what its surfaces look like.
namespace mothion::rig3d {

struct Node
{
    std::string name;
    int parent = -1;
    std::vector<int> children;
    // Where it is in its parent at rest (glTF's translation, rotation, scale). A pose changes the first two.
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1, 1, 1};
    int mesh = -1; // the mesh it carries, if any
    int skin = -1; // the skin that bends that mesh, if any
};

struct Skin
{
    std::vector<int> joints;        // the nodes that are its bones
    std::vector<Mat4> inverseBind;  // one for each: the model's space at bind to the bone's
};

// Sparse weights in compressed rows (CSR): the bones of vertex v and how much each holds it are entries offsets[v] up
// to offsets[v + 1] of `joint` (an index into the skin's joints) and `weight`.
struct SkinWeights
{
    std::vector<std::uint32_t> offsets; // vertexCount + 1
    std::vector<std::uint16_t> joint;
    std::vector<float> weight;

    bool isEmpty() const { return offsets.empty(); }
};

// A shape the mesh can blend toward (a blend shape: an expression of a face): how far each vertex (and its normal) moves
// at weight 1.
struct MorphTarget
{
    std::vector<float> positions; // 3 for each vertex
    std::vector<float> normals;   // 3 for each vertex, or empty
};

struct Primitive
{
    std::vector<float> positions; // x y z for each vertex
    std::vector<float> normals;   // x y z for each vertex (made from the faces when the file has none)
    std::vector<float> uvs;       // u v for each vertex, or empty
    std::vector<std::uint32_t> indices; // three for each triangle
    int material = -1;
    SkinWeights weights;          // empty for a mesh no skin bends
    std::vector<MorphTarget> targets;

    std::size_t vertexCount() const { return positions.size() / 3; }
};

struct Mesh
{
    std::string name;
    std::vector<Primitive> primitives;
    std::vector<std::string> targetNames; // the names of its morph targets (the same for each primitive)
    std::vector<double> targetWeights;    // their weights at rest
};

struct Material
{
    std::string name;
    std::array<float, 4> baseColor{1, 1, 1, 1}; // linear RGBA
    int texture = -1;                           // its base color texture (an index into images), if any
    bool doubleSided = false;
    bool blend = false; // alpha blended (glTF's BLEND mode), else opaque or cut out
};

// An image of a texture as the file holds it (PNG or JPEG bytes): decoded by the one that draws.
struct Image
{
    std::vector<std::uint8_t> bytes;
    std::string mimeType;
};

// A blend shape as the pose sees it: a morph target of a mesh, and its name.
struct MorphSlot
{
    int mesh = 0;
    int target = 0;
    std::string name;
};

struct Model
{
    std::vector<Node> nodes;
    std::vector<int> order;  // the nodes parents first (each node after its parent)
    std::vector<Skin> skins;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<MorphSlot> morphs; // all the morph targets of all the meshes, in order
    // What its meshes cover in the rest pose: the middle and the half diagonal (0: not worked out, see computeRestBounds()).
    Vec3 boundsCenter;
    double boundsRadius = 0;

    // The nodes that are bones to pose: those of the skins, or, a model with no skin, all the nodes.
    std::vector<int> joints() const;
    bool isJoint(int node) const;
    // The rest pose's global matrices of the nodes (the model's space).
    std::vector<Mat4> restGlobals() const;
};

} // namespace mothion::rig3d
