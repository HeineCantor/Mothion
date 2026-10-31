#include "core/RateMeter.h"

namespace mothion {

void RateMeter::add(double nowSeconds)
{
    m_times.push_back(nowSeconds);
    prune(nowSeconds);
}

double RateMeter::rateHz(double nowSeconds)
{
    prune(nowSeconds);
    return double(m_times.size()) / m_window;
}

void RateMeter::prune(double nowSeconds)
{
    while (!m_times.empty() && m_times.front() < nowSeconds - m_window)
        m_times.pop_front();
}

} // namespace mothion
