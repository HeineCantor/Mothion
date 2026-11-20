#pragma once

#include "core/document/Cel.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace mothion::doc {

// Lip sync: the mouth of a character as a cel (a mouth chart, see CelProperties::mouthChart) with a drawing for each
// mouth shape, named after it, and shown frame by frame as the sound says. The shapes are Preston Blair's, as Rhubarb Lip Sync and most mouth charts have them:
//   A: M, B, P (lips closed)        B: most consonants (K, S, T...) and EE, teeth together
//   C: E, open                      D: A, wide open
//   E: O, rounded                   F: U, W, puckered
//   G: F, V (teeth on the lip)      H: L (tongue up)
//   X: at rest, silence
inline constexpr std::array<char, 9> kMouthShapes{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'X'};

// The shape a drawing's name says: its letter alone ("A", "x"), or first and followed by something else than a letter or
// a digit ("A - MBP", "D_open"). Nothing for other names.
std::optional<char> mouthShapeOf(const std::string &drawingName);

// The cel's drawing for `shape` (the first by its id when several are named so); kNoId if it has none.
DrawingId mouthDrawing(const Cel &cel, char shape);

// A mouth shape from `start` to `end` (seconds from the beginning of the sound), as Rhubarb Lip Sync gives them.
struct MouthCue
{
    double start = 0.0;
    double end = 0.0;
    char shape = 'X';
};

// The timing of a mouth chart cel with `cues` laid on it: each cue's drawing from its frame (the sound begins at
// `offset`, at `fps` frames a second) to the next cue's, the last one to its end; the rest of the timing as it was.
// A cue that would begin less than `minFrames` after the one before is dropped (the mouth before holds), and so is one
// whose shape the cel has no drawing for. Frames out of the scene (`frameCount`) are left out.
Timing lipSyncTiming(const Cel &cel, const std::vector<MouthCue> &cues, double fps, int offset, int minFrames,
                     int frameCount);

} // namespace mothion::doc
