#include "core/document/commands/TimingCommands.h"

#include "core/document/Document.h"
#include "core/document/commands/GroupCommand.h"

#include <algorithm>
#include <map>

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

Timing movedExposures(const Timing &timing, const std::vector<int> &starts, int offset)
{
    std::vector<Exposure> moving;
    for (int start : starts)
        if (const Exposure *e = timing.exposureAt(start); e && e->start == start
            && std::find(moving.begin(), moving.end(), *e) == moving.end())
            moving.push_back(*e);
    Timing after = timing;
    for (const Exposure &e : moving)
        after.clear(e.start, e.length);
    for (const Exposure &e : moving)
        after.setExposure(std::max(0, e.start + offset), e.length, e.drawing);
    return after;
}

bool exposuresFit(const Timing &timing, const std::vector<int> &starts, int offset)
{
    Timing rest = timing; // without the ones that move
    std::vector<Exposure> moving;
    for (int start : starts)
        if (const Exposure *e = timing.exposureAt(start); e && e->start == start)
            moving.push_back(*e);
    for (const Exposure &e : moving)
        rest.clear(e.start, e.length);
    for (const Exposure &e : moving) {
        const int start = e.start + offset;
        if (start < 0)
            return false;
        for (const Exposure &other : rest.exposures())
            if (other.start < start + e.length && start < other.end())
                return false;
    }
    return true;
}

std::unique_ptr<Command> makeMoveExposures(const Document &doc, const std::vector<ExposureRef> &exposures, int offset)
{
    if (offset == 0)
        return nullptr;
    std::map<CelId, std::vector<int>> byCel;
    for (const ExposureRef &e : exposures)
        byCel[e.cel].push_back(e.start);
    std::vector<std::unique_ptr<Command>> steps;
    for (const auto &[celId, starts] : byCel) {
        const Cel *cel = doc.cel(celId);
        if (!cel)
            continue;
        if (!exposuresFit(cel->timing(), starts, offset))
            return nullptr;
        Timing after = movedExposures(cel->timing(), starts, offset);
        if (after != cel->timing())
            steps.push_back(std::make_unique<SetTimingCommand>(celId, cel->timing(), std::move(after)));
    }
    if (steps.empty())
        return nullptr;
    return std::make_unique<GroupCommand>(CommandKind::MoveFrames, std::move(steps));
}

ChangeSet SetFrameCountCommand::apply(Document &doc)
{
    m_before = doc.frameCount();
    doc.setFrameCount(m_after);
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

ChangeSet SetFrameCountCommand::revert(Document &doc)
{
    doc.setFrameCount(m_before);
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

} // namespace mothion::doc
