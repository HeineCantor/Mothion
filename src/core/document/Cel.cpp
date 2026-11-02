#include "core/document/Cel.h"

#include <cassert>
#include <set>

namespace mothion::doc {

Cel::Cel(CelId id, CelProperties properties)
    : m_id(id)
    , m_properties(std::move(properties))
{
}

Drawing *Cel::drawing(DrawingId id)
{
    const auto it = m_drawings.find(id);
    return it == m_drawings.end() ? nullptr : &it->second;
}

const Drawing *Cel::drawing(DrawingId id) const
{
    const auto it = m_drawings.find(id);
    return it == m_drawings.end() ? nullptr : &it->second;
}

Drawing &Cel::addDrawing(Drawing drawing)
{
    assert(drawing.id != kNoId && !m_drawings.contains(drawing.id));
    const DrawingId id = drawing.id;
    return m_drawings.emplace(id, std::move(drawing)).first->second;
}

std::optional<Drawing> Cel::takeDrawing(DrawingId id)
{
    auto node = m_drawings.extract(id);
    if (node.empty())
        return std::nullopt;
    return std::move(node.mapped());
}

const Drawing *Cel::drawingAt(int frame) const
{
    return drawing(m_timing.drawingAt(frame));
}

std::string Cel::nextDrawingName() const
{
    std::set<std::string> used;
    for (const auto &[id, d] : m_drawings)
        used.insert(d.name);
    for (int n = 1;; ++n)
        if (!used.contains(std::to_string(n)))
            return std::to_string(n);
}

} // namespace mothion::doc
