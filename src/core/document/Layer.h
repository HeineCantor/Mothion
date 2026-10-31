#pragma once

#include "core/document/Drawing.h"
#include "core/document/Timing.h"

#include <map>
#include <optional>
#include <string>

namespace mothion::doc {

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add };

struct LayerProperties
{
    std::string name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f; // 0..1
    BlendMode blend = BlendMode::Normal;

    bool operator==(const LayerProperties &) const = default;
};

// A layer ("element" in Harmony terms): its own set of drawings plus the
// timing that says which of them is shown on each frame.
class Layer
{
public:
    Layer(LayerId id, LayerProperties properties);

    LayerId id() const { return m_id; }

    const LayerProperties &properties() const { return m_properties; }
    void setProperties(LayerProperties properties) { m_properties = std::move(properties); }

    // Drawings, ordered by id (creation order).
    const std::map<DrawingId, Drawing> &drawings() const { return m_drawings; }
    Drawing *drawing(DrawingId id);
    const Drawing *drawing(DrawingId id) const;
    Drawing &addDrawing(Drawing drawing); // id must be new
    std::optional<Drawing> takeDrawing(DrawingId id);

    // Drawing shown at `frame`, or nullptr on an empty frame.
    const Drawing *drawingAt(int frame) const;

    Timing &timing() { return m_timing; }
    const Timing &timing() const { return m_timing; }

    // Smallest positive number not already used as a name: "1", "2", ...
    std::string nextDrawingName() const;

private:
    LayerId m_id;
    LayerProperties m_properties;
    std::map<DrawingId, Drawing> m_drawings;
    Timing m_timing;
};

} // namespace mothion::doc
