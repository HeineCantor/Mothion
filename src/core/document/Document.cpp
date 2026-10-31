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

int Document::indexOf(LayerId id) const
{
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
                                 [id](const auto &l) { return l->id() == id; });
    return it == m_layers.end() ? -1 : int(it - m_layers.begin());
}

Layer *Document::layer(LayerId id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_layers[std::size_t(i)].get();
}

const Layer *Document::layer(LayerId id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_layers[std::size_t(i)].get();
}

Layer &Document::insertLayer(int index, std::unique_ptr<Layer> layer)
{
    index = std::clamp(index, 0, layerCount());
    return **m_layers.insert(m_layers.begin() + index, std::move(layer));
}

std::unique_ptr<Layer> Document::takeLayer(LayerId id)
{
    const int i = indexOf(id);
    if (i < 0)
        return nullptr;
    std::unique_ptr<Layer> taken = std::move(m_layers[std::size_t(i)]);
    m_layers.erase(m_layers.begin() + i);
    return taken;
}

void Document::moveLayer(LayerId id, int newIndex)
{
    if (auto taken = takeLayer(id))
        insertLayer(newIndex, std::move(taken));
}

} // namespace mothion::doc
