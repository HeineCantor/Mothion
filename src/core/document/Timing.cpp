#include "core/document/Timing.h"

#include <algorithm>

namespace mothion::doc {

const Exposure *Timing::exposureAt(int frame) const
{
    // First run whose end is after `frame`; it contains the frame if it starts at or before it.
    const auto it = std::upper_bound(m_exposures.begin(), m_exposures.end(), frame,
                                     [](int f, const Exposure &e) { return f < e.end(); });
    return it != m_exposures.end() && it->contains(frame) ? &*it : nullptr;
}

DrawingId Timing::drawingAt(int frame) const
{
    const Exposure *e = exposureAt(frame);
    return e ? e->drawing : kNoId;
}

int Timing::lastFrame() const
{
    return m_exposures.empty() ? 0 : m_exposures.back().end();
}

bool Timing::usesDrawing(DrawingId drawing) const
{
    return std::any_of(m_exposures.begin(), m_exposures.end(),
                       [drawing](const Exposure &e) { return e.drawing == drawing; });
}

void Timing::setExposure(int start, int length, DrawingId drawing)
{
    if (length <= 0)
        return;
    const int end = start + length;
    std::vector<Exposure> result;
    result.reserve(m_exposures.size() + 2);
    for (const Exposure &e : m_exposures) {
        if (e.end() <= start || e.start >= end) {
            result.push_back(e); // untouched
            continue;
        }
        if (e.start < start) // part before the new range survives
            result.push_back({e.start, start - e.start, e.drawing});
        if (e.end() > end) // part after the new range survives
            result.push_back({end, e.end() - end, e.drawing});
    }
    if (drawing != kNoId)
        result.push_back({start, length, drawing});
    m_exposures = std::move(result);
    normalize();
}

bool Timing::resizeExposure(int frame, int newLength)
{
    if (newLength < 1)
        return false;
    const auto it = std::find_if(m_exposures.begin(), m_exposures.end(),
                                 [frame](const Exposure &e) { return e.contains(frame); });
    if (it == m_exposures.end())
        return false;
    const int oldEnd = it->end();
    const int delta = newLength - it->length;
    it->length = newLength;
    for (Exposure &e : m_exposures)
        if (e.start >= oldEnd)
            e.start += delta;
    normalize();
    return true;
}

void Timing::insertEmptyFrames(int frame, int count)
{
    if (count <= 0)
        return;
    std::vector<Exposure> result;
    for (const Exposure &e : m_exposures) {
        if (e.end() <= frame) {
            result.push_back(e);
        } else if (e.start >= frame) {
            result.push_back({e.start + count, e.length, e.drawing});
        } else { // crosses the insertion point: split around the gap
            result.push_back({e.start, frame - e.start, e.drawing});
            result.push_back({frame + count, e.end() - frame, e.drawing});
        }
    }
    m_exposures = std::move(result);
    normalize();
}

void Timing::removeFrames(int frame, int count)
{
    if (count <= 0)
        return;
    clear(frame, count);
    for (Exposure &e : m_exposures)
        if (e.start >= frame + count)
            e.start -= count;
    normalize();
}

void Timing::removeDrawing(DrawingId drawing)
{
    std::erase_if(m_exposures, [drawing](const Exposure &e) { return e.drawing == drawing; });
}

void Timing::normalize()
{
    std::erase_if(m_exposures, [](const Exposure &e) { return e.length <= 0 || e.drawing == kNoId; });
    std::sort(m_exposures.begin(), m_exposures.end(),
              [](const Exposure &a, const Exposure &b) { return a.start < b.start; });
    std::vector<Exposure> merged;
    merged.reserve(m_exposures.size());
    for (const Exposure &e : m_exposures) {
        if (!merged.empty() && merged.back().end() == e.start && merged.back().drawing == e.drawing)
            merged.back().length += e.length;
        else
            merged.push_back(e);
    }
    m_exposures = std::move(merged);
}

} // namespace mothion::doc
