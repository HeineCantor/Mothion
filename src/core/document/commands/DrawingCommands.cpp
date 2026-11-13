#include "core/document/commands/DrawingCommands.h"

#include "core/document/Document.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace mothion::doc {

namespace {

ChangeSet structural()
{
    ChangeSet c;
    c.structure = true;
    return c;
}

// A copy of `source` with its own ids, named `name`; the pixels are shared
// until one of them is painted.
Drawing cloneDrawing(Document &doc, const Drawing &source, std::string name)
{
    Drawing copy = source;
    copy.id = doc.allocateDrawingId();
    copy.name = std::move(name);
    for (Layer &layer : copy.layers)
        layer.id = doc.allocateLayerId();
    return copy;
}

// The first free number as a name, past the cel's drawings and `taken`.
std::string freeName(const Cel &cel, const std::set<std::string> &taken)
{
    std::set<std::string> used = taken;
    for (const auto &[id, d] : cel.drawings())
        used.insert(d.name);
    for (int n = 1;; ++n)
        if (!used.contains(std::to_string(n)))
            return std::to_string(n);
}

} // namespace

// --- AddDrawingCommand ------------------------------------------------------------

AddDrawingCommand::AddDrawingCommand(CelId cel, Drawing drawing)
    : m_cel(cel)
    , m_id(drawing.id)
    , m_drawing(std::move(drawing))
{
}

ChangeSet AddDrawingCommand::apply(Document &doc)
{
    Cel *cel = doc.cel(m_cel);
    if (!cel || !m_drawing)
        return {};
    cel->addDrawing(std::move(*m_drawing));
    m_drawing.reset();
    return structural();
}

ChangeSet AddDrawingCommand::revert(Document &doc)
{
    if (Cel *cel = doc.cel(m_cel))
        m_drawing = cel->takeDrawing(m_id);
    return structural();
}

std::size_t AddDrawingCommand::memoryBytes() const
{
    return sizeof(*this) + (m_drawing ? m_drawing->memoryBytes() : 0);
}

// --- RemoveDrawingCommand ---------------------------------------------------------

RemoveDrawingCommand::RemoveDrawingCommand(CelId cel, DrawingId drawing)
    : m_cel(cel)
    , m_id(drawing)
{
}

ChangeSet RemoveDrawingCommand::apply(Document &doc)
{
    Cel *cel = doc.cel(m_cel);
    if (!cel)
        return {};
    m_timingBefore = cel->timing();
    cel->timing().removeDrawing(m_id);
    m_drawing = cel->takeDrawing(m_id);
    return structural();
}

ChangeSet RemoveDrawingCommand::revert(Document &doc)
{
    Cel *cel = doc.cel(m_cel);
    if (!cel || !m_drawing)
        return {};
    cel->addDrawing(std::move(*m_drawing));
    m_drawing.reset();
    cel->timing() = m_timingBefore;
    return structural();
}

std::size_t RemoveDrawingCommand::memoryBytes() const
{
    return sizeof(*this) + (m_drawing ? m_drawing->memoryBytes() : 0);
}

// --- factory ----------------------------------------------------------------------

NewDrawing makeNewDrawing(Document &doc, CelId celId, int frame, int length, std::string layerName)
{
    const Cel *cel = doc.cel(celId);
    if (!cel)
        return {};
    const DrawingId id = doc.allocateDrawingId();
    const LayerId layer = doc.allocateLayerId();
    Timing after = cel->timing();
    after.setExposure(frame, length, id);

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddDrawingCommand>(
        celId, Drawing{id, cel->nextDrawingName(), {Layer{layer, {std::move(layerName)}, {}, LayerKind::Raster, std::nullopt, {}}}}));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::AddDrawing, std::move(steps)), id, layer};
}

NewDrawing makeSplitDrawing(Document &doc, CelId celId, int frame)
{
    const Cel *cel = doc.cel(celId);
    const Exposure *hold = cel ? cel->timing().exposureAt(frame) : nullptr;
    if (!hold || hold->start == frame)
        return {};
    Drawing copy = cloneDrawing(doc, *cel->drawing(hold->drawing), cel->nextDrawingName());
    const DrawingId id = copy.id;
    const LayerId top = copy.layers.empty() ? kNoId : copy.layers.back().id;
    Timing after = cel->timing();
    after.setExposure(frame, hold->end() - frame, id);

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddDrawingCommand>(celId, std::move(copy)));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::SplitFrame, std::move(steps)), id, top};
}

std::unique_ptr<Command> makeSplitEvery(Document &doc, CelId celId, int frame, int step)
{
    const Cel *cel = doc.cel(celId);
    const Exposure *hold = cel ? cel->timing().exposureAt(frame) : nullptr;
    if (!hold || step < 1 || hold->length <= step)
        return nullptr;
    const Drawing *source = cel->drawing(hold->drawing);
    if (!source)
        return nullptr;
    const int start = hold->start, end = hold->end();
    Timing after = cel->timing();
    std::vector<std::unique_ptr<Command>> steps;
    std::set<std::string> names;
    for (int from = start + step; from < end; from += step) {
        const std::string name = freeName(*cel, names);
        names.insert(name);
        Drawing copy = cloneDrawing(doc, *source, name);
        after.setExposure(from, std::min(step, end - from), copy.id);
        steps.push_back(std::make_unique<AddDrawingCommand>(celId, std::move(copy)));
    }
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return std::make_unique<GroupCommand>(CommandKind::SplitFrame, std::move(steps));
}

std::unique_ptr<Command> makePasteExposures(Document &doc, const std::vector<ExposureCopy> &copies, int frame,
                                            bool clone)
{
    // By cel, the copies still there.
    std::map<CelId, std::vector<ExposureCopy>> byCel;
    for (const ExposureCopy &c : copies)
        if (const Cel *cel = doc.cel(c.cel); cel && cel->drawing(c.drawing) && c.length >= 1)
            byCel[c.cel].push_back(c);
    if (byCel.empty() || frame < 0)
        return nullptr;

    std::vector<std::unique_ptr<Command>> steps;
    for (const auto &[celId, pasted] : byCel) {
        const Cel *cel = doc.cel(celId);
        // Only onto free frames, none of the pasted ones over another.
        Timing after = cel->timing();
        for (const ExposureCopy &c : pasted) {
            const int start = frame + c.start;
            for (const Exposure &e : after.exposures())
                if (e.start < start + c.length && start < e.end())
                    return nullptr;
            after.setExposure(start, c.length, c.drawing); // claims the frames; the drawing is set below
        }
        after = cel->timing();
        std::map<DrawingId, DrawingId> clones; // copied drawing -> its clone
        std::set<std::string> names;
        for (const ExposureCopy &c : pasted) {
            DrawingId shown = c.drawing;
            if (clone) {
                auto it = clones.find(c.drawing);
                if (it == clones.end()) {
                    const std::string name = freeName(*cel, names);
                    names.insert(name);
                    Drawing copy = cloneDrawing(doc, *cel->drawing(c.drawing), name);
                    it = clones.emplace(c.drawing, copy.id).first;
                    steps.push_back(std::make_unique<AddDrawingCommand>(celId, std::move(copy)));
                }
                shown = it->second;
            }
            after.setExposure(frame + c.start, c.length, shown);
        }
        steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    }
    return std::make_unique<GroupCommand>(CommandKind::PasteFrames, std::move(steps));
}

NewDrawing makeInbetween(Document &doc, CelId celId, int frame, std::string layerName, CommandKind kind)
{
    const Cel *cel = doc.cel(celId);
    if (!cel || frame < 0)
        return {};
    const DrawingId id = doc.allocateDrawingId();
    const LayerId layer = doc.allocateLayerId();
    Timing after = cel->timing();
    after.insertEmptyFrames(frame, 1);
    after.setExposure(frame, 1, id);

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddDrawingCommand>(
        celId, Drawing{id, cel->nextDrawingName(), {Layer{layer, {std::move(layerName)}, {}, LayerKind::Raster, std::nullopt, {}}}}));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(kind, std::move(steps)), id, layer};
}

} // namespace mothion::doc
