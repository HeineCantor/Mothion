#include "core/document/commands/BlenderLinkCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetBlenderLinkCommand::SetBlenderLinkCommand(CelId cel, std::optional<BlenderLink> after)
    : m_cel(cel)
    , m_after(std::move(after))
{
}

ChangeSet SetBlenderLinkCommand::apply(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        m_before = cel->blenderLink();
        cel->setBlenderLink(m_after);
        changes.structure = true; // who watches the files hears of it
    }
    return changes;
}

ChangeSet SetBlenderLinkCommand::revert(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        cel->setBlenderLink(m_before);
        changes.structure = true;
    }
    return changes;
}

std::size_t SetBlenderLinkCommand::memoryBytes() const
{
    return sizeof(SetBlenderLinkCommand) + (m_after ? m_after->path.size() : 0) + (m_before ? m_before->path.size() : 0);
}

} // namespace mothion::doc
