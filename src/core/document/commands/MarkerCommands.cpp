#include "core/document/commands/MarkerCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetMarkersCommand::SetMarkersCommand(std::map<int, std::string> after)
    : m_after(std::move(after))
{
}

ChangeSet SetMarkersCommand::apply(Document &doc)
{
    m_before = doc.markers();
    doc.setMarkers(m_after);
    ChangeSet changes;
    changes.markers = true;
    return changes;
}

ChangeSet SetMarkersCommand::revert(Document &doc)
{
    doc.setMarkers(m_before);
    ChangeSet changes;
    changes.markers = true;
    return changes;
}

std::size_t SetMarkersCommand::memoryBytes() const
{
    std::size_t bytes = sizeof(SetMarkersCommand);
    for (const auto *set : {&m_after, &m_before})
        for (const auto &[frame, name] : *set)
            bytes += sizeof(frame) + name.size() + 48; // (a map node's own cost, about)
    return bytes;
}

} // namespace mothion::doc
