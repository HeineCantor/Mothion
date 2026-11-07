#pragma once

#include "core/document/Drawing.h"
#include "core/puppet/WarpRaster.h"
#include "core/tween/Flow.h"

#include <QImage>
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
    // The block of the document the pictures are made of (the drawings' bounds, a little more).
    QRect area() const { return m_area; }
    // The picture at `t`: the layers composed together over transparency, as big as area(). Slow on a big area.
    QImage picture(double t) const;
    // The same, small (at most about 400 px along its longer side), made from smaller copies of the layers made once:
    // fast enough to follow a slider. Its size is area() divided by previewScale(), rounded up.
    QImage preview(double t) const;
    int previewScale() const { return m_previewScale; }
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
    int m_reach = 0;      // px: the most any point of the first picture moves
    // The same, small, for the preview.
    int m_previewScale = 1;
    QSize m_previewSize;
    tween::Field m_previewField;
    struct SmallPair
    {
        puppet::RasterImage from, to;
        doc::LayerProperties look;
    };
    std::vector<SmallPair> m_small;
};

} // namespace mothion::document
