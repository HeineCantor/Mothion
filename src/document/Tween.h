#pragma once

#include "core/document/Drawing.h"
#include "core/tween/Flow.h"

#include <QRect>
#include <QString>

#include <optional>
#include <vector>

namespace mothion::document {

// How the time of a tween is eased: the picture at the middle of the frames is not always the middle of the way.
enum class TweenEase { Linear, EaseIn, EaseOut, EaseInOut };
// The part of the way (0..1) at `u` (0..1) of the time.
double easedTime(TweenEase ease, double u);

// The drawings between two others. The two drawings are laid over each other by registering them (tween::
// registerImages on how much ink there is at each point), once, and any picture between them is then made from the
// two, layer by layer: a layer of one is paired with the layer of the other of the same name (then the rest in the
// order they are stacked), and a layer with no partner fades out or in. Perspective layers (guides) are not part of it.
class DrawingTween
{
public:
    // Nothing if neither drawing has pixels (`reason` says so).
    static std::optional<DrawingTween> make(const doc::Drawing &from, const doc::Drawing &to, QString *reason = nullptr);

    // The layers a picture between the two has, bottom first.
    int layerCount() const { return int(m_pairs.size()); }
    // Layer `index` of the picture at `t` (0 is `from`, 1 is `to`): the look of its partner in `from` (else in `to`),
    // and the pixels. Its id is left unset. Safe to call from several threads at once.
    doc::Layer layerAt(int index, double t) const;

private:
    struct Pair
    {
        int from = -1; // indices into the layers of m_from and m_to, -1 for none
        int to = -1;
    };
    doc::Drawing m_from, m_to; // held: the layers are read from them
    std::vector<Pair> m_pairs;
    QRect m_area;
    tween::Field m_field; // on the area's pixels
};

} // namespace mothion::document
