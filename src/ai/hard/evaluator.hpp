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

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "core/state/game_state.hpp"
#include "hard/weights.hpp"

namespace wh40k::ai::hard {

// The duel the chain models: exactly one unit with models on each side. nullopt otherwise (Hard then plays as
// Normal).
struct Duel {
    std::array<core::UnitId, 2> units;  // indexed by side_index
};
[[nodiscard]] std::optional<Duel> duel_of(const core::GameState& state);

// What a position is worth to `side`.
struct Evaluation {
    double p_win = 0.0;
    double p_draw = 0.0;
    double p_loss = 0.0;
    double v = 0.0;      // p_win - p_loss, in [-1, 1]
    double score = 0.0;  // v plus the tempo and material terms: what Hard maximizes
};

class ChainTable;

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

// The evaluator for one decision: `root` fixes the duel and the line length; any later state of the same game
// can be evaluated with it (positions are scaled onto the line).
class Evaluator {
public:
    Evaluator(analysis::OddsCache& odds, const core::GameState& root, core::Side side, ChainWeights weights,
              ChainCache* cache = nullptr);
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    [[nodiscard]] bool ok() const noexcept { return table_ != nullptr; }
    [[nodiscard]] core::Side side() const noexcept { return side_; }
    [[nodiscard]] const Duel& duel() const noexcept { return duel_; }

    // Any state of the game, by where its turn is (the flow stage): before the move, before or after the
    // shooting, a charge to decide or made, the fight, the turn over. For the half-turn in progress, our shooting
    // comes from the engine (best_shooting) and charges from the real gaps after casualties.
    [[nodiscard]] Evaluation evaluate(const core::GameState& state) const;
    // The state taken as the start of the half-turn it is in (the Command or Movement phase): the chain alone.
    [[nodiscard]] Evaluation at_turn_start(const core::GameState& state) const;
    // The chain alone at a hypothetical position (tests): half-turn `half_turn` (0 = battle round 1, first
    // turn) about to start, the units `gap_mm` apart (0: engaged) with the rooms given, by side index.
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
    // Would the side whose turn it is charge, given P(the charge comes off) = p_success?
    [[nodiscard]] bool wants_charge(const core::GameState& state, double p_success) const {
        return p_success > 0.0 && charge_values(state, p_success).wanted;
    }

    [[nodiscard]] UnitView view(const core::GameState& state, core::Side of) const;
    [[nodiscard]] int line_buckets() const noexcept;
    [[nodiscard]] static constexpr float bucket_mm() noexcept { return kBucketMm; }

private:
    static constexpr float kBucketMm = 2.0f * 25.4f;  // the range band width: 2"

    // The game is over, or a side has no models left (a hypothetical position): exact.
    [[nodiscard]] std::optional<Evaluation> terminal(const core::GameState& state) const;

    analysis::OddsCache& odds_;
    core::Side side_;
    ChainWeights weights_;
    Duel duel_{};
    std::shared_ptr<const ChainTable> table_;
};

}  // namespace wh40k::ai::hard
