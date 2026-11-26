#pragma once

// Client test fixtures shared by the M94 and M95 tests: sessions that play V1 with the session's automatic answers.

#include <cstdint>
#include <utility>

#include "core/engine/engine.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

namespace wh40k::test {

// A short V1 game (the Termagants 1" closer) played by random legal actions through a session: moves, shots, charges
// and fights, the session making every roll (auto_roll) and declining every window (auto_decline, D-096), each a step
// of its own. Seeds chosen so the game shoots and charges.
inline client::GameSession random_session_game(std::uint64_t seed, std::uint64_t rng_seed, int actions) {
    core::GameState scenario = load_v1_scenario_closer();
    client::GameSession s = client::GameSession::new_game(std::move(scenario), seed);
    TestRng rng(rng_seed);
    for (int i = 0; i < actions && s.pending(); ++i) {
        const core::GameAction a = random_action(s.state(), rng);
        const core::ValidationResult r = s.submit(a);
        EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.reasons.front());
        if (!r.ok()) break;
    }
    return s;
}

// The Shoot action firing every Bolt Rifle on Focused Fire at the Termagants.
inline core::action::Shoot rifle_volley(const core::GameState& gs) {
    core::action::Shoot a{gs.units[0].id, core::ShootingType::Normal, {}};
    for (const core::Model& m : gs.units[0].models) a.shots.push_back(core::ShotDeclaration{m.id, 0, 0, gs.units[1].id});
    return a;
}

// Side A's Shoot step in V1 (the Termagants 1" closer) with `seed` (round 1: Side A has 1 CP, Side B 1).
inline core::GameState shoot_step(std::uint64_t seed) { return shoot_step_state(core::Side::A, seed); }

}  // namespace wh40k::test
