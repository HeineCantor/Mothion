#pragma once

#include "core/document/Cel.h"

#include <array>
#include <optional>
#include <string>

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

} // namespace mothion::doc
