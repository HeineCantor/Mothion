#include "core/document/commands/MotionCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetCelMotionCommand::SetCelMotionCommand(CelId cel, CelMotion after)
    : m_cel(cel)
    , m_after(std::move(after))
{
}

SetCelMotionCommand::SetCelMotionCommand(CelId cel, CelMotion before, CelMotion after)
    : m_cel(cel)
    , m_after(std::move(after))
    , m_before(std::move(before))
{
}

ChangeSet SetCelMotionCommand::apply(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        m_before = cel->motion();
        cel->setMotion(m_after);
        changes.motion = true;
    }
    return changes;
}

ChangeSet SetCelMotionCommand::revert(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        cel->setMotion(m_before);
        changes.motion = true;
    }
    return changes;
}

std::size_t SetCelMotionCommand::memoryBytes() const
{
    std::size_t keys = 0;
    for (Channel c : kChannels)
        keys += m_after.keys(c).size() + m_before.keys(c).size();
    return sizeof(SetCelMotionCommand) + keys * sizeof(CelKey);
}

} // namespace mothion::doc
