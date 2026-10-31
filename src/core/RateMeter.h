#pragma once

#include <deque>

namespace mothion {

// Events per second over a sliding window.
//
// Fed with *arrival* times (a steady clock), not with event timestamps:
// we want to know how many samples the application actually receives.
class RateMeter
{
public:
    explicit RateMeter(double windowSeconds = 1.0) : m_window(windowSeconds) {}

    void add(double nowSeconds);
    double rateHz(double nowSeconds); // drops events older than the window

private:
    void prune(double nowSeconds);

    double m_window;
    std::deque<double> m_times;
};

} // namespace mothion
