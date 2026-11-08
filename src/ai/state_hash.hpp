#pragma once

// D-054: an FNV-1a hash of everything in a GameState that a decision can depend on: units, models (ids,
// positions, facings (M65: only when not 0), wounds), flags, the flow (stage, round, the move / charge / fight /
// attack in progress) and the pending decision. It never includes `state.dice`, so the bot's own RNG (seeded from it)
// and its plan memo cannot see the rolls still to come. Static data (names, weapons, characteristics) is left out.

#include <cstdint>

#include "core/state/game_state.hpp"

namespace wh40k::ai {

[[nodiscard]] std::uint64_t state_hash(const core::GameState& state);

// FNV-1a, for callers that hash more on top (e.g. a seed for one decision).
class Fnv1a {
public:
    void bytes(const void* data, std::size_t size) noexcept;
    void u64(std::uint64_t v) noexcept { bytes(&v, sizeof v); }
    void i32(int v) noexcept { bytes(&v, sizeof v); }
    void flag(bool v) noexcept { u64(v ? 1u : 0u); }
    void f32(float v) noexcept;  // the bit pattern (+0 and -0 hash the same)
    [[nodiscard]] std::uint64_t value() const noexcept { return h_; }

private:
    std::uint64_t h_ = 14695981039346656037ull;
};

}  // namespace wh40k::ai
