#pragma once

#include "core/document/Audio.h"
#include "core/document/Command.h"

#include <optional>

namespace mothion::doc {

// Puts `after` (a new track, a changed one, or none: it is taken away) in place of the audio track the document
// has, and undo puts the old one back. `kind` only tells what it was for (SetAudio, ChangeAudio).
class SetAudioCommand : public Command
{
public:
    SetAudioCommand(std::optional<AudioTrack> after, CommandKind kind = CommandKind::SetAudio);

    CommandKind kind() const override { return m_kind; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    // The sound is shared with the track that is in the document, or with the old one: no memory of its own
    // beyond the small parts.
    std::size_t memoryBytes() const override { return 1024; }

private:
    CommandKind m_kind;
    std::optional<AudioTrack> m_after;
    std::optional<AudioTrack> m_before;
};

} // namespace mothion::doc
