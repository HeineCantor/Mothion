#pragma once

#include "core/document/Ids.h"
#include "core/document/TiledImage.h"
#include "core/perspective/PerspectiveGrid.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mothion::doc {

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add };

struct LayerProperties
{
    std::string name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f; // 0..1
    BlendMode blend = BlendMode::Normal;
    perspective::Grid perspective = {}; // what a perspective layer shows

    bool operator==(const LayerProperties &) const = default;
};

// What a layer is: pixels drawn on it, or a perspective grid (a guide that
// cannot be drawn on: its image stays empty, its look comes from
// LayerProperties::perspective).
enum class LayerKind { Raster, Perspective };

// One layer of a drawing, stacked with the drawing's other layers.
struct Layer
{
    LayerId id = kNoId;
    LayerProperties properties;
    TiledImage image;
    LayerKind kind = LayerKind::Raster;
};

// One drawing of a cel, a "frame" in the interface. It exists independently
// of time: the cel's Timing decides on which frames it is shown, possibly
// several times (holds, cycles). What it shows is its layers composed
// together, like a folder, before the cel's own opacity and blend apply.
struct Drawing
{
    DrawingId id = kNoId;
    std::string name; // shown in the timeline cells: "1", "2", "A"...
    std::vector<Layer> layers; // bottom (index 0) to top

    int indexOf(LayerId layer) const // -1 if absent
    {
        for (std::size_t i = 0; i < layers.size(); ++i)
            if (layers[i].id == layer)
                return int(i);
        return -1;
    }
    Layer *layer(LayerId layerId)
    {
        const int i = indexOf(layerId);
        return i < 0 ? nullptr : &layers[std::size_t(i)];
    }
    const Layer *layer(LayerId layerId) const
    {
        const int i = indexOf(layerId);
        return i < 0 ? nullptr : &layers[std::size_t(i)];
    }
    // No pixels on any layer.
    bool isEmpty() const
    {
        for (const Layer &l : layers)
            if (!l.image.isEmpty())
                return false;
        return true;
    }
    std::size_t memoryBytes() const
    {
        std::size_t total = 0;
        for (const Layer &l : layers)
            total += l.image.memoryBytes();
        return total;
    }
};

} // namespace mothion::doc
