#pragma once

#include "core/PenSample.h"

namespace mothion {

// Minimal stabilizer: exponential moving average of position and pressure.
//
//   out = previous_out + (in - previous_out) * (1 - strength)
//
// strength 0 passes samples through unchanged; towards 1 the line gets
// smoother and lags more behind the pen. Being per-sample, the lag in time
// depends on the device rate. Good enough to test input, not a final brush engine.
class StrokeSmoother
{
public:
    void setStrength(double strength); // clamped to [0, 0.95]
    double strength() const { return m_strength; }

    // Forget the previous stroke; the next sample passes through unchanged.
    void reset() { m_hasState = false; }

    PenSample filter(const PenSample &in);

private:
    double m_strength = 0.0;
    bool m_hasState = false;
    PenSample m_state;
};

} // namespace mothion
