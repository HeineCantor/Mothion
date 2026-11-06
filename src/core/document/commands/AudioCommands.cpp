#include "core/document/commands/AudioCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetAudioCommand::SetAudioCommand(std::optional<AudioTrack> after, CommandKind kind)
    : m_kind(kind)
    , m_after(std::move(after))
{
}

ChangeSet SetAudioCommand::apply(Document &doc)
{
    m_before = doc.audio();
    doc.setAudio(m_after);
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

ChangeSet SetAudioCommand::revert(Document &doc)
{
    doc.setAudio(m_before);
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

} // namespace mothion::doc
