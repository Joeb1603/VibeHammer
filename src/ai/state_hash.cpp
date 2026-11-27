#include "state_hash.hpp"

#include <cstring>
#include <optional>
#include <type_traits>
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
    // M101 (D-103): 10.07's "remained stationary" only when set (a unit with an [INDIRECT FIRE] weapon), behind a tag.
    if (t.remained_stationary) h.u64(0x73746179ull);  // "stay"
    const core::UnitPhaseFlags& p = u.phase_flags;
    for (const bool b : {p.selected_to_move, p.desperate_escape_hazards_made, p.selected_to_shoot, p.declared_charge,
                         p.piled_in, p.engaged_at_fight_step_start, p.selected_to_fight,
                         p.eligible_to_fight_this_phase, p.selected_for_overrun, p.forced_to_fight, p.consolidated}) {
        h.flag(b);
    }
    add_opt_int(h, p.advance_roll);
    add_opt_int(h, p.charge_roll);
    // M96 (D-097): 15.12 Counteroffensive's Fights First only when set, behind a tag, so every other state hashes as
    // before.
    if (p.fights_first) h.u64(0x63666673ull);  // "cffs"
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
        // M100 (D-102): a spent [ONE SHOT] weapon (24.26) only when one is, behind a tag, so other states hash as before.
        for (std::size_t w = 0; w < m.weapons.size(); ++w) {
            if (!m.weapons[w].one_shot_spent) continue;
            h.u64(0x316f7368ull);  // "1osh"
            h.u64(w);
        }
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
            // M94 (D-095): only while a random D waits for its Damage roll, so other states hash as before.
            if (a.current->priority) {
                h.u64(0x7072696full);  // "prio"
                for (const core::ModelId id : *a.current->priority) add(h, id);
            }
            // M95 (D-096): only while a batch's damage is being rolled per attack, so other states hash as before.
            if (!a.current->damage_rolls.empty()) {
                h.u64(0x646d6772ull);  // "dmgr"
                for (const core::CharacteristicRoll& r : a.current->damage_rolls) h.i32(r.value);
            }
            // M99 (D-101): only for a [DEVASTATING WOUNDS] batch with critical wounds (24.10), so other states hash as
            // before.
            if (a.current->devastating != 0 || a.current->mortal) {
                h.u64(0x64657677ull);  // "devw"
                h.i32(a.current->devastating);
                h.flag(a.current->mortal);
            }
        }
        // M60 (24.15): only when set, so a state without hazard rolls hashes as before.
        if (a.hazard_rolls != 0 || a.pending_mortal_wounds != 0) {
            h.i32(a.hazard_rolls);
            h.i32(a.pending_mortal_wounds);
        }
        // M100 (D-102): only while a [LETHAL HITS] batch's automatic wounds wait for its wound rolls (24.23).
        if (a.automatic_wounds != 0) {
            h.u64(0x6c74686cull);  // "lthl"
            h.i32(a.automatic_wounds);
        }
        // M61 (24.38): only when set, so a state without pending re-rolls hashes as before.
        if (a.wound_rerolls) {
            h.u64(0x7477696eull);  // "twin": apart from the fields above
            h.u64(a.wound_rerolls->batch);
            h.u64(a.wound_rerolls->rolls.rolls.size());
            for (const core::WoundRoll& r : a.wound_rerolls->rolls.rolls) h.i32(r.unmodified);
        }
        // M94 (D-095): only while a batch's dice are being rolled, so other states hash as before.
        if (a.rolling) {
            h.u64(0x726f6c6cull);  // "roll"
            h.u64(a.rolling->batch);
            h.flag(a.rolling->hits.has_value());
            if (a.rolling->hits) {
                h.u64(a.rolling->hits->rolls.size());
                for (const core::HitRoll& r : a.rolling->hits->rolls) h.i32(r.unmodified.value_or(0));
                h.i32(a.rolling->hits->sustained_hits);
            }
        }
        // M96 (D-097): 15.09 snap shooting only when set, behind a tag, so every other state hashes as before.
        if (a.context.snap_shooting) h.u64(0x736e6170ull);  // "snap"
        // M92 (D-093): AttackFlow::shooting_type is not hashed. It only narrows the new targets of 04.03.03, and the
        // batches hashed above already differ by its effects (10.05 / 10.06), so every older state hashes as before.
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
        if (m.objective) {  // M90 (D-091): only an Objective Consolidation's, so every older state hashes as before
            h.u64(0x6f626a63ull);  // "objc"
            h.u64(*m.objective);
        }
    }
    h.flag(f.charge.has_value());
    if (f.charge) {
        add(h, f.charge->unit);
        // M96 (D-097): 15.11 Heroic Intervention's mode only when set, behind a tag, so every other state hashes as
        // before.
        if (f.charge->heroic) {
            h.u64(0x6865726full);  // "hero"
            h.u64(static_cast<std::uint64_t>(*f.charge->heroic));
        }
    }
    h.flag(f.step_done[0]);
    h.flag(f.step_done[1]);
    h.flag(f.fight_sequence.has_value());
    if (f.fight_sequence) {
        h.u64(static_cast<std::uint64_t>(f.fight_sequence->sub_step));
        h.u64(static_cast<std::uint64_t>(f.fight_sequence->selecting));
        add_opt_int(h, f.fight_sequence->last_pass);
        if (f.fight_sequence->next_to_fight) {  // M96 (D-097, 15.12): only when set, so older states hash as before
            h.u64(0x6e657874ull);  // "next"
            add(h, *f.fight_sequence->next_to_fight);
        }
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
    // M94 (D-095): only while battle-shock rolls are owed, so other states hash as before.
    if (!f.battle_shock_tests.empty()) {
        h.u64(0x6273686bull);  // "bshk"
        for (const core::UnitId u : f.battle_shock_tests) add(h, u);
    }
    // M95 (D-096): only while a reaction window is open, so other states hash as before.
    if (f.window) {
        h.u64(0x77696e64ull);  // "wind"
        h.u64(static_cast<std::uint64_t>(f.window->timing));
        // M96 (D-097): a window after no roll has no roll, and an end-of-phase window no unit; a tag stands for each,
        // so M95's windows (both set) hash as before.
        if (f.window->roll) {
            h.u64(static_cast<std::uint64_t>(*f.window->roll));
        } else {
            h.u64(0x6e726f6cull);  // "nrol"
        }
        if (f.window->unit) {
            add(h, *f.window->unit);
        } else {
            h.u64(0x6e756e74ull);  // "nunt"
        }
        h.u64(f.window->batch);
        for (const core::Side s : f.window->sides) h.u64(static_cast<std::uint64_t>(s));
        for (const int face : f.window->faces) h.i32(face);
        for (const std::size_t i : f.window->rerolled) h.u64(i);
    }
    // M96 (D-097): 15.08's snap shooter only while it waits to shoot, behind a tag, so every other state hashes as
    // before.
    if (f.snap_shooter) {
        h.u64(0x73686f6full);  // "shoo"
        add(h, *f.snap_shooter);
    }
    // M97 (D-099): 15.05 / 15.06's effect only while it is in progress, behind a tag, so every other state hashes as
    // before.
    if (f.effect) {
        const core::StratagemEffect& e = *f.effect;
        h.u64(0x65666663ull);  // "effc"
        h.u64(static_cast<std::uint64_t>(e.stratagem));
        add(h, e.unit);
        h.u64(e.model ? e.model->v + 1ull : 0ull);
        h.u64(e.enemy ? e.enemy->v + 1ull : 0ull);
        h.u64(e.rolled ? 1u : 0u);
        h.i32(e.enemy_mortal_wounds);
        h.i32(e.unit_mortal_wounds);
    }
}

// The decision's kind and the unit it is about (every kind holds at most one id).
void add(Fnv1a& h, const core::PendingDecision& d) {
    h.u64(static_cast<std::uint64_t>(d.side));
    h.u64(d.kind.index());
    std::visit(
        [&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (requires { k.unit; }) {
                if constexpr (std::is_same_v<std::decay_t<decltype(k.unit)>, std::optional<core::UnitId>>) {
                    // M96 (D-097): UseStratagems' unit is optional; set (every M95 window) it hashes as before.
                    if (k.unit) {
                        add(h, *k.unit);
                    } else {
                        h.u64(0x6e756e74ull);  // "nunt"
                    }
                } else {
                    add(h, k.unit);
                }
            }
            if constexpr (requires { k.target; }) add(h, k.target);
            if constexpr (requires { k.attacker; }) add(h, k.attacker);
            if constexpr (requires { k.roll; }) h.u64(static_cast<std::uint64_t>(k.roll));  // M94: RollDice
            if constexpr (requires { k.timing; }) h.u64(static_cast<std::uint64_t>(k.timing));  // M95: UseStratagems
            if constexpr (requires { k.stratagem; }) {
                h.u64(static_cast<std::uint64_t>(k.stratagem));  // M97 (D-099): SelectModelAndEnemy
            }
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
    // M95 (D-096): the stratagem records only when one is set, behind a tag, so every other state hashes as before.
    if (!state.stratagems[0].empty() || !state.stratagems[1].empty()) {
        h.u64(0x73747274ull);  // "strt"
        for (const core::StratagemRecord& r : state.stratagems) {
            h.u64(r.used_this_phase.size());
            for (const core::Stratagem x : r.used_this_phase) h.u64(static_cast<std::uint64_t>(x));
            h.u64(r.targeted_this_phase.size());
            for (const core::UnitId u : r.targeted_this_phase) add(h, u);
            h.u64(r.used_this_battle.size());
            for (const core::Stratagem x : r.used_this_battle) h.u64(static_cast<std::uint64_t>(x));
        }
    }
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
            if (a.marker) h.u64(0x6d61726bull);  // M93 (D-094): only for a marker, so areas hash as before
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
