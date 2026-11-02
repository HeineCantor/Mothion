#include "core/document/commands/TimingCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetTimingCommand::SetTimingCommand(CelId cel, Timing before, Timing after)
    : m_cel(cel)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetTimingCommand::put(Document &doc, const Timing &timing)
{
    Cel *cel = doc.cel(m_cel);
    if (!cel)
        return {};
    cel->timing() = timing;
    ChangeSet c;
    c.structure = true;
    return c;
}

ChangeSet SetTimingCommand::apply(Document &doc)
{
    return put(doc, m_after);
}

ChangeSet SetTimingCommand::revert(Document &doc)
{
    return put(doc, m_before);
}

namespace {

// Runs `edit` on a copy of the cel's timing; a command only if it changed.
template<typename Edit>
std::unique_ptr<Command> editTiming(const Document &doc, CelId celId, Edit edit)
{
    const Cel *cel = doc.cel(celId);
    if (!cel)
        return nullptr;
    Timing after = cel->timing();
    edit(after);
    if (after == cel->timing())
        return nullptr;
    return std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after));
}

} // namespace

std::unique_ptr<Command> makeSetExposure(const Document &doc, CelId cel, int start, int length,
                                         DrawingId drawing)
{
    return editTiming(doc, cel, [&](Timing &t) { t.setExposure(start, length, drawing); });
}

std::unique_ptr<Command> makeResizeExposure(const Document &doc, CelId cel, int frame, int newLength)
{
    return editTiming(doc, cel, [&](Timing &t) { t.resizeExposure(frame, newLength); });
}

std::unique_ptr<Command> makeInsertEmptyFrames(const Document &doc, CelId cel, int frame, int count)
{
    return editTiming(doc, cel, [&](Timing &t) { t.insertEmptyFrames(frame, count); });
}

std::unique_ptr<Command> makeRemoveFrames(const Document &doc, CelId cel, int frame, int count)
{
    return editTiming(doc, cel, [&](Timing &t) { t.removeFrames(frame, count); });
}

} // namespace mothion::doc
