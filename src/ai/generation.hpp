#pragma once

// D-066 (M56): which generation of levels Easy and Normal plays. `M55` is a regression switch for the arena and
// the tests only (the client never sets it): Easy and Normal exactly as they played at M55 (commit d76e51b), so
// the M56 changes can be measured against them in one binary. With one unit per side both generations play the
// same. Level Hard ignores `M55` (it plays as at M57, D-067; the Normal it starts from is the current Normal).
//
// D-083 (M78): `M77` plays as the pre-batch bots (main at c54c47c): every M79-M82 behaviour (the Teamwork batch) is
// off, for Normal and Hard alike (the hidden arena levels normal-m77 and hard-m77; arena and tests only, never the
// client, the settings or a save file). Code that tests `generation == Generation::M55` keeps that exact test (M55
// still means M55 only); a Teamwork behaviour is gated on `!pre_teamwork(generation)`.

#include <cstdint>

namespace wh40k::ai {

enum class Generation : std::uint8_t { Current, M55, M77 };

// D-083 (M78): a generation from before the Teamwork batch (M78-M83): every behaviour it adds is off.
[[nodiscard]] constexpr bool pre_teamwork(Generation g) noexcept { return g == Generation::M55 || g == Generation::M77; }

// D-083 (M78): the generation level Hard plays. Hard ignores `M55` (D-066: it plays the current Normal underneath), so
// only `M77` reaches it; every other generation is `Current`.
[[nodiscard]] constexpr Generation hard_generation(Generation g) noexcept {
    return g == Generation::M77 ? Generation::M77 : Generation::Current;
}

}  // namespace wh40k::ai
