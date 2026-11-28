#include "core/rig3d/GltfLoader.h"

#include "core/rig3d/Pose.h"

#include <charconv>
#include <cstring>

namespace {
// The numbers of the JSON read whatever the locale (atof, cgltf's own, takes "0.8" for 0 where the decimal point is a
// comma, as in Italian).
double jsonNumber(const char *text)
{
    double value = 0;
    std::from_chars(text, text + std::strlen(text), value);
    return value;
}
} // namespace

#define CGLTF_ATOF(str) jsonNumber(str)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace mothion::rig3d {

namespace {

struct DataFree
{
    void operator()(cgltf_data *d) const { cgltf_free(d); }
};

std::vector<float> readFloats(const cgltf_accessor *accessor, int components)
{
    std::vector<float> out;
    if (!accessor)
        return out;
    out.resize(accessor->count * std::size_t(components));
    for (cgltf_size i = 0; i < accessor->count; ++i)
        cgltf_accessor_read_float(accessor, i, out.data() + i * std::size_t(components), cgltf_size(components));
    return out;
}

// The normal of each vertex from the faces round it, weighted by their area.
std::vector<float> faceNormals(const std::vector<float> &positions, const std::vector<std::uint32_t> &indices)
{
    std::vector<Vec3> n(positions.size() / 3);
    auto p = [&](std::uint32_t i) { return Vec3{positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]}; };
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const std::uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        if (a >= n.size() || b >= n.size() || c >= n.size())
            continue;
        const Vec3 f = cross(p(b) - p(a), p(c) - p(a));
        n[a] += f;
        n[b] += f;
        n[c] += f;
    }
    std::vector<float> out;
    out.reserve(positions.size());
    for (const Vec3 &v : n) {
        const Vec3 u = normalized(v);
        out.insert(out.end(), {float(u.x), float(u.y), float(u.z)});
    }
    return out;
}

// The triangles of a primitive as a list of indices, whatever its mode (triangles, strip, fan).
std::vector<std::uint32_t> triangles(const cgltf_primitive &prim, std::size_t vertexCount)
{
    std::vector<std::uint32_t> list;
    if (prim.indices) {
        list.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            list[i] = std::uint32_t(cgltf_accessor_read_index(prim.indices, i));
    } else {
        list.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
            list[i] = std::uint32_t(i);
    }
    if (prim.type == cgltf_primitive_type_triangles)
        return list;
    std::vector<std::uint32_t> out;
    for (std::size_t i = 2; i < list.size(); ++i) {
        if (prim.type == cgltf_primitive_type_triangle_strip)
            out.insert(out.end(), i % 2 ? std::initializer_list<std::uint32_t>{list[i - 1], list[i - 2], list[i]}
                                        : std::initializer_list<std::uint32_t>{list[i - 2], list[i - 1], list[i]});
        else // a fan
            out.insert(out.end(), {list[0], list[i - 1], list[i]});
    }
    return out;
}

} // namespace

std::optional<Model> loadGltf(const std::vector<std::uint8_t> &bytes, std::string *error)
{
    auto fail = [&](const std::string &why) -> std::optional<Model> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (bytes.empty())
        return fail("The file is empty.");
    cgltf_options options{};
    cgltf_data *raw = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &raw) != cgltf_result_success)
        return fail("This is not a glTF file, or it is damaged.");
    std::unique_ptr<cgltf_data, DataFree> data(raw);
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        const cgltf_buffer &b = data->buffers[i];
        if (b.uri && std::strncmp(b.uri, "data:", 5) != 0)
            return fail("The model keeps its data in other files (" + std::string(b.uri)
                        + "): export it as one binary glTF (.glb).");
    }
    // Only buffers in the file or in data: URIs are loaded (no path: none other is looked for).
    if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success)
        return fail("The model's data could not be read.");
    if (cgltf_validate(data.get()) != cgltf_result_success)
        return fail("The model's data are not consistent.");

    Model model;
    std::map<const cgltf_node *, int> nodeIndex;
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
        nodeIndex[&data->nodes[i]] = int(i);
    std::map<const cgltf_mesh *, int> meshIndex;
    std::map<const cgltf_skin *, int> skinIndex;
    std::map<const cgltf_material *, int> materialIndex;
    std::map<const cgltf_image *, int> imageIndex;

    // Images: their bytes, from a buffer view or a data: URI.
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image &img = data->images[i];
        Image out;
        if (img.mime_type)
            out.mimeType = img.mime_type;
        if (img.buffer_view) {
            const auto *start = static_cast<const std::uint8_t *>(img.buffer_view->buffer->data) + img.buffer_view->offset;
            out.bytes.assign(start, start + img.buffer_view->size);
        } else if (img.uri && std::strncmp(img.uri, "data:", 5) == 0) {
            const char *comma = std::strchr(img.uri, ',');
            if (comma && std::strstr(img.uri, ";base64")) {
                const char *payload = comma + 1;
                const std::size_t length = std::strlen(payload);
                void *decoded = nullptr;
                const cgltf_size size = length / 4 * 3 - (length >= 1 && payload[length - 1] == '=')
                                        - (length >= 2 && payload[length - 2] == '=');
                if (cgltf_load_buffer_base64(&options, size, payload, &decoded) == cgltf_result_success) {
                    out.bytes.assign(static_cast<std::uint8_t *>(decoded), static_cast<std::uint8_t *>(decoded) + size);
                    std::free(decoded);
                }
            }
        }
        imageIndex[&img] = int(model.images.size());
        model.images.push_back(std::move(out));
    }
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material &m = data->materials[i];
        Material out;
        if (m.name)
            out.name = m.name;
        if (m.has_pbr_metallic_roughness) {
            const auto &pbr = m.pbr_metallic_roughness;
            out.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                             pbr.base_color_factor[3]};
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                out.texture = imageIndex[pbr.base_color_texture.texture->image];
        }
        out.doubleSided = m.double_sided;
        out.blend = m.alpha_mode == cgltf_alpha_mode_blend;
        materialIndex[&m] = int(model.materials.size());
        model.materials.push_back(std::move(out));
    }

    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh &m = data->meshes[i];
        Mesh mesh;
        if (m.name)
            mesh.name = m.name;
        for (cgltf_size t = 0; t < m.target_names_count; ++t)
            mesh.targetNames.emplace_back(m.target_names[t] ? m.target_names[t] : "");
        for (cgltf_size p = 0; p < m.primitives_count; ++p) {
            const cgltf_primitive &prim = m.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip
                && prim.type != cgltf_primitive_type_triangle_fan)
                continue; // points and lines: nothing to fill
            Primitive out;
            const cgltf_accessor *joints = nullptr, *weights = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute &attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position)
                    out.positions = readFloats(attr.data, 3);
                else if (attr.type == cgltf_attribute_type_normal)
                    out.normals = readFloats(attr.data, 3);
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                    out.uvs = readFloats(attr.data, 2);
                else if (attr.type == cgltf_attribute_type_joints && attr.index == 0)
                    joints = attr.data;
                else if (attr.type == cgltf_attribute_type_weights && attr.index == 0)
                    weights = attr.data;
            }
            if (out.positions.empty())
                continue;
            const std::size_t count = out.vertexCount();
            out.indices = triangles(prim, count);
            out.indices.erase(std::remove_if(out.indices.begin(), out.indices.end(), [count](std::uint32_t v) { return v >= count; }),
                              out.indices.end());
            out.indices.resize(out.indices.size() / 3 * 3);
            if (out.normals.size() != out.positions.size())
                out.normals = faceNormals(out.positions, out.indices);
            if (out.uvs.size() != count * 2)
                out.uvs.clear();
            if (prim.material)
                out.material = materialIndex[prim.material];
            // Weights: the four of each vertex into compressed rows, those of 0 left out, made to add up to 1 (glTF asks
            // for it; a file that does not keep it would shrink the mesh toward the origin).
            if (joints && weights && joints->count == count && weights->count == count) {
                out.weights.offsets.reserve(count + 1);
                out.weights.offsets.push_back(0);
                for (cgltf_size v = 0; v < count; ++v) {
                    cgltf_uint j[4] = {};
                    float w[4] = {};
                    cgltf_accessor_read_uint(joints, v, j, 4);
                    cgltf_accessor_read_float(weights, v, w, 4);
                    float sum = 0;
                    for (float x : w)
                        sum += std::max(0.0f, x);
                    for (int k = 0; k < 4; ++k)
                        if (w[k] > 0 && sum > 0) {
                            out.weights.joint.push_back(std::uint16_t(j[k]));
                            out.weights.weight.push_back(w[k] / sum);
                        }
                    out.weights.offsets.push_back(std::uint32_t(out.weights.joint.size()));
                }
            }
            for (cgltf_size t = 0; t < prim.targets_count; ++t) {
                MorphTarget target;
                for (cgltf_size a = 0; a < prim.targets[t].attributes_count; ++a) {
                    const cgltf_attribute &attr = prim.targets[t].attributes[a];
                    if (attr.type == cgltf_attribute_type_position && attr.data->count == count)
                        target.positions = readFloats(attr.data, 3);
                    else if (attr.type == cgltf_attribute_type_normal && attr.data->count == count)
                        target.normals = readFloats(attr.data, 3);
                }
                if (target.positions.empty())
                    target.positions.assign(count * 3, 0.0f);
                out.targets.push_back(std::move(target));
            }
            mesh.primitives.push_back(std::move(out));
        }
        std::size_t targets = 0;
        for (const Primitive &prim : mesh.primitives)
            targets = std::max(targets, prim.targets.size());
        for (Primitive &prim : mesh.primitives) // every primitive with as many targets (an empty one moves nothing)
            while (prim.targets.size() < targets)
                prim.targets.push_back({std::vector<float>(prim.positions.size(), 0.0f), {}});
        mesh.targetNames.resize(targets);
        mesh.targetWeights.assign(targets, 0.0);
        for (cgltf_size w = 0; w < m.weights_count && w < targets; ++w)
            mesh.targetWeights[w] = m.weights[w];
        meshIndex[&m] = int(model.meshes.size());
        model.meshes.push_back(std::move(mesh));
    }

    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const cgltf_skin &s = data->skins[i];
        Skin skin;
        for (cgltf_size j = 0; j < s.joints_count; ++j) {
            skin.joints.push_back(nodeIndex[s.joints[j]]);
            Mat4 inv;
            if (s.inverse_bind_matrices) {
                float f[16];
                cgltf_accessor_read_float(s.inverse_bind_matrices, j, f, 16);
                for (int k = 0; k < 16; ++k)
                    inv.m[std::size_t(k)] = f[k];
            }
            skin.inverseBind.push_back(inv);
        }
        skinIndex[&s] = int(model.skins.size());
        model.skins.push_back(std::move(skin));
    }

    model.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node &n = data->nodes[i];
        Node &out = model.nodes[i];
        out.name = n.name ? n.name : "Node " + std::to_string(i + 1);
        out.parent = n.parent ? nodeIndex[n.parent] : -1;
        for (cgltf_size c = 0; c < n.children_count; ++c)
            out.children.push_back(nodeIndex[n.children[c]]);
        if (n.has_matrix) {
            // A matrix in the file: split into translation, turn and scale (a shear, which glTF does not allow, is lost).
            Mat4 m;
            for (int k = 0; k < 16; ++k)
                m.m[std::size_t(k)] = n.matrix[k];
            const Mat3 l = m.linear();
            out.translation = m.translationPart();
            out.scale = {length(l.column(0)), length(l.column(1)), length(l.column(2))};
            if (l.determinant() < 0)
                out.scale.x = -out.scale.x;
            const Mat3 r = Mat3::fromColumns(l.column(0) / out.scale.x, l.column(1) / out.scale.y, l.column(2) / out.scale.z);
            out.rotation = r.orthonormalized().toQuat();
        } else {
            if (n.has_translation)
                out.translation = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation) // glTF: x y z w
                out.rotation = Quat{n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]}.normalized();
            if (n.has_scale)
                out.scale = {n.scale[0], n.scale[1], n.scale[2]};
        }
        if (n.mesh)
            out.mesh = meshIndex[n.mesh];
        if (n.skin && n.mesh)
            out.skin = skinIndex[n.skin];
    }

    // The nodes of the default scene, parents first; the others are left out.
    const cgltf_scene *scene = data->scene ? data->scene : data->scenes_count > 0 ? &data->scenes[0] : nullptr;
    std::vector<int> roots;
    if (scene) {
        for (cgltf_size r = 0; r < scene->nodes_count; ++r)
            roots.push_back(nodeIndex[scene->nodes[r]]);
    } else {
        for (std::size_t n = 0; n < model.nodes.size(); ++n)
            if (model.nodes[n].parent < 0)
                roots.push_back(int(n));
    }
    std::vector<int> stack(roots.rbegin(), roots.rend());
    std::vector<bool> seen(model.nodes.size(), false);
    while (!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        if (seen[std::size_t(n)])
            continue;
        seen[std::size_t(n)] = true;
        model.order.push_back(n);
        const auto &children = model.nodes[std::size_t(n)].children;
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            stack.push_back(*it);
    }
    // The skins' bones are posed even out of the scene's tree (a file may keep them apart).
    for (const Skin &skin : model.skins)
        for (int j : skin.joints) {
            int top = j;
            while (model.nodes[std::size_t(top)].parent >= 0)
                top = model.nodes[std::size_t(top)].parent;
            if (!seen[std::size_t(top)]) {
                stack.push_back(top);
                while (!stack.empty()) {
                    const int n = stack.back();
                    stack.pop_back();
                    if (seen[std::size_t(n)])
                        continue;
                    seen[std::size_t(n)] = true;
                    model.order.push_back(n);
                    for (int c : model.nodes[std::size_t(n)].children)
                        stack.push_back(c);
                }
            }
        }

    for (std::size_t m = 0; m < model.meshes.size(); ++m)
        for (std::size_t t = 0; t < model.meshes[m].targetNames.size(); ++t) {
            std::string name = model.meshes[m].targetNames[t];
            if (name.empty())
                name = (model.meshes[m].name.empty() ? "Shape" : model.meshes[m].name) + " " + std::to_string(t + 1);
            model.morphs.push_back({int(m), int(t), std::move(name)});
        }

    bool anyMesh = false;
    for (int n : model.order)
        anyMesh = anyMesh || model.nodes[std::size_t(n)].mesh >= 0;
    if (!anyMesh)
        return fail("The model has no mesh to show.");
    computeRestBounds(model);
    return model;
}

} // namespace mothion::rig3d
