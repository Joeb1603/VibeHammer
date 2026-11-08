#pragma once

// M63 (D-073) fixtures for 17.01 and 17.03: a few units of the rosters fixture (D-070) on its 44"x60" board, placed
// by hand. Shared by the engine, client and ai tests.
//
// - Side A: the Chaos Lord, the chainsword Legionaries (one rank of 10, 32 mm) and the Havocs (one rank of 5, 40 mm).
// - Side B: the Hive Tyrant (MONSTER, 60 mm), the Tyrant Guard (INFANTRY, 3 x 50 mm) and a Screamer-killer (MONSTER,
//   90 mm).
//
// Layout (inches, y north): the Legionaries' rank is centred on (22, 26.15); the Tyrant is at (22, 30), 2.04" north
// of them, so an 8" Normal move straight south to (22, 22.3) passes through the rank and ends 2.04" beyond it. With
// `engaged`, the Tyrant is 1" from the rank instead and the Tyrant Guard 1" from its west end, both engaged with the
// Legionaries. The Havocs stand east of the Tyrant on its line (no model between), the Lord north-west of the Tyrant
// within 12" of the Tyrant.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/game_state.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

namespace wh40k::test::monsters {

// Unit indices in the GameState the fixture returns.
inline constexpr std::size_t kLord = 0;
inline constexpr std::size_t kLegionaries = 1;
inline constexpr std::size_t kHavocs = 2;
inline constexpr std::size_t kTyrant = 3;
inline constexpr std::size_t kGuard = 4;
inline constexpr std::size_t kScreamer = 5;

inline constexpr float kRankY = 26.15f;    // the Legionaries' rank
inline constexpr float kTyrantY = 30.0f;   // the Tyrant, unengaged
inline constexpr float kThroughY = 22.3f;  // 7.7" south of it: 2.04" beyond the rank

// One rank of `u`'s models, touching, centred on (x_in, y_in).
inline void place_rank(core::Unit& u, float x_in, float y_in) {
    float width = 0.0f;
    for (const core::Model& m : u.models) width += 2.0f * core::base_radius(m.base);
    float x = in(x_in) - width * 0.5f;
    for (core::Model& m : u.models) {
        const float r = core::base_radius(m.base);
        m.position = core::Position{x + r, in(y_in), 0.0f};
        x += 2.0f * r;
    }
}

// The gap (inches, centre to centre) that leaves `gap_in` edge to edge between a 60 mm Tyrant and a 32 mm Legionary.
inline float tyrant_centre_gap(float gap_in) { return gap_in + core::mm_to_inches(30.0f + 16.0f); }

inline core::GameState fixture(bool engaged = false, core::Side first_turn = core::Side::B) {
    core::GameState all = load_rosters_fixture();
    core::GameState gs = all;
    gs.units.clear();
    for (const std::size_t i : {std::size_t{0}, std::size_t{1}, std::size_t{4}, std::size_t{5}, std::size_t{10},
                                std::size_t{11}}) {
        gs.units.push_back(all.units[i]);
    }
    gs.first_turn = first_turn;
    place_rank(gs.units[kLegionaries], 22.0f, kRankY);
    place_rank(gs.units[kHavocs], 39.5f, kTyrantY);
    place_rank(gs.units[kLord], 16.0f, 36.0f);
    place_rank(gs.units[kScreamer], 34.0f, 45.0f);
    if (engaged) {
        place_rank(gs.units[kTyrant], 22.0f, kRankY + tyrant_centre_gap(1.0f));
        // 1" north of the rank's west end (its first model, centre 22 - 6.3 + 0.63).
        place_rank(gs.units[kGuard], 16.33f, kRankY + 1.0f + core::mm_to_inches(16.0f + 25.0f));
    } else {
        place_rank(gs.units[kTyrant], 22.0f, kTyrantY);
        place_rank(gs.units[kGuard], 6.0f, 40.0f);
    }
    return gs;
}

// Plays first legal actions (remaining stationary, ending steps) until `done(gs)` or the guard runs out.
template <typename Done>
inline void play_until(core::GameState& gs, Done done, int guard = 200) {
    for (int i = 0; i < guard && gs.pending && !done(gs); ++i) {
        core::GameAction a = core::legal_actions(gs, decider(gs)).front();
        if (auto* oc = std::get_if<core::action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = model_ids(*gs.find_unit(oc->target));
        }
        core::apply_action(gs, decider(gs), a);
    }
}

// The fixture, started, at the first turn side's Move Units decision.
inline core::GameState at_movement(bool engaged = false, core::Side first_turn = core::Side::B) {
    core::GameState gs = fixture(engaged, first_turn);
    core::start_game(gs);
    play_until(gs, [](const core::GameState& g) {
        return g.pending && std::holds_alternative<core::decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// The fixture (engaged), started with Side A first, at Side A's Shoot decision (every unit remained stationary).
inline core::GameState at_side_a_shooting() {
    core::GameState gs = fixture(true, core::Side::A);
    core::start_game(gs);
    play_until(gs, [](const core::GameState& g) {
        return g.pending && std::holds_alternative<core::decision::Shoot>(g.pending->kind);
    });
    return gs;
}

}  // namespace wh40k::test::monsters

// M65 (D-074 part 2): a few units of the Tyrannofex fixture (tests/data/scenarios/rosters_tyrannofex_44x60.json) on its
// 44"x60" board, placed by hand, for the engine tests of an oval base. Engine tests only: the client and the bots handle
// oval bases from M66 (fixture() above stays round-only, as the client and ai tests share it).
// - Side A: the chainsword Legionaries (one rank of 10, 32 mm) and the Havocs (one rank of 5, 40 mm).
// - Side B: the Tyrannofex (MONSTER, a 92 x 120 mm oval), the Tyrant Guard (INFANTRY, 3 x 50 mm) and the west
//   Screamer-killer (MONSTER, 90 mm).
// Layout (inches, y north): the Legionaries' rank is centred on (22, 26.15), its top edge at 26.78; the Tyrannofex
// stands at (22, 34) facing 0, 4.86" north of the rank (its length, 4.72", south to north); the Havocs' rank at
// (39.5, 20), the Tyrant Guard at (6, 40) and the Screamer-killer at (34, 45).
namespace wh40k::test::tyrannofex {

// Unit indices in the GameState the fixture returns (in UnitId order).
inline constexpr std::size_t kLegionaries = 0;
inline constexpr std::size_t kHavocs = 1;
inline constexpr std::size_t kGuard = 2;
inline constexpr std::size_t kScreamer = 3;
inline constexpr std::size_t kTyrannofex = 4;

inline constexpr float kRankY = 26.15f;       // the Legionaries' rank
inline constexpr float kTyrannofexY = 34.0f;  // the Tyrannofex
inline const core::Base kOval{core::BaseShape::Oval, 92.0f, 120.0f};

inline core::Model& the_tyrannofex(core::GameState& gs) { return gs.units[kTyrannofex].models[0]; }

// With `engaged`, the Tyrannofex's south end is 1" from the Legionaries' rank (engaged, 03.04) instead.
inline core::GameState fixture(core::Side first_turn = core::Side::B, bool engaged = false) {
    core::GameState all = load_tyrannofex_fixture();
    core::GameState gs = all;
    gs.units.clear();
    for (const std::size_t i : {std::size_t{1}, std::size_t{4}, std::size_t{10}, std::size_t{11}, std::size_t{13}}) {
        gs.units.push_back(all.units[i]);
    }
    gs.first_turn = first_turn;
    monsters::place_rank(gs.units[kLegionaries], 22.0f, kRankY);
    monsters::place_rank(gs.units[kHavocs], 39.5f, 20.0f);
    monsters::place_rank(gs.units[kGuard], 6.0f, 40.0f);
    monsters::place_rank(gs.units[kScreamer], 34.0f, 45.0f);
    core::Model& t = the_tyrannofex(gs);
    t.position = engaged ? at(22.0f, kRankY + core::mm_to_inches(16.0f + 60.0f) + 1.0f) : at(22.0f, kTyrannofexY);
    t.facing_deg = 0.0f;
    return gs;
}

// The fixture, started, at the first turn side's Move Units decision.
inline core::GameState at_movement(core::Side first_turn = core::Side::B) {
    core::GameState gs = fixture(first_turn);
    core::start_game(gs);
    monsters::play_until(gs, [](const core::GameState& g) {
        return g.pending && std::holds_alternative<core::decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

}  // namespace wh40k::test::tyrannofex
