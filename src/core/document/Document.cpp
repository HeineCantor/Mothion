#include "core/document/Document.h"

#include "core/deform/BoneField.h"
#include "core/deform/CurveField.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
    if (!c || !p)
        return true;
    if (p->properties().deformer != DeformerKind::None)
        return c->properties().drawsNothing(); // only a drawn cel is bent, and nothing follows it
    if (!p->properties().trajectory)
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
    if (cel.properties().trajectory || cel.properties().parent == kNoId || cel.properties().parent == cel.id())
        return nullptr;
    const Cel *parent = this->cel(cel.properties().parent);
    if (!parent)
        return nullptr;
    if (parent->properties().trajectory)
        return parent;
    // Bent by a deformation cel (an inactive one too): what follows it follows its trajectory.
    if (parent->properties().deformer != DeformerKind::None && cel.properties().deformer == DeformerKind::None)
        return followedTrajectory(*parent);
    return nullptr;
}

const Cel *Document::followedDeformer(const Cel &cel) const
{
    if (cel.properties().drawsNothing() || cel.properties().parent == kNoId || cel.properties().parentPart >= 0)
        return nullptr; // carried by one of its parts, it is not bent
    const Cel *parent = this->cel(cel.properties().parent);
    return parent && parent->properties().deformer != DeformerKind::None && parent->properties().visible ? parent : nullptr;
}

Matrix2x3 Document::partMatrix(const Cel &cel, int frame) const
{
    const int part = cel.properties().parentPart;
    const Cel *deformer = part >= 0 && !cel.properties().drawsNothing() ? this->cel(cel.properties().parent) : nullptr;
    if (!deformer || deformer->properties().deformer == DeformerKind::None || !deformer->properties().visible
        || part >= int(deformer->deformer().bones.size()))
        return {};
    // The part's place and angle at rest and in the pose.
    double restX = 0, restY = 0, restAngle = 0, nowX = 0, nowY = 0, nowAngle = 0;
    const std::vector<double> pose = deformPoseAt(*deformer, frame);
    if (deformer->properties().deformer == DeformerKind::Bones) {
        const deform::BoneFrame rest = deform::boneFrames(deformer->deformer(), deformer->deformer().rest)[std::size_t(part)];
        const deform::BoneFrame now = deform::boneFrames(deformer->deformer(), pose)[std::size_t(part)];
        restX = rest.origin.x, restY = rest.origin.y, restAngle = rest.angle;
        nowX = now.origin.x, nowY = now.origin.y, nowAngle = now.angle;
    } else {
        const deform::CurveNode rest = deform::curveNodes(deformer->deformer(), deformer->deformer().rest)[std::size_t(part)];
        const deform::CurveNode now = deform::curveNodes(deformer->deformer(), pose)[std::size_t(part)];
        restX = rest.at.x, restY = rest.at.y, restAngle = rest.angle;
        nowX = now.at.x, nowY = now.at.y, nowAngle = now.angle;
    }
    // Turned round its place at rest as far as it turned, then moved to where it is.
    const double turn = nowAngle - restAngle, c = std::cos(turn), s = std::sin(turn);
    Matrix2x3 m{c, s, -s, c, 0, 0};
    m.e = nowX - (m.a * restX + m.c * restY);
    m.f = nowY - (m.b * restX + m.d * restY);
    return m;
}

std::vector<double> Document::deformPoseAt(const Cel &deformer, int frame) const
{
    std::vector<double> pose = deformer.motion().poseAt(frame).deform;
    return pose.empty() ? deformer.deformer().rest : pose;
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

Matrix2x3 Document::followMatrix(const Cel &cel, int frame, bool turned) const
{
    Matrix2x3 part = partMatrix(cel, frame);
    const Cel *trajectory = followedTrajectory(cel);
    const int start = trajectory ? trajectoryStartFrame(trajectory->id()) : -1;
    if (start < 0)
        return part;
    if (turned)
        if (const std::optional<PathTurn> turn = pathTurn(cel, frame); turn && turn->degrees != 0) {
            // Around where the centre of mass is once the cel's own pose (and its part) has put it.
            double x = turn->x, y = turn->y;
            if (!cel.motion().isEmpty())
                cel.motion().poseAt(frame).matrix(cel.motion().pivotX, cel.motion().pivotY).map(turn->x, turn->y, x, y);
            double cx = 0, cy = 0;
            part.map(x, y, cx, cy);
            CelPose about;
            about.rotation = turn->degrees;
            part = part.then(about.matrix(cx, cy));
        }
    // Relative: what the trajectory has done since its first key, so the cel is where it is drawn until then.
    return part.then(trajectoryMatrix(trajectory->id(), start).inverted().then(trajectoryMatrix(trajectory->id(), frame)));
}

std::optional<Document::PathTurn> Document::pathTurn(const Cel &cel, int frame) const
{
    if (!cel.properties().orientToPath || cel.properties().drawsNothing())
        return std::nullopt;
    const Cel *trajectory = followedTrajectory(cel);
    const Drawing *drawing = trajectory ? cel.drawingAt(frame) : nullptr;
    const auto centre = drawing ? centreOfMass(*drawing) : std::nullopt;
    if (!centre)
        return std::nullopt;
    return PathTurn{pathAngle(trajectory->id(), frame), centre->first, centre->second};
}

double Document::pathAngle(CelId trajectory, int frame) const
{
    const Cel *own = cel(trajectory);
    const int first = own ? trajectoryStartFrame(trajectory) : -1;
    if (first < 0)
        return 0;
    int last = first;
    int depth = 0;
    for (const Cel *c = own; c && depth <= celCount(); c = trajectoryParent(*c), ++depth)
        if (c->properties().visible && !c->motion().isEmpty())
            last = std::max(last, c->motion().keyFrames().back());

    // The way of each step from a frame to the next, in degrees, going on from the one before (never jumping by a turn).
    std::vector<std::optional<double>> ways;
    std::optional<double> previous;
    double fromX = 0, fromY = 0;
    for (int f = first; f <= last; ++f) {
        double x = 0, y = 0;
        trajectoryMatrix(trajectory, f).map(own->motion().pivotX, own->motion().pivotY, x, y);
        if (f > first) {
            std::optional<double> way;
            if (std::hypot(x - fromX, y - fromY) > 0.01) {
                double angle = std::atan2(y - fromY, x - fromX) * 180.0 / std::numbers::pi;
                if (previous)
                    angle = *previous + std::remainder(angle - *previous, 360.0);
                way = previous = angle;
            }
            ways.push_back(way);
        }
        fromX = x;
        fromY = y;
    }
    if (!previous)
        return 0; // it goes nowhere
    // Standing still: the way it had, or (before it first moves) the first it takes.
    double kept = 0;
    for (const std::optional<double> &way : ways)
        if (way) {
            kept = *way;
            break;
        }
    std::vector<double> steps;
    for (const std::optional<double> &way : ways)
        steps.push_back(kept = way.value_or(kept));

    // Eased over the steps around a frame, nearer ones counting more; past the ends, the first and the last go on.
    constexpr int kReach = 4;
    const int count = int(steps.size());
    auto eased = [&](int at) { // `at` in frames from the first
        double sum = 0, weights = 0;
        for (int i = at - kReach; i < at + kReach; ++i) {
            const double weight = kReach - std::abs(i + 0.5 - at);
            sum += weight * steps[std::size_t(std::clamp(i, 0, count - 1))];
            weights += weight;
        }
        return sum / weights;
    };
    return eased(std::clamp(frame, first, last) - first) - eased(0);
}

void Document::holdCentres(bool held) const
{
    if (!held)
        m_heldCentres.reset();
    else if (!m_heldCentres)
        m_heldCentres.emplace();
}

std::optional<std::pair<double, double>> Document::centreOfMass(const Drawing &drawing) const
{
    if (m_heldCentres)
        if (const auto it = m_heldCentres->find(drawing.id); it != m_heldCentres->end())
            return it->second;
    // Every pixel of what shows, weighted by how opaque it is.
    double sumX = 0, sumY = 0, sum = 0;
    for (const Layer &layer : drawing.layers) {
        if (!layer.properties.visible || layer.kind == LayerKind::Perspective)
            continue;
        for (const TileCoord &at : layer.image.tileCoords()) {
            const TilePtr tile = layer.image.tile(at);
            for (int y = 0; y < kTileSize; ++y) {
                const Pixel *row = tile->row(y);
                double weight = 0, across = 0;
                for (int x = 0; x < kTileSize; ++x) {
                    const double alpha = alphaOf(row[x]);
                    weight += alpha;
                    across += alpha * x;
                }
                sum += weight;
                sumX += across + weight * (at.x * kTileSize + 0.5);
                sumY += weight * (at.y * kTileSize + y + 0.5);
            }
        }
    }
    std::optional<std::pair<double, double>> centre;
    if (sum > 0)
        centre = {sumX / sum, sumY / sum};
    if (m_heldCentres)
        m_heldCentres->emplace(drawing.id, centre);
    return centre;
}

Cel Document::timeLayerAsCel(const Cel &cel, const TimeLayer &layer)
{
    CelProperties properties = cel.properties();
    properties.parent = layer.parent;
    properties.parentPart = layer.parentPart;
    properties.orientToPath = false; // the cel's own choice, for its drawings: not a time layer's
    return Cel(cel.id(), properties);
}

namespace {

bool sameTiles(const TiledImage &a, const TiledImage &b)
{
    if (a.tileCount() != b.tileCount())
        return false;
    for (const TileCoord &c : a.tileCoords())
        if (a.tile(c) != b.tile(c))
            return false;
    return true;
}

// The same content (not where it is in the stack).
bool sameLayer(const Layer &a, const Layer &b)
{
    return a.properties == b.properties && a.kind == b.kind && sameTiles(a.image, b.image)
           && a.mask.has_value() == b.mask.has_value() && (!a.mask || sameTiles(*a.mask, *b.mask)) && a.strokes == b.strokes;
}

std::vector<TileCoord> unitedTiles(const TiledImage &a, const TiledImage &b)
{
    std::vector<TileCoord> out = a.tileCoords();
    for (const TileCoord &c : b.tileCoords())
        if (std::find(out.begin(), out.end(), c) == out.end())
            out.push_back(c);
    return out;
}

} // namespace

ChangeSet Document::syncTimeLayers()
{
    ChangeSet changes;
    for (const std::unique_ptr<Cel> &owned : m_cels) {
        Cel &cel = *owned;
        bool any = !cel.timeLayers().empty();
        for (const auto &[id, d] : cel.drawings())
            for (const Layer &l : d.layers)
                any = any || l.time;
        if (!any)
            continue;
        // A copy of a time layer the cel does not have any more (its making undone): gone.
        for (const auto &[id, d] : cel.drawings()) {
            Drawing &drawing = *cel.drawing(id);
            const auto gone = std::erase_if(drawing.layers, [&](const Layer &l) { return l.time && !cel.timeLayer(l.id); });
            if (gone > 0)
                changes.structure = true;
        }
        for (const TimeLayer &t : cel.timeLayers()) {
            const Layer *known = cel.timeContent(t.id);
            const Layer *changed = nullptr;
            int place = -1; // where it is in the first drawing that has it
            for (const auto &[id, d] : cel.drawings()) {
                const int i = d.indexOf(t.id);
                if (i < 0)
                    continue;
                if (place < 0)
                    place = i;
                const Layer &copy = d.layers[std::size_t(i)];
                if (!known || !sameLayer(copy, *known)) {
                    changed = &copy;
                    break;
                }
            }
            if (changed) {
                Layer content = *changed;
                content.time = true;
                cel.setTimeContent(content);
            }
            const Layer *content = cel.timeContent(t.id);
            if (!content)
                continue; // no drawing has it yet: given to the first one that comes by its making
            for (const auto &[id, d] : cel.drawings()) {
                Drawing &drawing = *cel.drawing(id);
                Layer *copy = drawing.layer(t.id);
                if (!copy) {
                    const int at = std::clamp(place < 0 ? int(drawing.layers.size()) : place, 0, int(drawing.layers.size()));
                    drawing.layers.insert(drawing.layers.begin() + at, *content);
                    changes.structure = true;
                    continue;
                }
                if (sameLayer(*copy, *content))
                    continue;
                if (copy->properties != content->properties || copy->kind != content->kind
                    || copy->mask.has_value() != content->mask.has_value())
                    changes.structure = true;
                changes.pixels.push_back({cel.id(), id, t.id, unitedTiles(copy->image, content->image)});
                copy->properties = content->properties;
                copy->kind = content->kind;
                copy->image = content->image;
                copy->mask = content->mask;
                copy->strokes = content->strokes;
                copy->time = true;
            }
        }
    }
    return changes;
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
