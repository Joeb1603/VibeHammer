#pragma once

// M42 fixtures: Side A's Charge step in the V1 scenario with either unit charging the other, at a chosen gap,
// sideways offset and formation, and the charge roll scripted. Core and gtest only, so both wh40k_client_tests
// and wh40k_ai_tests use them.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "test_helpers.hpp"

namespace wh40k::test {

enum class Charger { Intercessors, Termagants };

// How the charging unit stands: as deployed (Intercessors one rank, Termagants two), or reformed in `ranks`
// touching ranks facing the target.
struct ChargeLayout {
    float gap_in = 6.0f;     // edge to edge, the closest pair of models
    float offset_in = 0.0f;  // the target unit shifted sideways (east) by this much
    int ranks = 0;           // 0: as deployed
};

inline constexpr std::size_t kM42Marines = 0;
inline constexpr std::size_t kM42Gants = 1;

// The charging unit's index (units[0] is the Intercessors, units[1] the Termagants).
inline std::size_t charger_index(Charger c) { return c == Charger::Intercessors ? kM42Marines : kM42Gants; }
inline std::size_t target_index(Charger c) { return c == Charger::Intercessors ? kM42Gants : kM42Marines; }

// The unit's models in `ranks` touching ranks round its current centre, the front rank towards +y when
// `north` (models in unit order, front rank first, each rank left to right).
inline void reform(core::Unit& u, int ranks, bool north) {
    if (ranks <= 0 || u.models.empty()) return;
    float cx = 0.0f;
    float cy = 0.0f;
    for (const core::Model& m : u.models) {
        cx += m.position.x;
        cy += m.position.y;
    }
    cx /= static_cast<float>(u.models.size());
    cy /= static_cast<float>(u.models.size());
    const float d = u.models.front().base.diameter_mm;
    const std::size_t n = u.models.size();
    const std::size_t per_rank = (n + static_cast<std::size_t>(ranks) - 1) / static_cast<std::size_t>(ranks);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t rank = i / per_rank;
        const std::size_t in_rank = std::min(per_rank, n - rank * per_rank);
        const float x = cx + (static_cast<float>(i % per_rank) - 0.5f * static_cast<float>(in_rank - 1)) * d;
        const float y = cy + (north ? -1.0f : 1.0f) * static_cast<float>(rank) * d;
        u.models[i].position = core::Position{x, y, 0.0f};
    }
}

// Side A's Charge step with `c` (on side A) `layout.gap_in` from the other unit (on side B). Turn and phase
// flags are cleared and the pending decision recomputed.
// The V1 scenario played (first legal action each time) to Side A's first Charge step; made once.
inline const core::GameState& first_charge_step() {
    static const core::GameState base = [] {
        core::GameState gs = load_v1_scenario();
        core::start_game(gs);
        while (!(gs.flow.stage == core::Stage::Charge && core::turn_side(gs) == core::Side::A)) {
            const core::Side side = gs.pending.value().side;
            core::apply_action(gs, side, core::legal_actions(gs, side).front());
        }
        return gs;
    }();
    return base;
}

inline core::GameState charge_fixture(Charger c, const ChargeLayout& layout) {
    core::GameState gs = first_charge_step();
    core::Unit& charger = gs.units[charger_index(c)];
    core::Unit& target = gs.units[target_index(c)];
    charger.side = core::Side::A;
    target.side = core::Side::B;
    for (core::Unit* u : {&charger, &target}) {
        u->turn_flags = {};
        u->phase_flags = {};
    }
    // The charger stands south of the target, its front rank facing north.
    const bool charger_south = charger.models.front().position.y < target.models.front().position.y;
    if (!charger_south) {
        for (core::Model& m : charger.models) m.position.y = gs.battlefield.depth_mm - m.position.y;
        for (core::Model& m : target.models) m.position.y = gs.battlefield.depth_mm - m.position.y;
    }
    reform(charger, layout.ranks, true);
    for (core::Model& m : target.models) m.position.x += in(layout.offset_in);
    for (int i = 0; i < 60; ++i) {
        const float d = *core::unit_distance(charger, target);
        if (std::fabs(d - in(layout.gap_in)) < 1e-4f) break;
        for (core::Model& m : target.models) m.position.y += in(layout.gap_in) - d;
    }
    gs.pending = core::compute_decision(gs);
    return gs;
}

// A face pair that core's 2D6 sums to `total`.
inline std::array<int, 2> faces_for(int total) {
    for (int a = 1; a <= 6; ++a) {
        for (int b = 1; b <= 6; ++b) {
            if (a + b == total) return {a, b};
        }
    }
    return {1, 1};
}

// Declares the charge with a scripted roll. True if SelectChargeTargets is then pending and the target unit is
// an eligible charge target (11.04 Before Moving).
inline bool declare_with_roll(core::GameState& gs, Charger c, int roll) {
    const core::UnitId unit = gs.units[charger_index(c)].id;
    const core::GameAction declare = core::action::DeclareCharge{unit};
    if (!core::validate_action(gs, core::Side::A, declare).ok()) return false;
    const auto f = faces_for(roll);
    core::ScriptedDice dice{f[0], f[1]};
    core::apply_action(gs, core::Side::A, declare, dice);
    if (!gs.pending || !std::holds_alternative<core::decision::SelectChargeTargets>(gs.pending->kind)) return false;
    const core::GameAction start = core::action::StartChargeMove{unit, {gs.units[target_index(c)].id}};
    return core::validate_action(gs, core::Side::A, start).ok();
}

// declare_with_roll, then the charge move started against the target unit.
inline std::optional<core::GameState> charge_move_state(Charger c, const ChargeLayout& layout, int roll) {
    core::GameState gs = charge_fixture(c, layout);
    if (!declare_with_roll(gs, c, roll)) return std::nullopt;
    core::apply_action(gs, core::Side::A,
                       core::action::StartChargeMove{gs.units[charger_index(c)].id, {gs.units[target_index(c)].id}});
    return gs;
}

}  // namespace wh40k::test
