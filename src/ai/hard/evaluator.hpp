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
#include "hard/weights.hpp"

namespace wh40k::ai::hard {

// The duel the chain models: exactly one unit with models on each side. nullopt otherwise (D-067: the army path).
struct Duel {
    std::array<core::UnitId, 2> units;  // indexed by side_index
};
[[nodiscard]] std::optional<Duel> duel_of(const core::GameState& state);

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

// Built tables, most recently used last (a bounded cache of a pure function; D-054: never a source of behaviour).
class ChainCache {
public:
    [[nodiscard]] std::size_t size() const noexcept { return tables_.size(); }
    [[nodiscard]] std::size_t builds() const noexcept { return builds_; }
    void clear() noexcept { tables_.clear(); }

private:
    friend class Evaluator;
    std::vector<std::shared_ptr<const ChainTable>> tables_;
    std::size_t builds_ = 0;
};

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

// The evaluator for one decision: `root` fixes the duel and the line length (D-067: or the army's tables); any
// later state of the same game can be evaluated with it (positions are scaled onto the line).
class Evaluator {
public:
    // `force_army` (tests, D-067): with one unit per side, the army's combination of the duel (each pair read as the
    // duel reads it: the duel's table, line and stage tests), so its value can be compared with the duel's.
    Evaluator(analysis::OddsCache& odds, const core::GameState& root, core::Side side, ChainWeights weights,
              ChainCache* cache = nullptr, bool force_army = false);
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

    [[nodiscard]] UnitView view(const core::GameState& state, core::Side of) const;
    [[nodiscard]] int line_buckets() const noexcept;
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
                                           std::vector<UnitFate>* fates = nullptr) const;
    // The cached table covering `spec`, else a new one (kept in the cache, most recently used last).
    [[nodiscard]] std::shared_ptr<const ChainTable> find_or_build(Spec spec, ChainCache* cache) const;

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
};

}  // namespace wh40k::ai::hard
