#pragma once

// A rigged glTF model made in memory for the tests (no binary files in the repository).

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace rig3d_fixture {

// Appends `data` to `buffer` on a 4-byte boundary and returns where it starts.
template <class T>
inline int append(QByteArray &buffer, const std::vector<T> &data)
{
    while (buffer.size() % 4)
        buffer.append('\0');
    const int at = int(buffer.size());
    buffer.append(reinterpret_cast<const char *>(data.data()), qsizetype(data.size() * sizeof(T)));
    return at;
}

// A glTF arm along +y: bones 0 (at the origin), 1, 2 and 3 (the tip) a unit apart, a square tube round them bent by
// the skin (each ring held by the bone below it, wholly), and a node carrying the mesh. With `targets`, a morph target
// that moves every vertex by +1 in x. As a .gltf with its buffer in a data: URI.
inline std::vector<std::uint8_t> armGltf(bool targets = false)
{
    std::vector<float> positions, morph;
    std::vector<std::uint16_t> joints;
    std::vector<float> weights;
    std::vector<std::uint32_t> indices;
    const int rings = 7;
    for (int r = 0; r < rings; ++r) {
        const float y = r * 0.5f;
        for (auto [x, z] : {std::pair{0.1f, 0.1f}, {-0.1f, 0.1f}, {-0.1f, -0.1f}, {0.1f, -0.1f}}) {
            positions.insert(positions.end(), {x, y, z});
            morph.insert(morph.end(), {1.0f, 0.0f, 0.0f});
            joints.insert(joints.end(), {std::uint16_t(std::min(2, int(y))), 0, 0, 0});
            weights.insert(weights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
        }
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t a = std::uint32_t(r * 4 + k), b = std::uint32_t(r * 4 + (k + 1) % 4);
            indices.insert(indices.end(), {a, b + 4, b, a, a + 4, b + 4}); // counterclockwise seen from outside
        }
    std::vector<float> inverseBind;
    for (int j = 0; j < 4; ++j) {
        const std::vector<float> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, float(-j), 0, 1};
        inverseBind.insert(inverseBind.end(), m.begin(), m.end());
    }
    QByteArray buffer;
    const int posAt = append(buffer, positions), jointAt = append(buffer, joints), weightAt = append(buffer, weights),
              indexAt = append(buffer, indices), bindAt = append(buffer, inverseBind), morphAt = append(buffer, morph);
    const int vertices = int(positions.size() / 3);
    auto view = [](int offset, qsizetype length) { return QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", int(length)}}; };
    QJsonArray views{view(posAt, qsizetype(positions.size() * 4)), view(jointAt, qsizetype(joints.size() * 2)),
                     view(weightAt, qsizetype(weights.size() * 4)), view(indexAt, qsizetype(indices.size() * 4)),
                     view(bindAt, qsizetype(inverseBind.size() * 4)), view(morphAt, qsizetype(morph.size() * 4))};
    QJsonArray accessors{
        QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", vertices}, {"type", "VEC3"},
                    {"min", QJsonArray{-0.1, 0, -0.1}}, {"max", QJsonArray{0.1, 3, 0.1}}},
        QJsonObject{{"bufferView", 1}, {"componentType", 5123}, {"count", vertices}, {"type", "VEC4"}},
        QJsonObject{{"bufferView", 2}, {"componentType", 5126}, {"count", vertices}, {"type", "VEC4"}},
        QJsonObject{{"bufferView", 3}, {"componentType", 5125}, {"count", int(indices.size())}, {"type", "SCALAR"}},
        QJsonObject{{"bufferView", 4}, {"componentType", 5126}, {"count", 4}, {"type", "MAT4"}},
        QJsonObject{{"bufferView", 5}, {"componentType", 5126}, {"count", vertices}, {"type", "VEC3"},
                    {"min", QJsonArray{1, 0, 0}}, {"max", QJsonArray{1, 0, 0}}},
    };
    QJsonObject primitive{{"attributes", QJsonObject{{"POSITION", 0}, {"JOINTS_0", 1}, {"WEIGHTS_0", 2}}}, {"indices", 3}};
    QJsonObject mesh{{"name", "Arm"}};
    if (targets) {
        primitive["targets"] = QJsonArray{QJsonObject{{"POSITION", 5}}};
        mesh["extras"] = QJsonObject{{"targetNames", QJsonArray{"Wide"}}};
        mesh["weights"] = QJsonArray{0.0};
    }
    mesh["primitives"] = QJsonArray{primitive};
    QJsonArray nodes{
        QJsonObject{{"name", "Root"}, {"children", QJsonArray{1}}},
        QJsonObject{{"name", "Upper"}, {"translation", QJsonArray{0, 1, 0}}, {"children", QJsonArray{2}}},
        QJsonObject{{"name", "Lower"}, {"translation", QJsonArray{0, 1, 0}}, {"children", QJsonArray{3}}},
        QJsonObject{{"name", "Tip"}, {"translation", QJsonArray{0, 1, 0}}},
        QJsonObject{{"name", "Body"}, {"mesh", 0}, {"skin", 0}},
    };
    QJsonObject root{
        {"asset", QJsonObject{{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0, 4}}}}},
        {"nodes", nodes},
        {"meshes", QJsonArray{mesh}},
        {"skins", QJsonArray{QJsonObject{{"joints", QJsonArray{0, 1, 2, 3}}, {"inverseBindMatrices", 4}}}},
        {"accessors", accessors},
        {"bufferViews", views},
        {"buffers", QJsonArray{QJsonObject{{"byteLength", int(buffer.size())},
                                           {"uri", QString("data:application/octet-stream;base64,")
                                                       + QString::fromLatin1(buffer.toBase64())}}}},
    };
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return {json.begin(), json.end()};
}

} // namespace rig3d_fixture
