#pragma once

#include <vector>

namespace mothion::tween {

// A gray picture of floats (0 empty, 1 full): what the registration looks at, e.g. how much ink there is at a point.
struct Image
{
    int width = 0, height = 0;
    std::vector<float> v;

    Image() = default;
    Image(int w, int h) : width(w), height(h), v(std::size_t(w) * std::size_t(h), 0.0f) {}
    float at(int x, int y) const { return v[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
    float &at(int x, int y) { return v[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
    // Bilinear; outside the picture it is 0.
    float sample(double x, double y) const;
    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// Where each point of one picture goes to in another: a displacement for every pixel.
struct Field
{
    int width = 0, height = 0;
    std::vector<float> dx, dy;

    Field() = default;
    Field(int w, int h) : width(w), height(h), dx(std::size_t(w) * std::size_t(h), 0.0f), dy(dx) {}
    // Bilinear, the edge's value beyond it.
    void sample(double x, double y, double &outDx, double &outDy) const;
    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// The picture made `factor` times smaller (the mean of each block of pixels).
Image downscaled(const Image &image, int factor);
// The picture blurred (a Gaussian of `sigma` pixels).
Image blurred(const Image &image, double sigma);
// The field seen on a grid of another size: resampled, its values scaled by the ratio of the sizes.
Field resized(const Field &field, int width, int height);

struct RegisterParams
{
    int levels = 4;          // pyramid levels, each half the size of the one before
    int iterations = 40;     // at each level
    double blur = 1.2;       // px, on each level: the lines become ridges that attract from some way off
    double smooth = 1.6;     // px, the blur of the field after each step: keeps it from tearing
    double maxStep = 0.8;    // px, the most a point moves in a step
};

// The displacement s on `fixed`'s grid such that moving(x + s(x)) is about fixed(x): where each point of `fixed`
// is in `moving`. Found coarse to fine, by demons (Thirion): each point is pushed along the picture's slope, by how
// much the two differ there, and the field is smoothed. Meant for pictures of lines of about the same shape (a pose
// and the next one): it follows shifts of a few line widths per level, and leaves alone what has no counterpart.
Field registerImages(const Image &fixed, const Image &moving, const RegisterParams &params = {});

} // namespace mothion::tween
