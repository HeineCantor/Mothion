#pragma once

#include <deque>

namespace mothion::brush {

// Pen speed as a 0..1 input for dynamics.
//
// Distance over time across the last few samples, in screen pixels per
// millisecond, divided by a reference speed (8 px/ms reads as 1) and capped
// at 1. Averaging over `history` samples steadies the jitter of tablets that
// report at uneven intervals. Without usable timestamps (same time twice)
// it falls back to distance per sample, 20 px reading as 1.
class VelocityMeter
{
public:
    explicit VelocityMeter(int history = 2) : m_history(history < 2 ? 2 : history) {}

    void reset() { m_samples.clear(); }

    // `t` in seconds; x, y in screen pixels. Returns the speed after this sample.
    double sample(double t, double x, double y);

private:
    struct Sample
    {
        double t, x, y, distance;
    };
    int m_history;
    std::deque<Sample> m_samples;
};

} // namespace mothion::brush
