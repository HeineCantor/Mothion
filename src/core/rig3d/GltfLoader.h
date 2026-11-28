#pragma once

#include "core/rig3d/Model.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mothion::rig3d {

// A model read from the bytes of a glTF file: binary (.glb), or JSON (.gltf) whose buffers and images are in it (data:
// URIs). One that names files beside it is refused: the project keeps the model's bytes whole, so it needs them all in
// one. The default scene (or the first) is read: its nodes, meshes of triangles (positions, normals, the first UVs, the
// first set of joints and weights), morph targets, skins, and the base color of the materials (factor and texture).
// Animations, cameras and lights are left out. On failure: nothing, and `error` says why.
std::optional<Model> loadGltf(const std::vector<std::uint8_t> &bytes, std::string *error = nullptr);

} // namespace mothion::rig3d
