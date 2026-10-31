#include "core/StrokeSmoother.h"

#include <algorithm>

namespace mothion {

void StrokeSmoother::setStrength(double strength)
{
    m_strength = std::clamp(strength, 0.0, 0.95);
}

PenSample StrokeSmoother::filter(const PenSample &in)
{
    if (!m_hasState) {
        m_state = in;
        m_hasState = true;
        return in;
    }
    const double k = 1.0 - m_strength;
    PenSample out = in; // timestamp, tilt, buttons etc. are passed through
    out.x = m_state.x + (in.x - m_state.x) * k;
    out.y = m_state.y + (in.y - m_state.y) * k;
    out.pressure = float(m_state.pressure + (in.pressure - m_state.pressure) * k);
    m_state = out;
    return out;
}

} // namespace mothion
