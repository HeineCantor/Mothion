#include "core/document/Document.h"

#include <algorithm>
#include <cmath>

namespace mothion::doc {

Document::Document(int width, int height, FrameRate frameRate)
    : m_width(width)
    , m_height(height)
    , m_frameRate(frameRate)
    , m_frameCount(std::max(1, int(std::lround(frameRate.fps())))) // one second by default
{
}

int Document::indexOf(CelId id) const
{
    const auto it = std::find_if(m_cels.begin(), m_cels.end(),
                                 [id](const auto &l) { return l->id() == id; });
    return it == m_cels.end() ? -1 : int(it - m_cels.begin());
}

Cel *Document::cel(CelId id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_cels[std::size_t(i)].get();
}

const Cel *Document::cel(CelId id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_cels[std::size_t(i)].get();
}

Cel &Document::insertCel(int index, std::unique_ptr<Cel> cel)
{
    index = std::clamp(index, 0, celCount());
    return **m_cels.insert(m_cels.begin() + index, std::move(cel));
}

std::unique_ptr<Cel> Document::takeCel(CelId id)
{
    const int i = indexOf(id);
    if (i < 0)
        return nullptr;
    std::unique_ptr<Cel> taken = std::move(m_cels[std::size_t(i)]);
    m_cels.erase(m_cels.begin() + i);
    return taken;
}

void Document::moveCel(CelId id, int newIndex)
{
    if (auto taken = takeCel(id))
        insertCel(newIndex, std::move(taken));
}

void Document::restoreIdCounters(CelId nextCel, DrawingId nextDrawing)
{
    CelId celFloor = 1;
    DrawingId drawingFloor = 1;
    for (const auto &l : m_cels) {
        celFloor = std::max(celFloor, l->id() + 1);
        if (!l->drawings().empty())
            drawingFloor = std::max(drawingFloor, l->drawings().rbegin()->first + 1);
    }
    m_nextCelId = std::max(nextCel, celFloor);
    m_nextDrawingId = std::max(nextDrawing, drawingFloor);
}

} // namespace mothion::doc
