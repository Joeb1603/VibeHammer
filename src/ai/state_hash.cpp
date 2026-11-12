#include "state_hash.hpp"

#include <cstring>
#include <optional>
#include <variant>

namespace wh40k::ai {

void Fnv1a::bytes(const void* data, std::size_t size) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h_ ^= p[i];
        h_ *= 1099511628211ull;
    }
}

void Fnv1a::f32(float v) noexcept {
    if (v == 0.0f) v = 0.0f;  // -0 -> +0
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    bytes(&bits, sizeof bits);
}

namespace {

void add(Fnv1a& h, const core::Position& p) {
    h.f32(p.x);
    h.f32(p.y);
    h.f32(p.z);
}

void add(Fnv1a& h, core::UnitId id) { h.u64(id.v); }
void add(Fnv1a& h, core::ModelId id) { h.u64(id.v); }

// M65 (D-074 part 2): a facing only when it isn't 0, behind a tag, so a state with only round bases (every facing 0)
// hashes as before.
void add_facing(Fnv1a& h, float facing_deg) {
    if (facing_deg == 0.0f) return;
    h.u64(0x66616365ull);  // "face": apart from the fields around it
    h.f32(facing_deg);
}

template <typename T>
void add_opt_int(Fnv1a& h, const std::optional<T>& v) {
    h.flag(v.has_value());
    if (v) h.i32(static_cast<int>(*v));
}

void add(Fnv1a& h, const core::Unit& u) {
    add(h, u.id);
    h.u64(static_cast<std::uint64_t>(u.side));
    h.flag(u.battle_shocked);
    h.i32(u.starting_strength);
    const core::UnitTurnFlags& t = u.turn_flags;
    for (const bool b : {t.advanced, t.fell_back, t.moved_over_3in, t.made_charge_move, t.fights_first}) h.flag(b);
    const core::UnitPhaseFlags& p = u.phase_flags;
    for (const bool b : {p.selected_to_move, p.desperate_escape_hazards_made, p.selected_to_shoot, p.declared_charge,
                         p.piled_in, p.engaged_at_fight_step_start, p.selected_to_fight,
                         p.eligible_to_fight_this_phase, p.selected_for_overrun, p.forced_to_fight, p.consolidated}) {
        h.flag(b);
    }
    add_opt_int(h, p.advance_roll);
    add_opt_int(h, p.charge_roll);
    // M67 (D-076): a unit's points and starting wounds only when it has points, behind a tag, so a state without
    // them (every scenario before M67) hashes as before.
    if (u.points != 0 || u.starting_wounds != 0) {
        h.u64(0x706e7473ull);  // "pnts": apart from the fields around it
        h.i32(u.points);
        h.i32(u.starting_wounds);
    }
    // M88 (D-088): the ranged-attacks record (13.09 Hidden reads it) only when set, behind a tag, so a state without
    // terrain (never set) hashes as before.
    if (u.ranged_attacks != core::RangedAttackRecord{}) {
        h.u64(0x726e6764ull);  // "rngd"
        h.flag(u.ranged_attacks.this_turn);
        h.flag(u.ranged_attacks.previous_turn);
    }
    h.u64(u.models.size());
    for (const core::Model& m : u.models) {
        add(h, m.id);
        add(h, m.position);
        add_facing(h, m.facing_deg);
        h.i32(m.wounds_remaining);
    }
}

void add(Fnv1a& h, const core::FlowState& f) {
    h.u64(static_cast<std::uint64_t>(f.stage));
    h.i32(f.battle_round);
    h.i32(f.turn_in_round);
    h.flag(f.activation.has_value());
    if (f.activation) {
        add(h, f.activation->unit);
        h.u64(static_cast<std::uint64_t>(f.activation->kind));
    }
    h.flag(f.attack.has_value());
    if (f.attack) {
        const core::AttackFlow& a = *f.attack;
        add(h, a.attacker);
        h.u64(a.batches.size());
        for (const core::AttackBatch& b : a.batches) {
            add(h, b.target);
            h.i32(b.attack_dice);
            // M62 (D-069): only while a random A waits to be rolled, so other states hash as before.
            if (!b.random_attacks.empty()) {
                h.u64(0x72616e64ull);  // "rand": apart from the fields above
                h.u64(b.random_attacks.size());
                for (const core::RandomAttackDice& r : b.random_attacks) {
                    h.u64(r.selection);
                    h.i32(r.attacks.num_dice);
                    h.i32(r.attacks.sides);
                    h.i32(r.attacks.modifier);
                    h.i32(r.bonus);
                }
            }
        }
        for (const bool d : a.batch_done) h.flag(d);
        h.flag(a.current_target.has_value());
        if (a.current_target) add(h, *a.current_target);
        h.flag(a.current.has_value());
        if (a.current) {
            h.u64(a.current->batch);
            h.i32(a.current->wounds);
            h.u64(a.current->groups.size());
            h.flag(a.current->order.has_value());
            if (a.current->order) {
                for (const std::size_t i : *a.current->order) h.u64(i);
            }
            h.flag(a.current->save_rolls.has_value());
            if (a.current->save_rolls) {
                for (const int r : *a.current->save_rolls) h.i32(r);
            }
        }
        // M60 (24.15): only when set, so a state without hazard rolls hashes as before.
        if (a.hazard_rolls != 0 || a.pending_mortal_wounds != 0) {
            h.i32(a.hazard_rolls);
            h.i32(a.pending_mortal_wounds);
        }
        // M61 (24.38): only when set, so a state without pending re-rolls hashes as before.
        if (a.wound_rerolls) {
            h.u64(0x7477696eull);  // "twin": apart from the fields above
            h.u64(a.wound_rerolls->batch);
            h.u64(a.wound_rerolls->rolls.rolls.size());
            for (const core::WoundRoll& r : a.wound_rerolls->rolls.rolls) h.i32(r.unmodified);
        }
    }
    h.flag(f.move.has_value());
    if (f.move) {
        const core::MoveInProgress& m = *f.move;
        add(h, m.unit);
        h.u64(static_cast<std::uint64_t>(m.type));
        add_opt_int(h, m.mode);
        for (const core::ModelStart& s : m.starts) {
            add(h, s.model);
            add(h, s.position);
            add_facing(h, s.facing_deg);
        }
        h.u64(m.paths.size());
        for (const core::ModelPath& p : m.paths) {
            add(h, p.model);
            h.u64(p.points.size());
            for (const core::Position& pt : p.points) add(h, pt);
            if (!p.facings.empty()) {  // M65: only a path that turns its model
                h.u64(0x7475726eull);  // "turn"
                for (const float facing : p.facings) h.f32(facing);
            }
        }
        h.i32(m.pending_mortal_wounds);
        for (const core::UnitId t : m.targets) add(h, t);
        add_opt_int(h, m.consolidation_mode);
    }
    h.flag(f.charge.has_value());
    if (f.charge) add(h, f.charge->unit);
    h.flag(f.step_done[0]);
    h.flag(f.step_done[1]);
    h.flag(f.fight_sequence.has_value());
    if (f.fight_sequence) {
        h.u64(static_cast<std::uint64_t>(f.fight_sequence->sub_step));
        h.u64(static_cast<std::uint64_t>(f.fight_sequence->selecting));
        add_opt_int(h, f.fight_sequence->last_pass);
    }
    h.flag(f.fight.has_value());
    if (f.fight) {
        add(h, f.fight->unit);
        h.u64(static_cast<std::uint64_t>(f.fight->type));
        h.flag(f.fight->overrun_pile_in_pending);
        h.flag(f.fight->attacks_started);
        h.flag(f.fight->forced);
    }
    h.u64(f.forced_fights.size());
    for (const core::UnitId u : f.forced_fights) add(h, u);
}

// The decision's kind and the unit it is about (every kind holds at most one id).
void add(Fnv1a& h, const core::PendingDecision& d) {
    h.u64(static_cast<std::uint64_t>(d.side));
    h.u64(d.kind.index());
    std::visit(
        [&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (requires { k.unit; }) add(h, k.unit);
            if constexpr (requires { k.target; }) add(h, k.target);
            if constexpr (requires { k.attacker; }) add(h, k.attacker);
            (void)sizeof(K);
        },
        d.kind);
}

}  // namespace

std::uint64_t state_hash(const core::GameState& state) {
    Fnv1a h;
    h.i32(state.battle_rounds);
    h.u64(static_cast<std::uint64_t>(state.first_turn));
    h.u64(state.units.size());
    for (const core::Unit& u : state.units) add(h, u);
    h.i32(state.command_points[0]);
    h.i32(state.command_points[1]);
    add(h, state.flow);
    h.flag(state.pending.has_value());
    if (state.pending) add(h, *state.pending);
    h.flag(state.result.has_value());
    if (state.result) add_opt_int(h, state.result->winner);
    // M67 (D-076): the result rule only when it isn't D-007's, so older states hash as before.
    if (state.result_rule != core::ResultRule::D007Draw) {
        h.u64(0x72756c65ull);  // "rule"
        h.u64(static_cast<std::uint64_t>(state.result_rule));
    }
    // M74 (D-081): the units not yet set up only when there are some, behind a tag, so every other state hashes as
    // before.
    if (!state.undeployed.empty()) {
        h.u64(0x756e6470ull);  // "undp"
        h.u64(state.undeployed.size());
        for (const core::Unit& u : state.undeployed) add(h, u);
    }
    // M86 (D-084): the mission (its areas, their control and the VP scored) only when there is one, behind a tag, so
    // every other state hashes as before.
    if (state.mission) {
        const core::Mission& m = *state.mission;
        h.u64(0x6d697373ull);  // "miss"
        h.i32(m.first_scoring_round);
        h.i32(m.kill_points);
        h.i32(m.kill_vp);
        h.u64(m.objectives.size());
        for (const core::ObjectiveArea& a : m.objectives) {
            add(h, a.centre);
            h.f32(a.diameter_mm);
            h.i32(a.vp[0]);
            h.i32(a.vp[1]);
            add_opt_int(h, a.controller);
            add_opt_int(h, a.secured_by);
        }
        h.i32(m.objective_vp[0]);
        h.i32(m.objective_vp[1]);
    }
    // M88 (D-087): the terrain only when there is some, behind a tag, so every other state hashes as before. Fixed for
    // the game, so its shape is enough: each area's and feature's category and footprint.
    if (!state.terrain.empty()) {
        h.u64(0x7465727aull);  // "terz"
        h.u64(state.terrain.size());
        const auto add_shape = [&h](const core::TerrainShape& t) {
            h.u64(t.vertices.size());
            for (const core::Position& p : t.vertices) add(h, p);
            add(h, t.centre);
            h.f32(t.diameter_mm);
        };
        for (const core::TerrainArea& a : state.terrain) {
            add_shape(a.shape);
            h.u64(a.features.size());
            for (const core::TerrainFeature& f : a.features) {
                h.u64(static_cast<std::uint64_t>(f.category));
                h.f32(f.height_mm.value_or(0.0f));
                add_shape(f.shape);
            }
        }
    }
    // state.dice is deliberately not hashed (D-054: the bot never peeks at the rolls to come).
    return h.value();
}

}  // namespace wh40k::ai
