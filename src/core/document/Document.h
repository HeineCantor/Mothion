#pragma once

#include "core/document/Audio.h"
#include "core/document/Camera.h"
#include "core/document/Cel.h"
#include "core/document/Command.h"
#include "core/document/Compositing.h"

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace mothion::doc {

// Frames per second as a fraction, so 23.976 (24000/1001) is exact.
struct FrameRate
{
    int numerator = 24;
    int denominator = 1;

    double fps() const { return double(numerator) / denominator; }
    bool operator==(const FrameRate &) const = default;
};

// The whole project: canvas size, frame rate, scene length and the cel
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

    // The audio track, if the project has one (see AudioTrack).
    const std::optional<AudioTrack> &audio() const { return m_audio; }
    void setAudio(std::optional<AudioTrack> track) { m_audio = std::move(track); }

    // The camera, if the project has one (see Camera): what the picture of each frame is cut from the sheet.
    const std::optional<Camera> &camera() const { return m_camera; }
    void setCamera(std::optional<Camera> camera) { m_camera = std::move(camera); }

    // Markers: a name on a frame of the timeline (frame -> name; the name may be empty), to find one's way in a long
    // scene. They are not tied to a cel: they stay where they are as frames are added or taken away.
    // The scene's first frame (0-based; the Timeline's From): the scene is from it to its last frame, frameCount() - 1
    // (the To). Frames before it stay in the project; they are not part of the scene. Saved with the project, but not an
    // edit: it is not an undo step.
    int sceneStart() const { return m_sceneStart; }
    void setSceneStart(int first) { m_sceneStart = first < 0 ? 0 : first; }
    // The axes of the brushes' symmetry (tools::SymmetrySettings): a center on the page and an angle (degrees). Until
    // they are placed (`placed` false) they are at the middle of the page, upright. Saved with the project, but not an
    // edit: it is not an undo step.
    struct SymmetryAxes
    {
        bool placed = false;
        double x = 0, y = 0;
        double angle = 0;
        bool operator==(const SymmetryAxes &) const = default;
    };
    const SymmetryAxes &symmetryAxes() const { return m_symmetryAxes; }
    void setSymmetryAxes(const SymmetryAxes &axes) { m_symmetryAxes = axes; }

    const std::map<int, std::string> &markers() const { return m_markers; }
    void setMarkers(std::map<int, std::string> markers) { m_markers = std::move(markers); }

    // The compositing network (see GraphData); empty until the editor makes one.
    const GraphData &compositing() const { return m_compositing; }
    void setCompositing(GraphData graph) { m_compositing = std::move(graph); }
    const GraphView &compositingView() const { return m_compositingView; }
    void setCompositingView(const GraphView &view) { m_compositingView = view; }

    // Cels from bottom (index 0) to top.
    int celCount() const { return int(m_cels.size()); }
    Cel &celAt(int index) { return *m_cels[std::size_t(index)]; }
    const Cel &celAt(int index) const { return *m_cels[std::size_t(index)]; }
    int indexOf(CelId id) const; // -1 if absent
    Cel *cel(CelId id);
    const Cel *cel(CelId id) const;

    // Trajectories (see CelProperties::trajectory) that follow one another. `cel`'s parent if it has one that is a trajectory
    // there, other than itself; nullptr otherwise.
    const Cel *trajectoryParent(const Cel &cel) const;
    // The trajectory a cel that is no trajectory follows (its parent, if that is one, or the one the deformation cel that
    // bends it follows); nullptr for none.
    const Cel *followedTrajectory(const Cel &cel) const;
    // The deformation cel that bends a drawn cel (its parent, if that is one); nullptr for none (an inactive one, whose
    // `visible` is off, bends nothing).
    const Cel *followedDeformer(const Cel &cel) const;
    // What the bone (or node) a cel is carried by (see CelProperties::parentPart) does to it on `frame`: it moves and turns
    // as that part did from its rest (identity for a cel carried by none, or by an inactive deformer).
    Matrix2x3 partMatrix(const Cel &cel, int frame) const;
    // A deformation cel's pose on `frame`: its Deform channel's there, or its rest without one.
    std::vector<double> deformPoseAt(const Cel &deformer, int frame) const;
    // What the trajectory a cel follows does to it on `frame` (identity for a cel that follows none), after what the bone
    // it is carried by does (see partMatrix()): on top of the cel's own pose, and relative: only what the trajectory has done since its first key (see trajectoryStartFrame()), so the cel is
    // where it is drawn at that frame and before it.
    // `turned`: with the turn of pathTurn() too.
    Matrix2x3 followMatrix(const Cel &cel, int frame, bool turned = true) const;
    // What "Turn with the path" does to a cel on `frame` (see CelProperties::orientToPath): so many degrees clockwise
    // around the centre of mass of what its drawing shows there, a point of the drawing (before the cel's pose). Nothing
    // for a cel that does not turn, or shows no pixels.
    struct PathTurn
    {
        double degrees = 0;
        double x = 0, y = 0;
    };
    std::optional<PathTurn> pathTurn(const Cel &cel, int frame) const;
    // How far the way a trajectory's middle goes has turned on `frame` since the first key, in degrees clockwise: the
    // angles at its corners are eased over a few frames, and where it stands still it keeps the way it had.
    double pathAngle(CelId trajectory, int frame) const;
    // While held, each drawing keeps the centre of mass it was first asked for, so that what is being drawn does not
    // move under the pen. (Only how the document is shown: not part of it.)
    void holdCentres(bool held) const;
    bool centresHeld() const { return m_heldCentres.has_value(); }
    // The frame of the first key of a trajectory or of the ones it follows; -1 if none has a key.
    int trajectoryStartFrame(CelId trajectory) const;
    // Whether making `parent` the parent of `child` would be a loop or cannot be: the parent is no trajectory or deformation
    // cel; a trajectory's child is not one; a deformation cel's child is a trajectory or a deformation cel.
    bool trajectoryLoop(CelId child, CelId parent) const;
    // What a trajectory does on `frame` to what follows it: its parent's way, then its own pose (an inactive one has none)
    // on top of that (the parent's matrix after its own: a child turns round its parent as the parent turns).
    Matrix2x3 trajectoryMatrix(CelId trajectory, int frame) const;
    // Only what its parents do: where the trajectory's own pose is applied.
    Matrix2x3 trajectoryParentMatrix(CelId trajectory, int frame) const;

    // A time layer seen as a cel of its own, for what follows a parent (followMatrix(), followedDeformer(), partMatrix()):
    // `cel`'s id and properties, with the time layer's parent and part.
    static Cel timeLayerAsCel(const Cel &cel, const TimeLayer &layer);
    // The time layers put right in every cel (see Layer::time): the one copy changed since (by a command, an undo...)
    // copied to the others; a drawing without one given one (where it is in another drawing, or on top); one whose time
    // layer the cel no longer has loses it. What it changed: pixels of the copies changed, structure if a layer came, went
    // or changed otherwise.
    ChangeSet syncTimeLayers();

    // Cels are heap objects, so Cel references stay valid across reordering.
    Cel &insertCel(int index, std::unique_ptr<Cel> cel);
    std::unique_ptr<Cel> takeCel(CelId id);
    void moveCel(CelId id, int newIndex);

    // Fresh ids; never reused within this document.
    CelId allocateCelId() { return m_nextCelId++; }
    DrawingId allocateDrawingId() { return m_nextDrawingId++; }
    LayerId allocateLayerId() { return m_nextLayerId++; }
    // The next ids to allocate, as a saved file records them.
    CelId nextCelId() const { return m_nextCelId; }
    DrawingId nextDrawingId() const { return m_nextDrawingId; }
    LayerId nextLayerId() const { return m_nextLayerId; }
    // For loading: never moves the counters back below an id already in use.
    void restoreIdCounters(CelId nextCel, DrawingId nextDrawing, LayerId nextLayer);

    // The layer `layer` of drawing `drawing` of cel `cel`, or nullptr.
    Layer *layer(CelId cel, DrawingId drawing, LayerId layer);
    const Layer *layer(CelId cel, DrawingId drawing, LayerId layer) const;

private:
    std::optional<std::pair<double, double>> centreOfMass(const Drawing &drawing) const;
    mutable std::optional<std::map<DrawingId, std::optional<std::pair<double, double>>>> m_heldCentres;
    int m_width;
    int m_height;
    FrameRate m_frameRate;
    int m_frameCount;
    Pixel m_paperColor = 0xffffffff;
    std::optional<AudioTrack> m_audio;
    std::optional<Camera> m_camera;
    std::map<int, std::string> m_markers;
    SymmetryAxes m_symmetryAxes;
    int m_sceneStart = 0;
    GraphData m_compositing;
    GraphView m_compositingView;
    std::vector<std::unique_ptr<Cel>> m_cels;
    CelId m_nextCelId = 1;
    DrawingId m_nextDrawingId = 1;
    LayerId m_nextLayerId = 1;
};

} // namespace mothion::doc
