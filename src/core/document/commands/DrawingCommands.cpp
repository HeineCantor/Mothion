#include "core/document/commands/DrawingCommands.h"

#include "core/document/Document.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/TimingCommands.h"

#include <vector>

namespace mothion::doc {

namespace {

ChangeSet structural()
{
    ChangeSet c;
    c.structure = true;
    return c;
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
        celId, Drawing{id, cel->nextDrawingName(), {Layer{layer, {std::move(layerName)}, {}}}}));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::AddDrawing, std::move(steps)), id, layer};
}

NewDrawing makeSplitDrawing(Document &doc, CelId celId, int frame)
{
    const Cel *cel = doc.cel(celId);
    const Exposure *hold = cel ? cel->timing().exposureAt(frame) : nullptr;
    if (!hold || hold->start == frame)
        return {};
    const Drawing &source = *cel->drawing(hold->drawing);
    Drawing copy = source;
    copy.id = doc.allocateDrawingId();
    copy.name = cel->nextDrawingName();
    for (Layer &layer : copy.layers)
        layer.id = doc.allocateLayerId();
    const DrawingId id = copy.id;
    const LayerId top = copy.layers.empty() ? kNoId : copy.layers.back().id;
    Timing after = cel->timing();
    after.setExposure(frame, hold->end() - frame, id);

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddDrawingCommand>(celId, std::move(copy)));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::SplitFrame, std::move(steps)), id, top};
}

NewDrawing makeInbetween(Document &doc, CelId celId, int frame, std::string layerName)
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
        celId, Drawing{id, cel->nextDrawingName(), {Layer{layer, {std::move(layerName)}, {}}}}));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::InsertInbetween, std::move(steps)), id, layer};
}

} // namespace mothion::doc
