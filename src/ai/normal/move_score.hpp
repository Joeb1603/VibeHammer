#pragma once

// M43 (D-054): the Normal level's whole-turn score for a Movement-phase move (09.04-09.07). This is the seam the
// M44 evaluator replaces or wraps: `score_move_plan` (cheap, for every candidate) and `refine_move_score` (from the
// state the engine leaves after the simulated move, for the few best). Both return the components separately.
//
// Value unit: shares of a unit's starting strength (value.hpp), our side positive. A ScoreLine follows both units'
// losses through a short horizon (this turn, the enemy's next turn, our next turn), cut at the game's end
// (turns_left, D-007), each volley scaled by its attacker's surviving fraction and capped at what is left of the
// target (as M42's Exchange):
// 1. Our shooting this turn from the end positions (each model's own gap to the closest enemy model decides range
//    and [RAPID FIRE] half range): [HEAVY] after Remain Stationary or a move of no more than 3" (24.16), [ASSAULT]
//    weapons only after an Advance (10.05), nothing after a Fall Back (09.07), [CLOSE-QUARTERS] while engaged
//    (10.06).
// 2. Our charge this turn (not after an Advance or a Fall Back, 09.06 / 09.07): P(the roll covers the gap to the
//    enemy model left closest after our shooting) x the charged line, else the hold line; a charge move is only
//    made when it beats holding, so the term is E[max(charge, hold)] - hold. The charged line: we fight first
//    (Fights First, 11.04), they reply; in their turn they stay (close-quarters shooting, they fight first, we
//    reply) or fall back, whichever is worse for us; in our next turn we shoot at close quarters and fight first.
// 3. The hold line: the enemy's turn, its reply worst for us of: stay ([HEAVY]), close to its half range, or close
//    by its M, then shoot and charge if its roll can cover the gap; Advance and shoot [ASSAULT] weapons; keep away
//    from us by its M (as far as the battlefield lets it) and shoot. Then our next turn's best of staying
//    ([HEAVY]), closing to our half range or closing by our M, then shooting and charging.
// 4. Time preference (game length, D-007: a wipe-out wins, surviving round 5 draws): a side that loses the
//    exchange (its best attacks against the enemy's, at half range and in melee) counts its own later losses less
//    (each half-turn later x (1 - delay x how much it is losing): it prefers to survive longer), and a side that
//    wins it counts the enemy's later losses less (it prefers to wipe the enemy out sooner).
// 5. Survival (game length again): a side losing the exchange adds survival x P(it survives the enemy's next turn)
//    (the exact distribution of models destroyed by the enemy's reply's shooting and, if it charges, its melee) x
//    how much it is losing x the game's progress (battle round / rounds): a surviving unit keeps its chance of
//    the draw. The enemy's reply is chosen with it.
// 6. Small terms: board-edge room (a unit backed against an edge can't keep away next turn: scaled by the
//    enemy's shooting threat) and coherency slack (models in groups of two or fewer, which lose a model to 03.03
//    whenever one of them is a casualty).
// Strategic knowledge (analysis::allowance_after) says what a hypothetical move allows; the live answer is the
// engine's, through refine_move_score on the simulated state (best_shooting, and charge_prospect's exact odds and
// engaged models per roll).
// D-066 (M56), several enemy units: the line above still follows the closest enemy unit ("the enemy"), and every
// other enemy unit adds (1) a target for our shooting this turn: the best unit to shoot is the one our shooting is
// worth most against (the closest one keeps a tie), and a unit we shoot replies with what is left of it; (2) its
// reply in its next turn, the worst for us of: stay and shoot ([HEAVY]), close to its half range and shoot, close by
// its M, shoot and charge if the roll covers the gap; a direct loss to our unit in the enemy's half-turn, and a
// factor on P(we survive). While we are engaged it can't shoot us (04.02) but may charge us (or fights us, if it is
// engaged with us). A unit engaged with another of our units does neither (04.02, 11.02). A side winning the
// exchange (playing for tempo) counts only an enemy unit that can reach no other of our units (its M plus its
// longest range: marginal exposure, otherwise its attacks go to one of our units whatever this move does); a side
// losing it (playing for the draw, D-007) counts one whenever this unit would be its closest target (D-010). With
// one enemy unit nothing is added: the score is M43's, term by term.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/threat.hpp"
#include "core/geometry/coherency.hpp"
#include "core/rules/move_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"
#include "generation.hpp"
#include "normal/value.hpp"

namespace wh40k::ai::normal {

// Where the moving unit's models end, and how they got there (no rules: the engine checks the move itself).
struct PlannedEnd {
    core::MoveType type = core::MoveType::RemainStationary;  // Remain Stationary, Normal, Advance or Fall Back
    std::vector<core::Position> positions;                   // every model of the unit, in unit order
    float longest_path_mm = 0.0f;                            // 24.16 [HEAVY]: no model moved more than 3"
};

// The weights (M44 tunes them; the values used are recorded in D-054).
struct MoveWeights {
    double delay = 0.5;       // time preference: x how much a side wins or loses the exchange, per half-turn
    double survival = 1.0;    // x P(we survive the enemy's next turn) x how much we lose the exchange x game progress
    double edge = 0.05;       // x (1 - room / M) x the enemy's shooting rate on us
    double coherency = 0.05;  // x the share of our models in coherency groups of two or fewer
};

struct MoveScore {
    // Components (unit shares, ours positive); total is their sum.
    double shoot_now = 0.0;  // our shooting this turn
    double charge = 0.0;     // what a charge this turn adds: E[max(charge, hold)] - hold
    double hold = 0.0;       // the rest of the horizon without our charge (the enemy's reply, our next turn)
    double edge = 0.0;
    double coherency = 0.0;
    double tie_break = 0.0;  // a side winning the exchange prefers to be closer, a losing side further (1e-5 per inch)
    double total = 0.0;
    // What the reason says.
    float gap_mm = 0.0f;               // edge to edge, the closest pair after the move
    double p_charge = 0.0;             // our charge this turn (0: none possible)
    double p_enemy_charge = 0.0;       // the enemy's charge in its best reply
    double p_survive = 1.0;            // P(our unit survives the enemy's reply), from its volleys' distributions
    int half_range_models = 0;         // our models within half range of their longest weapon ([RAPID FIRE])
    bool in_enemy_half_range = false;  // within half the enemy's longest range
    std::string enemy_reply;           // its best reply: "stays and shoots", "closes and charges", ...
    bool refined = false;              // from the simulated state (refine_move_score)
};

// Both units' losses through the horizon, valued with the time preference: their losses minus ours, each
// half-turn's losses weighted by the discount reached by then.
class ScoreLine {
public:
    ScoreLine(double us, double them, double our_discount, double their_discount)
        : x_(us, them), our_discount_(our_discount), their_discount_(their_discount) {}
    void we_hit(double rate) {
        const double before = x_.them();
        x_.we_hit(rate);
        value_ += w_them_ * (before - x_.them());
    }
    void they_hit(double rate) {
        const double before = x_.us();
        x_.they_hit(rate);
        value_ -= w_us_ * (before - x_.us());
    }
    // D-066: a loss of `share` of us from another enemy unit (already scaled by its own strength).
    void others_hit(double share) {
        const double before = x_.us();
        x_.we_lose(share);
        value_ -= w_us_ * (before - x_.us());
    }
    // The next half-turn: later losses count less for the side that prefers them later.
    void next_half_turn() {
        w_us_ *= our_discount_;
        w_them_ *= their_discount_;
    }
    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] double us() const noexcept { return x_.us(); }
    [[nodiscard]] double them() const noexcept { return x_.them(); }

private:
    Exchange x_;
    double our_discount_;
    double their_discount_;
    double w_us_ = 1.0;
    double w_them_ = 1.0;
    double value_ = 0.0;
};

// D-066 (M56): what the enemy units other than the closest one can do to our unit in their next turn, from the
// end of our move, each scaled by its strength left after our shooting this turn.
struct OtherThreat {
    double unengaged = 0.0;        // shares of our unit: their shooting and charges while we stand unengaged
    double engaged = 0.0;          // while we are engaged: their charges (fights, if engaged with us); no shooting
    double survive = 1.0;          // P(our unit survives them), unengaged
    double survive_engaged = 1.0;  // the same while engaged
};

// D-066: our shooting this turn at an enemy unit other than the closest one (index into the context's others).
struct OtherShot {
    std::size_t index = 0;
    double share = 0.0;  // as a share of that unit
    double models_destroyed = 0.0;
};

// Everything the score needs about the position before the move: the two units, their threat ranges and rates,
// the turns left. Rates are cached by what changes them (range and half-range bands, [HEAVY], shooting type), so
// a candidate costs a few lookups and one exact outcome for our shooting.
class MoveScoreContext {
public:
    // `origin`: the state before the move (MoveUnits pending, or the move just started with no model moved). The
    // enemy is the enemy unit closest to `unit` there. `origin` must outlive the context. D-066: the other enemy
    // units are weighed too, unless `generation` is M55 (the regression switch).
    MoveScoreContext(analysis::OddsCache& cache, const core::GameState& origin, core::UnitId unit,
                     MoveWeights weights = {}, Generation generation = Generation::Current);

    [[nodiscard]] const core::GameState& origin() const noexcept { return origin_; }
    [[nodiscard]] const core::Unit& unit() const noexcept { return us_; }
    [[nodiscard]] const core::Unit* enemy() const noexcept { return enemy_ ? &*enemy_ : nullptr; }
    [[nodiscard]] analysis::OddsCache& cache() noexcept { return cache_; }
    [[nodiscard]] const MoveWeights& weights() const noexcept { return weights_; }
    [[nodiscard]] Generation generation() const noexcept { return generation_; }
    [[nodiscard]] int turns() const noexcept { return turns_; }
    [[nodiscard]] bool start_engaged() const noexcept { return start_engaged_; }
    [[nodiscard]] const analysis::ThreatRanges& our_threat() const noexcept { return us_threat_; }
    [[nodiscard]] const analysis::ThreatRanges& enemy_threat() const noexcept { return them_threat_; }
    // The time preference: our and their per-half-turn loss discounts (1: no preference).
    [[nodiscard]] double our_discount() const noexcept { return our_discount_; }
    [[nodiscard]] double their_discount() const noexcept { return their_discount_; }
    // How much we win (positive) or lose (negative) the exchange, -1..1.
    [[nodiscard]] double lean() const noexcept { return lean_; }
    [[nodiscard]] ScoreLine new_line() const;

    // ---- the pieces score_move_plan is made of (shares of the target unit at full strength) ----
    // A whole unit's shooting across a unit gap (the enemy's reply, our next turn).
    [[nodiscard]] double ranged_rate(bool ours, core::ShootingType type, bool heavy, float gap_mm);
    [[nodiscard]] double melee_rate(bool ours);
    [[nodiscard]] double close_quarters_rate(bool ours);
    // Our shooting this turn from `positions`, each model at its own gap; `outcome` gets the exact outcome.
    [[nodiscard]] double our_shooting(const std::vector<core::Position>& positions, core::ShootingType type, bool heavy,
                                      analysis::AttackOutcome* outcome = nullptr, int* half_range_models = nullptr);
    // Edge to edge from each enemy model to the closest of our models at `positions`, closest first.
    [[nodiscard]] std::vector<float> enemy_gaps(const std::vector<core::Position>& positions) const;
    // How far the models at `positions` (ours or the enemy's) could still get away from `from` (a centroid) next
    // turn, as far as the battlefield allows (along the best of the fixed 16 directions, D-054).
    [[nodiscard]] float room_away(const std::vector<core::Position>& positions, bool ours,
                                  const core::Position& from) const;
    [[nodiscard]] core::Position enemy_centroid() const noexcept { return enemy_centroid_; }

    // The enemy's turn and our next turn from `line` (after our turn), the units `gap_mm` apart and unengaged,
    // our models' centroid at `our_centroid`. `reply` / `p_enemy_charge` get the enemy's reply. D-066: `others`, the
    // other enemy units' threat, if any.
    [[nodiscard]] double hold_line(const ScoreLine& line, float gap_mm, const core::Position& our_centroid,
                                   std::string* reply = nullptr, double* p_enemy_charge = nullptr,
                                   double* p_survive = nullptr, const OtherThreat* others = nullptr);
    // The units engaged after our charge (or a combat we stay in): our fight now (`our_fighters`: the share of
    // our models that fight, we strike first), then their turn (they fight on or fall back) and ours.
    [[nodiscard]] double engaged_line(const ScoreLine& line, double our_fighters = 1.0, double* p_survive = nullptr,
                                      const OtherThreat* others = nullptr);

    // ---- D-066 (M56): the enemy units other than enemy() ----
    [[nodiscard]] std::size_t other_count() const noexcept { return others_.size(); }
    [[nodiscard]] const core::Unit& other(std::size_t i) const { return others_[i].unit; }
    // Our shooting this turn from `positions` at other(i), each model at its own gap (0 if it is engaged, 04.02).
    [[nodiscard]] double our_shooting_at_other(std::size_t i, const std::vector<core::Position>& positions,
                                               core::ShootingType type, bool heavy,
                                               analysis::AttackOutcome* outcome = nullptr);
    // The best of our_shooting_at_other over the others, if it is worth more than `closest_share` (ours at enemy()).
    [[nodiscard]] std::optional<OtherShot> better_other_shot(const std::vector<core::Position>& positions,
                                                             core::ShootingType type, bool heavy, double closest_share);
    // Their threat to our unit standing at `positions` after our move; `shot`: the one we shoot this turn, if any.
    [[nodiscard]] OtherThreat other_threat(const std::vector<core::Position>& positions,
                                           const std::optional<OtherShot>& shot);
    // The survival weight (0 for a side that isn't losing the exchange).
    [[nodiscard]] double survival_weight() const noexcept { return survival_weight_; }

private:
    struct RateKey {
        bool ours;
        int type;
        bool heavy;
        std::uint64_t band;
        friend auto operator<=>(const RateKey&, const RateKey&) = default;
    };
    [[nodiscard]] std::uint64_t band_of(bool ours, float gap_mm) const;
    [[nodiscard]] double our_next_turn(const ScoreLine& line, float gap_mm);
    [[nodiscard]] double enemy_turn_from(const ScoreLine& line, float from_mm, float to_mm, double* p_charge,
                                         double* p_survive, const OtherThreat* others = nullptr);
    // P(the enemy's whole-unit shooting / melee / close-quarters shooting wipes out our unit as it is now).
    [[nodiscard]] double enemy_ranged_wipe(core::ShootingType type, bool heavy, float gap_mm);
    [[nodiscard]] double enemy_melee_wipe();
    [[nodiscard]] double enemy_engaged_wipe();

    analysis::OddsCache& cache_;
    const core::GameState& origin_;
    MoveWeights weights_;
    Generation generation_ = Generation::Current;
    core::Unit us_;
    std::optional<core::Unit> enemy_;
    std::vector<core::PlacedModel> enemy_models_;
    core::Position enemy_centroid_;
    analysis::TargetSnapshot enemy_snap_;
    std::vector<float> our_ranges_;  // distinct usable ranged ranges (band_of)
    std::vector<float> enemy_ranges_;
    analysis::ThreatRanges us_threat_;
    analysis::ThreatRanges them_threat_;
    int turns_ = 0;
    bool start_engaged_ = false;
    bool enemy_engaged_elsewhere_ = false;  // with another of our units: it neither shoots us nor charges (04.02)
    double our_discount_ = 1.0;
    double survival_weight_ = 0.0;
    double lean_ = 0.0;
    std::map<RateKey, double> wipes_;
    std::optional<double> melee_wipe_;
    std::optional<double> engaged_wipe_;
    double their_discount_ = 1.0;
    std::map<RateKey, double> rates_;
    std::array<std::optional<double>, 2> melee_;
    std::array<std::optional<double>, 2> cq_;
    // (model index, shooting type, heavy, band) -> that model's best volley alone
    std::map<std::tuple<std::size_t, int, bool, std::uint64_t>, analysis::Volley> model_volleys_;

    // D-066: an enemy unit other than enemy(), with its rates against us and ours against it.
    struct Other {
        core::Unit unit;
        std::vector<core::PlacedModel> models;
        analysis::TargetSnapshot snap;
        analysis::ThreatRanges threat;
        std::vector<float> ranges;  // its distinct usable ranged ranges (bands of its shooting at us)
        bool locked = false;        // engaged with another of our units: it neither shoots us nor charges (04.02)
        float other_target_mm = 0.0f;  // its gap to the closest of our other units (its other targets), or far
        bool engaged = false;       // engaged at all: we can't shoot it (04.02)
        double melee = 0.0;         // its melee on us, shares of our unit
        double cq = 0.0;            // its close-quarters shooting on us
        double melee_wipe = 0.0;    // P(its melee wipes us out)
        std::map<std::pair<bool, std::uint64_t>, std::pair<double, double>> ranged;  // (heavy, band) -> (rate, wipe)
        // (model index, shooting type, heavy, our band) -> that model's best volley at it
        std::map<std::tuple<std::size_t, int, bool, std::uint64_t>, analysis::Volley> our_volleys;
        // (shooting type, heavy, every model's band) -> (share, models destroyed)
        std::map<std::tuple<int, bool, std::vector<std::uint64_t>>, std::pair<double, double>> our_shots;
    };
    [[nodiscard]] std::uint64_t other_band(const Other& o, float gap_mm) const;
    [[nodiscard]] std::pair<double, double> other_ranged(Other& o, bool heavy, float gap_mm);
    [[nodiscard]] float gap_to_other(const Other& o, const std::vector<core::Position>& positions) const;
    std::vector<Other> others_;
};

// The whole-turn score of ending the move at `plan` (cheap: every candidate gets one).
[[nodiscard]] MoveScore score_move_plan(MoveScoreContext& ctx, const PlannedEnd& plan);

// The same from the engine's own answers in `after` (the state after the simulated move was committed, D-054):
// the shooting from best_shooting, and, when `with_charge_prospect` and a charge is possible, charge_prospect's
// exact odds per roll and the models each roll engages (with our shooting's expected kills taken off the enemy's
// front). D-066: best_shooting at every enemy unit, the best one shot (the kills come off that unit's front).
[[nodiscard]] MoveScore refine_move_score(MoveScoreContext& ctx, const PlannedEnd& plan, const core::GameState& after,
                                          std::uint64_t seed, bool with_charge_prospect);

}  // namespace wh40k::ai::normal
