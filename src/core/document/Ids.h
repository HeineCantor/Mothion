#pragma once

#include <cstdint>

namespace mothion::doc {

// Stable identifiers, unique within a document and never reused (an undone
// creation does not give its id back). Names can change and indices move
// when cels are reordered; ids do not, so commands and files refer to ids.
using CelId = std::uint64_t;
using DrawingId = std::uint64_t;
using LayerId = std::uint64_t; // a layer of a drawing

inline constexpr std::uint64_t kNoId = 0;

} // namespace mothion::doc
