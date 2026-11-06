#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mothion::doc {

// The song under the animation: one audio file kept whole inside the project (so the project can be moved
// without losing it), and where and how it plays. Decoding and playing are not here (the core knows nothing of
// audio devices): the file's bytes and its `format` are what the player is given.
struct AudioTrack
{
    std::string name;   // shown: the file's name without its extension
    std::string format; // the file's extension, lower case ("mp3", "wav"): tells the decoder what it is
    // The file as it was; shared, so copies of the track (undo steps) do not copy the sound.
    std::shared_ptr<const std::vector<unsigned char>> data;
    // Frames from the first frame of the scene to where the sound begins; negative: the sound began
    // before the scene, and its first part is not heard.
    int offset = 0;
    float volume = 1.0f; // 0..1
    bool muted = false;
    bool scrub = true; // heard in short bursts as the timeline is scrubbed or stepped
    double duration = 0.0; // seconds; 0 until it is known (by decoding it)

    std::size_t bytes() const { return data ? data->size() : 0; }
    bool operator==(const AudioTrack &o) const
    {
        return name == o.name && format == o.format && data == o.data && offset == o.offset && volume == o.volume
               && muted == o.muted && scrub == o.scrub && duration == o.duration;
    }
};

} // namespace mothion::doc
