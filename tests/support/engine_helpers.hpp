#pragma once

// Helpers for engine tests: a deterministic test RNG, random template filling, random play, and
// shortcuts to common positions in the game.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/deployment.hpp"
#include "core/state/queries.hpp"
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

// ---- shared assertions (formerly copied into each test file) ----

template <typename E>
std::vector<E> events_of(const std::vector<core::GameEvent>& events) {
    std::vector<E> out;
    for (const auto& e : events) {
        if (const auto* x = std::get_if<E>(&e)) out.push_back(*x);
    }
    return out;
}

inline bool starts_with(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

inline bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// The validation reasons for an action that must be rejected; each must be rule-cited.
inline std::vector<std::string> rejection(const core::GameState& gs, core::Side side, const core::GameAction& a) {
    const auto r = core::validate_action(gs, side, a);
    EXPECT_FALSE(r.ok()) << core::action_name(a) << " was accepted";
    for (const auto& reason : r.reasons) {
        EXPECT_TRUE(is_rule_cited(reason)) << reason;
    }
    return r.reasons;
}

inline void expect_valid(const core::GameState& gs, core::Side side, const core::GameAction& a) {
    const auto r = core::validate_action(gs, side, a);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.reasons.front());
}

inline std::vector<core::ModelId> model_ids(const core::Unit& u) {
    std::vector<core::ModelId> out;
    for (const auto& m : u.models) out.push_back(m.id);
    return out;
}

// D-051: answers a pending casualty decision (05.04 / 06.02) in unit order and returns its events. The
// decision is raised whenever damage is inflicted, even when the casualty is forced.
inline std::vector<core::GameEvent> answer_casualties(core::GameState& gs) {
    EXPECT_TRUE(gs.pending && std::holds_alternative<core::decision::OrderCasualties>(gs.pending->kind));
    if (!gs.pending) return {};
    const auto* d = std::get_if<core::decision::OrderCasualties>(&gs.pending->kind);
    if (d == nullptr) return {};
    const core::UnitId target = d->target;
    return core::apply_action(gs, gs.pending->side,
                              core::action::OrderCasualties{target, model_ids(*gs.find_unit(target))});
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
        // M65 (D-074 part 2): an oval base also gets turns. Only its fills draw the extra numbers, so every game
        // without an oval plays exactly as before.
        const core::Model* model = core::find_model(gs, pm->model);
        const bool oval = model != nullptr && model->base.shape != core::BaseShape::Round;
        const auto random_facing = [&rng] { return static_cast<float>(rng.below(3600)) * 0.1f - 180.0f; };
        if (corrupt) {
            switch (rng.below(oval ? 5 : 3)) {
                case 0: filled.path = {core::Position{start.x + max + in(2), start.y, 0.0f}}; break;  // too far
                case 1: filled.path = {core::Position{-in(1), start.y, 0.0f}}; break;  // off the battlefield
                case 3:  // D-074: one facing too many
                    filled.path = {start};
                    filled.facings = {random_facing(), random_facing()};
                    break;
                case 4:  // D-074: not a number
                    filled.path = {start};
                    filled.facings = {std::numeric_limits<float>::quiet_NaN()};
                    break;
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
        if (oval) {
            const float keep = core::move_start_facing_deg(gs, pm->model).value_or(0.0f);
            switch (rng.below(4)) {
                case 0: break;  // no turn (as for a round base)
                case 1:         // a facing at every vertex: a turn before each straight line
                    for (std::size_t i = 0; i < filled.path.size(); ++i) filled.facings.push_back(random_facing());
                    break;
                case 2:  // a turn without moving (03.01): one vertex at the start
                    filled.path = {start};
                    filled.facings = {random_facing()};
                    break;
                default:  // a last turn after the last straight line (a zero-length segment)
                    filled.facings.assign(filled.path.size(), keep);
                    filled.path.push_back(filled.path.back());
                    filled.facings.push_back(random_facing());
                    break;
            }
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
    if (const auto* fi = std::get_if<core::action::Fight>(&tmpl)) {
        // D-021: every model that can attack declares one random single-target option (always valid). 24.11
        // (M60): a model offered [EXTRA ATTACKS] weapons declares a random option of each of them, plus one of its
        // other weapons' options if it is offered any.
        core::action::Fight filled = *fi;
        const core::Unit& unit = *gs.find_unit(fi->unit);
        const auto options = core::melee_options(gs, unit);
        for (const core::Model& m : unit.models) {
            std::vector<core::MeleeDeclaration> mine;
            for (const auto& o : options) {
                if (o.model == m.id) mine.push_back(o);
            }
            const auto extra = [&m](const core::MeleeDeclaration& o) {
                return core::is_extra_attacks_weapon(m.weapons[o.weapon_index]);
            };
            if (std::none_of(mine.begin(), mine.end(), extra)) {
                if (!mine.empty()) filled.declarations.push_back(mine[rng.below(mine.size())]);
                continue;
            }
            std::vector<std::size_t> extra_weapons;
            std::vector<core::MeleeDeclaration> others;
            for (const auto& o : mine) {
                if (!extra(o)) {
                    others.push_back(o);
                } else if (std::find(extra_weapons.begin(), extra_weapons.end(), o.weapon_index) == extra_weapons.end()) {
                    extra_weapons.push_back(o.weapon_index);
                }
            }
            for (const std::size_t w : extra_weapons) {
                std::vector<core::MeleeDeclaration> of_w;
                for (const auto& o : mine) {
                    if (o.weapon_index == w) of_w.push_back(o);
                }
                filled.declarations.push_back(of_w[rng.below(of_w.size())]);
            }
            if (!others.empty()) filled.declarations.push_back(others[rng.below(others.size())]);
        }
        if (corrupt) {
            switch (rng.below(4)) {
                case 0: filled.declarations.push_back(filled.declarations.front()); break;  // two weapons
                case 1: filled.declarations.front().model = core::ModelId{9999}; break;     // not in the unit
                case 2: filled.declarations.front().targets.front().attacks += 1; break;    // too many attacks
                default: filled.declarations.clear(); break;                                // the template
            }
        }
        return filled;
    }
    if (const auto* du = std::get_if<core::action::DeployUnit>(&tmpl)) {
        // D-081 (M74): the engine's proposal (without one, the unit's models in a line across the middle of its zone),
        // moved by up to 6" each way, an oval turned to a random facing half the time. It can still be refused (out of
        // the zone, on a model, engaged): that's the point. Only these fills draw the extra numbers, so every game
        // without a deployment by hand plays exactly as before.
        core::action::DeployUnit filled = *du;
        const core::Unit* unit = core::find_undeployed(gs, du->unit);
        if (unit == nullptr) {
            ADD_FAILURE() << "DeployUnit template for unit " << du->unit.v << ", which is not waiting to be set up";
            return tmpl;
        }
        if (const std::optional<core::action::DeployUnit> p = core::propose_set_up(gs, du->unit)) {
            filled.models = p->models;
        } else {
            const core::Rect zone = core::zone_rect(gs.battlefield, gs.side(unit->side).deployment_zone);
            float x = zone.min_x + core::circumscribed_radius(unit->models.front().base);
            const float y = (zone.min_y + zone.max_y) * 0.5f;
            for (const core::Model& m : unit->models) {
                const float r = core::circumscribed_radius(m.base);
                filled.models.push_back(core::ModelPlacement{m.id, core::Position{x, y, 0.0f}, 0.0f});
                x += 2.0f * r;
            }
        }
        const float dx = in(static_cast<float>(rng.below(1201)) * 0.01f - 6.0f);
        const float dy = in(static_cast<float>(rng.below(1201)) * 0.01f - 6.0f);
        for (core::ModelPlacement& p : filled.models) {
            p.position.x += dx;
            p.position.y += dy;
            const core::Model* m = core::find_model(*unit, p.model);
            if (m != nullptr && m->base.shape != core::BaseShape::Round && rng.below(2) == 0) {
                p.facing_deg = static_cast<float>(rng.below(3600)) * 0.1f - 180.0f;
            }
        }
        if (corrupt) {
            switch (rng.below(6)) {
                case 0: filled.models.pop_back(); break;                                       // a model not placed
                case 1: filled.models.push_back(filled.models.front()); break;                 // placed twice
                case 2: filled.models.back().model = core::ModelId{9999}; break;               // not in the unit
                case 3: filled.models.front().position.x = std::numeric_limits<float>::quiet_NaN(); break;
                case 4: filled.models.front().position = core::Position{-in(2), in(1), 0.0f}; break;  // off the field
                default: filled.models.clear(); break;                                         // the template itself
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
           std::holds_alternative<core::action::Shoot>(tmpl) || std::holds_alternative<core::action::Fight>(tmpl);
}

// One random legal action for whoever must decide. Templates are filled until one validates (a few
// tries); if none does, a fully specified legal action is chosen instead.
inline core::GameAction random_action(const core::GameState& gs, TestRng& rng) {
    const core::Side side = decider(gs);
    const auto legal = core::legal_actions(gs, side);
    EXPECT_FALSE(legal.empty());
    // M61 (D-072, a D-004 extension): legal_actions lists two of the re-roll subsets; a third of the time random
    // play picks any subset instead. Only this decision draws the extra numbers, so other games are unchanged.
    if (gs.pending && std::holds_alternative<core::decision::RerollWoundRolls>(gs.pending->kind) && gs.flow.attack &&
        gs.flow.attack->wound_rerolls && rng.below(3) == 0) {
        core::action::RerollWoundRolls subset;
        for (std::size_t i = 0; i < gs.flow.attack->wound_rerolls->rolls.rolls.size(); ++i) {
            if (rng.below(2) == 0) subset.indices.push_back(i);
        }
        return subset;
    }
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

// Side A's Charge step after a successful Intercessor charge (11.04): the V1 scenario with the
// Termagants 9.9" away, a charge roll of 10 (4, 6) and every Intercessor moved 8.9" north, ending about
// 1" from the Termagants' front rank. The Intercessors have Fights First; the units are engaged.
inline core::GameState after_successful_charge() {
    core::GameState gs = load_v1_scenario();
    core::start_game(gs);
    while (!(gs.flow.stage == core::Stage::Charge && core::turn_side(gs) == core::Side::A)) {
        core::apply_action(gs, decider(gs), core::legal_actions(gs, decider(gs)).front());
    }
    const float gap = core::mm_to_inches(*core::unit_distance(gs.units[0], gs.units[1]));
    for (auto& m : gs.units[1].models) m.position.y -= in(gap - 9.9f);
    core::ScriptedDice dice{4, 6};
    core::apply_action(gs, core::Side::A, core::action::DeclareCharge{gs.units[0].id}, dice);
    core::apply_action(gs, core::Side::A, core::action::StartChargeMove{gs.units[0].id, {gs.units[1].id}});
    for (const auto& m : gs.units[0].models) {
        const core::Position s = *core::move_start_position(gs, m.id);
        core::apply_action(gs, core::Side::A,
                           core::action::ProposeModelMove{m.id, {core::Position{s.x, s.y + in(8.9f), 0.0f}}});
    }
    core::apply_action(gs, core::Side::A, core::action::CommitUnitMove{});
    EXPECT_TRUE(gs.units[0].turn_flags.fights_first);
    return gs;
}

// Every model's first melee option (D-021): a valid Fight action for `unit`. A model with [EXTRA ATTACKS]
// weapons also declares the first option of each other weapon whose kind it still needs (24.11, M60).
inline core::action::Fight first_melee_options(const core::GameState& gs, core::UnitId unit) {
    core::action::Fight a{unit, {}};
    const core::Unit& u = *gs.find_unit(unit);
    for (const auto& o : core::melee_options(gs, u)) {
        const core::Model& m = *core::find_model(u, o.model);
        const auto extra = [&m](std::size_t w) { return core::is_extra_attacks_weapon(m.weapons[w]); };
        bool seen = false;
        bool needed = false;
        for (const core::MeleeDeclaration& d : a.declarations) {
            if (d.model != o.model) continue;
            seen = true;
            needed = needed || d.weapon_index == o.weapon_index;
        }
        if (!seen) {
            a.declarations.push_back(o);
            continue;
        }
        if (needed) continue;  // this weapon is already declared
        // 24.11: every [EXTRA ATTACKS] weapon, and one other weapon.
        const bool have_other = std::any_of(a.declarations.begin(), a.declarations.end(), [&](const core::MeleeDeclaration& d) {
            return d.model == o.model && !extra(d.weapon_index);
        });
        if (extra(o.weapon_index) || !have_other) a.declarations.push_back(o);
    }
    return a;
}

// M65: the neutral answer to the pending decision: remain stationary, end every step, skip pile-ins and
// consolidations, fight with the first options (first_melee_options), casualties in unit order; a move in progress
// commits when it can end, else resets.
inline core::GameAction neutral_action(const core::GameState& gs) {
    const core::DecisionKind& k = gs.pending->kind;
    if (std::holds_alternative<core::decision::Shoot>(k)) return core::action::EndShootStep{};
    if (std::holds_alternative<core::decision::Charge>(k)) return core::action::EndChargeStep{};
    if (std::holds_alternative<core::decision::PileIn>(k)) return core::action::EndPileIns{};
    if (std::holds_alternative<core::decision::Consolidate>(k)) return core::action::EndConsolidations{};
    if (const auto* o = std::get_if<core::decision::OverrunPileIn>(&k)) return core::action::SkipPileIn{o->unit};
    if (const auto* d = std::get_if<core::decision::DeclareMeleeAttacks>(&k)) return first_melee_options(gs, d->unit);
    if (std::holds_alternative<core::decision::MoveModels>(k)) {
        if (core::move_end_problems(gs).empty()) return core::action::CommitUnitMove{};
        return core::action::ResetUnitMove{};
    }
    core::GameAction a = core::legal_actions(gs, decider(gs)).front();
    if (auto* oc = std::get_if<core::action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
        oc->priority = model_ids(*gs.find_unit(oc->target));
    }
    return a;
}

// M65: neutral answers (neutral_action) until `done(gs)`, at most `guard` of them.
template <typename Done>
void play_neutral_until(core::GameState& gs, Done done, int guard = 800) {
    for (int i = 0; i < guard && gs.pending && !done(gs); ++i) core::apply_action(gs, decider(gs), neutral_action(gs));
}

// D-047: the Consolidate step after after_successful_charge's fight (as R12 FullFightPhase): no pile-ins,
// every melee attack misses on both sides, so the units are still engaged. Side A consolidates first.
inline core::GameState consolidate_step_state() {
    core::GameState gs = after_successful_charge();
    core::apply_action(gs, core::Side::A, core::action::EndChargeStep{});
    while (gs.flow.stage == core::Stage::PileIn) core::apply_action(gs, decider(gs), core::action::EndPileIns{});
    for (const core::UnitId unit : {gs.units[0].id, gs.units[1].id}) {
        core::apply_action(gs, decider(gs), core::action::SelectToFight{unit, core::FightType::Normal});
        core::ScriptedDice misses(std::vector<int>(64, 1));
        core::apply_action(gs, decider(gs), first_melee_options(gs, unit), misses);
    }
    EXPECT_EQ(gs.flow.stage, core::Stage::Consolidate);
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
