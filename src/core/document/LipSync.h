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

// Lip sync from phones (what a phone recognizer such as ZIPA hears): each phone, in IPA, from `start` to `end` seconds.
struct TimedPhone
{
    std::string phone; // UTF-8 ("a", "ʃ", "ɛː")
    double start = 0.0;
    double end = 0.0;
};

// The phones in a CTC recognizer's output: `logProbs` holds `frames` rows of `tokens.size()` scores, frame `t` at
// `firstFrameSeconds + t * frameSeconds`; token 0 is the blank, and tokens in angle brackets ("<unk>") or the word
// boundary "▁" are no phones. Each phone begins where its run of frames does and lasts to the next one, or, when a
// pause follows, a little past its run.
std::vector<TimedPhone> ctcPhones(const float *logProbs, int frames, const std::vector<std::string> &tokens,
                                  double frameSeconds, double firstFrameSeconds = 0.0);

// The mouth shape a phone is said with: A for P B M, B for most consonants and I, C for E, D for A, E for O, F for U W,
// G for F V, H for L. Nothing for a mark alone (a length, a nasalization). Its marks do not count ("aː" is "a").
std::optional<char> mouthShapeOfPhone(const std::string &phone);

// The mouths of `phones`, laid over `duration` seconds: rests (X) where nothing is said, a pause too short to close the
// mouth bridged by the mouth before. The shapes past A to F that the mouths lack (not in `extendedShapes`) are said
// with the nearest of them, as Rhubarb does: G as B, H as C, X as A.
std::vector<MouthCue> mouthCuesFromPhones(const std::vector<TimedPhone> &phones, double duration,
                                          const std::string &extendedShapes = "GHX");

} // namespace mothion::doc
