#pragma once

// M44 (D-054): the Hard level's game-aware evaluator. A small Markov chain over the rest of the game (D-007: a
// wipe-out wins, surviving battle round 5 draws) gives V = P(win) - P(loss) for a position, with a draw worth 0.
//
// The chain models the duel between the two units (V1: one unit per side; duel_of says when it applies):
// - State: each unit's wounds left (with the wounded model first, 05.04, that fixes what is left of a unit whose
//   models share one W), the half-turn (battle round and turn, so whose turn it is and how many are left), and
//   the range band: the gap between the units in 2" bands (0: engaged) and how much room each unit has behind it
//   to back away (a 1D line as long as the gap plus both rooms, measured on the battlefield along the best of the
//   16 fixed directions, D-054).
// - A half-turn of the side whose turn it is, which picks what is best for it (the other side is assumed to do
//   the same in its turns, so the chain is a two-player expectimax):
//   - unengaged: a Normal move to any band it can reach (towards the enemy, staying, or back as far as its room
//     allows; [HEAVY] only after a move of no more than 3", 24.16), or an Advance (09.06: each D6 result, then the
//     band it likes best, [ASSAULT] shooting only, 10.05, and no charge); its shooting; then, unless it advanced,
//     a charge if it wants one (11.02: P(the 2D6 roll covers the gap), within 12"), and if the charge comes off
//     the fight (it strikes first, 11.04, then the defender replies);
//   - engaged: stay (close-quarters shooting, 10.06, then the fight: the side whose turn it is selects first,
//     12.04, then the other replies) or Fall Back (09.07: its M away, no shooting, no charge).
// - Every volley's outcome is the exact distribution of the target's wounds left (M40's expected_outcome) for the
//   attacker's models alive against the target's wounds left, with the weapon profile each model would pick in
//   that band (M40's unit_ranged_volley / unit_melee_volley). No rules threshold is typed here.
// - Payoffs: a win +1, a loss -1, a draw 0, plus the time preference (ChainWeights::tempo: the winner wants it
//   sooner, the loser later) and material (ChainWeights::material: wounds left at the end, a tie-break). V
//   itself is P(win) - P(loss) under the chain's play; `score` (what Hard maximizes) adds the two terms.
//
// For the half-turn in progress the evaluator uses the real position: our shooting's exact distribution from the
// engine's best_shooting, and after it the gap to the enemy model left closest (the defender removes the front
// models: M40's distance_after_casualties, the worst case for a charge).
//
// The table of chain values depends only on the two units' representative models, the starting wounds, the game
// length and who goes first, and the line length; it is built for a region (from a half-turn on, up to the
// wounds left now) and kept in a ChainCache, so later decisions of the same game reuse it. A state's value never
// depends on which table answered (the same arithmetic in the same order), so the cache never changes a decision.
//
// M57 (D-067): several units per side (the army). When either side has more than one unit with models, the
// evaluator values the army instead of a duel:
// - Each unit's main enemy (main_enemy, the real target choice): an enemy unit it is engaged with (the closest of
//   them), else the closest enemy unit (01.04, D-010; a tie goes to the first in unit order).
// - A pair's duel: the chain of the pair (an attacker, a defender) at the pair's real position (both units' wounds
//   left, the half-turn, the gap in 2" bands and the rooms to back away), with the half-turn in progress read as
//   above (the engine's best_shooting for a unit of the pair that still shoots, the charge, the fight).
// - Attention: each unit deals with the enemy units closest to it first, so its duel with a further one starts when
//   it is free of the closer ones: a timeline of each side's turns in which every attacker works on its closest
//   target still standing once in reach, at its pace (ChainTable::volley_wounds), the attackers sharing the work;
//   the duel then starts from where the attacker will stand (it closes on its first target meanwhile). A unit is
//   wiped out unless it survives every enemy unit's duel with it.
// - D-007 over the army: a side loses only when every one of its units is wiped out. With the units' fates taken as
//   independent, P(A wins) = the product over side B's units of P(that unit is wiped out), P(B wins) likewise over
//   side A's, the draw the rest (if the two add up to more than 1 they are scaled down to 1). V = P(win) - P(loss);
//   the score adds the tempo and material terms of each side's longest-lasting unit (its duel with its main enemy;
//   the army's win or loss comes when that unit falls), the mean of the two sides'. With one unit per side this is
//   the duel's own value term by term (tested), though that case keeps the duel path.
// - Tables: one per pair of unit types (representative model, keywords, starting strength), built for the whole
//   unit (every later state of any unit of those types) on a fixed line: the battlefield's diagonal in 2" bands
//   (kMaxLine covers a 60"x44" board's 74.4"). A pair's position on that line: its real gap and the smaller of its
//   two rooms exactly, the rest of the line to the larger room (the side with room to spare).
//
// M69 (D-078): the roster units.
// - One cache for the game (ChainCache) holds every pair of unit types in play (30 on the rosters) under a cap on its
//   bytes, evicting the least recently used table no Evaluator holds; missing tables are built by several workers.
// - A side whose unit type starts with more than 20 wounds gets a coarser wounds grid (a level every ceil(W / 20)
//   wounds; lookups between levels interpolate); an army whose tables would take more than 256 MiB at full precision
//   stores them as floats. Neither applies to any table of V1, the bot scenarios or Strike (their values are M68's).
// - A unit whose models differ in loadout is its own models in the chain (n alive: the n the defender keeps), not
//   copies of one; [HAZARDOUS] is priced at Normal's rate.
// - Under the points tiebreak (D-076) the army value plays for points: a finished game is the engine's result, and
//   otherwise P(neither side wiped out) x (P(ahead on points) - P(behind)) is added to D-007's terms (duel_play: no duel).
//
// M82 (D-083 part 4): the army plan of the side valued for (normal::army_plan at the decision's root, worked out once per
// decision with the enemy's, ArmyPlans; none under Generation::M77). The enemy's units keep D-067's queues: its
// coordination is not modelled (it re-plans at every decision). The lookahead's rollouts read both sides' plans for their
// shooting (lookahead.hpp).
// - A unit of one of our groups whose target has models and that has a partner with models starts its queue with the
//   target (then the others by distance), unless it is engaged with another enemy unit and not with the target. Every
//   other unit's queue is distance alone, as before.
// - The members that start on the target and take wounds from it in the timeline fight it together: each member's duel
//   is with its slice of the target (the target's wounds x the member's share of the wounds they take from it, so each
//   alone wipes its slice at the group's combined pace), and the target's P(wiped) by the group is the share-weighted
//   P(each slice is wiped), in place of those members' duels with the whole target. The target's own attacks and every
//   other pair are as before.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/odds.hpp"
#include "core/state/game_state.hpp"
#include "generation.hpp"
#include "hard/weights.hpp"
#include "normal/army_plan.hpp"

namespace wh40k::ai::hard {

// The duel the chain models: exactly one unit with models on each side. nullopt otherwise (D-067: the army path).
struct Duel {
    std::array<core::UnitId, 2> units;  // indexed by side_index
};
[[nodiscard]] std::optional<Duel> duel_of(const core::GameState& state);
// D-078: the duel Hard plays as M44's: duel_of, and D-007's result. Under the points tiebreak (D-076) Hard keeps the army
// path even with one unit each (the duel's chain plays for D-007's draw).
[[nodiscard]] std::optional<Duel> duel_play(const core::GameState& state);

// D-078: the points tiebreak in the army value. What destroying `unit` adds to its destroyer's points destroyed, in the
// engine's units (numerators over `denominator`, core::points_denominator): its points x (denominator / starting wounds)
// x the wounds it has left (core::points_destroyed counts its full points once it is destroyed, its losses so far before).
// 0 for a unit without points or already destroyed.
[[nodiscard]] std::int64_t points_increment(const core::Unit& unit, std::int64_t denominator);
// D-078: T = P(side A ahead on points at the end) - P(side B ahead), from the expected margin `mu` (A's points destroyed
// minus B's) and its variance: erf(mu / sqrt(2 variance)) by Abramowitz and Stegun 7.1.28 (arithmetic only, so the same
// on every platform); the exact sign of mu when the variance is 0 (0 for an exact tie).
[[nodiscard]] double points_lead(double mu, double variance) noexcept;

// D-067: the longest line a chain table has, in 2" bands (76": a 60"x44" board's diagonal, 74.4", fits).
[[nodiscard]] int max_line_buckets() noexcept;


// D-067: the eligible charge target closest to `unit` (within 12", 11.04 Before Moving; D-010, a tie to the first in
// unit order), nullptr if none.
[[nodiscard]] const core::Unit* closest_charge_target(const core::GameState& state, const core::Unit& unit);

// What a position is worth to `side`.
struct Evaluation {
    double p_win = 0.0;
    double p_draw = 0.0;
    double p_loss = 0.0;
    double v = 0.0;      // p_win - p_loss, in [-1, 1]
    double score = 0.0;  // v plus the tempo and material terms: what Hard maximizes
};

class ChainTable;
struct Spec;  // evaluator.cpp: a table's static description and region

// D-078: what a table is (tests and diagnostics).
struct ChainTableInfo {
    std::uint64_t key = 0;
    int h0 = 0;          // its first half-turn
    int half_turns = 0;  // the game's
    int line = 0;        // buckets
    std::array<int, 2> max_wounds{};  // the region's top, by side index
    std::array<int, 2> levels{};      // wounds grid levels (0 included), by side index
    std::array<bool, 2> exact{};      // every wound a level (else the coarse grid: over 20 starting wounds)
    bool compact = false;             // stored as floats (the game's tables at full precision over 256 MiB)
    std::size_t bytes = 0;            // its stored values
};

// Built tables, most recently used last (a cache of a pure function; D-054: never a source of behaviour). D-078: one
// cache serves the whole game (the arena's two Hard bots share one, share_with); it holds at least every pair of unit
// types the latest army evaluation needed, under a cap on its tables' bytes, evicting the least recently used table that
// no live Evaluator holds; at each decision every table not in use drops the half-turns before it (trim_before). A copy
// shares the same tables.
class ChainCache {
public:
    ChainCache();
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t builds() const noexcept;
    void clear() noexcept;
    // The stored bytes of every table kept.
    [[nodiscard]] std::size_t bytes() const noexcept;
    // D-078: the cap on bytes() (640 MiB).
    [[nodiscard]] static std::size_t cap_bytes() noexcept;
    // The table capacity now: at least 8, and every pair the latest army evaluation needed.
    [[nodiscard]] std::size_t capacity() const noexcept;
    // D-078: from now on this cache and `other` hold the same tables (the arena's two Hard bots).
    void share_with(ChainCache& other);
    // D-078: the threads that build missing tables (0: min(4, the hardware's)); never changes a value.
    void set_workers(std::size_t workers) noexcept;
    [[nodiscard]] std::size_t workers() const noexcept;
    // D-078: every table no Evaluator holds drops its half-turns before `half_turn` (no later evaluation reads them).
    void trim_before(int half_turn);
    [[nodiscard]] std::vector<ChainTableInfo> info() const;
    // D-078 (tests): each table's FNV-1a hash of every value it holds from half-turn `from` on (its nodes by half-turn,
    // wounds and position, read through its lookups), combined in sorted order.
    [[nodiscard]] std::uint64_t fingerprint(int from = 0) const;
    // D-078 (tests): another cap on bytes() than cap_bytes() (0: back to it).
    void set_cap_bytes(std::size_t cap) noexcept;

    struct Store;  // evaluator.cpp

private:
    friend class Evaluator;
    std::shared_ptr<Store> store_;
};

// D-078 (tests): the tables an Evaluator at `root` would read (their keys, regions, grids and forms; `bytes` the
// estimate), without building them.
[[nodiscard]] std::vector<ChainTableInfo> plan_chain_tables(const core::GameState& root, ChainWeights weights);

// D-080 (M73): how many tables an Evaluator at `root` would hold and their stored bytes (estimated, from its half-turn
// on): plan_chain_tables' count and the sum of its estimates, from the unit types and their wounds alone (no spec is
// built, so it is cheap enough for Hard's budget check at every decision). A pure function of the state.
struct ChainPlanSize {
    std::size_t tables = 0;
    std::size_t bytes = 0;
    friend bool operator==(const ChainPlanSize&, const ChainPlanSize&) = default;
};
[[nodiscard]] ChainPlanSize chain_plan_size(const core::GameState& root);

// The chain's view of one unit in a real state.
struct UnitView {
    int wounds = 0;          // wounds left, all models
    float room_mm = 0.0f;    // how far it could back away from the other unit (battlefield edges)
};

// D-067: one unit's fate in an army evaluation, from its duel with its main enemy (for tests and reasons).
struct UnitFate {
    core::UnitId unit;
    core::UnitId enemy;    // its main enemy
    double p_wiped = 0.0;  // P(the unit is wiped out in that duel)
};

// D-083 part 4 (M82): a decision's army plans, both sides' (normal::army_plan at its root), worked out once per decision:
// the army value reads the plan of the side it values for, the lookahead's rollouts read each side's for its shooting
// (its focus targets, D-083 part 3) instead of working it out at every Shoot decision. Inactive for a pre-teamwork
// generation (M55, M77).
struct ArmyPlans {
    std::array<normal::ArmyPlan, 2> sides{};  // by side index
};
// Both sides' plans in `root` (Generation::M55 / M77: inactive, nothing worked out).
[[nodiscard]] ArmyPlans army_plans(analysis::OddsCache& cache, const core::GameState& root, Generation generation);

// D-083 part 4 (tests, diagnostics): the army value's attention timeline at a state.
struct AttentionTarget {
    core::UnitId unit;
    std::optional<int> start;  // the attacker's turn (0: its next, or this one if it is its own) it starts on it
    double dealt = 0.0;        // the expected wounds it takes from it in the timeline
};
struct AttentionQueue {
    core::UnitId attacker;
    std::optional<core::UnitId> focus;     // its group's target, first in its queue (nullopt: distance alone)
    std::vector<AttentionTarget> targets;  // in queue order
};
// A group's target as the army value reads it: the members that start on it and take wounds from it (each with its
// slice), and its P(wiped) by them.
struct GroupPace {
    core::UnitId target;
    std::vector<core::UnitId> members;  // in unit order
    std::vector<double> shares;         // each member's share of the wounds they take from it in the timeline
    double p_wiped = 0.0;  // by the group: the members' slices, share-weighted
    double p_apart = 0.0;  // by the same members' duels with the whole target, independent (as without the plan)
};
struct Attention {
    std::vector<AttentionQueue> queues;  // every unit with models, in unit order
    // Every unit with models: the turn of the other side in which the timeline's wounds on it run out (nullopt: never
    // within the game).
    std::vector<std::pair<core::UnitId, std::optional<int>>> wiped_turn;
    std::vector<GroupPace> groups;  // in unit order of the target
    [[nodiscard]] const AttentionQueue* queue_of(core::UnitId attacker) const;
    [[nodiscard]] std::optional<int> wiped(core::UnitId unit) const;
    [[nodiscard]] const GroupPace* group_on(core::UnitId target) const;
};

// The evaluator for one decision: `root` fixes the duel and the line length (D-067: or the army's tables); any
// later state of the same game can be evaluated with it (positions are scaled onto the line).
class Evaluator {
public:
    // `force_army` (tests, D-067): with one unit per side, the army's combination of the duel (each pair read as the
    // duel reads it: the duel's table, line and stage tests), so its value can be compared with the duel's.
    // D-083 part 4 (M82): with several units on a side the army value reads `side`'s army plan, and the rollouts both
    // sides': `plans` (the decision's, worked out once by the caller at its root), else army_plans(root) at
    // `generation`. None under M55 / M77, whatever is given.
    Evaluator(analysis::OddsCache& odds, const core::GameState& root, core::Side side, ChainWeights weights,
              ChainCache* cache = nullptr, bool force_army = false, Generation generation = Generation::Current,
              const ArmyPlans* plans = nullptr);
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    [[nodiscard]] bool ok() const noexcept { return table_ != nullptr || !army_tables_.empty(); }
    // D-067: the army path (several units on a side), not the duel.
    [[nodiscard]] bool army() const noexcept { return army_; }
    [[nodiscard]] core::Side side() const noexcept { return side_; }
    [[nodiscard]] const Duel& duel() const noexcept { return duel_; }

    // Any state of the game, by where its turn is (the flow stage): before the move, before or after the
    // shooting, a charge to decide or made, the fight, the turn over. For the half-turn in progress, our shooting
    // comes from the engine (best_shooting) and charges from the real gaps after casualties.
    [[nodiscard]] Evaluation evaluate(const core::GameState& state) const;
    // The state taken as the start of the half-turn it is in (the Command or Movement phase): the chain alone.
    [[nodiscard]] Evaluation at_turn_start(const core::GameState& state) const;
    // The chain alone at a hypothetical position (tests): half-turn `half_turn` (0 = battle round 1, first
    // turn) about to start, the units `gap_mm` apart (0: engaged) with the rooms given, by side index. The duel's
    // table (D-067: the army's first table).
    [[nodiscard]] Evaluation at(int half_turn, std::array<int, 2> wounds, float gap_mm,
                                std::array<float, 2> rooms_mm) const;

    // The chain's own choice at the start of the half-turn of `state` for the side whose turn it is (the rollout
    // policy, lookahead.hpp): `advance` (an Advance, then the band per D6 result), `fall_back`, else a Normal
    // move by `towards_mm` (negative: back; 0: stay), and whether it would charge from where it ends.
    struct TurnChoice {
        bool fall_back = false;
        bool advance = false;
        std::array<float, 7> advance_towards_mm{};  // [f]: after an advance roll of f
        float towards_mm = 0.0f;
    };
    [[nodiscard]] TurnChoice turn_choice(const core::GameState& state) const;
    // D-067: the same for `unit` (of the side whose turn it is) in its duel with `target` (its main enemy when not
    // given); the duel's own choice when the evaluator is a duel's.
    [[nodiscard]] TurnChoice turn_choice(const core::GameState& state, core::UnitId unit,
                                         std::optional<core::UnitId> target = std::nullopt) const;
    // A charge by the side whose turn it is, from where it stands now: holding (the turn ends there), the fight
    // (it strikes first, 11.04; then the defender replies) and a declared charge that comes off with p_success.
    // Each for `side`.
    struct ChargeValues {
        Evaluation hold;
        Evaluation fight;
        Evaluation charge;  // p_success x fight + (1 - p_success) x hold
        bool wanted = false;  // the side whose turn it is prefers the charge
    };
    [[nodiscard]] ChargeValues charge_values(const core::GameState& state, double p_success) const;
    // D-067: a charge by `unit` (of the side whose turn it is) at `target`: the army value with no further charge
    // this phase (hold), with the two units engaged and `unit` striking first (fight), and the declared charge
    // (p_success x fight + (1 - p_success) x hold, each an army value). `wanted`: the charge is worth more to the
    // side whose turn it is. The duel's charge_values when the evaluator is a duel's.
    [[nodiscard]] ChargeValues charge_values(const core::GameState& state, core::UnitId unit, core::UnitId target,
                                             double p_success) const;
    // Would the side whose turn it is charge, given P(the charge comes off) = p_success?
    [[nodiscard]] bool wants_charge(const core::GameState& state, double p_success) const {
        return p_success > 0.0 && charge_values(state, p_success).wanted;
    }
    [[nodiscard]] bool wants_charge(const core::GameState& state, core::UnitId unit, core::UnitId target,
                                    double p_success) const {
        return p_success > 0.0 && charge_values(state, unit, target, p_success).wanted;
    }

    // D-067: `unit`'s main enemy in `state` (see the file comment); nullptr if the enemy side has no models.
    [[nodiscard]] static const core::Unit* main_enemy(const core::GameState& state, const core::Unit& unit);
    // D-067: every unit's fate in the army evaluation of `state` (empty for a duel's evaluator).
    [[nodiscard]] std::vector<UnitFate> fates(const core::GameState& state) const;
    // D-083 part 4 (tests, diagnostics): the attention timeline of the army evaluation of `state` (empty for a duel's).
    [[nodiscard]] Attention attention(const core::GameState& state) const;
    // D-083 part 4: the army plan the army value reads (side()'s; inactive: none), and both sides' (the rollouts').
    [[nodiscard]] const normal::ArmyPlan& plan() const noexcept { return plans_.sides[core::side_index(side_)]; }
    [[nodiscard]] const ArmyPlans& plans() const noexcept { return plans_; }

    [[nodiscard]] UnitView view(const core::GameState& state, core::Side of) const;
    [[nodiscard]] int line_buckets() const noexcept;
    // D-078 (tests): the tables this evaluator reads (the duel's, or the army's in pair order).
    [[nodiscard]] std::vector<ChainTableInfo> tables_info() const;
    // D-078 (tests): the chain's outcome of one volley in the table of `attacker`'s and `target`'s unit types:
    // [l] = P(the target is left at grid level l) after `attacker`'s models alive with `attacker_wounds` shoot without
    // moving at line bucket `g` (melee: fight) at `target` with `target_wounds` (a grid level's). Empty if no table.
    [[nodiscard]] std::vector<double> volley_outcome(const core::Unit& attacker, const core::Unit& target, bool melee,
                                                     int g, int attacker_wounds, int target_wounds) const;
    [[nodiscard]] static constexpr float bucket_mm() noexcept { return kBucketMm; }

private:
    static constexpr float kBucketMm = 2.0f * 25.4f;  // the range band width: 2"

    struct PairTable {
        std::uint64_t key_a = 0;  // the two unit types (D-067)
        std::uint64_t key_b = 0;
        std::shared_ptr<const ChainTable> table;
    };

    // D-067 charge_values: `unit` has charged `target` (engaged, `unit` strikes first).
    struct Charged {
        core::UnitId unit;
        core::UnitId target;
    };

    // The game is over, or a side has no models left (a hypothetical position): exact.
    [[nodiscard]] std::optional<Evaluation> terminal(const core::GameState& state) const;
    [[nodiscard]] std::optional<Evaluation> army_terminal(const core::GameState& state) const;
    // D-067: the table of the pair of unit types of `a` (side A) and `b` (side B); nullptr if none was built.
    [[nodiscard]] const ChainTable* table_for(const core::Unit& a, const core::Unit& b) const;
    // D-067: the army value of `state`. `no_charge`: that unit (of the side whose turn it is) charges no more this
    // phase; `charged`: that unit's charge at its target came off (the pair engaged, the charger strikes first);
    // `fates`, if given, gets every unit's fate.
    [[nodiscard]] Evaluation army_evaluate(const core::GameState& state,
                                           std::optional<core::UnitId> no_charge = std::nullopt,
                                           const Charged* charged = nullptr,
                                           std::vector<UnitFate>* fates = nullptr,
                                           Attention* attention = nullptr) const;
    // The cached table covering `spec`, else a new one (kept in the cache, most recently used last).
    [[nodiscard]] std::shared_ptr<const ChainTable> find_or_build(Spec spec, ChainCache* cache) const;
    // D-078: the same for several specs at once, the missing tables built by the cache's workers (each with its own odds
    // cache) and kept in spec order.
    [[nodiscard]] std::vector<std::shared_ptr<const ChainTable>> find_or_build(std::vector<Spec> specs,
                                                                             ChainCache* cache) const;

    analysis::OddsCache& odds_;
    core::Side side_;
    ChainWeights weights_;
    Duel duel_{};
    std::shared_ptr<const ChainTable> table_;
    bool army_ = false;
    bool duel_reading_ = false;  // force_army: pairs read as the duel reads them
    std::vector<PairTable> army_tables_;
    std::vector<std::pair<core::UnitId, std::uint64_t>> unit_keys_;  // D-067: each unit's type, from the root
    std::array<double, 2> start_wounds_{};  // D-067: each side's starting wounds (units with models at the root)
    ArmyPlans plans_{};  // D-083 part 4: both sides' army plans at the root (inactive: none)
};

}  // namespace wh40k::ai::hard
