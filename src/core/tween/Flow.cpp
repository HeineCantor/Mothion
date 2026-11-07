#include "core/tween/Flow.h"

#include <algorithm>
#include <cmath>

namespace mothion::tween {

float Image::sample(double x, double y) const
{
    if (x <= -1.0 || y <= -1.0 || x >= width || y >= height)
        return 0.0f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = float(x - x0), fy = float(y - y0);
    auto pixel = [this](int px, int py) {
        return px < 0 || py < 0 || px >= width || py >= height ? 0.0f : at(px, py);
    };
    const float top = pixel(x0, y0) * (1 - fx) + pixel(x0 + 1, y0) * fx;
    const float bottom = pixel(x0, y0 + 1) * (1 - fx) + pixel(x0 + 1, y0 + 1) * fx;
    return top * (1 - fy) + bottom * fy;
}

void Field::sample(double x, double y, double &outDx, double &outDy) const
{
    if (isEmpty()) {
        outDx = outDy = 0.0;
        return;
    }
    x = std::clamp(x, 0.0, double(width - 1));
    y = std::clamp(y, 0.0, double(height - 1));
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float fx = float(x - x0), fy = float(y - y0);
    auto lerp = [&](const std::vector<float> &a) {
        auto at = [&](int px, int py) { return a[std::size_t(py) * std::size_t(width) + std::size_t(px)]; };
        const float top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
        const float bottom = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
        return double(top * (1 - fy) + bottom * fy);
    };
    outDx = lerp(dx);
    outDy = lerp(dy);
}

Image downscaled(const Image &image, int factor)
{
    if (factor <= 1)
        return image;
    Image out((image.width + factor - 1) / factor, (image.height + factor - 1) / factor);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            float sum = 0;
            int n = 0;
            for (int j = y * factor; j < std::min(image.height, (y + 1) * factor); ++j)
                for (int i = x * factor; i < std::min(image.width, (x + 1) * factor); ++i) {
                    sum += image.at(i, j);
                    ++n;
                }
            out.at(x, y) = n ? sum / float(n) : 0.0f;
        }
    return out;
}

namespace {

std::vector<float> gaussianKernel(double sigma)
{
    const int radius = std::max(1, int(std::ceil(sigma * 3.0)));
    std::vector<float> k(std::size_t(2 * radius + 1));
    float sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        k[std::size_t(i + radius)] = float(std::exp(-0.5 * i * i / (sigma * sigma)));
        sum += k[std::size_t(i + radius)];
    }
    for (float &c : k)
        c /= sum;
    return k;
}

// Separable blur of a channel stored row by row; the edge's value beyond the edge.
void blurChannel(std::vector<float> &data, int width, int height, double sigma)
{
    if (sigma < 0.3)
        return;
    const std::vector<float> k = gaussianKernel(sigma);
    const int radius = int(k.size() / 2);
    std::vector<float> tmp(data.size());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float s = 0;
            for (int i = -radius; i <= radius; ++i)
                s += k[std::size_t(i + radius)] * data[std::size_t(y) * std::size_t(width) + std::size_t(std::clamp(x + i, 0, width - 1))];
            tmp[std::size_t(y) * std::size_t(width) + std::size_t(x)] = s;
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float s = 0;
            for (int i = -radius; i <= radius; ++i)
                s += k[std::size_t(i + radius)] * tmp[std::size_t(std::clamp(y + i, 0, height - 1)) * std::size_t(width) + std::size_t(x)];
            data[std::size_t(y) * std::size_t(width) + std::size_t(x)] = s;
        }
}

} // namespace

Image blurred(const Image &image, double sigma)
{
    Image out = image;
    blurChannel(out.v, out.width, out.height, sigma);
    return out;
}

Field resized(const Field &field, int width, int height)
{
    Field out(width, height);
    if (field.isEmpty() || width <= 0 || height <= 0)
        return out;
    const double sx = double(field.width) / width, sy = double(field.height) / height;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            double dx, dy;
            field.sample((x + 0.5) * sx - 0.5, (y + 0.5) * sy - 0.5, dx, dy);
            out.dx[std::size_t(y) * std::size_t(width) + std::size_t(x)] = float(dx / sx); // a displacement in the new px
            out.dy[std::size_t(y) * std::size_t(width) + std::size_t(x)] = float(dy / sy);
        }
    return out;
}

Field registerImages(const Image &fixed, const Image &moving, const RegisterParams &params)
{
    if (fixed.isEmpty() || fixed.width != moving.width || fixed.height != moving.height)
        return {};
    // The pyramid, finest first.
    std::vector<Image> fine, coarse;
    fine.push_back(fixed);
    coarse.push_back(moving);
    for (int l = 1; l < params.levels; ++l) {
        if (fine.back().width < 8 || fine.back().height < 8)
            break;
        fine.push_back(downscaled(fine.back(), 2));
        coarse.push_back(downscaled(coarse.back(), 2));
    }
    Field field;
    for (int level = int(fine.size()) - 1; level >= 0; --level) {
        const Image a = blurred(fine[std::size_t(level)], params.blur);
        const Image b = blurred(coarse[std::size_t(level)], params.blur);
        const int w = a.width, h = a.height;
        field = field.isEmpty() ? Field(w, h) : resized(field, w, h);
        for (int it = 0; it < params.iterations; ++it) {
            Field step(w, h);
            double biggest = 0.0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
                    const double px = double(x) + double(field.dx[i]), py = double(y) + double(field.dy[i]);
                    const float bw = b.sample(px, py);
                    const float diff = bw - a.at(x, y); // positive: the moving picture has more here than the fixed
                    // The slope of both pictures, averaged (the "active" demons).
                    const double gx = 0.5 * ((a.sample(x + 1.0, y) - a.sample(x - 1.0, y)) * 0.5
                                             + (b.sample(px + 1.0, py) - b.sample(px - 1.0, py)) * 0.5);
                    const double gy = 0.5 * ((a.sample(x, y + 1.0) - a.sample(x, y - 1.0)) * 0.5
                                             + (b.sample(px, py + 1.0) - b.sample(px, py - 1.0)) * 0.5);
                    const double denom = gx * gx + gy * gy + double(diff) * double(diff);
                    if (denom < 1e-9)
                        continue;
                    // moving(x + s) should equal fixed(x): move s against the slope where moving has too much.
                    step.dx[i] = float(-diff * gx / denom);
                    step.dy[i] = float(-diff * gy / denom);
                    biggest = std::max(biggest, std::hypot(double(step.dx[i]), double(step.dy[i])));
                }
            if (biggest < 1e-5)
                break;
            const double scale = biggest > params.maxStep ? params.maxStep / biggest : 1.0;
            for (std::size_t i = 0; i < field.dx.size(); ++i) {
                field.dx[i] += float(step.dx[i] * scale);
                field.dy[i] += float(step.dy[i] * scale);
            }
            blurChannel(field.dx, w, h, params.smooth);
            blurChannel(field.dy, w, h, params.smooth);
        }
    }
    return field;
}

} // namespace mothion::tween
