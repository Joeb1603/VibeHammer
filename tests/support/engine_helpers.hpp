#pragma once

// Helpers for engine tests: a deterministic test RNG, random template filling, random play, and
// shortcuts to common positions in the game.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/rules/attacks.hpp"
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
    ADD_FAILURE() << "unknown template kind " << core::action_name(tmpl);
    return tmpl;
}

// One random legal (and, for templates, validly filled) action for whoever must decide.
inline core::GameAction random_action(const core::GameState& gs, TestRng& rng) {
    const auto legal = core::legal_actions(gs, decider(gs));
    EXPECT_FALSE(legal.empty());
    const core::GameAction& pick = legal[rng.below(legal.size())];
    return core::is_template(pick) ? fill_template(gs, pick, rng, false) : pick;
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
