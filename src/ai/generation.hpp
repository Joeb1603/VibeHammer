#pragma once

// D-066 (M56): which generation of levels Easy and Normal plays. `M55` is a regression switch for the arena and
// the tests only (the client never sets it): Easy and Normal exactly as they played at M55 (commit d76e51b), so
// the M56 changes can be measured against them in one binary. With one unit per side both generations play the
// same. Level Hard ignores it (its own code is M57's; where it plays as Normal it plays the current Normal).

#include <cstdint>

namespace wh40k::ai {

enum class Generation : std::uint8_t { Current, M55 };

}  // namespace wh40k::ai
