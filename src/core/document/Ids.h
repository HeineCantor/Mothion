#pragma once

#include <cstdint>

namespace mothion::doc {

// Stable identifiers, unique within a document and never reused (an undone
// creation does not give its id back). Names can change and indices move
// when layers are reordered; ids do not, so commands and files refer to ids.
using LayerId = std::uint64_t;
using DrawingId = std::uint64_t;

inline constexpr std::uint64_t kNoId = 0;

} // namespace mothion::doc
