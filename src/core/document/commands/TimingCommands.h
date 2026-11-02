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

} // namespace mothion::doc
