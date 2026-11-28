#include "core/rig3d/SoftwareRaster.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mothion::rig3d {

namespace {

struct Vertex
{
    double x, y, z, invW; // on the supersampled grid; depth; 1/w for perspective-correct UVs
    double u, v;
    double light;
};

// sRGB from linear, 0..1.
double toSrgb(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
}
double fromSrgb(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

} // namespace

void rasterize(const Model &model, const std::vector<PosedPrimitive> &meshes, const Projection &projection, const Vec3 &eye,
               const std::vector<TexturePixels> &textures, int width, int height, std::vector<std::uint32_t> &pixels,
               int samples)
{
    samples = std::clamp(samples, 1, 4);
    const int sw = width * samples, sh = height * samples;
    pixels.assign(std::size_t(width) * std::size_t(height), 0u);
    if (width <= 0 || height <= 0)
        return;
    std::vector<float> depth(std::size_t(sw) * std::size_t(sh), std::numeric_limits<float>::infinity());
    std::vector<float> color(std::size_t(sw) * std::size_t(sh) * 4, 0.0f); // linear premultiplied RGBA

    const Mat4 &vp = projection.viewProjection;
    for (const PosedPrimitive &posed : meshes) {
        const Primitive &prim = model.meshes[std::size_t(posed.mesh)].primitives[std::size_t(posed.primitive)];
        const Material material = prim.material >= 0 ? model.materials[std::size_t(prim.material)] : Material{};
        const TexturePixels *texture = material.texture >= 0 && std::size_t(material.texture) < textures.size()
                                               && textures[std::size_t(material.texture)].width > 0
                                           ? &textures[std::size_t(material.texture)]
                                           : nullptr;
        const std::size_t count = posed.positions.size() / 3;
        std::vector<Vertex> verts(count);
        std::vector<bool> behind(count, false);
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 p{posed.positions[i * 3], posed.positions[i * 3 + 1], posed.positions[i * 3 + 2]};
            const Vec3 n{posed.normals[i * 3], posed.normals[i * 3 + 1], posed.normals[i * 3 + 2]};
            const double cx = vp(0, 0) * p.x + vp(0, 1) * p.y + vp(0, 2) * p.z + vp(0, 3);
            const double cy = vp(1, 0) * p.x + vp(1, 1) * p.y + vp(1, 2) * p.z + vp(1, 3);
            const double cz = vp(2, 0) * p.x + vp(2, 1) * p.y + vp(2, 2) * p.z + vp(2, 3);
            const double cw = vp(3, 0) * p.x + vp(3, 1) * p.y + vp(3, 2) * p.z + vp(3, 3);
            behind[i] = cw <= 1e-9;
            const double iw = behind[i] ? 0 : 1 / cw;
            // A light from the camera: what faces it is lit, what does not is in the ambient light alone (both sides
            // for a double-sided material).
            const double facing = dot(normalized(n), normalized(eye - p));
            const double lambert = material.doubleSided ? std::abs(facing) : std::max(0.0, facing);
            verts[i] = {(cx * iw + 1) / 2 * sw, (1 - cy * iw) / 2 * sh, cz * iw, iw,
                        prim.uvs.empty() ? 0.0 : prim.uvs[i * 2], prim.uvs.empty() ? 0.0 : prim.uvs[i * 2 + 1],
                        0.35 + 0.65 * lambert};
        }
        for (std::size_t t = 0; t + 2 < prim.indices.size(); t += 3) {
            const std::uint32_t ia = prim.indices[t], ib = prim.indices[t + 1], ic = prim.indices[t + 2];
            if (ia >= count || ib >= count || ic >= count || behind[ia] || behind[ib] || behind[ic])
                continue; // a triangle partly behind the camera is left out (no clipping)
            const Vertex &a = verts[ia], &b = verts[ib], &c = verts[ic];
            const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::abs(area) < 1e-12)
                continue;
            if (!material.doubleSided && area > 0)
                continue; // seen from behind (counterclockwise in glTF is the front; y is down here)
            const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
            const int x1 = std::min(sw - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
            const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
            const int y1 = std::min(sh - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5, py = y + 0.5;
                    const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                    const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                    const double w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0)
                        continue;
                    const double z = w0 * a.z + w1 * b.z + w2 * c.z;
                    if (z < -1 || z > 1)
                        continue;
                    const std::size_t at = std::size_t(y) * std::size_t(sw) + std::size_t(x);
                    if (z >= depth[at])
                        continue;
                    double r = material.baseColor[0], g = material.baseColor[1], bl = material.baseColor[2],
                           al = material.baseColor[3];
                    if (texture) {
                        const double iw = w0 * a.invW + w1 * b.invW + w2 * c.invW;
                        const double u = (w0 * a.u * a.invW + w1 * b.u * b.invW + w2 * c.u * c.invW) / iw;
                        const double v = (w0 * a.v * a.invW + w1 * b.v * b.invW + w2 * c.v * c.invW) / iw;
                        const double fu = u - std::floor(u), fv = v - std::floor(v); // repeated
                        const int tx = std::clamp(int(fu * texture->width), 0, texture->width - 1);
                        const int ty = std::clamp(int(fv * texture->height), 0, texture->height - 1);
                        const std::uint32_t texel = texture->pixels[std::size_t(ty) * std::size_t(texture->width) + std::size_t(tx)];
                        r *= fromSrgb(((texel >> 16) & 0xff) / 255.0);
                        g *= fromSrgb(((texel >> 8) & 0xff) / 255.0);
                        bl *= fromSrgb((texel & 0xff) / 255.0);
                        al *= ((texel >> 24) & 0xff) / 255.0;
                    }
                    if (!material.blend && al < 0.5)
                        continue; // cut out
                    if (!material.blend)
                        al = 1;
                    const double light = w0 * a.light + w1 * b.light + w2 * c.light;
                    float *out = &color[at * 4];
                    if (material.blend) { // over what is there, not hiding what is behind it
                        out[0] = float(out[0] * (1 - al) + r * light * al);
                        out[1] = float(out[1] * (1 - al) + g * light * al);
                        out[2] = float(out[2] * (1 - al) + bl * light * al);
                        out[3] = float(out[3] * (1 - al) + al);
                    } else {
                        depth[at] = float(z);
                        out[0] = float(r * light);
                        out[1] = float(g * light);
                        out[2] = float(bl * light);
                        out[3] = 1.0f;
                    }
                }
        }
    }
    // The samples of each pixel averaged, then to 8-bit sRGB, premultiplied.
    const double n = double(samples * samples);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < samples; ++sy)
                for (int sx = 0; sx < samples; ++sx) {
                    const float *c = &color[(std::size_t(y * samples + sy) * std::size_t(sw) + std::size_t(x * samples + sx)) * 4];
                    r += c[0];
                    g += c[1];
                    b += c[2];
                    a += c[3];
                }
            r /= n, g /= n, b /= n, a /= n;
            if (a <= 0)
                continue;
            auto channel = [a](double premultiplied) {
                return std::uint32_t(std::lround(toSrgb(premultiplied / a) * a * 255.0));
            };
            pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] =
                (std::uint32_t(std::lround(a * 255.0)) << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
        }
}

} // namespace mothion::rig3d
