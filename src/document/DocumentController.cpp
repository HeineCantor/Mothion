#include "document/DocumentController.h"

#include "canvas/ColorAdjust.h"
#include "canvas/FrameRenderer.h"
#include "tools/Preferences.h"
#include "canvas/SelectionMask.h"
#include "canvas/SelectionGrow.h"
#include "canvas/VectorRender.h"
#include "canvas/TileQt.h"
#include "core/deform/BoneField.h"
#include "core/deform/CurveField.h"
#include "core/document/LipSync.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/Parallel.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/AudioCommands.h"
#include "core/document/commands/CameraCommands.h"
#include "core/document/commands/MarkerCommands.h"
#include "core/document/commands/BlenderLinkCommands.h"
#include "core/document/commands/MotionCommands.h"
#include "core/document/commands/CompositingCommands.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/LayerCommands.h"
#include "core/document/commands/PaintCommand.h"
#include "core/document/commands/StrokeCommands.h"
#include "core/document/commands/TimingCommands.h"

#include <limits>
#include <map>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <functional>
#include <vector>

namespace mothion::document {

namespace {


// Bounding rect of changed tiles, in document coordinates.
QRect boundsOf(const std::vector<doc::TileCoord> &tiles)
{
    QRect r;
    for (const doc::TileCoord &c : tiles)
        r |= canvas::tileRect(c);
    return r;
}

// The smallest rect holding the image's visible pixels, in its own coordinates.
QRect opaqueBounds(const QImage &image)
{
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const doc::Pixel *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (doc::alphaOf(line[x]) == 0)
                continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

// The selection as it was before and after a step: made, cleared, or carried
// by a move. The selection lives in the controller, not in the document, so
// the command sets it there through `assign`; it changes no pixels.
class SelectionCommand : public doc::Command
{
public:
    using Assign = std::function<void(const QPainterPath &)>;

    SelectionCommand(doc::CommandKind kind, Assign assign, QPainterPath before, QPainterPath after)
        : m_kind(kind)
        , m_assign(std::move(assign))
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    doc::CommandKind kind() const override { return m_kind; }
    doc::ChangeSet apply(doc::Document &) override
    {
        m_assign(m_after);
        return {};
    }
    doc::ChangeSet revert(doc::Document &) override
    {
        m_assign(m_before);
        return {};
    }

private:
    doc::CommandKind m_kind;
    Assign m_assign;
    QPainterPath m_before;
    QPainterPath m_after;
};

// The keys of a trajectory's path chosen before and after a step (see choosePathKeys()).
class PathKeysCommand : public doc::Command
{
public:
    using Assign = std::function<void(doc::CelId, const std::set<int> &)>;
    struct Keys
    {
        doc::CelId cel = doc::kNoId;
        std::set<int> frames;
    };

    PathKeysCommand(Assign assign, Keys before, Keys after)
        : m_assign(std::move(assign))
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    doc::CommandKind kind() const override { return doc::CommandKind::Select; }
    doc::ChangeSet apply(doc::Document &) override
    {
        m_assign(m_after.cel, m_after.frames);
        return {};
    }
    doc::ChangeSet revert(doc::Document &) override
    {
        m_assign(m_before.cel, m_before.frames);
        return {};
    }

private:
    Assign m_assign;
    Keys m_before;
    Keys m_after;
};

// A tool's choice before and after a step (see recordToolChoice()).
class ToolChoiceCommand : public doc::Command
{
public:
    ToolChoiceCommand(std::function<void()> undo, std::function<void()> redo, std::weak_ptr<void> alive,
                      std::function<void()> restored)
        : m_undo(std::move(undo))
        , m_redo(std::move(redo))
        , m_alive(std::move(alive))
        , m_restored(std::move(restored))
    {
    }

    doc::CommandKind kind() const override { return doc::CommandKind::Select; }
    doc::ChangeSet apply(doc::Document &) override
    {
        run(m_redo);
        return {};
    }
    doc::ChangeSet revert(doc::Document &) override
    {
        run(m_undo);
        return {};
    }

private:
    void run(const std::function<void()> &f)
    {
        if (m_alive.expired())
            return;
        f();
        m_restored();
    }

    std::function<void()> m_undo, m_redo;
    std::weak_ptr<void> m_alive;
    std::function<void()> m_restored;
};

} // namespace

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_document(makeDefaultDocument())
{
    m_currentCel = m_document->celAt(0).id();
    applyUndoLimits();
    connect(&tools::Preferences::instance(), &tools::Preferences::changed, this, &DocumentController::applyUndoLimits);
}

void DocumentController::applyUndoLimits()
{
    const tools::Preferences &prefs = tools::Preferences::instance();
    doc::HistoryLimits limits; // the memory limit stays at its default
    limits.maxSteps = std::size_t(prefs.undoSteps());
    m_history.setLimits(limits);
}

DocumentController::~DocumentController() = default;

std::unique_ptr<doc::Document> DocumentController::makeDefaultDocument()
{
    const tools::Preferences &prefs = tools::Preferences::instance();
    return makeDocument(prefs.newWidth(), prefs.newHeight(), prefs.newFrameRate(), prefs.newFrames(), prefs.paperColor());
}

std::unique_ptr<doc::Document> DocumentController::makeDocument(int width, int height, int frameRate, int frames,
                                                                const QColor &paper)
{
    auto d = std::make_unique<doc::Document>(width, height, doc::FrameRate{frameRate, 1});
    d->setFrameCount(frames);
    d->setPaperColor(paper.rgba());
    // The first layer is part of the new document, not an undoable step.
    doc::AddCelCommand first(*d, 0, {tr("Cel 1").toStdString()});
    first.apply(*d);
    return d;
}

// --- whole document ----------------------------------------------------------------

void DocumentController::newDocument()
{
    replaceDocument(makeDefaultDocument());
}

void DocumentController::newDocument(int width, int height, int frameRate, int frames, const QColor &paper)
{
    replaceDocument(makeDocument(width, height, frameRate, frames, paper));
}

void DocumentController::replaceDocument(std::unique_ptr<doc::Document> document)
{
    if (!document)
        return;
    interruptStroke(); // open edits land in the old document before it goes
    m_document = std::move(document);
    m_history.clear();
    m_history.markClean();
    m_currentCel = m_document->celCount() > 0 ? m_document->celAt(m_document->celCount() - 1).id()
                                                  : doc::kNoId;
    m_currentFrame = 0;
    m_mouthEdit = doc::kNoId;
    m_pathKeysCel = doc::kNoId;
    m_pathKeys.clear();
    m_currentLayer = doc::kNoId;
    m_currentLayerName.clear();
    m_currentLayerKind = doc::LayerKind::Raster;
    m_lastLayerOf.clear();
    const bool cameraWasSelected = m_cameraSelected;
    m_cameraSelected = false;
    m_layerChosen = true;
    m_layerSelection.clear();
    m_selection = QPainterPath();
    if (const doc::Drawing *drawing = currentDrawing(); drawing && !drawing->layers.empty()) {
        m_currentLayer = drawing->layers.back().id;
        m_currentLayerName = drawing->layers.back().properties.name;
        m_currentLayerKind = drawing->layers.back().kind;
        m_lastLayerOf[currentDrawingId()] = m_currentLayer;
    }

    m_tempPoseCel = doc::kNoId;
    m_floating.reset();
    emit playRangeChanged();
    emit documentReplaced();
    if (cameraWasSelected)
        emit cameraSelectionChanged();
    emit structureChanged();
    emit currentCelChanged(m_currentCel);
    emit currentFrameChanged(m_currentFrame);
    emit currentLayerChanged(m_currentLayer);
    emit selectionChanged();
    emit historyChanged();
}

void DocumentController::markSaved()
{
    m_history.markClean();
    emit historyChanged();
}

void DocumentController::markUnsaved()
{
    m_history.markUnsaved();
    emit historyChanged();
}

// --- commands ------------------------------------------------------------------

void DocumentController::execute(std::unique_ptr<doc::Command> command)
{
    if (!command)
        return;
    interruptStroke();
    publish(m_history.execute(*m_document, std::move(command)));
    emit historyChanged();
}

void DocumentController::addCel()
{
    doc::Document &d = *m_document;
    QString name; // the first free number
    for (int n = d.celCount() + 1;; ++n) {
        name = tr("Cel %1").arg(n);
        bool used = false;
        for (int i = 0; i < d.celCount() && !used; ++i)
            used = d.celAt(i).properties().name == name.toStdString();
        if (!used)
            break;
    }
    auto command =
        std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, doc::CelProperties{name.toStdString()});
    const doc::CelId id = command->celId();
    execute(std::move(command));
    setCurrentCel(id);
}

void DocumentController::addTrajectory()
{
    doc::Document &d = *m_document;
    QString name; // the first free number
    for (int n = 1;; ++n) {
        name = tr("Trajectory %1").arg(n);
        bool used = false;
        for (int i = 0; i < d.celCount() && !used; ++i)
            used = d.celAt(i).properties().name == name.toStdString();
        if (!used)
            break;
    }
    doc::CelProperties properties{name.toStdString()};
    properties.trajectory = true;
    auto add = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, std::move(properties));
    const doc::CelId id = add->celId();
    doc::CelMotion motion; // no keys yet; the pivot is where the circle is drawn
    motion.pivotX = d.width() / 2.0;
    motion.pivotY = d.height() / 2.0;
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::move(add));
    steps.push_back(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::AddCel, std::move(steps)));
    chooseCel(id);
}

bool DocumentController::trajectoryFromMarks(QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    QString why;
    const doc::Layer *layer = workLayer(false, tr("Nothing to make a trajectory from"), &why, true);
    if (!layer)
        return fail(why);
    if (layer->kind != doc::LayerKind::Vector)
        return fail(tr("The marks of a spacing chart are strokes: draw them on a vector layer"));
    // The strokes are kept in the cel's own place: a cel moved by its keys, or by a trajectory it follows, shows them moved,
    // and the trajectory goes where they are shown.
    doc::Matrix2x3 shown;
    if (const doc::Cel *cel = m_document->cel(m_currentCel)) {
        shown = m_document->followMatrix(*cel, m_currentFrame);
        if (!cel->motion().isEmpty())
            shown = celPoseAt(cel->id(), m_currentFrame).matrix(cel->motion().pivotX, cel->motion().pivotY).then(shown);
    }
    // Each stroke, in the order drawn (the strokes are kept in that order): its points, its middle and how long it is across.
    struct Mark
    {
        std::vector<QPointF> points;
        QPointF middle;
        double extent = 0;
    };
    std::vector<Mark> all;
    for (const doc::VectorStroke &stroke : layer->strokes) {
        if (stroke.erase || stroke.fill || stroke.points.empty())
            continue;
        Mark mark;
        double x = 0, y = 0;
        double minX = std::numeric_limits<double>::max(), maxX = -minX, minY = minX, maxY = -minX;
        for (const auto &in : stroke.points) {
            QPointF p;
            shown.map(in.x, in.y, p.rx(), p.ry());
            mark.points.push_back(p);
            x += p.x();
            y += p.y();
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
        mark.middle = QPointF(x / double(stroke.points.size()), y / double(stroke.points.size()));
        mark.extent = std::max(maxX - minX, maxY - minY);
        all.push_back(std::move(mark));
    }
    // The path a chart is drawn along is not a mark: a stroke much longer than the usual one is left out (more than three
    // times the median), and if there is one it gives the places and the order. A mark that crosses it is the point where it
    // crosses (the nearest to the mark's middle if more than once); one that does not reach it, its middle. The marks go in
    // the order they come along the path, from where it was begun to where it ended (the way the pen went), whatever the
    // order they were drawn in. With no path, the middles, in the order drawn.
    std::vector<QPointF> middles;
    if (all.size() >= 3) {
        std::vector<double> extents;
        for (const Mark &m : all)
            extents.push_back(m.extent);
        std::nth_element(extents.begin(), extents.begin() + std::ptrdiff_t(extents.size() / 2), extents.end());
        const double median = extents[extents.size() / 2];
        const Mark *pathMark = nullptr; // the longest of the long strokes
        std::vector<const Mark *> marks;
        for (const Mark &m : all) {
            if (m.extent <= 3 * std::max(median, 1.0))
                marks.push_back(&m);
            else if (!pathMark || m.extent > pathMark->extent)
                pathMark = &m;
        }
        const std::vector<QPointF> path = pathMark ? pathMark->points : std::vector<QPointF>();
        if (path.size() >= 2 && marks.size() >= 2) {
            std::vector<double> along(path.size(), 0.0); // the length of the path up to each point
            for (std::size_t i = 1; i < path.size(); ++i)
                along[i] = along[i - 1] + QLineF(path[i - 1], path[i]).length();
            // How far along the path a point is: its nearest point on the line, counted from the start.
            auto position = [&](const QPointF &p) {
                double best = std::numeric_limits<double>::max(), at = 0;
                for (std::size_t i = 1; i < path.size(); ++i) {
                    const QPointF ab = path[i] - path[i - 1];
                    const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
                    const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(p - path[i - 1], ab) / length2, 0.0, 1.0) : 0.0;
                    const double distance = QLineF(p, path[i - 1] + ab * t).length();
                    if (distance < best) {
                        best = distance;
                        at = along[i - 1] + t * std::sqrt(length2);
                    }
                }
                return at;
            };
            std::vector<std::pair<double, QPointF>> placed;
            for (const Mark *m : marks) {
                // Where the mark crosses the path, and how far along it that is.
                bool crosses = false;
                double nearest = std::numeric_limits<double>::max(), at = 0;
                QPointF point;
                const std::vector<QPointF> &q = m->points;
                for (std::size_t j = 1; j < q.size(); ++j) {
                    const QLineF segment(q[j - 1], q[j]);
                    for (std::size_t i = 1; i < path.size(); ++i) {
                        const QLineF line(path[i - 1], path[i]);
                        QPointF hit;
                        if (segment.intersects(line, &hit) != QLineF::BoundedIntersection)
                            continue;
                        const double distance = QLineF(hit, m->middle).length();
                        if (distance < nearest) {
                            nearest = distance;
                            crosses = true;
                            point = hit;
                            at = along[i - 1] + QLineF(path[i - 1], hit).length();
                        }
                    }
                }
                if (crosses)
                    placed.emplace_back(at, point);
                else
                    placed.emplace_back(position(m->middle), m->middle);
            }
            std::stable_sort(placed.begin(), placed.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
            for (const auto &[s, p] : placed)
                middles.push_back(p);
        } else {
            for (const Mark *m : marks)
                middles.push_back(m->middle);
        }
    } else {
        for (const Mark &m : all)
            middles.push_back(m.middle);
    }
    if (middles.size() < 2)
        return fail(tr("Draw at least two marks, one for each frame, in the order of the movement"));

    doc::Document &d = *m_document;
    QString name; // the first free number
    for (int n = 1;; ++n) {
        name = tr("Trajectory %1").arg(n);
        bool used = false;
        for (int i = 0; i < d.celCount() && !used; ++i)
            used = d.celAt(i).properties().name == name.toStdString();
        if (!used)
            break;
    }
    doc::CelProperties properties{name.toStdString()};
    properties.trajectory = true;
    auto add = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, std::move(properties));
    const doc::CelId id = add->celId();
    doc::CelMotion motion;
    motion.pivotX = d.width() / 2.0;
    motion.pivotY = d.height() / 2.0;
    for (std::size_t i = 0; i < middles.size(); ++i) {
        doc::CelPose pose; // the pivot goes to the middle of the mark
        pose.x = middles[i].x() - motion.pivotX;
        pose.y = middles[i].y() - motion.pivotY;
        motion.setKey(doc::Channel::Position, m_currentFrame + int(i), pose, doc::Easing::linear());
    }
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::move(add));
    steps.push_back(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::AddCel, std::move(steps)));
    chooseCel(id);
    return true;
}

bool DocumentController::setTrajectoryParent(doc::CelId childId, doc::CelId parentId)
{
    const doc::Cel *child = m_document->cel(childId);
    if (!child || child->properties().parent == parentId)
        return false;
    if (parentId != doc::kNoId && m_document->trajectoryLoop(childId, parentId))
        return false;
    doc::CelProperties properties = child->properties();
    properties.parent = parentId;
    properties.parentPart = -1;
    execute(doc::makeSetCelProperties(*m_document, childId, std::move(properties)));
    return true;
}

bool DocumentController::setDeformerParent(doc::CelId childId, doc::CelId deformerId, int part)
{
    const doc::Cel *child = m_document->cel(childId);
    const doc::Cel *deformer = m_document->cel(deformerId);
    if (!child || !deformer || deformer->properties().deformer == doc::DeformerKind::None
        || m_document->trajectoryLoop(childId, deformerId) || part >= int(deformer->deformer().bones.size()))
        return false;
    part = std::max(-1, part);
    if (child->properties().parent == deformerId && child->properties().parentPart == part)
        return false;
    doc::CelProperties properties = child->properties();
    properties.parent = deformerId;
    properties.parentPart = part;
    execute(doc::makeSetCelProperties(*m_document, childId, std::move(properties)));
    return true;
}

void DocumentController::addDeformer(doc::DeformerKind kind)
{
    doc::Document &d = *m_document;
    const QString base = kind == doc::DeformerKind::Bones ? tr("Bones %1")
                         : kind == doc::DeformerKind::Curve ? tr("Curve %1")
                                                              : tr("Envelope %1");
    QString name; // the first free number
    for (int n = 1;; ++n) {
        name = base.arg(n);
        bool used = false;
        for (int i = 0; i < d.celCount() && !used; ++i)
            used = d.celAt(i).properties().name == name.toStdString();
        if (!used)
            break;
    }
    const doc::Cel *current = d.cel(m_currentCel);
    const bool bend = current && !current->properties().drawsNothing();
    doc::CelProperties properties{name.toStdString()};
    properties.deformer = kind;
    if (bend) // it follows what the cel followed: what it bends still goes along that way
        if (const doc::Cel *trajectory = d.followedTrajectory(*current))
            properties.parent = trajectory->id();
    auto add = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, std::move(properties));
    const doc::CelId id = add->celId();
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::move(add));
    if (bend) {
        doc::CelProperties child = current->properties();
        child.parent = id;
        steps.push_back(doc::makeSetCelProperties(d, current->id(), std::move(child)));
    }
    // An Envelope starts round what the cel shows (a rounded box: four nodes at the middles of its sides).
    if (kind == doc::DeformerKind::Envelope && bend)
        if (const doc::Drawing *drawing = current->drawingAt(m_currentFrame)) {
            const QRect shown = canvas::visibleBounds(*drawing);
            if (!shown.isEmpty()) {
                const QRectF box = QRectF(shown).adjusted(-16, -16, 16, 16);
                const QPointF c = box.center();
                const double rx = box.width() / 2, ry = box.height() / 2;
                constexpr double kRound = 0.5523; // handles that make a quarter of an ellipse
                std::vector<deform::CurveNode> nodes = {
                    {{c.x(), c.y() - ry}, 0, rx * kRound, rx * kRound},
                    {{c.x() + rx, c.y()}, std::numbers::pi / 2, ry * kRound, ry * kRound},
                    {{c.x(), c.y() + ry}, std::numbers::pi, rx * kRound, rx * kRound},
                    {{c.x() - rx, c.y()}, -std::numbers::pi / 2, ry * kRound, ry * kRound},
                };
                doc::Deformer envelope;
                envelope.bones.resize(nodes.size());
                envelope.rest = deform::poseFromNodes(nodes);
                steps.push_back(std::make_unique<doc::SetDeformerCommand>(id, std::move(envelope)));
            }
        }
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::AddCel, std::move(steps)));
    chooseCel(id);
}

namespace {
// A deformation cel's motion with the numbers of every Deform key changed by `change`.
doc::CelMotion withDeformKeys(const doc::CelMotion &motion, const std::function<void(std::vector<double> &)> &change)
{
    doc::CelMotion out = motion;
    for (const doc::CelKey &key : motion.keys(doc::Channel::Deform)) {
        doc::CelPose pose = key.pose;
        change(pose.deform);
        out.setKey(doc::Channel::Deform, key.frame, pose, key.easing);
    }
    return out;
}
} // namespace

int DocumentController::addBone(doc::CelId celId, int parent, QPointF from, QPointF to)
{
    const doc::Cel *cel = m_document->cel(celId);
    if (!cel || cel->properties().deformer != doc::DeformerKind::Bones)
        return -1;
    const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
    if (length < 1e-6)
        return -1;
    doc::Deformer deformer = cel->deformer();
    if (parent >= int(deformer.bones.size()))
        parent = -1;
    double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    std::array<double, doc::kBoneParams> numbers{from.x(), from.y(), 0, length};
    if (parent >= 0) {
        // From the parent's end, along and across it; the angle from the parent's.
        const deform::BoneFrame p = deform::boneFrames(deformer, deformer.rest)[std::size_t(parent)];
        const deform::Vec2 along = p.direction(), across{-along.y, along.x}, offset = deform::Vec2{from.x(), from.y()} - p.end();
        numbers[doc::BoneX] = puppet::dot(offset, along);
        numbers[doc::BoneY] = puppet::dot(offset, across);
        angle -= p.angle;
    }
    numbers[doc::BoneAngle] = std::remainder(angle * 180.0 / std::numbers::pi, 360.0);
    doc::Bone bone;
    bone.parent = parent;
    bone.radius = std::clamp(length * 0.35, 8.0, 200.0);
    bone.fade = bone.radius;
    deformer.bones.push_back(bone);
    deformer.rest.resize((deformer.bones.size() - 1) * doc::kBoneParams, 0.0);
    deformer.rest.insert(deformer.rest.end(), numbers.begin(), numbers.end());
    const std::size_t count = deformer.rest.size();
    const std::vector<double> rest = deformer.rest;
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::make_unique<doc::SetDeformerCommand>(celId, std::move(deformer)));
    if (cel->motion().hasKeys(doc::Channel::Deform))
        steps.push_back(std::make_unique<doc::SetCelMotionCommand>(
            celId, withDeformKeys(cel->motion(), [&](std::vector<double> &pose) {
                // The numbers it lacks are the rest's (the new bone's among them).
                for (std::size_t i = pose.size(); i < count; ++i)
                    pose.push_back(rest[i]);
            })));
    const int index = int(cel->deformer().bones.size());
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditDeformer, std::move(steps)));
    return index;
}

int DocumentController::addNode(doc::CelId celId, QPointF at, int after)
{
    const doc::Cel *cel = m_document->cel(celId);
    if (!cel || (cel->properties().deformer != doc::DeformerKind::Curve
                 && cel->properties().deformer != doc::DeformerKind::Envelope))
        return -1;
    const bool envelope = cel->properties().deformer == doc::DeformerKind::Envelope;
    doc::Deformer deformer = cel->deformer();
    std::vector<deform::CurveNode> nodes = deform::curveNodes(deformer, deformer.rest);
    if (envelope)
        after = int(nodes.size()) - 1; // an envelope is one closed curve: on from its last node
    if (after >= int(nodes.size()))
        return -1;
    // Only on from the last node of its curve.
    if (after >= 0 && std::size_t(after) + 1 < nodes.size() && !nodes[std::size_t(after) + 1].starts)
        return -1;
    deform::CurveNode node;
    node.at = {at.x(), at.y()};
    node.starts = after < 0;
    if (after >= 0) {
        deform::CurveNode &before = nodes[std::size_t(after)];
        const deform::Vec2 way = node.at - before.at;
        const double length = puppet::length(way);
        if (length < 1e-6)
            return -1;
        node.angle = std::atan2(way.y, way.x);
        node.in = length / 3;
        const bool lone = before.starts && (envelope ? nodes.size() == 1 : true);
        if (lone && (envelope || before.out <= 0)) { // the first piece: the first node's tangent and handle go its way
            before.angle = node.angle;
            before.out = length / 3;
        } else if (before.out <= 0) {
            before.out = length / 3;
        }
        node.out = length / 3;
    }
    const std::size_t index = after >= 0 ? std::size_t(after) + 1 : nodes.size();
    nodes.insert(nodes.begin() + std::ptrdiff_t(index), node);
    doc::Bone part;
    if (after >= 0)
        part = deformer.bones[std::size_t(after)]; // the zone as the node before has it
    else if (!deformer.bones.empty())
        part = deformer.bones.back();
    part.starts = node.starts && index > 0;
    deformer.bones.insert(deformer.bones.begin() + std::ptrdiff_t(index), part);
    const std::vector<double> before = deformer.rest;
    deformer.rest = deform::poseFromNodes(nodes, {});
    // The numbers it had keep their turns (an angle is the same however many turns it shows).
    for (std::size_t k = 0; k < deformer.rest.size(); ++k) {
        const std::size_t old = k < index * doc::kNodeParams ? k : k >= (index + 1) * doc::kNodeParams ? k - doc::kNodeParams : before.size();
        if (old < before.size() && std::abs(std::remainder(deformer.rest[k] - before[old], 360.0)) < 1e-9)
            deformer.rest[k] = before[old];
    }
    const std::vector<double> rest = deformer.rest;
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::make_unique<doc::SetDeformerCommand>(celId, std::move(deformer)));
    if (cel->motion().hasKeys(doc::Channel::Deform))
        steps.push_back(std::make_unique<doc::SetCelMotionCommand>(
            celId, withDeformKeys(cel->motion(), [&](std::vector<double> &pose) {
                // The new node as it is at rest (from the node before as the key has it); the numbers of the node before
                // that the rest changed (its tangent and handle, for the first piece of a curve) as the rest has them
                // where the key had them as at rest.
                pose.resize(before.size(), 0.0);
                for (std::size_t k = 0; k < before.size(); ++k) {
                    const std::size_t now = k < index * doc::kNodeParams ? k : k + doc::kNodeParams;
                    if (rest[now] != before[k] && std::abs(pose[k] - before[k]) < 1e-9)
                        pose[k] = rest[now];
                }
                pose.insert(pose.begin() + std::ptrdiff_t(index * doc::kNodeParams),
                            rest.begin() + std::ptrdiff_t(index * doc::kNodeParams),
                            rest.begin() + std::ptrdiff_t((index + 1) * doc::kNodeParams));
            })));
    std::vector<int> renumbered(nodes.size() - 1);
    for (std::size_t k = 0; k < renumbered.size(); ++k)
        renumbered[k] = int(k < index ? k : k + 1);
    carriedRenumbered(celId, renumbered, steps);
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditDeformer, std::move(steps)));
    return int(index);
}

void DocumentController::carriedRenumbered(doc::CelId deformer, const std::vector<int> &renumbered,
                                           std::vector<std::unique_ptr<doc::Command>> &steps) const
{
    for (int i = 0; i < m_document->celCount(); ++i) {
        const doc::Cel &c = m_document->celAt(i);
        const int part = c.properties().parentPart;
        if (c.properties().parent != deformer || part < 0 || part >= int(renumbered.size()) || renumbered[std::size_t(part)] == part)
            continue;
        doc::CelProperties p = c.properties();
        p.parentPart = renumbered[std::size_t(part)]; // -1: its part is gone, it is bent instead
        steps.push_back(doc::makeSetCelProperties(*m_document, c.id(), std::move(p)));
    }
}

bool DocumentController::removeDeformerPart(doc::CelId celId, int bone)
{
    const doc::Cel *cel = m_document->cel(celId);
    if (!cel || bone < 0 || bone >= int(cel->deformer().bones.size()))
        return false;
    if (cel->properties().deformer != doc::DeformerKind::Bones) {
        // A node: the others stay where they are, at rest and in every key.
        const doc::Deformer &old = cel->deformer();
        auto without = [&](std::vector<double> &numbers) {
            std::vector<deform::CurveNode> nodes = deform::curveNodes(old, numbers);
            const bool began = nodes[std::size_t(bone)].starts;
            if (began && std::size_t(bone) + 1 < nodes.size())
                nodes[std::size_t(bone) + 1].starts = true;
            nodes.erase(nodes.begin() + bone);
            numbers = deform::poseFromNodes(nodes, {});
        };
        doc::Deformer deformer = old;
        // The node after it begins the curve if it did.
        const bool began = bone == 0 || old.bones[std::size_t(bone)].starts;
        if (began && std::size_t(bone) + 1 < old.bones.size())
            deformer.bones[std::size_t(bone) + 1].starts = true;
        deformer.bones.erase(deformer.bones.begin() + bone);
        if (!deformer.bones.empty())
            deformer.bones.front().starts = false; // the first always begins one
        without(deformer.rest);
        std::vector<std::unique_ptr<doc::Command>> steps;
        steps.push_back(std::make_unique<doc::SetDeformerCommand>(celId, std::move(deformer)));
        if (cel->motion().hasKeys(doc::Channel::Deform))
            steps.push_back(std::make_unique<doc::SetCelMotionCommand>(celId, withDeformKeys(cel->motion(), without)));
        std::vector<int> renumbered(old.bones.size());
        for (std::size_t i = 0; i < renumbered.size(); ++i)
            renumbered[i] = int(i) == bone ? -1 : int(i) > bone ? int(i) - 1 : int(i);
        carriedRenumbered(celId, renumbered, steps);
        execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditDeformer, std::move(steps)));
        return true;
    }
    const doc::Deformer &old = cel->deformer();
    // The bone and all under it (a parent comes before its children).
    std::vector<char> gone(old.bones.size(), 0);
    gone[std::size_t(bone)] = 1;
    for (std::size_t i = std::size_t(bone) + 1; i < old.bones.size(); ++i)
        if (old.bones[i].parent >= 0 && gone[std::size_t(old.bones[i].parent)])
            gone[i] = 1;
    std::vector<int> newIndex(old.bones.size(), -1);
    doc::Deformer deformer;
    for (std::size_t i = 0; i < old.bones.size(); ++i) {
        if (gone[i])
            continue;
        newIndex[i] = int(deformer.bones.size());
        doc::Bone b = old.bones[i];
        b.parent = b.parent >= 0 ? newIndex[std::size_t(b.parent)] : -1;
        deformer.bones.push_back(b);
    }
    auto keep = [&](std::vector<double> &numbers) {
        std::vector<double> kept;
        for (std::size_t i = 0; i < old.bones.size(); ++i)
            if (!gone[i])
                for (int k = 0; k < doc::kBoneParams; ++k) {
                    const std::size_t at = i * doc::kBoneParams + std::size_t(k);
                    kept.push_back(at < numbers.size() ? numbers[at] : at < old.rest.size() ? old.rest[at] : 0.0);
                }
        numbers = std::move(kept);
    };
    deformer.rest = old.rest;
    keep(deformer.rest);
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::make_unique<doc::SetDeformerCommand>(celId, std::move(deformer)));
    if (cel->motion().hasKeys(doc::Channel::Deform))
        steps.push_back(std::make_unique<doc::SetCelMotionCommand>(celId, withDeformKeys(cel->motion(), keep)));
    carriedRenumbered(celId, newIndex, steps);
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditDeformer, std::move(steps)));
    return true;
}

bool DocumentController::setDeformerRest(doc::CelId celId, doc::Deformer deformer)
{
    const doc::Cel *cel = m_document->cel(celId);
    if (!cel || cel->properties().deformer == doc::DeformerKind::None || deformer == cel->deformer()
        || deformer.bones.size() != cel->deformer().bones.size())
        return false;
    deformer.rest.resize(deformer.bones.size() * std::size_t(doc::partParams(cel->properties().deformer)), 0.0);
    execute(std::make_unique<doc::SetDeformerCommand>(celId, std::move(deformer)));
    return true;
}

bool DocumentController::setDeformKey(doc::CelId celId, int frame, std::vector<double> pose)
{
    const doc::Cel *cel = m_document->cel(celId);
    if (!cel || cel->properties().deformer == doc::DeformerKind::None)
        return false;
    doc::CelMotion motion = cel->motion();
    doc::CelPose p;
    p.deform = std::move(pose);
    motion.setKey(doc::Channel::Deform, frame, p);
    if (motion == cel->motion())
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(celId, std::move(motion)));
    return true;
}

bool DocumentController::addLayer(bool onTop, doc::LayerKind kind)
{
    const doc::Drawing *drawing = currentDrawing();
    if (!drawing)
        return false;
    const bool perspective = kind == doc::LayerKind::Perspective;
    const bool vector = kind == doc::LayerKind::Vector;
    QString name; // the first free number
    for (int n = perspective || vector ? 1 : int(drawing->layers.size()) + 1;; ++n) {
        name = perspective ? tr("Perspective %1").arg(n) : vector ? tr("Vector %1").arg(n) : tr("Layer %1").arg(n);
        if (std::none_of(drawing->layers.begin(), drawing->layers.end(),
                         [&](const doc::Layer &l) { return l.properties.name == name.toStdString(); }))
            break;
    }
    const int index = onTop ? int(drawing->layers.size()) : drawing->indexOf(m_currentLayer) + 1;
    doc::LayerProperties properties{name.toStdString()};
    if (perspective)
        properties.perspective = perspective::defaultGrid(m_document->width(), m_document->height());
    auto command = std::make_unique<doc::AddLayerCommand>(*m_document, m_currentCel, drawing->id, index,
                                                          std::move(properties), doc::TiledImage(), kind);
    const doc::LayerId id = command->layerId();
    execute(std::move(command));
    setCurrentLayer(id);
    return true;
}

std::vector<const doc::Layer *> DocumentController::mergeableLayers(const std::vector<doc::LayerId> &ids,
                                                                     bool *incompatible) const
{
    // Raster layers, or vector layers, shown and not locked, of the current drawing, bottom to top. Kinds do not
    // mix; a vector layer keeps what it shows as strokes, so it takes no blend mode and no clipping.
    if (incompatible)
        *incompatible = false;
    std::vector<const doc::Layer *> found;
    const doc::Cel *cel = m_document->cel(m_currentCel);
    const doc::Drawing *drawing = currentDrawing();
    if (!cel || !drawing || !cel->properties().visible || cel->properties().locked)
        return found;
    for (const doc::Layer &layer : drawing->layers)
        if (std::find(ids.begin(), ids.end(), layer.id) != ids.end() && layer.kind != doc::LayerKind::Perspective
            && layer.properties.visible && !layer.properties.locked)
            found.push_back(&layer);
    const bool vector = !found.empty() && found.front()->kind == doc::LayerKind::Vector;
    const bool bad = std::any_of(found.begin(), found.end(), [&](const doc::Layer *layer) {
        return (layer->kind == doc::LayerKind::Vector) != vector
               || (vector && (layer->properties.blend != doc::BlendMode::Normal || layer->properties.clipped));
    });
    if (bad) {
        if (incompatible)
            *incompatible = true;
        found.clear();
    }
    return found;
}

bool DocumentController::canMergeLayers(const std::vector<doc::LayerId> &ids) const
{
    return mergeableLayers(ids).size() >= 2;
}

bool DocumentController::canRasterizeLayers(const std::vector<doc::LayerId> &ids) const
{
    const doc::Drawing *drawing = currentDrawing();
    if (!drawing)
        return false;
    return std::any_of(drawing->layers.begin(), drawing->layers.end(), [&](const doc::Layer &layer) {
        return layer.kind == doc::LayerKind::Vector && std::find(ids.begin(), ids.end(), layer.id) != ids.end();
    });
}

bool DocumentController::rasterizeLayers(const std::vector<doc::LayerId> &ids, QString *reason)
{
    interruptStroke();
    const doc::Cel *cel = m_document->cel(m_currentCel);
    const doc::Drawing *drawing = currentDrawing();
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (cel && drawing && !cel->properties().locked)
        for (const doc::Layer &layer : drawing->layers)
            if (layer.kind == doc::LayerKind::Vector && !layer.properties.locked
                && std::find(ids.begin(), ids.end(), layer.id) != ids.end())
                steps.push_back(std::make_unique<doc::RasterizeLayerCommand>(m_currentCel, drawing->id, layer.id,
                                                                             layer.strokes));
    if (steps.empty()) {
        if (reason)
            *reason = tr("Choose an unlocked vector layer to rasterize");
        return false;
    }
    if (steps.size() == 1)
        execute(std::move(steps.front()));
    else
        execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::ChangeLayerProperties, std::move(steps)));
    return true;
}

bool DocumentController::mergeLayers(const std::vector<doc::LayerId> &ids, QString *reason)
{
    interruptStroke();
    bool incompatible = false;
    const auto layers = mergeableLayers(ids, &incompatible);
    if (layers.size() < 2) {
        if (reason)
            *reason = incompatible ? tr("Layers merge when all are raster, or all vector (with the Normal blend, not clipped)")
                                   : tr("Choose at least two shown, unlocked layers to merge");
        return false;
    }
    const doc::Drawing *drawing = currentDrawing();
    // What they show together, as one image (each with its own opacity and blend).
    doc::Drawing together;
    for (const doc::Layer *layer : layers)
        together.layers.push_back(*layer);
    doc::TiledImage merged;
    if (const QRect bounds = canvas::drawingBounds(together, false); !bounds.isEmpty()) {
        const QImage image = canvas::composeDrawing(together, bounds, false);
        merged.write(bounds.x(), bounds.y(), bounds.width(), bounds.height(),
                     reinterpret_cast<const doc::Pixel *>(image.constBits()), image.bytesPerLine() / 4);
    }
    // The lowest of them keeps the result, drawn plainly (its look is in the pixels now).
    const doc::Layer &bottom = *layers.front();
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (auto paint = doc::PaintCommand::capture(m_currentCel, drawing->id, bottom.id, bottom.image, merged))
        steps.push_back(std::move(paint));
    if (bottom.kind == doc::LayerKind::Vector) { // the strokes of all, bottom to top, each with its layer's opacity
        std::vector<doc::VectorStroke> all;
        for (const doc::Layer *layer : layers)
            for (doc::VectorStroke stroke : layer->strokes) {
                stroke.opacity *= double(layer->properties.opacity);
                all.push_back(std::move(stroke));
            }
        steps.push_back(std::make_unique<doc::SetVectorStrokesCommand>(m_currentCel, drawing->id, bottom.id,
                                                                       bottom.strokes, std::move(all)));
    }
    doc::LayerProperties plain = bottom.properties;
    plain.opacity = 1.0f;
    plain.blend = doc::BlendMode::Normal;
    if (auto look = doc::makeSetLayerProperties(*m_document, m_currentCel, drawing->id, bottom.id, plain))
        steps.push_back(std::move(look));
    // Its mask too is in the pixels: kept, it would hide what the layers above put there.
    if (auto unmask = doc::makeRemoveLayerMask(*m_document, m_currentCel, drawing->id, bottom.id))
        steps.push_back(std::move(unmask));
    for (std::size_t i = 1; i < layers.size(); ++i)
        steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_currentCel, drawing->id, layers[i]->id));
    const doc::LayerId result = bottom.id;
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MergeLayers, std::move(steps)));
    setCurrentLayer(result);
    if (m_maskLayer == result)
        setMaskEditing(doc::kNoId);
    return true;
}

doc::LayerId DocumentController::layerBelow(const std::vector<doc::LayerId> &ids) const
{
    const doc::Drawing *drawing = currentDrawing();
    if (!drawing)
        return doc::kNoId;
    int lowest = -1;
    for (doc::LayerId id : ids) {
        const int i = drawing->indexOf(id);
        if (i >= 0 && (lowest < 0 || i < lowest))
            lowest = i;
    }
    return lowest > 0 ? drawing->layers[std::size_t(lowest - 1)].id : doc::kNoId;
}

bool DocumentController::canMergeDown(const std::vector<doc::LayerId> &ids) const
{
    const doc::LayerId below = layerBelow(ids);
    if (below == doc::kNoId)
        return false;
    std::vector<doc::LayerId> together = ids;
    together.push_back(below);
    // Everything chosen, and the layer below, must be takeable: none is left out silently.
    const auto taken = mergeableLayers(together);
    std::size_t wanted = 0;
    const doc::Drawing *drawing = currentDrawing();
    for (doc::LayerId id : together)
        wanted += drawing->indexOf(id) >= 0 ? 1 : 0;
    return taken.size() >= 2 && taken.size() == wanted;
}

bool DocumentController::mergeDown(const std::vector<doc::LayerId> &ids, QString *reason)
{
    if (layerBelow(ids) == doc::kNoId) {
        if (reason)
            *reason = tr("There is no layer below to merge with");
        return false;
    }
    if (!canMergeDown(ids)) {
        if (reason)
            *reason = tr("The layers to merge, and the one below, must be shown, unlocked, and all raster or all vector");
        return false;
    }
    std::vector<doc::LayerId> together = ids;
    together.push_back(layerBelow(ids));
    return mergeLayers(together, reason);
}

bool DocumentController::copyLayers(const std::vector<LayerRef> &layers, QString *reason)
{
    std::vector<doc::Layer> copied;
    // In the order the document draws them: cels bottom to top, layers bottom to top.
    for (int i = 0; i < m_document->celCount(); ++i) {
        const doc::Cel &cel = m_document->celAt(i);
        std::vector<const doc::Drawing *> seen;
        for (const LayerRef &ref : layers) {
            if (ref.cel != cel.id())
                continue;
            const doc::Drawing *drawing = cel.drawing(ref.drawing);
            if (!drawing || std::find(seen.begin(), seen.end(), drawing) != seen.end())
                continue;
            seen.push_back(drawing);
            for (const doc::Layer &layer : drawing->layers)
                if (std::find(layers.begin(), layers.end(), LayerRef{cel.id(), drawing->id, layer.id}) != layers.end())
                    copied.push_back(layer);
        }
    }
    if (copied.empty()) {
        if (reason)
            *reason = tr("No layers to copy");
        return false;
    }
    m_copiedLayers = std::move(copied); // the pixels shared with the originals until one is painted
    return true;
}

bool DocumentController::pasteLayers(bool merged, const std::vector<doc::LayerId> &replace, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    if (m_copiedLayers.empty())
        return fail(tr("No layers copied"));
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));
    interruptStroke();

    // The new layers: each copied layer as it is (a vector layer with its strokes, which it is drawn again from), or
    // all of them as one raster layer.
    std::vector<doc::Layer> pieces;
    if (merged) {
        doc::Drawing together;
        for (const doc::Layer &layer : m_copiedLayers)
            together.layers.push_back(layer);
        const QRect bounds = canvas::drawingBounds(together, false);
        if (bounds.isEmpty())
            return fail(tr("The copied layers show nothing to paste"));
        const QImage image = canvas::composeDrawing(together, bounds, false);
        doc::Layer piece;
        piece.properties.name = tr("Merged layers").toStdString();
        piece.image.write(bounds.x(), bounds.y(), bounds.width(), bounds.height(),
                          reinterpret_cast<const doc::Pixel *>(image.constBits()), image.bytesPerLine() / 4);
        pieces.push_back(std::move(piece));
    } else {
        pieces = m_copiedLayers;
    }

    std::vector<std::unique_ptr<doc::Command>> steps;
    doc::DrawingId drawingId = currentDrawingId();
    int index = 0;
    doc::LayerId unwanted = doc::kNoId;
    if (drawingId == doc::kNoId) { // nothing here yet: a frame is made, without its empty layer
        doc::NewDrawing created = doc::makeNewDrawing(*m_document, m_currentCel, m_currentFrame);
        drawingId = created.id;
        unwanted = created.layer;
        index = 1;
        steps.push_back(std::move(created.command));
    } else {
        index = int(currentDrawing()->layers.size());
        // Layers chosen to be overwritten: the new ones go where the lowest of them was.
        const doc::Drawing *drawing = currentDrawing();
        const bool theCopied =
            replace.size() == m_copiedLayers.size()
            && std::all_of(replace.begin(), replace.end(), [this](doc::LayerId id) {
                   return std::any_of(m_copiedLayers.begin(), m_copiedLayers.end(),
                                      [id](const doc::Layer &l) { return l.id == id; });
               });
        int lowest = index;
        std::vector<doc::LayerId> going;
        for (const doc::Layer &layer : drawing->layers)
            if (!theCopied && !layer.properties.locked
                && std::find(replace.begin(), replace.end(), layer.id) != replace.end()) {
                going.push_back(layer.id);
                lowest = std::min(lowest, drawing->indexOf(layer.id));
            }
        if (!going.empty()) {
            for (auto it = going.rbegin(); it != going.rend(); ++it) // top first
                steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_currentCel, drawingId, *it));
            index = lowest;
        }
    }
    doc::LayerId last = doc::kNoId;
    for (doc::Layer &piece : pieces) {
        auto add = std::make_unique<doc::AddLayerCommand>(*m_document, m_currentCel, drawingId, index++,
                                                          std::move(piece));
        last = add->layerId();
        steps.push_back(std::move(add));
    }
    if (unwanted != doc::kNoId)
        steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_currentCel, drawingId, unwanted));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::PasteLayers, std::move(steps)));
    setCurrentLayer(last);
    return true;
}

bool DocumentController::previewLayerProperties(const LayerRef &ref, const doc::LayerProperties &properties)
{
    doc::Layer *layer = m_document->layer(ref.cel, ref.drawing, ref.layer);
    if (!layer)
        return false;
    layer->properties = properties;
    emit pixelsChanged(ref.cel, ref.drawing, kWholeCanvas);
    return true;
}

bool DocumentController::previewCelProperties(doc::CelId id, const doc::CelProperties &properties)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return false;
    cel->setProperties(properties);
    // The cel's looks change what the whole frame shows.
    emit pixelsChanged(id, doc::kNoId, kWholeCanvas);
    return true;
}

void DocumentController::commitLayerProperties(const LayerRef &ref, const doc::LayerProperties &before,
                                               const doc::LayerProperties &after)
{
    if (!previewLayerProperties(ref, before))
        return;
    execute(doc::makeSetLayerProperties(*m_document, ref.cel, ref.drawing, ref.layer, after));
}

std::string DocumentController::newFrameLayerName() const
{
    // A vector or perspective layer's name ("Vector 1") would be wrong on a raster layer.
    if (m_currentLayerName.empty() || m_currentLayerKind != doc::LayerKind::Raster)
        return tr("Layer 1").toStdString();
    return m_currentLayerName;
}

void DocumentController::insertFrame(doc::CelId cel, int frame, bool after)
{
    if (!m_document->cel(cel))
        return;
    const int at = std::max(0, frame + (after ? 1 : 0));
    const std::string layerName = newFrameLayerName();
    doc::NewDrawing made = doc::makeInbetween(*m_document, cel, at, layerName,
                                              after ? doc::CommandKind::InsertInbetween : doc::CommandKind::AddDrawing);
    execute(std::move(made.command));
    setCurrentCel(cel);
    setCurrentFrame(at);
}

bool DocumentController::stepToFrame(int direction)
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return false;
    const auto &exposures = cel->timing().exposures();
    std::optional<int> to;
    if (direction > 0) {
        for (const doc::Exposure &e : exposures)
            if (e.start > m_currentFrame) {
                to = e.start;
                break;
            }
    } else {
        // Before the start of the frame shown now (or of the point, when empty).
        const doc::Exposure *here = cel->timing().exposureAt(m_currentFrame);
        const int from = here ? here->start : m_currentFrame;
        for (auto it = exposures.rbegin(); it != exposures.rend(); ++it)
            if (it->start < from) {
                to = it->start;
                break;
            }
    }
    if (!to)
        return false;
    setCurrentFrame(*to);
    return true;
}

bool DocumentController::changeCurrentHold(int delta)
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    const doc::Exposure *hold = cel ? cel->timing().exposureAt(m_currentFrame) : nullptr;
    if (!hold)
        return false;
    const doc::Exposure before = *hold;
    auto command = doc::makeResizeExposure(*m_document, m_currentCel, before.start, std::max(1, before.length + delta));
    if (!command)
        return false;
    execute(std::move(command));
    const doc::Exposure *after = m_document->cel(m_currentCel)->timing().exposureAt(before.start);
    if (after && m_currentFrame >= after->end())
        setCurrentFrame(after->end() - 1);
    return true;
}

void DocumentController::importImage(const QImage &image, const QString &name)
{
    if (image.isNull())
        return;
    interruptStroke();
    doc::Document &d = *m_document;

    // The pixels: scaled down to fit the document if larger, centered.
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixels.width() > d.width() || pixels.height() > d.height())
        pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);

    // New layer above the current one, one drawing held for the whole scene,
    // then the pixels: each step applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1,
                                                    doc::CelProperties{name.toStdString()});
    const doc::CelId celId = cel->celId();
    publish(cel->apply(d));
    steps.push_back(std::move(cel));

    doc::NewDrawing drawing = doc::makeNewDrawing(d, celId, 0, d.frameCount(), tr("Layer 1").toStdString());
    publish(drawing.command->apply(d));
    steps.push_back(std::move(drawing.command));

    doc::TiledImage &target = d.layer(celId, drawing.id, drawing.layer)->image;
    const doc::TiledImage before = target;
    target.write(at.x(), at.y(), pixels.width(), pixels.height(),
                 reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    if (auto paint = doc::PaintCommand::capture(celId, drawing.id, drawing.layer, before, target))
        steps.push_back(std::move(paint));
    emit pixelsChanged(celId, drawing.id, QRect(at, pixels.size()));

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportImage, std::move(steps)));
    emit historyChanged();
    setCurrentCel(celId);
}

bool DocumentController::importAudio(const QString &path, QString *reason)
{
    constexpr qint64 kMaxBytes = qint64(512) << 20; // a song, not an album
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (reason)
            *reason = tr("Cannot read %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    if (file.size() > kMaxBytes || file.size() == 0) {
        if (reason)
            *reason = file.size() == 0 ? tr("%1 is empty.").arg(QFileInfo(path).fileName())
                                       : tr("%1 is too large for the project.").arg(QFileInfo(path).fileName());
        return false;
    }
    const QByteArray bytes = file.readAll();
    doc::AudioTrack track;
    track.name = QFileInfo(path).completeBaseName().toStdString();
    track.format = QFileInfo(path).suffix().toLower().toStdString();
    track.data = std::make_shared<const std::vector<unsigned char>>(bytes.cbegin(), bytes.cend());
    // A new song begins where the old one did, or at the start.
    if (const auto &old = m_document->audio())
        track.offset = old->offset;
    execute(std::make_unique<doc::SetAudioCommand>(std::move(track), doc::CommandKind::SetAudio));
    return true;
}

void DocumentController::removeAudio()
{
    if (m_document->audio())
        execute(std::make_unique<doc::SetAudioCommand>(std::nullopt, doc::CommandKind::SetAudio));
}

namespace {

// The same track, changed by `change`; none if there is no track or nothing changes.
template <class Change>
std::unique_ptr<doc::Command> audioChange(const std::optional<doc::AudioTrack> &track, Change change)
{
    if (!track)
        return nullptr;
    doc::AudioTrack changed = *track;
    change(changed);
    if (changed == *track)
        return nullptr;
    return std::make_unique<doc::SetAudioCommand>(std::move(changed), doc::CommandKind::ChangeAudio);
}

} // namespace

void DocumentController::setAudioOffset(int frames)
{
    execute(audioChange(m_document->audio(), [frames](doc::AudioTrack &t) { t.offset = frames; }));
}

void DocumentController::setAudioVolume(float volume)
{
    execute(audioChange(m_document->audio(), [volume](doc::AudioTrack &t) { t.volume = std::clamp(volume, 0.0f, 1.0f); }));
}

void DocumentController::setAudioMuted(bool muted)
{
    execute(audioChange(m_document->audio(), [muted](doc::AudioTrack &t) { t.muted = muted; }));
}

void DocumentController::previewAudioVolume(float volume)
{
    auto track = m_document->audio();
    if (!track)
        return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (track->volume == volume)
        return;
    track->volume = volume;
    m_document->setAudio(std::move(track));
    emit audioChanged();
}

void DocumentController::commitAudioVolume(float before)
{
    const auto track = m_document->audio();
    if (!track)
        return;
    const float after = track->volume;
    if (after == before)
        return;
    previewAudioVolume(before); // the step goes from where the drag began
    setAudioVolume(after);
}

int DocumentController::tweenFrames(doc::CelId celId, int from, int to, TweenEase ease, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return 0;
    };
    interruptStroke();
    doc::Cel *cel = m_document->cel(celId);
    if (!cel)
        return fail(tr("No cel selected"));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));
    if (from < 0 || to - from < 2)
        return fail(tr("There is no frame between the two poses"));
    const doc::DrawingId a = cel->timing().drawingAt(from), b = cel->timing().drawingAt(to);
    if (a == doc::kNoId || b == doc::kNoId)
        return fail(tr("Both ends of a tween need a drawing"));
    if (a == b)
        return fail(tr("The two frames show the same drawing"));
    const auto tween = DrawingTween::make(*cel->drawing(a), *cel->drawing(b), reason);
    if (!tween)
        return 0;

    // The pictures between: each layer of each frame on its own core.
    const int frames = to - from - 1, layers = tween->layerCount();
    std::vector<doc::Layer> made(std::size_t(frames) * std::size_t(layers));
    parallelFor(frames * layers, [&](int k) {
        const int frame = k / layers, layer = k % layers;
        const double u = double(frame + 1) / double(to - from);
        made[std::size_t(k)] = tween->layerAt(layer, easedTime(ease, u));
    });

    // New drawings, named with the next free numbers, shown one frame each from the first frame after `from`.
    std::set<std::string> used;
    for (const auto &[id, drawing] : cel->drawings())
        used.insert(drawing.name);
    doc::Timing after = cel->timing();
    std::vector<std::unique_ptr<doc::Command>> steps;
    int number = 0;
    for (int frame = 0; frame < frames; ++frame) {
        std::string name;
        do
            name = std::to_string(++number);
        while (used.contains(name));
        used.insert(name);
        doc::Drawing drawing{m_document->allocateDrawingId(), name, {}};
        for (int layer = 0; layer < layers; ++layer) {
            doc::Layer &l = made[std::size_t(frame) * std::size_t(layers) + std::size_t(layer)];
            l.id = m_document->allocateLayerId();
            drawing.layers.push_back(std::move(l));
        }
        after.setExposure(from + 1 + frame, 1, drawing.id);
        steps.push_back(std::make_unique<doc::AddDrawingCommand>(celId, std::move(drawing)));
    }
    steps.push_back(std::make_unique<doc::SetTimingCommand>(celId, cel->timing(), std::move(after)));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::Tween, std::move(steps)));
    return frames;
}

void DocumentController::selectCamera(bool selected)
{
    selected = selected && m_document->camera().has_value();
    if (selected == m_cameraSelected)
        return;
    m_cameraSelected = selected;
    emit cameraSelectionChanged();
}

void DocumentController::addCamera()
{
    if (m_document->camera())
        return; // one for now
    execute(std::make_unique<doc::SetCameraCommand>(
        doc::Camera::wholeSheet(m_document->width(), m_document->height())));
}

void DocumentController::removeCamera()
{
    if (m_document->camera())
        execute(std::make_unique<doc::SetCameraCommand>(std::nullopt));
}

std::vector<doc::CelId> DocumentController::celSelection() const
{
    std::vector<doc::CelId> chosen;
    bool hasCurrent = false;
    for (int i = 0; i < m_document->celCount(); ++i) { // bottom up, those still there
        const doc::CelId id = m_document->celAt(i).id();
        if (std::find(m_celSelection.begin(), m_celSelection.end(), id) != m_celSelection.end()) {
            chosen.push_back(id);
            hasCurrent = hasCurrent || id == m_currentCel;
        }
    }
    // Another cel became current some other way: it alone.
    if (!hasCurrent)
        return m_document->cel(m_currentCel) ? std::vector<doc::CelId>{m_currentCel} : std::vector<doc::CelId>{};
    return chosen;
}

void DocumentController::setCelSelection(const std::vector<doc::CelId> &cels, doc::CelId current)
{
    if (!m_document->cel(current))
        return;
    std::vector<doc::CelId> kept;
    for (doc::CelId id : cels)
        if (m_document->cel(id) && std::find(kept.begin(), kept.end(), id) == kept.end())
            kept.push_back(id);
    if (std::find(kept.begin(), kept.end(), current) == kept.end())
        kept.push_back(current);
    const std::vector<doc::CelId> before = celSelection();
    m_celSelection = kept;
    if (current != m_currentCel)
        setCurrentCel(current);
    if (kept.size() > 1) { // the tools act on the layers of all of them (a pixel move, a transform)
        std::vector<LayerRef> layers;
        for (doc::CelId id : kept)
            for (const auto &[drawingId, drawing] : m_document->cel(id)->drawings())
                for (const doc::Layer &layer : drawing.layers)
                    layers.push_back({id, drawingId, layer.id});
        setLayerSelection(std::move(layers));
    }
    if (celSelection() != before)
        emit celSelectionChanged();
}

bool DocumentController::moveChosenCels(int insertAt)
{
    auto step = doc::makeMoveCels(*m_document, celSelection(), insertAt);
    if (!step)
        return false;
    execute(std::move(step));
    return true;
}

bool DocumentController::shiftChosenCels(bool up)
{
    auto step = doc::makeShiftCels(*m_document, celSelection(), up);
    if (!step)
        return false;
    execute(std::move(step));
    return true;
}

void DocumentController::chooseCel(doc::CelId id)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return;
    selectCamera(false);
    setLayerChosen(false); // a cel is not a layer: the pixel tools wait for one to be chosen
    setCurrentCel(id);
    std::vector<LayerRef> layers;
    for (const auto &[drawingId, drawing] : cel->drawings())
        for (const doc::Layer &layer : drawing.layers)
            layers.push_back({id, drawingId, layer.id});
    setLayerSelection(std::move(layers));
    const std::vector<doc::CelId> before = celSelection();
    m_celSelection = {id}; // chosen alone
    if (celSelection() != before)
        emit celSelectionChanged();
    emit celChosen(id);
}

void DocumentController::setLayerChosen(bool chosen)
{
    if (chosen == m_layerChosen)
        return;
    m_layerChosen = chosen;
    emit layerChosenChanged();
}

bool DocumentController::drawingBlocked() const
{
    if (m_cameraSelected)
        return true;
    if (m_layerChosen)
        return false;
    const doc::Cel *cel = m_document->cel(m_currentCel);
    return cel && shownDrawingId(*cel) != doc::kNoId;
}

void DocumentController::setMaskEditing(doc::LayerId layer)
{
    if (layer == m_maskLayer)
        return;
    interruptStroke();
    m_maskLayer = layer;
    emit maskEditingChanged();
}

bool DocumentController::createLayerMask()
{
    const doc::DrawingId drawing = currentDrawingId();
    auto step = doc::makeAddLayerMask(*m_document, m_currentCel, drawing, m_currentLayer);
    if (!step)
        return false;
    execute(std::move(step));
    setMaskEditing(m_currentLayer); // the mask is what is drawn on now
    emit maskEditingChanged();
    return true;
}

bool DocumentController::deleteLayerMask()
{
    const doc::DrawingId drawing = currentDrawingId();
    auto step = doc::makeRemoveLayerMask(*m_document, m_currentCel, drawing, m_currentLayer);
    if (!step)
        return false;
    execute(std::move(step));
    if (m_maskLayer == m_currentLayer)
        setMaskEditing(doc::kNoId);
    emit maskEditingChanged();
    return true;
}

const doc::CelMotion *DocumentController::celMotion(doc::CelId id) const
{
    const doc::Cel *cel = m_document->cel(id);
    return cel ? &cel->motion() : nullptr;
}

doc::CelPose DocumentController::celPoseAt(doc::CelId id, int frame) const
{
    const doc::Cel *cel = m_document->cel(id);
    return cel ? cel->motion().poseAt(frame) : doc::CelPose{};
}

bool DocumentController::celKeyMotion(const doc::Cel &cel, int frame, const doc::CelPose &given, doc::CelMotion &out) const
{
    doc::CelMotion motion = cel.motion();
    const bool first = motion.isEmpty();
    if (first)
        startMotion(motion, cel, frame);
    doc::CelPose pose = given;
    if (pose.deform.empty() && cel.properties().deformer != doc::DeformerKind::None)
        pose.deform = m_document->deformPoseAt(cel, frame); // a deformer with no keys yet: its rest
    doc::CelPose now = motion.interpolatedAt(frame);
    if (now.deform.empty())
        now.deform = cel.deformer().rest;
    bool changed = false;
    for (doc::Channel c : doc::kChannels) {
        if (!doc::hasChannel(cel.properties(), c))
            continue;
        const bool wanted = motion.hasKeys(c) || doc::channelsDiffer(c, pose, now) || (first && c != doc::Channel::Opacity);
        if (!wanted)
            continue;
        if (const doc::CelKey *existing = motion.keyAt(c, frame); existing && existing->pose == doc::channelOf(c, pose))
            continue;
        motion.setKey(c, frame, pose);
        changed = true;
    }
    if (changed)
        out = std::move(motion);
    return changed;
}

bool DocumentController::setCelKey(doc::CelId id, int frame, const doc::CelPose &pose)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0)
        return false;
    clearTemporaryPose(); // what is made a key now is the pose that was shown (the caller has it already)
    doc::CelMotion motion;
    if (!celKeyMotion(*cel, frame, pose, motion))
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::setCelChannelKey(doc::CelId id, doc::Channel channel, int frame, const doc::CelPose &given)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0 || !doc::hasChannel(cel->properties(), channel))
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    if (motion.isEmpty())
        startMotion(motion, *cel, frame);
    doc::CelPose pose = given;
    if (channel == doc::Channel::Deform && pose.deform.empty())
        pose.deform = m_document->deformPoseAt(*cel, frame);
    if (const doc::CelKey *existing = motion.keyAt(channel, frame); existing && existing->pose == doc::channelOf(channel, pose))
        return false;
    motion.setKey(channel, frame, pose);
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

void DocumentController::startMotion(doc::CelMotion &motion, const doc::Cel &cel, int frame) const
{
    // The first key: the cel turns and scales around the middle of what its frame shows (the page's, if it shows nothing).
    QRect bounds;
    if (const doc::Drawing *drawing = cel.drawingAt(frame))
        bounds = canvas::visibleBounds(*drawing);
    const QPointF middle = QRectF(bounds).center();
    motion.pivotX = bounds.isEmpty() ? m_document->width() / 2.0 : middle.x();
    motion.pivotY = bounds.isEmpty() ? m_document->height() / 2.0 : middle.y();
}

void DocumentController::toggleCelKey(doc::CelId id)
{
    interruptStroke(); // a pose edit left open (Transform, Move) is applied first
    const doc::CelMotion *motion = celMotion(id);
    if (motion && motion->hasKeyAt(m_currentFrame))
        removeCelKey(id, m_currentFrame);
    else
        setCelKey(id, m_currentFrame, celPoseAt(id, m_currentFrame)); // the pose left on the frame, or the one it has
}

void DocumentController::toggleChosenCelKeys()
{
    interruptStroke(); // a pose edit left open (Transform, Move) is applied first
    const std::vector<doc::CelId> chosen = celSelection();
    if (chosen.size() < 2) {
        toggleCelKey(m_currentCel);
        return;
    }
    // As for one cel: the current one decides. It has a key here: the keys on this frame go from all of them; it has none:
    // every one that has none gets a key, at the pose it has (or the one left on the frame). One undo step.
    const doc::CelMotion *current = celMotion(m_currentCel);
    const bool remove = current && current->hasKeyAt(m_currentFrame);
    std::vector<doc::CelPose> shown; // as they are shown now, before the pose left on the frame is cleared
    for (doc::CelId id : chosen)
        shown.push_back(celPoseAt(id, m_currentFrame));
    clearTemporaryPose();
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const doc::Cel *cel = m_document->cel(chosen[i]);
        if (!cel)
            continue;
        doc::CelMotion motion;
        if (remove) {
            motion = cel->motion();
            if (!motion.removeKeysAt(m_currentFrame))
                continue;
        } else if (cel->motion().hasKeyAt(m_currentFrame) || !celKeyMotion(*cel, m_currentFrame, shown[i], motion)) {
            continue;
        }
        steps.push_back(std::make_unique<doc::SetCelMotionCommand>(chosen[i], std::move(motion)));
    }
    if (steps.empty())
        return;
    if (steps.size() == 1)
        execute(std::move(steps.front()));
    else
        execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditMotion, std::move(steps)));
}

bool DocumentController::applyCelPose(doc::CelId id, int frame, const doc::CelPose &pose,
                                      const std::vector<doc::Channel> &channels)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0)
        return false;
    doc::CelMotion motion = cel->motion();
    for (doc::Channel channel : channels) {
        if (motion.keyAt(channel, frame)) {
            motion.setKey(channel, frame, pose);
        } else {
            // No key of this channel here: the value stays only while this frame is the one shown (or until it is made a key).
            if (m_tempPoseCel != id)
                clearTemporaryPose();
            motion.setTemporary(channel, frame, pose);
            m_tempPoseCel = id;
        }
    }
    cel->setMotion(std::move(motion));
    return true;
}

void DocumentController::previewCelPose(doc::CelId id, int frame, const doc::CelPose &pose,
                                        const std::vector<doc::Channel> &channels)
{
    if (!applyCelPose(id, frame, pose, channels))
        return;
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

void DocumentController::previewCelPoses(const std::vector<CelPoseChange> &changes)
{
    // All of them, then the views hear of it once: the frame is drawn again once, not for each cel.
    bool any = false;
    for (const CelPoseChange &c : changes)
        any = applyCelPose(c.cel, c.frame, c.pose, c.channels) || any;
    if (!any)
        return;
    doc::ChangeSet published;
    published.motion = true;
    publish(published);
}

void DocumentController::setCelPose(doc::CelId id, int frame, const doc::CelPose &pose, doc::Channel channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0 || cel->motion().isEmpty())
        return;
    if (cel->motion().keyAt(channel, frame))
        setCelChannelKey(id, channel, frame, pose);
    else
        previewCelPose(id, frame, pose, {channel});
}

void DocumentController::clearTemporaryPose()
{
    if (m_tempPoseCel == doc::kNoId)
        return;
    const doc::CelId id = m_tempPoseCel;
    m_tempPoseCel = doc::kNoId;
    doc::Cel *cel = m_document->cel(id);
    if (!cel || !cel->motion().hasTemporary())
        return;
    doc::CelMotion motion = cel->motion();
    motion.clearTemporary();
    cel->setMotion(std::move(motion));
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

void DocumentController::finishCelPose(doc::CelId id, const doc::CelMotion &before)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || cel->motion() == before)
        return;
    m_history.pushApplied(std::make_unique<doc::SetCelMotionCommand>(id, before, cel->motion()));
    emit historyChanged();
}

void DocumentController::finishCelPoses(const std::vector<std::pair<doc::CelId, doc::CelMotion>> &before)
{
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (const auto &[id, motion] : before) {
        const doc::Cel *cel = m_document->cel(id);
        if (cel && !(cel->motion() == motion))
            steps.push_back(std::make_unique<doc::SetCelMotionCommand>(id, motion, cel->motion()));
    }
    if (steps.empty())
        return;
    if (steps.size() == 1)
        m_history.pushApplied(std::move(steps.front()));
    else
        m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::EditMotion, std::move(steps)));
    emit historyChanged();
}

void DocumentController::beginFirstKey(doc::CelId id, int frame)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel || frame < 0 || !cel->motion().isEmpty())
        return;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    startMotion(motion, *cel, frame);
    if (cel->properties().deformer != doc::DeformerKind::None) { // a deformer: its pose, at rest
        doc::CelPose rest;
        rest.deform = cel->deformer().rest;
        motion.setKey(doc::Channel::Deform, frame, rest);
    } else {
        for (doc::Channel c : {doc::Channel::Position, doc::Channel::Rotation, doc::Channel::Scale})
            motion.setKey(c, frame, doc::CelPose{});
    }
    cel->setMotion(std::move(motion));
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

void DocumentController::cancelCelPose(doc::CelId id, const doc::CelMotion &before)
{
    doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return;
    cel->setMotion(before);
    doc::ChangeSet changes;
    changes.motion = true;
    publish(changes);
}

bool DocumentController::setCelKeyEasing(doc::CelId id, int frame, const doc::Easing &easing, std::optional<doc::Channel> channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    bool changed = false;
    for (doc::Channel c : doc::kChannels) {
        const doc::CelKey *key = motion.keyAt(c, frame);
        if ((channel && *channel != c) || !key || key->easing == easing)
            continue;
        motion.setEasing(c, frame, easing);
        changed = true;
    }
    if (!changed)
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::removeCelKey(doc::CelId id, int frame, std::optional<doc::Channel> channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    if (!(channel ? motion.removeKey(*channel, frame) : motion.removeKeysAt(frame)))
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

namespace {

// Applies a change made to copies of the motions of some cels as one undo step.
std::unique_ptr<doc::Command> motionsCommand(const doc::Document &document, std::map<doc::CelId, doc::CelMotion> &motions)
{
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (auto &[id, motion] : motions) {
        const doc::Cel *cel = document.cel(id);
        if (cel && !(cel->motion() == motion))
            steps.push_back(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    }
    if (steps.empty())
        return nullptr;
    if (steps.size() == 1)
        return std::move(steps.front());
    return std::make_unique<doc::GroupCommand>(doc::CommandKind::EditMotion, std::move(steps));
}

} // namespace

bool DocumentController::removeCelKeys(const std::vector<CelKeyRef> &keys)
{
    clearTemporaryPose();
    std::map<doc::CelId, doc::CelMotion> motions;
    for (const CelKeyRef &k : keys) {
        const doc::Cel *cel = m_document->cel(k.cel);
        if (!cel)
            continue;
        auto it = motions.try_emplace(k.cel, cel->motion()).first;
        it->second.removeKey(k.channel, k.frame);
    }
    auto command = motionsCommand(*m_document, motions);
    if (!command)
        return false;
    execute(std::move(command));
    return true;
}

bool DocumentController::setCelKeysEasing(const std::vector<CelKeyRef> &keys, const doc::Easing &easing)
{
    clearTemporaryPose();
    std::map<doc::CelId, doc::CelMotion> motions;
    for (const CelKeyRef &k : keys) {
        const doc::Cel *cel = m_document->cel(k.cel);
        if (!cel)
            continue;
        doc::CelMotion &motion = motions.try_emplace(k.cel, cel->motion()).first->second;
        if (motion.keyAt(k.channel, k.frame))
            motion.setEasing(k.channel, k.frame, easing);
    }
    auto command = motionsCommand(*m_document, motions);
    if (!command)
        return false;
    execute(std::move(command));
    return true;
}

bool DocumentController::movedKeyMotions(const std::vector<CelKeyRef> &keys, const std::function<int(int)> &to,
                                         std::map<doc::CelId, doc::CelMotion> &motions) const
{
    std::vector<std::pair<CelKeyRef, doc::CelKey>> lifted; // taken out of their motions, to be put back where they go
    for (const CelKeyRef &k : keys) {
        const doc::Cel *cel = m_document->cel(k.cel);
        const doc::CelKey *key = cel ? cel->motion().keyAt(k.channel, k.frame) : nullptr;
        if (!key || to(k.frame) < 0)
            return false;
        motions.try_emplace(k.cel, cel->motion());
        lifted.push_back({k, *key});
    }
    for (const auto &[k, key] : lifted)
        motions.at(k.cel).removeKey(k.channel, k.frame);
    for (const auto &[k, key] : lifted) {
        doc::CelMotion &motion = motions.at(k.cel);
        if (motion.keyAt(k.channel, to(k.frame))) // a key that stays (or moved there already): a place taken
            return false;
        motion.setKey(k.channel, to(k.frame), key.pose, key.easing);
    }
    return true;
}

bool DocumentController::moveCelKeys(const std::vector<CelKeyRef> &keys, int delta)
{
    if (delta == 0)
        return false;
    clearTemporaryPose();
    std::map<doc::CelId, doc::CelMotion> motions;
    if (!movedKeyMotions(keys, [delta](int f) { return f + delta; }, motions))
        return false;
    auto command = motionsCommand(*m_document, motions);
    if (!command)
        return false;
    execute(std::move(command));
    return true;
}

void DocumentController::previewKeyMove(const std::vector<CelKeyRef> &keys, int delta)
{
    std::map<doc::CelId, doc::CelMotion> motions;
    if (delta == 0 || !movedKeyMotions(keys, [delta](int f) { return f + delta; }, motions))
        motions.clear(); // nothing moved (or refused): the keys as they are
    previewMotions(std::move(motions));
}

void DocumentController::previewMotions(std::map<doc::CelId, doc::CelMotion> motions)
{
    if (motions.empty() && m_keyPreview.empty())
        return;
    m_keyPreview = std::move(motions);
    emit motionPreviewChanged();
}

int DocumentController::scaledKeyFrame(int frame, int pivot, double factor)
{
    return pivot + int(std::lround((frame - pivot) * factor));
}

bool DocumentController::scaleCelKeys(const std::vector<CelKeyRef> &keys, int pivot, double factor)
{
    clearTemporaryPose();
    std::map<doc::CelId, doc::CelMotion> motions;
    if (!(factor >= 0) || !movedKeyMotions(keys, [=](int f) { return scaledKeyFrame(f, pivot, factor); }, motions))
        return false;
    auto command = motionsCommand(*m_document, motions);
    if (!command)
        return false; // all where they were
    execute(std::move(command));
    return true;
}

void DocumentController::previewKeyScale(const std::vector<CelKeyRef> &keys, int pivot, double factor)
{
    std::map<doc::CelId, doc::CelMotion> motions;
    if (!(factor >= 0) || !movedKeyMotions(keys, [=](int f) { return scaledKeyFrame(f, pivot, factor); }, motions))
        motions.clear();
    previewMotions(std::move(motions));
}

void DocumentController::clearKeyMovePreview()
{
    if (m_keyPreview.empty())
        return;
    m_keyPreview.clear();
    emit motionPreviewChanged();
}

const doc::CelMotion *DocumentController::previewedMotion(doc::CelId cel) const
{
    const auto it = m_keyPreview.find(cel);
    return it == m_keyPreview.end() ? nullptr : &it->second;
}

std::vector<DocumentController::CelKeyCopy> DocumentController::copyCelKeys(const std::vector<CelKeyRef> &keys) const
{
    std::vector<CelKeyCopy> copied;
    int first = std::numeric_limits<int>::max();
    for (const CelKeyRef &k : keys) {
        const doc::Cel *cel = m_document->cel(k.cel);
        if (const doc::CelKey *key = cel ? cel->motion().keyAt(k.channel, k.frame) : nullptr) {
            copied.push_back({k.cel, k.frame, k.channel, *key});
            first = std::min(first, k.frame);
        }
    }
    for (CelKeyCopy &c : copied)
        c.offset -= first;
    return copied;
}

bool DocumentController::pasteCelKeys(const std::vector<CelKeyCopy> &copied, int frame, doc::CelId onto)
{
    if (copied.empty() || frame < 0)
        return false;
    bool oneCel = true;
    for (const CelKeyCopy &c : copied)
        oneCel = oneCel && c.cel == copied.front().cel;
    clearTemporaryPose();
    std::map<doc::CelId, doc::CelMotion> motions;
    for (const CelKeyCopy &c : copied) {
        const doc::CelId target = oneCel ? onto : c.cel;
        const doc::Cel *cel = m_document->cel(target);
        if (!cel)
            continue;
        auto [it, fresh] = motions.try_emplace(target, cel->motion());
        if (fresh && it->second.isEmpty())
            startMotion(it->second, *cel, frame); // its first keys: it turns around the middle of what it shows
        it->second.setKey(c.channel, frame + c.offset, c.key.pose, c.key.easing);
    }
    auto command = motionsCommand(*m_document, motions);
    if (!command)
        return false;
    execute(std::move(command));
    return true;
}

bool DocumentController::moveCelKey(doc::CelId id, int frame, int to, std::optional<doc::Channel> channel)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || frame == to)
        return false;
    clearTemporaryPose();
    doc::CelMotion motion = cel->motion();
    if (!(channel ? motion.moveKey(*channel, frame, to) : motion.moveKeysAt(frame, to)))
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::setCelPivot(doc::CelId id, double x, double y)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || (cel->motion().pivotX == x && cel->motion().pivotY == y))
        return false;
    doc::CelMotion motion = cel->motion();
    motion.pivotX = x;
    motion.pivotY = y;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(motion)));
    return true;
}

bool DocumentController::clearCelMotion(doc::CelId id)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || cel->motion().isEmpty())
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, doc::CelMotion{}));
    return true;
}

bool DocumentController::reverseCelMotion(doc::CelId id)
{
    const doc::Cel *cel = m_document->cel(id);
    if (!cel || cel->motion().keyFrames().size() < 2)
        return false;
    doc::CelMotion turned = cel->motion().reversed();
    if (turned == cel->motion())
        return false;
    execute(std::make_unique<doc::SetCelMotionCommand>(id, std::move(turned)));
    return true;
}

bool DocumentController::setMarker(int frame, const QString &name)
{
    if (frame < 0)
        return false;
    std::map<int, std::string> markers = m_document->markers();
    const std::string text = name.toStdString();
    if (const auto it = markers.find(frame); it != markers.end() && it->second == text)
        return false;
    markers[frame] = text;
    execute(std::make_unique<doc::SetMarkersCommand>(std::move(markers)));
    return true;
}

bool DocumentController::removeMarker(int frame)
{
    std::map<int, std::string> markers = m_document->markers();
    if (markers.erase(frame) == 0)
        return false;
    execute(std::make_unique<doc::SetMarkersCommand>(std::move(markers)));
    return true;
}

bool DocumentController::moveMarker(int from, int to)
{
    std::map<int, std::string> markers = m_document->markers();
    const auto it = markers.find(from);
    if (it == markers.end() || to < 0 || (to != from && markers.count(to)))
        return false;
    if (to == from)
        return false;
    markers[to] = it->second;
    markers.erase(from);
    execute(std::make_unique<doc::SetMarkersCommand>(std::move(markers)));
    return true;
}

void DocumentController::toggleMarker()
{
    if (!removeMarker(m_currentFrame))
        setMarker(m_currentFrame, QString());
}

bool DocumentController::goToMarker(int direction)
{
    const auto &markers = m_document->markers();
    if (direction > 0) {
        const auto it = markers.upper_bound(m_currentFrame);
        if (it == markers.end())
            return false;
        setCurrentFrame(it->first);
        return true;
    }
    auto it = markers.lower_bound(m_currentFrame);
    if (it == markers.begin())
        return false;
    setCurrentFrame(std::prev(it)->first);
    return true;
}

void DocumentController::setSceneLength(int frames)
{
    frames = std::max(1, frames);
    if (frames == m_document->frameCount())
        return;
    execute(std::make_unique<doc::SetFrameCountCommand>(frames));
    if (m_document->sceneStart() > frames - 1)
        m_document->setSceneStart(frames - 1);
    emit playRangeChanged();
}

void DocumentController::setFrameRate(int fps)
{
    const doc::FrameRate rate{std::max(fps, 1), 1};
    if (rate == m_document->frameRate())
        return;
    execute(std::make_unique<doc::SetFrameRateCommand>(rate));
}

QPointF DocumentController::symmetryCenter() const
{
    const doc::Document::SymmetryAxes &axes = m_document->symmetryAxes();
    if (axes.placed)
        return {axes.x, axes.y};
    return {m_document->width() / 2.0, m_document->height() / 2.0};
}

void DocumentController::setSymmetryAxes(const QPointF &center, double angle)
{
    angle = std::remainder(angle, 360.0);
    const doc::Document::SymmetryAxes axes{true, center.x(), center.y(), angle};
    if (axes == m_document->symmetryAxes())
        return;
    m_document->setSymmetryAxes(axes);
    emit symmetryAxesChanged();
}

void DocumentController::resetSymmetryAxes()
{
    if (!m_document->symmetryAxes().placed)
        return;
    m_document->setSymmetryAxes({});
    emit symmetryAxesChanged();
}

int DocumentController::playRangeLast() const
{
    return std::max(m_document->frameCount() - 1, m_document->sceneStart());
}

void DocumentController::setPlayRange(int first, int last)
{
    first = std::max(0, first);
    last = std::max(last == kSceneEnd ? m_document->frameCount() - 1 : last, first);
    const bool moved = first != m_document->sceneStart();
    m_document->setSceneStart(first);
    if (last + 1 != m_document->frameCount())
        setSceneLength(last + 1); // (tells of the range itself)
    else if (moved)
        emit playRangeChanged();
}

int DocumentController::sceneLength() const { return m_document->frameCount(); }

int DocumentController::usedFrames() const
{
    int last = 0;
    for (int i = 0; i < m_document->celCount(); ++i)
        last = std::max(last, m_document->celAt(i).timing().lastFrame());
    return last;
}

void DocumentController::setCameraVisible(bool visible)
{
    if (!m_document->camera() || m_document->camera()->visible == visible)
        return;
    doc::Camera camera = *m_document->camera();
    camera.visible = visible;
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
}

void DocumentController::setCameraKey(int frame, const doc::CameraPose &pose)
{
    doc::Camera camera = m_document->camera() ? *m_document->camera() : doc::Camera();
    if (const doc::CameraKey *key = camera.keyAt(frame); key && key->pose == pose)
        return;
    camera.setKey(frame, pose);
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
}

bool DocumentController::removeCameraKey(int frame)
{
    if (!m_document->camera())
        return false;
    doc::Camera camera = *m_document->camera();
    if (!camera.removeKey(frame))
        return false;
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
    return true;
}

bool DocumentController::moveCameraKey(int from, int to)
{
    if (!m_document->camera() || from == to)
        return false;
    doc::Camera camera = *m_document->camera();
    if (!camera.moveKey(from, to))
        return false;
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
    return true;
}

bool DocumentController::setCameraEasing(int frame, const doc::Easing &easing)
{
    if (!m_document->camera())
        return false;
    doc::Camera camera = *m_document->camera();
    if (!camera.setEasing(frame, easing))
        return false;
    if (camera == *m_document->camera())
        return true; // already so
    execute(std::make_unique<doc::SetCameraCommand>(std::move(camera)));
    return true;
}

doc::CameraPose DocumentController::cameraPoseAt(int frame) const
{
    const auto &camera = m_document->camera();
    if (camera && camera->visible && !camera->isEmpty())
        return camera->poseAt(frame);
    return doc::CameraPose::wholeSheet(m_document->width(), m_document->height());
}

void DocumentController::previewCamera(const std::optional<doc::Camera> &camera)
{
    m_document->setCamera(camera);
    emit cameraChanged();
}

void DocumentController::commitCamera(const std::optional<doc::Camera> &before, const std::optional<doc::Camera> &after)
{
    // Copies: either may be the document's own camera, which the preview below replaces.
    const std::optional<doc::Camera> was = before, now = after;
    previewCamera(was); // so the step records the whole change, not the part after the last preview
    if (was != now)
        execute(std::make_unique<doc::SetCameraCommand>(now));
}

void DocumentController::setCompositing(doc::GraphData graph)
{
    if (graph != m_document->compositing())
        execute(std::make_unique<doc::SetCompositingCommand>(std::move(graph)));
}

void DocumentController::setAudioScrub(bool scrub)
{
    execute(audioChange(m_document->audio(), [scrub](doc::AudioTrack &t) { t.scrub = scrub; }));
}

void DocumentController::setAudioDuration(double seconds)
{
    auto track = m_document->audio();
    if (!track || track->duration == seconds)
        return;
    track->duration = seconds;
    m_document->setAudio(std::move(track));
    emit structureChanged(); // not an undo step: the length of the block is a fact about the file
}

std::vector<std::pair<doc::CelId, doc::BlenderLink>> DocumentController::blenderLinks() const
{
    std::vector<std::pair<doc::CelId, doc::BlenderLink>> links;
    for (int i = 0; i < m_document->celCount(); ++i)
        if (const auto &link = m_document->celAt(i).blenderLink())
            links.emplace_back(m_document->celAt(i).id(), *link);
    return links;
}

std::optional<DocumentController::PendingSequence> DocumentController::beginSequenceCel(const QString &name, int startFrame,
                                                                                         int count,
                                                                                         const doc::BlenderLink *link)
{
    if (count <= 0)
        return std::nullopt;
    interruptStroke();
    doc::Document &d = *m_document;
    startFrame = std::max(0, startFrame);
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(m_currentCel) + 1, doc::CelProperties{name.toStdString()});
    PendingSequence sequence;
    sequence.cel = cel->celId();
    doc::ChangeSet changes = cel->apply(d);
    steps.push_back(std::move(cel));
    if (link) { // where the cel comes from, as part of the same step
        auto linking = std::make_unique<doc::SetBlenderLinkCommand>(sequence.cel, *link);
        changes.merge(linking->apply(d));
        steps.push_back(std::move(linking));
    }
    for (int i = 0; i < count; ++i) {
        doc::NewDrawing drawing = doc::makeNewDrawing(d, sequence.cel, startFrame + i, 1, tr("Layer 1").toStdString());
        changes.merge(drawing.command->apply(d));
        steps.push_back(std::move(drawing.command));
        sequence.drawings.push_back(drawing.id);
        sequence.layers.push_back(drawing.layer);
    }
    if (startFrame + count > d.frameCount())
        d.setFrameCount(startFrame + count);
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    publish(changes);
    emit historyChanged();
    setCurrentCel(sequence.cel);
    setCurrentFrame(startFrame);
    return sequence;
}

bool DocumentController::fillSequenceFrame(const PendingSequence &sequence, int index, const QImage &image)
{
    if (index < 0 || index >= int(sequence.drawings.size()) || image.isNull())
        return false;
    doc::Document &d = *m_document;
    doc::Layer *layer = d.layer(sequence.cel, sequence.drawings[std::size_t(index)], sequence.layers[std::size_t(index)]);
    if (!layer)
        return false;
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixels.width() != d.width() || pixels.height() != d.height())
        pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);
    layer->image.write(at.x(), at.y(), pixels.width(), pixels.height(),
                       reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    emit pixelsChanged(sequence.cel, sequence.drawings[std::size_t(index)], QRect(at, pixels.size()));
    return true;
}

std::unique_ptr<DocumentController::SequenceUpdate> DocumentController::beginSequenceUpdate(doc::CelId cel, int startFrame)
{
    if (!m_document->cel(cel))
        return nullptr;
    auto update = std::make_unique<SequenceUpdate>();
    update->cel = cel;
    update->startFrame = std::max(0, startFrame);
    return update;
}

bool DocumentController::applySequenceFrames(SequenceUpdate &update, const std::map<int, QImage> &frames)
{
    doc::Document &d = *m_document;
    doc::Cel *cel = d.cel(update.cel);
    if (!cel)
        return false;
    doc::ChangeSet changes;
    std::vector<std::pair<doc::DrawingId, QRect>> painted;
    for (const auto &[index, image] : frames) {
        if (image.isNull())
            continue;
        const int frame = update.startFrame + index;
        doc::DrawingId drawingId = cel->timing().drawingAt(frame);
        doc::LayerId layerId = doc::kNoId;
        const bool isNew = drawingId == doc::kNoId || !cel->drawing(drawingId);
        if (isNew) { // the scene got longer: the frames after the last one are new
            doc::NewDrawing drawing = doc::makeNewDrawing(d, update.cel, frame, 1, tr("Layer 1").toStdString());
            changes.merge(drawing.command->apply(d));
            update.steps.push_back(std::move(drawing.command));
            drawingId = drawing.id;
            layerId = drawing.layer;
            if (frame + 1 > d.frameCount())
                d.setFrameCount(frame + 1);
        } else if (!cel->drawing(drawingId)->layers.empty()) {
            layerId = cel->drawing(drawingId)->layers.front().id; // the render is the first layer; others are the person's
        }
        doc::Layer *layer = layerId == doc::kNoId ? nullptr : d.layer(update.cel, drawingId, layerId);
        if (!layer)
            continue;
        const std::pair<doc::DrawingId, doc::LayerId> key{drawingId, layerId};
        if (!update.before.count(key))
            update.before.emplace(key, isNew ? doc::TiledImage() : layer->image); // as it was, for the undo
        QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (pixels.width() != d.width() || pixels.height() != d.height())
            pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);
        layer->image = doc::TiledImage(); // the new picture replaces the old one, all of it
        layer->image.write(at.x(), at.y(), pixels.width(), pixels.height(),
                           reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
        painted.emplace_back(drawingId, QRect(0, 0, d.width(), d.height()));
    }
    publish(changes);
    for (const auto &[drawingId, area] : painted)
        emit pixelsChanged(update.cel, drawingId, area);
    return true;
}

void DocumentController::finishSequenceUpdate(SequenceUpdate &update)
{
    doc::Document &d = *m_document;
    std::vector<std::unique_ptr<doc::Command>> steps = std::move(update.steps);
    update.steps.clear();
    for (const auto &[key, before] : update.before) {
        const doc::Layer *layer = d.layer(update.cel, key.first, key.second);
        if (!layer)
            continue;
        if (auto paint = doc::PaintCommand::capture(update.cel, key.first, key.second, before, layer->image))
            steps.push_back(std::move(paint));
    }
    update.before.clear();
    if (steps.empty())
        return;
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::UpdateSequence, std::move(steps)));
    emit historyChanged();
}

void DocumentController::finishSequence(const PendingSequence &sequence)
{
    doc::Document &d = *m_document;
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (std::size_t i = 0; i < sequence.drawings.size(); ++i) {
        const doc::Layer *layer = d.layer(sequence.cel, sequence.drawings[i], sequence.layers[i]);
        if (!layer)
            continue;
        if (auto paint = doc::PaintCommand::capture(sequence.cel, sequence.drawings[i], sequence.layers[i], doc::TiledImage(),
                                                    layer->image))
            steps.push_back(std::move(paint));
    }
    if (steps.empty())
        return;
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    emit historyChanged();
}

int DocumentController::importImageSequence(const QList<QImage> &images, const QString &name, int startFrame,
                                            int hold, const std::function<void(int)> &progress)
{
    QList<QImage> usable;
    for (const QImage &image : images)
        if (!image.isNull())
            usable << image;
    if (usable.isEmpty())
        return 0;
    startFrame = std::max(0, startFrame);
    hold = std::max(1, hold);
    std::vector<std::pair<int, int>> runs;
    for (int i = 0; i < usable.size(); ++i)
        runs.emplace_back(startFrame + i * hold, hold);
    importRuns(runs, [&usable](int i) { return usable[i]; }, name, m_currentCel, progress);
    return int(usable.size());
}

doc::CelId DocumentController::importRuns(const std::vector<std::pair<int, int>> &runs,
                                          const std::function<QImage(int)> &picture, const QString &name,
                                          doc::CelId above, const std::function<void(int)> &progress)
{
    if (runs.empty())
        return doc::kNoId;
    interruptStroke();
    doc::Document &d = *m_document;

    // One new cel above `above`; each picture a drawing of its own. Applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    auto cel = std::make_unique<doc::AddCelCommand>(d, d.indexOf(above) + 1, doc::CelProperties{name.toStdString()});
    const doc::CelId celId = cel->celId();
    // The views hear of it once, when all is in: one refresh, not one for each of the frames.
    doc::ChangeSet changes = cel->apply(d);
    steps.push_back(std::move(cel));
    std::vector<std::pair<doc::DrawingId, QRect>> painted;
    int end = 0;

    for (int i = 0; i < int(runs.size()); ++i) {
        // Scaled down to fit the document if larger, centered, as a single imported image is.
        QImage pixels = picture(i).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (pixels.width() > d.width() || pixels.height() > d.height())
            pixels = pixels.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint at((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2);

        doc::NewDrawing drawing =
            doc::makeNewDrawing(d, celId, runs[std::size_t(i)].first, runs[std::size_t(i)].second, tr("Layer 1").toStdString());
        changes.merge(drawing.command->apply(d));
        steps.push_back(std::move(drawing.command));
        doc::TiledImage &target = d.layer(celId, drawing.id, drawing.layer)->image;
        const doc::TiledImage before = target;
        target.write(at.x(), at.y(), pixels.width(), pixels.height(),
                     reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
        if (auto paint = doc::PaintCommand::capture(celId, drawing.id, drawing.layer, before, target))
            steps.push_back(std::move(paint));
        painted.emplace_back(drawing.id, QRect(at, pixels.size()));
        end = std::max(end, runs[std::size_t(i)].first + runs[std::size_t(i)].second);
        if (progress)
            progress(i + 1);
    }
    // The scene is as long as the pictures need (this is not one of the undo steps).
    if (end > d.frameCount())
        d.setFrameCount(end);

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ImportSequence, std::move(steps)));
    publish(changes);
    for (const auto &[drawingId, area] : painted)
        emit pixelsChanged(celId, drawingId, area);
    emit historyChanged();
    setCurrentCel(celId);
    setCurrentFrame(runs.front().first);
    return celId;
}

doc::CelId DocumentController::rasterCel(doc::CelId celId, QString *reason)
{
    const doc::Cel *cel = m_document->cel(celId);
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return doc::kNoId;
    };
    if (!cel)
        return fail(tr("There is no cel."));
    int first = -1, last = -1;
    for (const doc::Exposure &e : cel->timing().exposures()) {
        first = first < 0 ? e.start : std::min(first, e.start);
        last = std::max(last, e.end());
    }
    if (first < 0)
        return fail(tr("The cel has no drawings."));

    // The frames with a drawing, in stretches that look the same: the same drawing under the same pose.
    std::vector<std::pair<int, int>> runs;
    auto poseAt = [cel](int frame) { return cel->motion().isEmpty() ? doc::CelPose{} : cel->motion().poseAt(frame); };
    for (int f = first; f < last; ++f) {
        if (!cel->drawingAt(f))
            continue;
        if (!runs.empty() && runs.back().first + runs.back().second == f && cel->drawingAt(f - 1) == cel->drawingAt(f) &&
            poseAt(f - 1) == poseAt(f))
            ++runs.back().second;
        else
            runs.emplace_back(f, 1);
    }
    if (runs.empty())
        return fail(tr("The cel shows nothing."));

    const doc::Document &d = *m_document;
    const QString name = tr("%1 (raster)").arg(QString::fromStdString(cel->properties().name));
    return importRuns(
        runs,
        [&d, celId, &runs](int i) {
            canvas::FrameRenderer renderer;
            renderer.setPaperVisible(false);
            renderer.setGuidesVisible(false);
            renderer.setResampleFilter(canvas::ResampleFilter::Bicubic); // moved by its keys: read with care
            renderer.setOnlyCel(celId);
            renderer.renderAll(d, runs[std::size_t(i)].first);
            return renderer.image();
        },
        name, celId, {});
}

void DocumentController::copyCel(doc::CelId celId)
{
    if (const doc::Cel *cel = m_document->cel(celId))
        m_copiedCel = std::make_shared<const doc::Cel>(*cel); // its pixels shared, copied only when written
}

bool DocumentController::deleteCel(doc::CelId celId)
{
    if (!m_document->cel(celId) || m_document->celCount() <= 1)
        return false;
    execute(std::make_unique<doc::RemoveCelCommand>(celId));
    return true;
}

bool DocumentController::cutCel(doc::CelId celId)
{
    if (!m_document->cel(celId) || m_document->celCount() <= 1)
        return false;
    copyCel(celId);
    return deleteCel(celId);
}

doc::CelId DocumentController::pasteCel()
{
    if (!m_copiedCel)
        return doc::kNoId;
    doc::Document &d = *m_document;
    doc::CelProperties properties = m_copiedCel->properties();
    if (properties.parent != doc::kNoId && !d.cel(properties.parent))
        properties.parent = doc::kNoId; // the trajectory it followed is gone
    auto cel = std::make_unique<doc::Cel>(d.allocateCelId(), properties);
    const doc::CelId id = cel->id();
    // New ids for its drawings and layers (a layer the same through the drawings stays so).
    std::map<doc::DrawingId, doc::DrawingId> drawings;
    std::map<doc::LayerId, doc::LayerId> layers;
    for (const auto &[old, drawing] : m_copiedCel->drawings()) {
        doc::Drawing copy = drawing;
        copy.id = d.allocateDrawingId();
        drawings[old] = copy.id;
        for (doc::Layer &layer : copy.layers) {
            auto [it, fresh] = layers.try_emplace(layer.id, doc::kNoId);
            if (fresh)
                it->second = d.allocateLayerId();
            layer.id = it->second;
        }
        cel->addDrawing(std::move(copy));
    }
    for (const doc::Exposure &e : m_copiedCel->timing().exposures())
        if (const auto it = drawings.find(e.drawing); it != drawings.end())
            cel->timing().setExposure(e.start, e.length, it->second);
    cel->setMotion(m_copiedCel->motion());
    cel->setDeformer(m_copiedCel->deformer()); // a deformation cel's bones or nodes
    const int below = d.indexOf(m_currentCel);
    execute(std::make_unique<doc::AddCelCommand>(std::move(cel), below < 0 ? d.celCount() : below + 1));
    setCurrentCel(id);
    return id;
}

std::optional<DocumentController::PerspectiveLayerInfo> DocumentController::perspectiveLayerById(doc::LayerId id) const
{
    if (id == doc::kNoId)
        return std::nullopt;
    for (int i = 0; i < m_document->celCount(); ++i) {
        const doc::Cel &cel = m_document->celAt(i);
        for (const auto &[drawingId, drawing] : cel.drawings())
            for (const doc::Layer &layer : drawing.layers)
                if (layer.id == id && layer.kind == doc::LayerKind::Perspective)
                    return PerspectiveLayerInfo{{cel.id(), drawingId, layer.id}, layer.properties.perspective};
    }
    return std::nullopt;
}

const doc::Drawing *DocumentController::currentDrawing() const
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    return cel ? cel->drawing(shownDrawingId(*cel)) : nullptr;
}

doc::DrawingId DocumentController::currentDrawingId() const
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    return cel ? shownDrawingId(*cel) : doc::kNoId;
}

doc::DrawingId DocumentController::shownDrawingId(const doc::Cel &cel) const
{
    if (m_mouthEdit != doc::kNoId && cel.id() == m_currentCel && cel.drawing(m_mouthEdit))
        return m_mouthEdit;
    return cel.timing().drawingAt(m_currentFrame);
}

doc::Layer *DocumentController::workLayer(bool changesIt, const QString &nothing, QString *reason, bool vectorOk)
{
    auto fail = [reason](const QString &why) -> doc::Layer * {
        if (reason)
            *reason = why;
        return nullptr;
    };
    doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    const QString celName = QString::fromStdString(cel->properties().name);
    if (!cel->properties().visible)
        return fail(tr("Cel “%1” is hidden").arg(celName));
    if (changesIt && cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(celName));
    doc::Layer *layer = m_document->layer(m_currentCel, currentDrawingId(), m_currentLayer);
    if (!layer)
        return fail(nothing);
    const QString name = QString::fromStdString(layer->properties.name);
    if (!layer->properties.visible)
        return fail(tr("Layer “%1” is hidden").arg(name));
    if (changesIt && layer->properties.locked)
        return fail(tr("Layer “%1” is locked").arg(name));
    if (layer->kind == doc::LayerKind::Perspective)
        return fail(tr("“%1” is a perspective layer: it has no pixels to work on").arg(name));
    if (layer->kind == doc::LayerKind::Vector && !vectorOk)
        return fail(tr("“%1” is a vector layer: its strokes can't be cut by a selection, use the eraser").arg(name));
    return layer;
}

bool DocumentController::currentLayerIsVector() const
{
    const doc::Layer *layer = m_document->layer(m_currentCel, currentDrawingId(), m_currentLayer);
    return layer && layer->kind == doc::LayerKind::Vector && !(layer->mask && m_maskLayer == layer->id);
}

bool DocumentController::canClearCurrentLayer() const
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel || !cel->properties().visible || cel->properties().locked)
        return false;
    const doc::Layer *layer = m_document->layer(m_currentCel, currentDrawingId(), m_currentLayer);
    if (!layer || !layer->properties.visible || layer->properties.locked)
        return false;
    // What there is to clear: the mask when it is what is drawn on, the layer's pixels otherwise.
    if (layer->mask && m_maskLayer == layer->id)
        return !layer->mask->isEmpty();
    return !layer->image.isEmpty() || !layer->strokes.empty();
}

bool DocumentController::clearCurrentLayer(QString *reason)
{
    interruptStroke(); // an open edit is applied first, then cleared with the rest
    const QString nothing = tr("Nothing to clear on this frame");
    doc::Layer *layer = workLayer(true, nothing, reason, true); // a vector layer loses its strokes too
    if (!layer)
        return false;
    // With the mask being drawn on, it is the mask that is cleared (nothing hidden again), not the layer.
    const bool onMask = layer->mask && m_maskLayer == layer->id;
    doc::TiledImage &image = onMask ? *layer->mask : layer->image;
    const bool strokesToo = layer->kind == doc::LayerKind::Vector && !onMask;
    if (image.isEmpty() && !(strokesToo && !layer->strokes.empty())) {
        if (reason)
            *reason = nothing;
        return false;
    }

    // The layer stays (and its drawing, exposures too), only its pixels go.
    const doc::DrawingId drawingId = currentDrawingId();
    const doc::TiledImage before = image;
    const QRect area = boundsOf(before.tileCoords());
    image = doc::TiledImage();
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (auto paint = doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, image, onMask))
        steps.push_back(std::move(paint));
    if (strokesToo && !layer->strokes.empty()) {
        auto strokes = std::make_unique<doc::SetVectorStrokesCommand>(m_currentCel, drawingId, m_currentLayer,
                                                                      layer->strokes, std::vector<doc::VectorStroke>());
        strokes->apply(*m_document);
        steps.push_back(std::move(strokes));
    }
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearLayer, std::move(steps)));
    emit pixelsChanged(m_currentCel, drawingId, area);
    emit historyChanged();
    return true;
}

void DocumentController::setSelection(const QPainterPath &area)
{
    QPainterPath clipped = area;
    if (clipped.boundingRect().isEmpty())
        clipped = QPainterPath();
    if (clipped == m_selection)
        return;
    if (m_paint) { // recorded with the paint, by endPaint()
        assignSelection(clipped);
        return;
    }
    QPainterPath before = m_selection;
    assignSelection(clipped);
    m_history.pushApplied(std::make_unique<SelectionCommand>(
        doc::CommandKind::Select, [this](const QPainterPath &a) { assignSelection(a); }, std::move(before),
        m_selection));
    emit historyChanged();
}

void DocumentController::choosePathKeys(doc::CelId cel, const std::set<int> &frames, int clicked)
{
    if (frames.empty())
        cel = m_pathKeysCel; // none chosen: of whichever trajectory
    if (cel == m_pathKeysCel && frames == m_pathKeys) {
        if (clicked >= 0)
            emit pathKeysChosen(cel, std::vector<int>(frames.begin(), frames.end()), clicked); // another one clicked
        return;
    }
    PathKeysCommand::Keys before{m_pathKeysCel, m_pathKeys};
    assignPathKeys(cel, frames, clicked);
    m_history.pushApplied(std::make_unique<PathKeysCommand>(
        [this](doc::CelId c, const std::set<int> &f) { assignPathKeys(c, f, -1); }, std::move(before),
        PathKeysCommand::Keys{m_pathKeysCel, m_pathKeys}));
    emit historyChanged();
}

void DocumentController::recordToolChoice(std::function<void()> undo, std::function<void()> redo,
                                          std::weak_ptr<void> alive)
{
    m_history.pushApplied(std::make_unique<ToolChoiceCommand>(std::move(undo), std::move(redo), std::move(alive),
                                                              [this] { emit toolChoiceRestored(); }));
    emit historyChanged();
}

void DocumentController::assignPathKeys(doc::CelId cel, const std::set<int> &frames, int clicked)
{
    m_pathKeysCel = cel;
    m_pathKeys = frames;
    emit pathKeysChosen(cel, std::vector<int>(frames.begin(), frames.end()), clicked);
}

void DocumentController::assignSelection(const QPainterPath &area)
{
    m_selection = area;
    emit selectionChanged();
}

void DocumentController::selectAll()
{
    QPainterPath all;
    all.addRect(0, 0, m_document->width(), m_document->height());
    setSelection(all);
}

void DocumentController::clearSelection()
{
    setSelection({});
}

void DocumentController::invertSelection()
{
    QPainterPath page;
    page.addRect(0, 0, m_document->width(), m_document->height());
    setSelection(page.subtracted(m_selection));
}

bool DocumentController::growSelection(int pixels, QString *reason)
{
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    if (pixels == 0)
        return true;
    const QPainterPath area = canvas::grownSelection(m_selection, pixels);
    if (area.boundingRect().width() < 1 || area.boundingRect().height() < 1) {
        if (reason)
            *reason = tr("The selection is too small to shrink that much");
        return false;
    }
    setSelection(area);
    return true;
}

bool DocumentController::fillSelection(const QColor &color, QString *reason)
{
    interruptStroke();
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    const auto target = beginPaint(true, reason);
    if (!target)
        return false;
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    const QColor tinted = target->tint(color);
    const int alpha = tinted.alpha();
    const doc::Pixel pixel = doc::Pixel(alpha) << 24 | doc::Pixel(tinted.red() * alpha / 255) << 16
                             | doc::Pixel(tinted.green() * alpha / 255) << 8 | doc::Pixel(tinted.blue() * alpha / 255);
    const doc::TiledImage before = *target->image;
    canvas::fillByMask(*target->image, mask, pixel, target->mask);
    notifyPainted(mask.bounds);
    endPaint(before, doc::CommandKind::Fill);
    return true;
}

bool DocumentController::eraseBySelection(bool insideIt, QString *reason)
{
    interruptStroke();
    const QString nothing = tr("Nothing in the selection on this frame");
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    doc::Layer *layer = workLayer(true, nothing, reason, true);
    if (!layer)
        return false;
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    doc::TiledImage inside, outside;
    canvas::splitByMask(layer->image, mask, inside, outside);
    if (!insideIt)
        std::swap(inside, outside); // `inside` is what goes, `outside` what stays
    // A vector layer's strokes are cut along the outline: the other side stays.
    const bool vector = layer->kind == doc::LayerKind::Vector;
    std::vector<doc::VectorStroke> strokesKept;
    bool strokesErased = false;
    if (vector)
        for (const doc::VectorStroke &stroke : layer->strokes) {
            canvas::SplitStroke split = canvas::splitStrokeByMask(stroke, mask, m_selection);
            const auto &erased = insideIt ? split.inside : split.outside;
            const auto &kept = insideIt ? split.outside : split.inside;
            strokesErased |= !erased.empty();
            strokesKept.insert(strokesKept.end(), kept.begin(), kept.end());
        }
    if (inside.isEmpty() && !strokesErased) {
        if (reason)
            *reason = insideIt ? nothing : tr("Nothing outside the selection on this frame");
        return false;
    }
    const doc::DrawingId drawingId = currentDrawingId();
    const doc::TiledImage before = layer->image;
    const QRect area = insideIt ? mask.bounds : boundsOf(before.tileCoords());
    layer->image = std::move(outside);
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (auto paint = doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, layer->image))
        steps.push_back(std::move(paint));
    if (vector) {
        auto strokes = std::make_unique<doc::SetVectorStrokesCommand>(m_currentCel, drawingId, m_currentLayer,
                                                                      layer->strokes, std::move(strokesKept));
        strokes->apply(*m_document);
        steps.push_back(std::move(strokes));
    }
    m_history.pushApplied(std::make_unique<doc::GroupCommand>(doc::CommandKind::ClearLayer, std::move(steps)));
    emit pixelsChanged(m_currentCel, drawingId, area);
    emit historyChanged();
    return true;
}

bool DocumentController::renameDrawing(doc::CelId celId, doc::DrawingId drawingId, const QString &name, QString *reason)
{
    const doc::Cel *cel = m_document->cel(celId);
    const doc::Drawing *drawing = cel ? cel->drawing(drawingId) : nullptr;
    const std::string wanted = name.trimmed().toStdString();
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    if (!drawing)
        return fail(tr("No such frame"));
    if (wanted.empty())
        return fail(tr("A frame needs a name"));
    if (wanted == drawing->name)
        return true;
    for (const auto &[id, other] : cel->drawings())
        if (id != drawingId && other.name == wanted)
            return fail(tr("Another frame of “%1” is named “%2”")
                            .arg(QString::fromStdString(cel->properties().name), QString::fromStdString(wanted)));
    interruptStroke();
    execute(std::make_unique<doc::RenameDrawingCommand>(celId, drawingId, drawing->name, wanted));
    return true;
}

bool DocumentController::setMouthChart(doc::CelId celId, bool on, QString *reason)
{
    const doc::Cel *cel = m_document->cel(celId);
    if (!cel || cel->properties().drawsNothing()) {
        if (reason)
            *reason = !cel ? tr("No such cel")
                      : cel->properties().trajectory ? tr("A trajectory has no drawings: it cannot be a mouth chart")
                                                     : tr("A deformer has no drawings: it cannot be a mouth chart");
        return false;
    }
    if (cel->properties().mouthChart == on)
        return true;
    interruptStroke();
    if (!on)
        stopEditingMouth();
    std::vector<std::unique_ptr<doc::Command>> steps;
    doc::CelProperties properties = cel->properties();
    properties.mouthChart = on;
    steps.push_back(doc::makeSetCelProperties(*m_document, celId, properties));
    if (on)
        for (const char shape : doc::kMouthShapes) {
            if (doc::mouthDrawing(*cel, shape) != doc::kNoId)
                continue;
            doc::Drawing drawing;
            drawing.id = m_document->allocateDrawingId();
            drawing.name = std::string(1, shape);
            doc::Layer layer;
            layer.id = m_document->allocateLayerId();
            layer.properties.name = tr("Mouth").toStdString();
            drawing.layers.push_back(std::move(layer));
            steps.push_back(std::make_unique<doc::AddDrawingCommand>(celId, std::move(drawing)));
        }
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MouthChart, std::move(steps)));
    return true;
}

bool DocumentController::applyMouthCues(const std::vector<doc::MouthCue> &cues, int minFrames, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel || !cel->properties().mouthChart)
        return fail(tr("The current cel is not a mouth chart"));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));
    const int offset = m_document->audio() ? m_document->audio()->offset : 0;
    doc::Timing after = doc::lipSyncTiming(*cel, cues, m_document->frameRate().fps(), offset, minFrames,
                                           m_document->frameCount());
    if (after == cel->timing()) {
        // Nothing changes: either the sound gave no mouth within the scene, or the mouths are those it says already
        // (lip synced before), which is no failure.
        const double fps = m_document->frameRate().fps();
        const bool reaches = std::any_of(cues.begin(), cues.end(), [&](const doc::MouthCue &cue) {
            return doc::mouthDrawing(*cel, cue.shape) != doc::kNoId
                   && offset + std::lround(cue.end * fps) > 0
                   && offset + std::lround(cue.start * fps) < m_document->frameCount();
        });
        return reaches || fail(tr("No mouth to put: the sound gave nothing within the scene"));
    }
    interruptStroke();
    stopEditingMouth();
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::make_unique<doc::SetTimingCommand>(m_currentCel, cel->timing(), std::move(after)));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::LipSync, std::move(steps)));
    resolveCurrentLayer();
    return true;
}

bool DocumentController::editMouthDrawing(doc::DrawingId drawing)
{
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel || !cel->properties().mouthChart || !cel->drawing(drawing))
        return false;
    if (m_mouthEdit == drawing)
        return true;
    interruptStroke();
    m_mouthEdit = drawing;
    resolveCurrentLayer();
    // A mouth chosen to draw: its layer is the selection, not the cel chosen as a whole before (which would stop the
    // brushes, with the Layers panel perhaps hidden behind the Mouth chart).
    selectCamera(false);
    if (!m_layerChosen) {
        setLayerChosen(true);
        emit layerChosenByDrawing();
    }
    emit mouthEditChanged();
    emit pixelsChanged(m_currentCel, doc::kNoId, kWholeCanvas); // the canvas shows another drawing
    return true;
}

void DocumentController::stopEditingMouth()
{
    if (m_mouthEdit == doc::kNoId)
        return;
    interruptStroke();
    m_mouthEdit = doc::kNoId;
    resolveCurrentLayer();
    emit mouthEditChanged();
    emit pixelsChanged(m_currentCel, doc::kNoId, kWholeCanvas);
}

bool DocumentController::setMouthShape(char shape, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    const doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    if (!cel->properties().mouthChart)
        return fail(tr("“%1” is not a mouth chart").arg(QString::fromStdString(cel->properties().name)));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));
    stopEditingMouth(); // the timeline is what is seen again
    const doc::DrawingId drawing = doc::mouthDrawing(*cel, shape);
    if (drawing == doc::kNoId)
        return fail(tr("“%1” has no frame named %2").arg(QString::fromStdString(cel->properties().name)).arg(QChar(shape)));
    // From here to where the cel showed something else next: the hold the frame is in, or the gap up to the next one.
    const int frame = m_currentFrame;
    int end = std::max(frame + 1, m_document->frameCount());
    if (const doc::Exposure *e = cel->timing().exposureAt(frame)) {
        end = e->end();
    } else {
        for (const doc::Exposure &next : cel->timing().exposures())
            if (next.start > frame) {
                end = next.start;
                break;
            }
    }
    if (cel->timing().drawingAt(frame) == drawing)
        return true; // that shape already
    interruptStroke();
    auto command = doc::makeSetExposure(*m_document, m_currentCel, frame, end - frame, drawing);
    if (!command)
        return true;
    std::vector<std::unique_ptr<doc::Command>> steps;
    steps.push_back(std::move(command));
    execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::SetMouthShape, std::move(steps)));
    resolveCurrentLayer(); // another drawing shown: its layer
    return true;
}

bool DocumentController::beginColorAdjust(QString *reason)
{
    interruptStroke();
    const auto target = beginPaint(false, reason, true, true);
    if (!target)
        return false;
    if (target->mask) {
        cancelPaint();
        if (reason)
            *reason = tr("A mask has no colors to change");
        return false;
    }
    ColorAdjust a;
    a.target = *target;
    a.before = *target->image;
    a.selection = hasSelection();
    std::optional<canvas::SelectionMask> mask;
    if (a.selection) {
        mask = canvas::SelectionMask::fromPath(m_selection);
        canvas::splitByMask(a.before, *mask, a.inside, a.outside);
    } else {
        a.inside = a.before;
    }
    if (target->vector) {
        const doc::Layer *layer = m_document->layer(target->cel, target->drawing, target->layer);
        a.strokesBefore = layer->strokes;
        for (const doc::VectorStroke &stroke : layer->strokes) {
            if (!mask) {
                a.strokesInside.push_back(stroke);
                continue;
            }
            canvas::SplitStroke split = canvas::splitStrokeByMask(stroke, *mask);
            a.strokesKept.insert(a.strokesKept.end(), split.outside.begin(), split.outside.end());
            a.strokesInside.insert(a.strokesInside.end(), split.inside.begin(), split.inside.end());
        }
    }
    if (a.inside.isEmpty() && a.strokesInside.empty()) {
        cancelPaint();
        if (reason)
            *reason = a.selection ? tr("Nothing in the selection on this layer") : tr("Nothing on this layer");
        return false;
    }
    m_colorAdjust = std::move(a);
    return true;
}

void DocumentController::previewColorAdjust(int hue, int saturation, int value)
{
    if (!m_colorAdjust)
        return;
    ColorAdjust &a = *m_colorAdjust;
    a.hue = hue;
    a.saturation = saturation;
    a.value = value;
    doc::TiledImage shown = canvas::shiftedImage(a.inside, {hue, saturation, value});
    if (a.selection) {
        doc::TiledImage all = a.outside;
        canvas::compositeOver(all, shown);
        shown = std::move(all);
    }
    *a.target.image = std::move(shown);
    notifyPainted(kWholeCanvas);
}

void DocumentController::finishColorAdjust(bool apply)
{
    if (!m_colorAdjust)
        return;
    ColorAdjust a = std::move(*m_colorAdjust);
    m_colorAdjust.reset();
    const canvas::HsvShift shift{a.hue, a.saturation, a.value};
    if (!apply || shift.isNone() || !m_paint) {
        if (m_paint) {
            *a.target.image = a.before;
            cancelPaint();
        }
        notifyPainted(kWholeCanvas);
        emit colorAdjustFinished();
        return;
    }
    if (a.target.vector) {
        std::vector<doc::VectorStroke> after = a.strokesKept;
        for (doc::VectorStroke stroke : a.strokesInside) {
            stroke.color = canvas::shiftedColor(stroke.color, shift);
            stroke.secondaryColor = canvas::shiftedColor(stroke.secondaryColor, shift);
            after.push_back(std::move(stroke));
        }
        attachStrokeChange(a.target, std::move(a.strokesBefore), std::move(after));
    }
    notifyPainted(kWholeCanvas);
    endPaint(a.before, doc::CommandKind::AdjustColor);
    emit colorAdjustFinished();
}

bool DocumentController::flipSelection(bool horizontal, QString *reason)
{
    interruptStroke();
    const QString nothing = tr("Nothing in the selection on this frame");
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    // The layers the tools work on: the one current, or every one chosen in the Layers panel (those of a chosen cel or
    // frame are all chosen together), the hidden and locked ones left out.
    const std::vector<PaintTarget> targets = beginPaintLayers(reason, true);
    if (targets.empty())
        return false;
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    const QRect box = mask.bounds;
    // The outline is mirrored across the same line (pixel x goes to left + right - x), and goes into the same undo step.
    const QTransform across = horizontal ? QTransform(-1, 0, 0, 1, 2.0 * box.left() + box.width(), 0)
                                         : QTransform(1, 0, 0, -1, 0, 2.0 * box.top() + box.height());
    std::vector<doc::TiledImage> before;
    bool anything = false;
    for (const PaintTarget &target : targets) {
        before.push_back(*target.image);
        doc::TiledImage inside, outside;
        canvas::splitByMask(*target.image, mask, inside, outside);
        // The strokes of a vector layer: those inside the outline are mirrored, the rest stays.
        bool strokesInside = false;
        if (target.vector) {
            const doc::Layer *vectorLayer = m_document->layer(target.cel, target.drawing, target.layer);
            std::vector<doc::VectorStroke> kept, flipped;
            for (const doc::VectorStroke &stroke : vectorLayer->strokes) {
                canvas::SplitStroke split = canvas::splitStrokeByMask(stroke, mask, m_selection);
                kept.insert(kept.end(), split.outside.begin(), split.outside.end());
                for (const doc::VectorStroke &piece : split.inside)
                    flipped.push_back(canvas::transformedStroke(piece, across));
            }
            strokesInside = !flipped.empty();
            if (strokesInside) {
                kept.insert(kept.end(), flipped.begin(), flipped.end());
                attachStrokeChange(target, vectorLayer->strokes, std::move(kept));
            }
        }
        if (inside.isEmpty() && !strokesInside)
            continue;
        anything = true;
        if (inside.isEmpty())
            continue;
        // The part under the selection, mirrored in the box it was in, and laid over what stays.
        QImage area(box.size(), QImage::Format_ARGB32_Premultiplied);
        inside.read(box.x(), box.y(), box.width(), box.height(), reinterpret_cast<doc::Pixel *>(area.bits()),
                    area.bytesPerLine() / 4);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        const QImage mirrored = area.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
        const QImage mirrored = area.mirrored(horizontal, !horizontal); // flipped() is Qt 6.9's
#endif
        doc::TiledImage piece;
        piece.write(box.x(), box.y(), box.width(), box.height(),
                    reinterpret_cast<const doc::Pixel *>(mirrored.constBits()), mirrored.bytesPerLine() / 4);
        canvas::compositeOver(outside, piece);
        *target.image = std::move(outside);
    }
    if (!anything) {
        cancelPaint();
        if (reason)
            *reason = nothing;
        return false;
    }
    setSelection(across.map(m_selection));
    notifyPainted(box);
    endPaint(before, doc::CommandKind::Flip);
    return true;
}

bool DocumentController::cutSelectionToLayer(QString *reason)
{
    return selectionToLayer(true, reason);
}

bool DocumentController::copySelectionToLayer(QString *reason)
{
    return selectionToLayer(false, reason);
}

bool DocumentController::selectionToLayer(bool cut, QString *reason)
{
    interruptStroke();
    const QString nothing = tr("Nothing in the selection on this frame");
    if (!hasSelection()) {
        if (reason)
            *reason = tr("Nothing is selected");
        return false;
    }
    doc::Layer *layer = workLayer(cut, nothing, reason, true);
    if (!layer)
        return false;
    doc::Document &d = *m_document;
    const doc::DrawingId drawingId = currentDrawingId();
    const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
    doc::TiledImage inside, outside;
    canvas::splitByMask(layer->image, mask, inside, outside);
    if (inside.isEmpty()) {
        if (reason)
            *reason = nothing;
        return false;
    }

    // A vector layer's strokes are cut along the outline too: the new layer is a vector layer with the parts inside.
    const bool vector = layer->kind == doc::LayerKind::Vector;
    std::vector<doc::VectorStroke> strokesInside, strokesOutside;
    if (vector)
        for (const doc::VectorStroke &stroke : layer->strokes) {
            canvas::SplitStroke split = canvas::splitStrokeByMask(stroke, mask, m_selection);
            strokesInside.insert(strokesInside.end(), split.inside.begin(), split.inside.end());
            strokesOutside.insert(strokesOutside.end(), split.outside.begin(), split.outside.end());
        }

    // The source loses its part (cut), then a new layer above it in the same
    // drawing gets the part: applied now, recorded together.
    std::vector<std::unique_ptr<doc::Command>> steps;
    if (cut) {
        const doc::TiledImage before = layer->image;
        layer->image = std::move(outside);
        steps.push_back(doc::PaintCommand::capture(m_currentCel, drawingId, m_currentLayer, before, layer->image));
        if (vector) {
            auto strokes = std::make_unique<doc::SetVectorStrokesCommand>(m_currentCel, drawingId, m_currentLayer,
                                                                          layer->strokes, strokesOutside);
            strokes->apply(*m_document);
            steps.push_back(std::move(strokes));
        }
    }
    const QString name = QString::fromStdString(layer->properties.name);
    auto added = std::make_unique<doc::AddLayerCommand>(
        d, m_currentCel, drawingId, currentDrawing()->indexOf(m_currentLayer) + 1,
        doc::LayerProperties{(cut ? tr("%1 (cut)") : tr("%1 (copy)")).arg(name).toStdString()}, std::move(inside),
        vector ? doc::LayerKind::Vector : doc::LayerKind::Raster);
    const doc::LayerId layerId = added->layerId();
    publish(added->apply(d)); // `layer` may have moved: not used from here on
    steps.push_back(std::move(added));
    if (vector) {
        auto strokes = std::make_unique<doc::SetVectorStrokesCommand>(m_currentCel, drawingId, layerId,
                                                                      std::vector<doc::VectorStroke>(), strokesInside);
        strokes->apply(d);
        steps.push_back(std::move(strokes));
    }
    emit pixelsChanged(m_currentCel, drawingId, mask.bounds);

    m_history.pushApplied(std::make_unique<doc::GroupCommand>(
        cut ? doc::CommandKind::CutToLayer : doc::CommandKind::CopyToLayer, std::move(steps)));
    emit historyChanged();
    setCurrentLayer(layerId);
    return true;
}

QImage DocumentController::copyPixels(QPoint *origin, QString *reason)
{
    auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return QImage();
    };
    interruptStroke(); // an open edit is part of what is copied
    const QString nothing = tr("Nothing to copy on this frame");
    QString why;
    const doc::Layer *layer = workLayer(false, nothing, &why, true);
    if (!layer)
        return fail(why);
    if (layer->image.isEmpty())
        return fail(nothing);

    doc::TiledImage copied = layer->image;
    if (hasSelection()) {
        doc::TiledImage outside;
        canvas::splitByMask(layer->image, canvas::SelectionMask::fromPath(m_selection), copied, outside);
    }
    const QRect tiles = boundsOf(copied.tileCoords());
    QRect crop;
    QImage area;
    if (!tiles.isEmpty()) {
        area = QImage(tiles.size(), QImage::Format_ARGB32_Premultiplied);
        copied.read(tiles.x(), tiles.y(), tiles.width(), tiles.height(), reinterpret_cast<doc::Pixel *>(area.bits()),
                    area.bytesPerLine() / 4);
        crop = opaqueBounds(area);
    }
    if (crop.isEmpty())
        return fail(tr("Nothing in the selection on this frame"));
    if (origin)
        *origin = tiles.topLeft() + crop.topLeft();
    // From a vector layer the strokes are kept too (inside the selection: cut along its outline), to paste as strokes.
    m_copiedStrokes.clear();
    if (layer->kind == doc::LayerKind::Vector) {
        if (!hasSelection()) {
            m_copiedStrokes = layer->strokes;
        } else {
            const canvas::SelectionMask mask = canvas::SelectionMask::fromPath(m_selection);
            for (const doc::VectorStroke &stroke : layer->strokes) {
                canvas::SplitStroke cut = canvas::splitStrokeByMask(stroke, mask, m_selection);
                m_copiedStrokes.insert(m_copiedStrokes.end(), cut.inside.begin(), cut.inside.end());
            }
        }
    }
    return area.copy(crop);
}

bool DocumentController::pasteImage(const QImage &image, std::optional<QPoint> origin, QString *reason)
{
    if (image.isNull()) {
        if (reason)
            *reason = tr("Nothing to paste");
        return false;
    }
    interruptStroke();
    doc::Document &d = *m_document;
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QPoint at = origin.value_or(QPoint((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2));
    // The whole image, even what lies off the page: the layer keeps it and the selection takes all of it, so it
    // can be moved (the page shows only its own part).
    const QRect area(at, pixels.size());
    if ((area & QRect(0, 0, d.width(), d.height())).isEmpty()) {
        if (reason)
            *reason = tr("The pasted image is outside the sheet");
        return false;
    }

    // Over what is drawn there, as a paint step: the drawing it may need, the
    // pixels and the selection around them are recorded together.
    const auto target = beginPaint(true, reason);
    if (!target)
        return false;
    const doc::TiledImage before = *target->image;
    doc::TiledImage pasted;
    pasted.write(area.x(), area.y(), area.width(), area.height(),
                 reinterpret_cast<const doc::Pixel *>(pixels.constBits()), pixels.bytesPerLine() / 4);
    canvas::compositeOver(*target->image, pasted);
    notifyPainted(area & QRect(0, 0, d.width(), d.height()));
    QPainterPath selected;
    selected.addRect(area);
    setSelection(selected);
    endPaint(before, doc::CommandKind::Paste);
    return true;
}

bool DocumentController::beginFloatingPaste(const QImage &image, std::optional<QPoint> origin,
                                            const std::vector<doc::VectorStroke> *strokes, QString *reason)
{
    if (image.isNull()) {
        if (reason)
            *reason = tr("Nothing to paste");
        return false;
    }
    doc::Document &d = *m_document;
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QPoint at = origin.value_or(QPoint((d.width() - pixels.width()) / 2, (d.height() - pixels.height()) / 2));
    if ((QRect(at, pixels.size()) & QRect(0, 0, d.width(), d.height())).isEmpty()) {
        if (reason)
            *reason = tr("The pasted image is outside the sheet");
        return false;
    }
    // The layer must take it: strokes go to a vector layer, pixels to a raster one.
    const bool wantsStrokes = strokes && !strokes->empty();
    QString why;
    // (An empty frame has no layer yet: the paste makes the drawing, as pasteImage() does.)
    const doc::Layer *layer = workLayer(true, tr("Nothing to paste"), &why, true);
    if (!layer && currentDrawingId() != doc::kNoId) {
        if (reason)
            *reason = why;
        return false;
    }
    if (!layer && m_document->cel(m_currentCel)
        && (!m_document->cel(m_currentCel)->properties().visible || m_document->cel(m_currentCel)->properties().locked)) {
        if (reason)
            *reason = why;
        return false;
    }
    if (layer && wantsStrokes != (layer->kind == doc::LayerKind::Vector)) {
        if (reason)
            *reason = wantsStrokes ? tr("These strokes need a vector layer") : tr("Pixels cannot be pasted on a vector layer");
        return false;
    }
    FloatingPaste floating;
    floating.image = std::move(pixels);
    floating.at = at;
    floating.vector = wantsStrokes;
    if (wantsStrokes)
        floating.strokes = *strokes;
    m_floating = std::move(floating);
    emit floatingPasteStarted();
    return true;
}

std::optional<DocumentController::FloatingPaste> DocumentController::takeFloatingPaste()
{
    std::optional<FloatingPaste> taken = std::move(m_floating);
    m_floating.reset();
    return taken;
}

void DocumentController::undo()
{
    interruptStroke();
    if (!m_history.canUndo())
        return;
    publish(m_history.undo(*m_document));
    emit historyChanged();
}

void DocumentController::redo()
{
    interruptStroke();
    if (!m_history.canRedo())
        return;
    publish(m_history.redo(*m_document));
    emit historyChanged();
}

QString DocumentController::commandName(doc::CommandKind kind)
{
    switch (kind) {
    case doc::CommandKind::Paint: return tr("stroke");
    case doc::CommandKind::AddDrawing: return tr("new frame");
    case doc::CommandKind::RemoveDrawing: return tr("delete frame");
    case doc::CommandKind::ChangeTiming: return tr("timing change");
    case doc::CommandKind::AddCel: return tr("new cel");
    case doc::CommandKind::RemoveCel: return tr("delete cel");
    case doc::CommandKind::MoveCel: return tr("move cel");
    case doc::CommandKind::ChangeCelProperties: return tr("cel properties");
    case doc::CommandKind::AddLayer: return tr("new layer");
    case doc::CommandKind::RemoveLayer: return tr("delete layer");
    case doc::CommandKind::MoveLayer: return tr("move layer");
    case doc::CommandKind::ChangeLayerProperties: return tr("layer properties");
    case doc::CommandKind::ImportImage: return tr("import image");
    case doc::CommandKind::ImportSequence: return tr("import image sequence");
    case doc::CommandKind::UpdateSequence: return tr("update of the Blender scene");
    case doc::CommandKind::SetAudio: return tr("audio");
    case doc::CommandKind::ChangeAudio: return tr("audio change");
    case doc::CommandKind::EditCompositing: return tr("compositing");
    case doc::CommandKind::Tween: return tr("tween");
    case doc::CommandKind::EditCamera: return tr("camera");
    case doc::CommandKind::SceneLength: return tr("scene length");
    case doc::CommandKind::EditMarkers: return tr("markers");
    case doc::CommandKind::Flip: return tr("flip");
    case doc::CommandKind::EditMotion: return tr("keyframes");
    case doc::CommandKind::EditDeformer: return tr("bones");
    case doc::CommandKind::EditBlenderLink: return tr("Blender link");
    case doc::CommandKind::AdjustColor: return tr("hue/saturation/value");
    case doc::CommandKind::RenameDrawing: return tr("rename frame");
    case doc::CommandKind::SetMouthShape: return tr("mouth shape");
    case doc::CommandKind::MouthChart: return tr("mouth chart");
    case doc::CommandKind::LipSync: return tr("lip sync");
    case doc::CommandKind::ChangeMask: return tr("mask");
    case doc::CommandKind::ClearLayer: return tr("clear");
    case doc::CommandKind::Move: return tr("move");
    case doc::CommandKind::Select: return tr("selection");
    case doc::CommandKind::CutToLayer: return tr("cut to layer");
    case doc::CommandKind::CopyToLayer: return tr("copy to layer");
    case doc::CommandKind::Fill: return tr("fill");
    case doc::CommandKind::Transform: return tr("transform");
    case doc::CommandKind::Paste: return tr("paste");
    case doc::CommandKind::SplitFrame: return tr("split frame");
    case doc::CommandKind::InsertInbetween: return tr("insert inbetween");
    case doc::CommandKind::MoveFrames: return tr("move frames");
    case doc::CommandKind::PasteFrames: return tr("paste frames");
    case doc::CommandKind::MergeLayers: return tr("merge layers");
    case doc::CommandKind::PasteLayers: return tr("paste layers");
    case doc::CommandKind::Blend: return tr("blend");
    case doc::CommandKind::Liquify: return tr("liquify");
    case doc::CommandKind::Shape: return tr("shape");
    }
    return {};
}

QString DocumentController::undoText() const
{
    const doc::Command *c = m_history.undoCommand();
    return c ? tr("Undo %1").arg(commandName(c->kind())) : tr("Undo");
}

QString DocumentController::redoText() const
{
    const doc::Command *c = m_history.redoCommand();
    return c ? tr("Redo %1").arg(commandName(c->kind())) : tr("Redo");
}

void DocumentController::publish(const doc::ChangeSet &changes)
{
    if (m_mouthEdit != doc::kNoId) { // the mouth drawn, gone with an undo (or its cel no chart any more)
        const doc::Cel *cel = m_document->cel(m_currentCel);
        if (!cel || !cel->properties().mouthChart || !cel->drawing(m_mouthEdit)) {
            m_mouthEdit = doc::kNoId;
            emit mouthEditChanged();
        }
    }
    if (changes.camera) {
        if (m_cameraSelected && !m_document->camera()) {
            m_cameraSelected = false;
            emit cameraSelectionChanged();
        }
        emit cameraChanged();
    }
    if (changes.markers)
        emit markersChanged();
    if (changes.motion)
        emit motionChanged();
    if (changes.structure) {
        ensureValidCurrentCel();
        resolveCurrentLayer();
        emit structureChanged();
    }
    for (const doc::ChangeSet::Pixels &p : changes.pixels)
        emit pixelsChanged(p.cel, p.drawing, boundsOf(p.tiles));
}

void DocumentController::ensureValidCurrentCel()
{
    if (m_document->cel(m_currentCel))
        return;
    // The current cel is gone (removed or undone): pick the top one, if any.
    const doc::CelId fallback = m_document->celCount() > 0
        ? m_document->celAt(m_document->celCount() - 1).id()
        : doc::kNoId;
    m_currentCel = fallback;
    emit currentCelChanged(fallback);
}

void DocumentController::resolveCurrentLayer()
{
    const doc::Drawing *drawing = currentDrawing();
    doc::LayerId layer = doc::kNoId;
    if (drawing && !drawing->layers.empty()) {
        const auto last = m_lastLayerOf.find(currentDrawingId());
        if (last != m_lastLayerOf.end() && drawing->layer(last->second)) {
            layer = last->second; // back on this drawing: the layer worked on in it last
        } else if (drawing->layer(m_currentLayer)) {
            layer = m_currentLayer;
        } else {
            layer = drawing->layers.back().id; // the top one, unless one has the name
            for (auto it = drawing->layers.rbegin(); it != drawing->layers.rend(); ++it)
                if (it->properties.name == m_currentLayerName) {
                    layer = it->id;
                    break;
                }
        }
        m_currentLayerName = drawing->layer(layer)->properties.name; // renamed, perhaps
        m_currentLayerKind = drawing->layer(layer)->kind;
        m_lastLayerOf[currentDrawingId()] = layer;
    }
    if (layer == m_currentLayer)
        return;
    m_currentLayer = layer;
    emit currentLayerChanged(layer);
}

// --- selection -------------------------------------------------------------------

void DocumentController::setCurrentFrame(int frame)
{
    frame = std::max(0, frame);
    if (frame == m_currentFrame)
        return;
    interruptStroke();
    clearTemporaryPose(); // a pose left on a frame that is not a key goes with the frame
    stopEditingMouth();
    m_currentFrame = frame;
    emit currentFrameChanged(frame);
    resolveCurrentLayer();
}

void DocumentController::setCurrentCel(doc::CelId cel)
{
    if (cel == m_currentCel || !m_document->cel(cel))
        return;
    interruptStroke();
    clearTemporaryPose();
    stopEditingMouth();
    m_currentCel = cel;
    emit currentCelChanged(cel);
    resolveCurrentLayer();
}

void DocumentController::setCurrentLayer(doc::LayerId layer)
{
    const doc::Drawing *drawing = currentDrawing();
    if (layer == m_currentLayer || !drawing || !drawing->layer(layer))
        return;
    interruptStroke();
    m_currentLayer = layer;
    m_currentLayerName = drawing->layer(layer)->properties.name;
    m_currentLayerKind = drawing->layer(layer)->kind;
    m_lastLayerOf[currentDrawingId()] = layer;
    emit currentLayerChanged(layer);
}

// --- painting ----------------------------------------------------------------------

std::optional<DocumentController::PaintTarget> DocumentController::beginPaint(bool createIfEmpty, QString *reason,
                                                                              bool forDrawing, bool vectorOk)
{
    auto fail = [reason](const QString &why) -> std::optional<PaintTarget> {
        if (reason)
            *reason = why;
        return std::nullopt;
    };
    interruptStroke(); // a previous stroke left open
    doc::Cel *cel = m_document->cel(m_currentCel);
    if (!cel)
        return fail(tr("No cel selected"));
    if (cel->properties().trajectory)
        return fail(tr("“%1” is a trajectory: nothing is drawn on it").arg(QString::fromStdString(cel->properties().name)));
    if (cel->properties().deformer != doc::DeformerKind::None)
        return fail(tr("“%1” is a deformer: nothing is drawn on it, it bends the cels that follow it")
                        .arg(QString::fromStdString(cel->properties().name)));
    if (!cel->properties().visible)
        return fail(tr("Cel “%1” is hidden").arg(QString::fromStdString(cel->properties().name)));
    if (cel->properties().locked)
        return fail(tr("Cel “%1” is locked").arg(QString::fromStdString(cel->properties().name)));

    if (forDrawing && m_cameraSelected)
        return fail(tr("The camera is selected: choose a layer to draw on"));
    PaintState state;
    doc::DrawingId drawing = shownDrawingId(*cel);
    if (forDrawing && !m_layerChosen && drawing != doc::kNoId)
        return fail(tr("Choose a layer to draw on: a cel or a frame is selected"));
    if (drawing == doc::kNoId) {
        if (!createIfEmpty)
            return fail(QString());
        // Harmony-like: drawing on an empty frame creates a drawing there,
        // its layer named like the one worked on before.
        const std::string layerName = newFrameLayerName();
        doc::NewDrawing created = doc::makeNewDrawing(*m_document, m_currentCel, m_currentFrame, 1, layerName);
        publish(created.command->apply(*m_document)); // the new layer becomes current
        state.createdDrawing = std::move(created.command);
        drawing = created.id;
        if (!m_layerChosen) { // the new frame's layer is the selection now
            m_layerChosen = true;
            emit layerChosenByDrawing();
        }
    }
    doc::Layer *layer = cel->drawing(drawing)->layer(m_currentLayer);
    if (!layer)
        return fail(tr("No layer selected"));
    const QString name = QString::fromStdString(layer->properties.name);
    if (!layer->properties.visible)
        return fail(tr("Layer “%1” is hidden").arg(name));
    if (layer->properties.locked)
        return fail(tr("Layer “%1” is locked").arg(name));
    if (layer->kind == doc::LayerKind::Perspective)
        return fail(tr("“%1” is a perspective layer: it can't be drawn on").arg(name));
    if (layer->kind == doc::LayerKind::Vector && !vectorOk)
        return fail(tr("“%1” is a vector layer: only the brush, the eraser and the bucket work on it so far").arg(name));
    state.target = {m_currentCel, drawing, m_currentLayer, &layer->image};
    state.target.vector = layer->kind == doc::LayerKind::Vector;
    if (layer->mask && m_maskLayer == layer->id) { // the mask is what is drawn on
        state.target.image = &*layer->mask;
        state.target.mask = true;
        state.target.vector = false; // a mask holds pixels only
    }
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return m_paint->target;
}

void DocumentController::setLayerSelection(std::vector<LayerRef> layers)
{
    if (layers.size() == 1 && layers.front().layer == m_currentLayer)
        layers.clear(); // just the current one
    if (layers == m_layerSelection)
        return;
    m_layerSelection = std::move(layers);
    emit layerSelectionChanged();
}

std::vector<DocumentController::PaintTarget> DocumentController::beginPaintLayers(QString *reason, bool vectorOk)
{
    if (m_cameraSelected) { // the camera is what the pen moves: not the layers
        if (reason)
            *reason = tr("The camera is selected: choose a cel or a layer first");
        return {};
    }
    if (m_layerSelection.empty()) {
        const auto target = beginPaint(false, reason, false, vectorOk);
        return target ? std::vector<PaintTarget>{*target} : std::vector<PaintTarget>{};
    }
    interruptStroke(); // a previous stroke left open
    std::vector<PaintTarget> targets;
    for (const LayerRef &ref : m_layerSelection) {
        const doc::Cel *cel = m_document->cel(ref.cel);
        doc::Layer *layer = m_document->layer(ref.cel, ref.drawing, ref.layer);
        if (!layer || !cel->properties().visible || cel->properties().locked || !layer->properties.visible
            || layer->properties.locked || layer->kind == doc::LayerKind::Perspective
            || (layer->kind == doc::LayerKind::Vector && !vectorOk))
            continue;
        PaintTarget target{ref.cel, ref.drawing, ref.layer, &layer->image};
        target.vector = layer->kind == doc::LayerKind::Vector;
        if (layer->mask && m_maskLayer == layer->id) {
            target.image = &*layer->mask;
            target.mask = true;
            target.vector = false;
        }
        if (ref.layer == m_currentLayer)
            targets.insert(targets.begin(), target);
        else
            targets.push_back(target);
    }
    if (targets.empty()) {
        if (reason)
            *reason = tr("The selected layers are hidden or locked");
        return targets;
    }
    PaintState state;
    state.target = targets.front();
    state.others.assign(targets.begin() + 1, targets.end());
    state.selectionBefore = m_selection;
    m_paint = std::move(state);
    return targets;
}

void DocumentController::notifyPainted(const QRect &docRect)
{
    if (!m_paint || docRect.isEmpty())
        return;
    emit pixelsChanged(m_paint->target.cel, m_paint->target.drawing, docRect);
    std::vector<std::pair<doc::CelId, doc::DrawingId>> told{{m_paint->target.cel, m_paint->target.drawing}};
    for (const PaintTarget &t : m_paint->others)
        if (std::find(told.begin(), told.end(), std::pair{t.cel, t.drawing}) == told.end()) {
            told.emplace_back(t.cel, t.drawing);
            emit pixelsChanged(t.cel, t.drawing, docRect);
        }
}

void DocumentController::endPaint(const doc::TiledImage &before, doc::CommandKind kind)
{
    endPaint(std::vector<doc::TiledImage>{before}, kind);
}

void DocumentController::endPaint(const std::vector<doc::TiledImage> &before, doc::CommandKind kind)
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();

    // New drawing + stroke(s) + selection change: one undo step.
    std::vector<std::unique_ptr<doc::Command>> steps;
    std::vector<PaintTarget> targets{state.target};
    targets.insert(targets.end(), state.others.begin(), state.others.end());
    for (std::size_t i = 0; i < targets.size() && i < before.size(); ++i)
        if (auto paint = doc::PaintCommand::capture(targets[i].cel, targets[i].drawing, targets[i].layer, before[i],
                                                    *targets[i].image, targets[i].mask))
            steps.push_back(std::move(paint));
    for (std::unique_ptr<doc::Command> &change : state.strokeChanges) // the strokes drawn again
        steps.push_back(std::move(change));
    if (!steps.empty() && state.target.vector) // the strokes go with the pixels they painted
        for (doc::VectorStroke &stroke : state.strokes) {
            auto add = std::make_unique<doc::AddVectorStrokeCommand>(state.target.cel, state.target.drawing,
                                                                     state.target.layer, std::move(stroke));
            add->apply(*m_document); // the pixels were painted live; the stroke is added now, as recorded
            steps.push_back(std::move(add));
        }
    if (!steps.empty()) {
        if (state.createdDrawing)
            steps.insert(steps.begin(), std::move(state.createdDrawing));
    } else if (state.createdDrawing) { // nothing drawn: the drawing we created is not wanted
        publish(state.createdDrawing->revert(*m_document));
    }
    if (m_selection != state.selectionBefore)
        steps.push_back(std::make_unique<SelectionCommand>(
            doc::CommandKind::Move, [this](const QPainterPath &a) { assignSelection(a); }, state.selectionBefore,
            m_selection));
    if (steps.empty())
        return;
    if (steps.size() == 1 && steps.front()->kind() == kind)
        m_history.pushApplied(std::move(steps.front()));
    else
        m_history.pushApplied(std::make_unique<doc::GroupCommand>(kind, std::move(steps)));
    emit historyChanged();
}

void DocumentController::attachStrokeChange(const PaintTarget &target, std::vector<doc::VectorStroke> before,
                                            std::vector<doc::VectorStroke> after)
{
    if (!m_paint || !target.vector)
        return;
    auto change = std::make_unique<doc::SetVectorStrokesCommand>(target.cel, target.drawing, target.layer,
                                                                 std::move(before), std::move(after));
    change->apply(*m_document); // the pixels are painted already; the strokes follow as recorded
    m_paint->strokeChanges.push_back(std::move(change));
}

void DocumentController::attachStroke(doc::VectorStroke stroke)
{
    if (m_paint && m_paint->target.vector)
        m_paint->strokes.push_back(std::move(stroke));
}

void DocumentController::cancelPaint()
{
    if (!m_paint)
        return;
    PaintState state = std::move(*m_paint);
    m_paint.reset();
    if (state.createdDrawing)
        publish(state.createdDrawing->revert(*m_document));
}

void DocumentController::interruptStroke()
{
    if (m_colorAdjust)
        finishColorAdjust(false);
    emit poseEditClosing(); // a pose edit left open (Transform, Move on a cel with keys) is applied as it is
    if (!m_paint)
        return;
    emit strokeInterrupted(); // the painter restores pixels and calls cancelPaint()
    cancelPaint();            // in case nobody listened
}

} // namespace mothion::document
