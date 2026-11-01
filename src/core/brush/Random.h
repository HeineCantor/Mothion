#pragma once

#include <cstdint>

namespace mothion::brush {

// Linear congruential generator used for every random choice of a stroke
// (dynamics, spacing, tip order, spray). Seeded once per stroke, so the same
// seed and the same input give exactly the same dabs (replay, tests).
//
//   x = x * 1103515245 + 1234567  (mod 2^32),   value = bits 16..30 / 32768
class Lcg
{
public:
    explicit Lcg(std::uint32_t seed = 1) : m_x(seed) {}

    // Uniform in [0, 1).
    double next01()
    {
        m_x = m_x * 1103515245u + 1234567u;
        return double((m_x >> 16) & 0x7FFFu) / 32768.0;
    }

    std::uint32_t state() const { return m_x; }

private:
    std::uint32_t m_x;
};

} // namespace mothion::brush
