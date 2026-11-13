#pragma once

#include "core/document/Ids.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"
#include "core/perspective/PerspectiveGrid.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mothion::doc {

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight,
                       Difference, Exclusion,
                       // Not in QPainter: blended by hand (document/Blend.h).
                       LinearBurn, Subtract, Divide, LinearLight, VividLight, PinLight, HardMix, LighterColor,
                       DarkerColor, Hue, Saturation, Color, Luminosity, GlowDodge, AddGlow };

struct LayerProperties
{
    std::string name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f; // 0..1
    BlendMode blend = BlendMode::Normal;
    // Clipped to the layer below: only what lies over the pixels of the first layer below that is not clipped shows (that
    // layer is the mask; nothing is erased). With no such layer, or when it is hidden, there is nothing to clip to / it hides.
    bool clipped = false;
    perspective::Grid perspective = {}; // what a perspective layer shows

    bool operator==(const LayerProperties &) const = default;
};

// What a layer is: pixels drawn on it, a perspective grid (a guide that
// cannot be drawn on: its image stays empty, its look comes from
// LayerProperties::perspective), or a vector layer: pixels too, but made from the strokes it keeps
// (Layer::strokes), which only the brush and the eraser add to.
enum class LayerKind { Raster, Perspective, Vector };

// One layer of a drawing, stacked with the drawing's other layers.
struct Layer
{
    LayerId id = kNoId;
    LayerProperties properties;
    TiledImage image;
    LayerKind kind = LayerKind::Raster;
    // The layer's mask, a bitmap painted in black and white (in any gray between): where it is white, or has no
    // pixel, the layer shows; where it is black, it does not. Only raster layers have one.
    std::optional<TiledImage> mask;
    // A vector layer's strokes, bottom to top: what `image` is drawn from. Empty on the other kinds.
    std::vector<VectorStroke> strokes;
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
