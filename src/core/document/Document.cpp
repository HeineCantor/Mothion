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

const Cel *Document::trajectoryParent(const Cel &cel) const
{
    if (!cel.properties().trajectory || cel.properties().parent == kNoId || cel.properties().parent == cel.id())
        return nullptr;
    const Cel *parent = this->cel(cel.properties().parent);
    return parent && parent->properties().trajectory ? parent : nullptr;
}

bool Document::trajectoryLoop(CelId child, CelId parent) const
{
    const Cel *c = cel(child), *p = cel(parent);
    if (!c || !p || !p->properties().trajectory)
        return true;
    if (!c->properties().trajectory)
        return false; // a cel that follows one: nothing follows it, so there is no way back
    // Up from the parent: meeting the child is a loop.
    for (int depth = 0; p && depth <= celCount(); ++depth) {
        if (p->id() == child)
            return true;
        p = trajectoryParent(*p);
    }
    return false;
}

const Cel *Document::followedTrajectory(const Cel &cel) const
{
    if (cel.properties().trajectory || cel.properties().parent == kNoId)
        return nullptr;
    const Cel *parent = this->cel(cel.properties().parent);
    return parent && parent->properties().trajectory ? parent : nullptr;
}

int Document::trajectoryStartFrame(CelId trajectory) const
{
    // The earliest key of the trajectory or of the ones it follows (an inactive one does nothing: it has none that count).
    int first = -1;
    int depth = 0;
    for (const Cel *c = cel(trajectory); c && depth <= celCount(); c = trajectoryParent(*c), ++depth) {
        if (!c->properties().visible || c->motion().isEmpty())
            continue;
        const int own = c->motion().keyFrames().front();
        first = first < 0 ? own : std::min(first, own);
    }
    return first;
}

Matrix2x3 Document::followMatrix(const Cel &cel, int frame) const
{
    const Cel *trajectory = followedTrajectory(cel);
    const int start = trajectory ? trajectoryStartFrame(trajectory->id()) : -1;
    if (start < 0)
        return {};
    // Relative: what the trajectory has done since its first key, so the cel is where it is drawn until then.
    return trajectoryMatrix(trajectory->id(), start).inverted().then(trajectoryMatrix(trajectory->id(), frame));
}

Matrix2x3 Document::trajectoryParentMatrix(CelId trajectory, int frame) const
{
    const Cel *c = cel(trajectory);
    const Cel *parent = c ? trajectoryParent(*c) : nullptr;
    return parent ? trajectoryMatrix(parent->id(), frame) : Matrix2x3{};
}

Matrix2x3 Document::trajectoryMatrix(CelId trajectory, int frame) const
{
    // From the root down; a loop (that cannot be made, but a file may have one) stops where it meets itself.
    std::vector<const Cel *> chain;
    for (const Cel *c = cel(trajectory); c && int(chain.size()) <= celCount(); c = trajectoryParent(*c)) {
        if (std::find(chain.begin(), chain.end(), c) != chain.end())
            break;
        chain.push_back(c);
    }
    Matrix2x3 world;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Cel &c = **it;
        if (!c.properties().visible)
            continue; // inactive: it does nothing of its own
        const CelMotion &motion = c.motion();
        world = motion.poseAt(frame).matrix(motion.pivotX, motion.pivotY).then(world);
    }
    return world;
}

Layer *Document::layer(CelId celId, DrawingId drawingId, LayerId layerId)
{
    Cel *c = cel(celId);
    Drawing *d = c ? c->drawing(drawingId) : nullptr;
    return d ? d->layer(layerId) : nullptr;
}

const Layer *Document::layer(CelId celId, DrawingId drawingId, LayerId layerId) const
{
    const Cel *c = cel(celId);
    const Drawing *d = c ? c->drawing(drawingId) : nullptr;
    return d ? d->layer(layerId) : nullptr;
}

void Document::restoreIdCounters(CelId nextCel, DrawingId nextDrawing, LayerId nextLayer)
{
    CelId celFloor = 1;
    DrawingId drawingFloor = 1;
    LayerId layerFloor = 1;
    for (const auto &l : m_cels) {
        celFloor = std::max(celFloor, l->id() + 1);
        for (const auto &[id, drawing] : l->drawings()) {
            drawingFloor = std::max(drawingFloor, id + 1);
            for (const Layer &layer : drawing.layers)
                layerFloor = std::max(layerFloor, layer.id + 1);
        }
    }
    m_nextCelId = std::max(nextCel, celFloor);
    m_nextDrawingId = std::max(nextDrawing, drawingFloor);
    m_nextLayerId = std::max(nextLayer, layerFloor);
}

} // namespace mothion::doc
