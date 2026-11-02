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
    return sizeof(*this) + (m_drawing ? m_drawing->image.memoryBytes() : 0);
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
    return sizeof(*this) + (m_drawing ? m_drawing->image.memoryBytes() : 0);
}

// --- factory ----------------------------------------------------------------------

NewDrawing makeNewDrawing(Document &doc, CelId celId, int frame, int length)
{
    const Cel *cel = doc.cel(celId);
    if (!cel)
        return {};
    const DrawingId id = doc.allocateDrawingId();
    Timing after = cel->timing();
    after.setExposure(frame, length, id);

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddDrawingCommand>(celId, Drawing{id, cel->nextDrawingName(), {}}));
    steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    return {std::make_unique<GroupCommand>(CommandKind::AddDrawing, std::move(steps)), id};
}

} // namespace mothion::doc
