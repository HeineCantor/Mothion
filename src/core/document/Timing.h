#pragma once

#include "core/document/Ids.h"

#include <vector>

namespace mothion::doc {

// A drawing shown for `length` consecutive frames starting at `start` (0-based).
struct Exposure
{
    int start = 0;
    int length = 1;
    DrawingId drawing = kNoId;

    int end() const { return start + length; } // one past the last frame
    bool contains(int frame) const { return frame >= start && frame < end(); }
    bool operator==(const Exposure &) const = default;
};

// The exposure sheet of one layer: which drawing is shown on each frame.
//
// Stored as runs, Harmony style. Invariants kept by every operation:
//  - sorted by start, non-overlapping, length >= 1, drawing != kNoId;
//  - two touching runs of the same drawing are merged into one hold;
//  - frames not covered by any run are empty.
class Timing
{
public:
    const std::vector<Exposure> &exposures() const { return m_exposures; }
    bool isEmpty() const { return m_exposures.empty(); }

    DrawingId drawingAt(int frame) const;          // kNoId on empty frames
    const Exposure *exposureAt(int frame) const;   // nullptr on empty frames
    int lastFrame() const;                         // end of the last run, 0 if empty
    bool usesDrawing(DrawingId drawing) const;

    // Shows `drawing` on [start, start+length), overwriting what was there
    // (neighbouring runs are trimmed or split). kNoId clears the range.
    void setExposure(int start, int length, DrawingId drawing);
    void clear(int start, int length) { setExposure(start, length, kNoId); }

    // Changes the length of the run containing `frame`; later runs move by
    // the difference (ripple), so the following timing is preserved.
    // Returns false if `frame` is empty or newLength < 1.
    bool resizeExposure(int frame, int newLength);

    // Inserts `count` empty frames at `frame`, pushing later frames right.
    // A run crossing `frame` is split around the gap.
    void insertEmptyFrames(int frame, int count);
    // Deletes frames [frame, frame+count) and pulls later frames left.
    void removeFrames(int frame, int count);

    // Clears every frame showing `drawing`.
    void removeDrawing(DrawingId drawing);

    bool operator==(const Timing &) const = default;

private:
    void normalize(); // sort, drop empty runs, merge touching holds

    std::vector<Exposure> m_exposures;
};

} // namespace mothion::doc
