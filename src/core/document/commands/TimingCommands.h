#pragma once

#include "core/document/Command.h"
#include "core/document/Timing.h"

#include <memory>

namespace mothion::doc {

// Replaces a cel's whole timing. Timings are small (a few runs), so
// storing both versions is simpler and safer than inverting each edit.
class SetTimingCommand : public Command
{
public:
    SetTimingCommand(CelId cel, Timing before, Timing after);

    CommandKind kind() const override { return CommandKind::ChangeTiming; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    ChangeSet put(Document &doc, const Timing &timing);

    CelId m_cel;
    Timing m_before;
    Timing m_after;
};

// Factories: compute the edited timing from the cel's current one.
// They return nullptr when the edit would change nothing (or is invalid).
std::unique_ptr<Command> makeSetExposure(const Document &doc, CelId cel, int start, int length,
                                         DrawingId drawing);
std::unique_ptr<Command> makeResizeExposure(const Document &doc, CelId cel, int frame, int newLength);
std::unique_ptr<Command> makeInsertEmptyFrames(const Document &doc, CelId cel, int frame, int count);
std::unique_ptr<Command> makeRemoveFrames(const Document &doc, CelId cel, int frame, int count);

// The exposures starting at `starts` moved by `offset` frames: all taken out
// first, then put back there over whatever is shown there (to frame 0 at
// the earliest).
Timing movedExposures(const Timing &timing, const std::vector<int> &starts, int offset);
// True if those exposures, moved so, land on frames that are free (or that
// they leave free themselves), none before frame 0.
bool exposuresFit(const Timing &timing, const std::vector<int> &starts, int offset);

// An exposure of a cel, by the frame it starts at.
struct ExposureRef
{
    CelId cel = kNoId;
    int start = 0;
    bool operator==(const ExposureRef &) const = default;
};
// movedExposures() on each cel the exposures belong to, as one step;
// nullptr when nothing moves or some exposure would not fit (exposuresFit()).
std::unique_ptr<Command> makeMoveExposures(const Document &doc, const std::vector<ExposureRef> &exposures,
                                           int offset);

} // namespace mothion::doc
