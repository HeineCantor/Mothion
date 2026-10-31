#include "core/document/Layer.h"

#include <cassert>
#include <set>

namespace mothion::doc {

Layer::Layer(LayerId id, LayerProperties properties)
    : m_id(id)
    , m_properties(std::move(properties))
{
}

Drawing *Layer::drawing(DrawingId id)
{
    const auto it = m_drawings.find(id);
    return it == m_drawings.end() ? nullptr : &it->second;
}

const Drawing *Layer::drawing(DrawingId id) const
{
    const auto it = m_drawings.find(id);
    return it == m_drawings.end() ? nullptr : &it->second;
}

Drawing &Layer::addDrawing(Drawing drawing)
{
    assert(drawing.id != kNoId && !m_drawings.contains(drawing.id));
    const DrawingId id = drawing.id;
    return m_drawings.emplace(id, std::move(drawing)).first->second;
}

std::optional<Drawing> Layer::takeDrawing(DrawingId id)
{
    auto node = m_drawings.extract(id);
    if (node.empty())
        return std::nullopt;
    return std::move(node.mapped());
}

const Drawing *Layer::drawingAt(int frame) const
{
    return drawing(m_timing.drawingAt(frame));
}

std::string Layer::nextDrawingName() const
{
    std::set<std::string> used;
    for (const auto &[id, d] : m_drawings)
        used.insert(d.name);
    for (int n = 1;; ++n)
        if (!used.contains(std::to_string(n)))
            return std::to_string(n);
}

} // namespace mothion::doc
