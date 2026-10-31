#pragma once

#include "core/document/Layer.h"

#include <memory>
#include <vector>

namespace mothion::doc {

// Frames per second as a fraction, so 23.976 (24000/1001) is exact.
struct FrameRate
{
    int numerator = 24;
    int denominator = 1;

    double fps() const { return double(numerator) / denominator; }
    bool operator==(const FrameRate &) const = default;
};

// The whole project: canvas size, frame rate, scene length and the layer
// stack. Changes that must be undoable go through Commands (see History);
// the methods here are the raw operations those commands use.
class Document
{
public:
    Document(int width, int height, FrameRate frameRate = {});

    int width() const { return m_width; }
    int height() const { return m_height; }
    FrameRate frameRate() const { return m_frameRate; }
    void setFrameRate(FrameRate rate) { m_frameRate = rate; }

    // Scene length in frames (>= 1). Exposures may extend past it.
    int frameCount() const { return m_frameCount; }
    void setFrameCount(int count) { m_frameCount = count < 1 ? 1 : count; }

    Pixel paperColor() const { return m_paperColor; }
    void setPaperColor(Pixel color) { m_paperColor = color; }

    // Layers from bottom (index 0) to top.
    int layerCount() const { return int(m_layers.size()); }
    Layer &layerAt(int index) { return *m_layers[std::size_t(index)]; }
    const Layer &layerAt(int index) const { return *m_layers[std::size_t(index)]; }
    int indexOf(LayerId id) const; // -1 if absent
    Layer *layer(LayerId id);
    const Layer *layer(LayerId id) const;

    // Layers are heap objects, so Layer references stay valid across reordering.
    Layer &insertLayer(int index, std::unique_ptr<Layer> layer);
    std::unique_ptr<Layer> takeLayer(LayerId id);
    void moveLayer(LayerId id, int newIndex);

    // Fresh ids; never reused within this document.
    LayerId allocateLayerId() { return m_nextLayerId++; }
    DrawingId allocateDrawingId() { return m_nextDrawingId++; }

private:
    int m_width;
    int m_height;
    FrameRate m_frameRate;
    int m_frameCount;
    Pixel m_paperColor = 0xffffffff;
    std::vector<std::unique_ptr<Layer>> m_layers;
    LayerId m_nextLayerId = 1;
    DrawingId m_nextDrawingId = 1;
};

} // namespace mothion::doc
