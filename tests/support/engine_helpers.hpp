#pragma once

// Helpers for engine tests: a deterministic test RNG, random template filling, random play, and
// shortcuts to common positions in the game.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/shooting.hpp"
#include "test_helpers.hpp"

namespace wh40k::test {

// SplitMix64, independent of the engine's dice (tests only).
class TestRng {
public:
    explicit TestRng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }

private:
    std::uint64_t state_;
};

// "09.02: ..." or "D-007: ..."
inline bool is_rule_cited(const std::string& reason) {
    static const std::regex kCited(R"(^(\d\d\.\d\d|D-\d{3}): .+)");
    return std::regex_match(reason, kCited);
}

inline core::Side decider(const core::GameState& gs) { return gs.pending.value().side; }

inline std::vector<core::ModelId> model_ids(const core::Unit& u) {
    std::vector<core::ModelId> out;
    for (const auto& m : u.models) out.push_back(m.id);
    return out;
}

// Fills a template (D-004/D-013) with a random valid parameter, or, when `corrupt`, a random invalid
// one (missing, duplicated, foreign or empty entries).
inline core::GameAction fill_template(const core::GameState& gs, const core::GameAction& tmpl, TestRng& rng,
                                      bool corrupt) {
    if (const auto* oc = std::get_if<core::action::OrderCasualties>(&tmpl)) {
        core::action::OrderCasualties filled = *oc;
        filled.priority = model_ids(*gs.find_unit(oc->target));
        for (std::size_t i = filled.priority.size(); i > 1; --i) {
            std::swap(filled.priority[i - 1], filled.priority[rng.below(i)]);
        }
        if (corrupt) {
            switch (rng.below(4)) {
                case 0: filled.priority.pop_back(); break;
                case 1: filled.priority.push_back(filled.priority.front()); break;
                case 2: filled.priority.back() = core::ModelId{9999}; break;
                default: filled.priority.clear(); break;
            }
        }
        return filled;
    }
    if (const auto* pm = std::get_if<core::action::ProposeModelMove>(&tmpl)) {
        // A path of 1-3 straight lines from the model's start position, within its maximum distance.
        // It can still be invalid (off the battlefield, through an enemy model): that's the point.
        core::action::ProposeModelMove filled = *pm;
        const core::Position start = core::move_start_position(gs, pm->model).value();
        const float max = core::max_move_distance_mm(gs, pm->model).value();
        if (corrupt) {
            switch (rng.below(3)) {
                case 0: filled.path = {core::Position{start.x + max + in(2), start.y, 0.0f}}; break;  // too far
                case 1: filled.path = {core::Position{-in(1), start.y, 0.0f}}; break;  // off the battlefield
                default: filled.path.clear(); break;
            }
            return filled;
        }
        const std::size_t legs = 1 + rng.below(3);
        float budget = max * 0.95f;
        core::Position at = start;
        for (std::size_t i = 0; i < legs; ++i) {
            const float angle = static_cast<float>(rng.below(3600)) * 0.1f * 3.14159265f / 180.0f;
            const float length = budget * static_cast<float>(rng.below(1000)) / 1000.0f / static_cast<float>(legs);
            at = core::Position{at.x + length * std::cos(angle), at.y + length * std::sin(angle), 0.0f};
            filled.path.push_back(at);
            budget -= length;
        }
        return filled;
    }
    if (const auto* sh = std::get_if<core::action::Shoot>(&tmpl)) {
        // D-016: each model fires at most one of its valid single-weapon options (so 24.07 and the
        // one-profile-per-weapon rule always hold), and at least one model fires.
        core::action::Shoot filled = *sh;
        const core::Unit& unit = *gs.find_unit(sh->unit);
        const auto options = core::shot_options(gs, unit, sh->type);
        for (const core::Model& m : unit.models) {
            std::vector<core::ShotDeclaration> mine;
            for (const auto& o : options) {
                if (o.model == m.id) mine.push_back(o);
            }
            if (!mine.empty() && rng.below(4) != 0) filled.shots.push_back(mine[rng.below(mine.size())]);
        }
        if (filled.shots.empty() && !options.empty()) filled.shots.push_back(options[rng.below(options.size())]);
        if (corrupt) {
            switch (rng.below(4)) {
                case 0: filled.shots.push_back(filled.shots.front()); break;  // one weapon selected twice
                case 1: filled.shots.back().model = core::ModelId{9999}; break;  // not in the unit
                case 2: filled.shots.back().target = sh->unit; break;            // not an enemy unit
                default: filled.shots.clear(); break;                            // the template itself
            }
        }
        return filled;
    }
    ADD_FAILURE() << "unknown template kind " << core::action_name(tmpl);
    return tmpl;
}

// Templates whose clean random fill is always valid (casualty orders, shot declarations). Path fills
// may be rejected.
inline bool clean_fill_always_valid(const core::GameAction& tmpl) {
    return std::holds_alternative<core::action::OrderCasualties>(tmpl) ||
           std::holds_alternative<core::action::Shoot>(tmpl);
}

// One random legal action for whoever must decide. Templates are filled until one validates (a few
// tries); if none does, a fully specified legal action is chosen instead.
inline core::GameAction random_action(const core::GameState& gs, TestRng& rng) {
    const core::Side side = decider(gs);
    const auto legal = core::legal_actions(gs, side);
    EXPECT_FALSE(legal.empty());
    const core::GameAction& pick = legal[rng.below(legal.size())];
    if (!core::is_template(pick)) {
        return pick;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        core::GameAction filled = fill_template(gs, pick, rng, false);
        if (core::validate_action(gs, side, filled).ok()) {
            return filled;
        }
    }
    std::vector<core::GameAction> concrete;
    for (const auto& a : legal) {
        if (!core::is_template(a)) concrete.push_back(a);
    }
    EXPECT_FALSE(concrete.empty());
    return concrete[rng.below(concrete.size())];
}

// Starts the V1 scenario (Termagants 1" closer, D-008 note) and plays to `side`'s Shoot step.
inline core::GameState shoot_step_state(core::Side side = core::Side::A, std::uint64_t seed = 0) {
    core::GameState gs = load_v1_scenario_closer();
    gs.dice.seed = seed;
    core::start_game(gs);
    for (int guard = 0; guard < 20; ++guard) {
        if (gs.flow.stage == core::Stage::Shoot && core::turn_side(gs) == side) {
            return gs;
        }
        const auto legal = core::legal_actions(gs, decider(gs));
        core::apply_action(gs, decider(gs), legal.front());
    }
    ADD_FAILURE() << "did not reach the Shoot step";
    return gs;
}

inline core::WeaponSelection select(const core::GameState& gs, std::size_t unit_index, std::size_t model_index,
                                    std::size_t weapon, std::size_t profile, core::UnitId target) {
    return core::select_weapon_target(gs, gs.units[unit_index].models[model_index].id, weapon, profile, target);
}

// Adds a copy of units[index] with fresh unit and model ids (positions unchanged).
inline core::UnitId add_unit_copy(core::GameState& gs, std::size_t index) {
    core::Unit copy = gs.units[index];
    std::uint32_t max_unit = 0;
    std::uint32_t max_model = 0;
    for (const auto& u : gs.units) {
        max_unit = std::max(max_unit, u.id.v);
        for (const auto& m : u.models) max_model = std::max(max_model, m.id.v);
    }
    copy.id = core::UnitId{max_unit + 1};
    for (auto& m : copy.models) m.id = core::ModelId{++max_model};
    gs.units.push_back(copy);
    return copy.id;
}

}  // namespace wh40k::test
