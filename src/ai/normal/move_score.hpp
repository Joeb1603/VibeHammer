#pragma once

// M43 (D-054): the Normal level's whole-turn score for a Movement-phase move (09.04-09.07): `score_move_plan` (cheap,
// for every candidate) and `refine_move_score` (from the state the engine leaves after the simulated move, for the few
// best). Both return the components separately.
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
// D-077 (M68): shares are weighted by points (share_weight: only with points and a side of several unit types).
// Under the points tiebreak (D-076) there is no draw to play for and no wipe-out needed to win: the time preference
// is off (both discounts 1), the survival term counts only for its side's last unit, scaled by what surviving buys on
// the engine's exact points tally (a win 2, a draw 1, nothing when behind: (1 + R) times D-007's), and every side counts
// the other units' replies by marginal exposure. The hypothetical volleys apply 10.06's and 17.03's -1s and price
// [HAZARDOUS] (analysis::ShotContext); an engaged enemy MONSTER/VEHICLE may be shot (17.03) and still shoots
// (10.06), and a MONSTER/VEHICLE of ours can be shot while engaged (17.03).
// D-083 part 2 (M80): a unit that follows its group in the army plan (army_plan.hpp: it has partners, and the group's
// gain is at least MoveWeights::follow of its risk; under D-007 not while it loses its exchange with the focus) moves as
// a member of it. While some partner reaches the focus by our next turn (its arrival in the plan: the synchronised
// partners) the line follows the focus, and the enemy unit closest to it becomes one of the others; the synchronised
// partners' expected volleys on the focus this turn and in our next turn take it down on the line
// (ScoreLine::allies_hit: it replies weaker, and what is left of it caps our own hits), and its reply is shared: 1/k on
// this unit, k = 1 + the synchronised partners in its reach (it attacks one unit a phase). Until then the unit keeps its
// closest enemy unit's line (so it waits at the edge of the focus's reach for its partners). Two small terms
// (MoveWeights): approach, x the turns saved towards the focus beyond the line's horizon (where it scores about 0 for
// every move), and cohesion, x the gap to the nearest partner beyond 6". A unit that follows no group scores as before.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/terrain_sight.hpp"
#include "analysis/threat.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/rules/move_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"
#include "normal/army_plan.hpp"
#include "normal/objectives.hpp"
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
    // D-083 part 2 (M80): only for a unit that follows its group in the army plan (nothing for any other unit).
    double approach = 0.05;   // x the turns saved towards the focus beyond the line's horizon
    double cohesion = 0.005;  // x max(0, the gap to the nearest partner - 6"), per inch
    // a unit follows its group when the group's gain is at least this share of its risk (M83: 0.1, was 0.2)
    double follow = 0.1;
    // D-083 part 5 (M83): x the turns saved towards the closest enemy unit beyond the line's horizon, for a unit that
    // follows no group and whose reach (Move plus its longest range, at least 7") is no shorter than that unit's.
    double approach_alone = 0.05;
    // M87 (D-086), under a mission only (with both 0 a mission game plays exactly as before M87): x the VP rate
    // (vp_share) on the objective terms, the enemy scoring a move denies and ours it holds through the enemy's turn ...
    double objective = 1.0;
    // ... and x an area's value per scoring (VP x vp_share) x the turns saved towards the area the plan sends a unit
    // with no group to, beyond its Move.
    double objective_approach = 0.1;
    // ... and in the Shooting phase, x the enemy scorings the shooting helps deny (ShootingAim, shooting.hpp).
    double objective_fire = 0.0;
    // M89 (D-090), on a battlefield with terrain only (at 0 it plays exactly as before M89): how far the move score
    // believes what terrain does to its hypothetical shots (13.08 cover, 13.09 Hidden, 13.10 / 13.11 sight), 0 to 1:
    // each such rate is (1 - cover) x the terrain-blind rate + cover x the terrain rate. Above 0 the candidates also
    // take spots in cover, hidden spots and detours, and hand deployment prefers cover.
    double cover = 1.0;
};

// M87 (D-086): the objective terms are on: a mission, and an objective weight (objective, objective_approach or
// objective_fire) above 0.
[[nodiscard]] bool objectives_on(const core::GameState& state, const MoveWeights& weights);
// M89 (D-090): the terrain terms are on: the battlefield has terrain and the cover weight is above 0.
[[nodiscard]] bool cover_on(const core::GameState& state, const MoveWeights& weights);

struct MoveScore {
    // Components (unit shares, ours positive); total is their sum.
    double shoot_now = 0.0;  // our shooting this turn
    double charge = 0.0;     // what a charge this turn adds: E[max(charge, hold)] - hold
    double hold = 0.0;       // the rest of the horizon without our charge (the enemy's reply, our next turn)
    double edge = 0.0;
    double coherency = 0.0;
    double tie_break = 0.0;  // a side winning the exchange prefers to be closer, a losing side further (1e-5 per inch)
    double approach = 0.0;   // D-083 part 2 (M80): a unit with partners only
    double cohesion = 0.0;
    double objective = 0.0;  // M87 (D-086): the objective terms (the enemy scoring denied, ours held), under a mission
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
    // M89 (D-090), under cover_on only (for the reason): the unit has the benefit of cover against the closest enemy
    // unit's reply where it stands, is hidden from it in its turn, or can't be seen by it from where it stands now.
    bool in_cover = false;
    bool hidden = false;
    bool unseen = false;
    // D-083 part 2 (M80), a unit that follows its group: its focus when the line follows it, and the share of the
    // focus's reply counted on it (1: it stands alone; 1/k: it shares the reply with k - 1 synced partners in its reach).
    std::optional<core::UnitId> focus;
    double reply_share = 1.0;
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
    // D-083 part 2 (M80): a loss of `share` of them from our partners' volleys (already scaled by their strength). It is
    // not this unit's value (the partners hit the focus whatever this move does): it weakens the focus's replies and
    // caps our later hits.
    void allies_hit(double share) { x_.they_lose(share); }
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

// D-083 part 2 (M80): the group a unit moves with, from the army plan (MoveScoreContext::team()).
struct MoveTeam {
    struct Partner {
        core::UnitId unit;
        int arrival = kNoArrival;            // the plan's: our turns until it can attack the focus
        AttackMode mode = AttackMode::Shoot;
        float gap_mm = 0.0f;                 // to the focus now, edge to edge
        float catch_up_mm = 0.0f;            // its Move if it hasn't moved yet this phase (cohesion), else 0
        bool synced = false;                 // it reaches the focus by our next turn (arrival <= 1)
        double now = 0.0;                    // its expected volley on the focus this turn (shares of the focus)
        double next = 0.0;                   // and in our next turn
        std::vector<core::PosedModel> models;
    };
    core::UnitId focus;
    bool line = true;                        // the line follows the focus (else the closest enemy unit's, as before)
    std::vector<core::PosedModel> focus_models;
    AttackMode mode = AttackMode::Shoot;     // ours, in the plan
    std::vector<Partner> partners;           // unit order
    bool synced = false;                     // some partner reaches the focus by our next turn
    double support_now = 0.0;                // the synced partners' volleys this turn, summed
    double support_next = 0.0;               // and in our next turn
    float start_gap_mm = 0.0f;               // ours to the focus at the move's start
    float reach_mm = 0.0f;                   // approach: the line's horizon, our Move plus our attack's reach
    float per_turn_mm = 0.0f;                // approach: a turn's travel (M plus the D6's mean)
    // D-083 part 5 (M83): no group: the approach term alone (MoveWeights::approach_alone) towards the closest enemy
    // unit, which the line follows as before; no partners, no support.
    bool alone = false;
    // M87 (D-086): no group, and an area set in the plan: the approach heads for the area instead (centroid to its edge,
    // MoveWeights::objective_approach); the line stays the closest enemy unit's (`focus`), with no partners.
    std::optional<std::size_t> area;  // index into Mission::objectives
    core::Position area_centre;
    float area_radius_mm = 0.0f;
    double area_value = 0.0;  // one scoring of it for us, in shares (VP x vp_share)
    std::string area_text;    // "Centre (take)"
};

// Everything the score needs about the position before the move: the two units, their threat ranges and rates,
// the turns left. Rates are cached by what changes them (range and half-range bands, [HEAVY], shooting type), so
// a candidate costs a few lookups and one exact outcome for our shooting.
class MoveScoreContext {
public:
    // `origin`: the state before the move (MoveUnits pending, or the move just started with no model moved). The
    // enemy is the enemy unit closest to `unit` there. `origin` must outlive the context. D-066: the other enemy
    // units are weighed too.
    // D-083 part 2 (M80): `plan`, the army plan of `unit`'s side in `origin` (army_plan(cache, origin, side): a caller
    // that has it passes it, so it is computed once per decision); nullptr: computed here. A unit with partners in it
    // follows its focus instead (see the file comment).
    MoveScoreContext(analysis::OddsCache& cache, const core::GameState& origin, core::UnitId unit,
                     MoveWeights weights = {}, const ArmyPlan* plan = nullptr);

    [[nodiscard]] const core::GameState& origin() const noexcept { return origin_; }
    [[nodiscard]] const core::Unit& unit() const noexcept { return us_; }
    // D-074 part 3: every model of unit() has a round base (the round-only code paths, as before M66).
    [[nodiscard]] bool unit_round() const noexcept { return us_round_; }
    [[nodiscard]] const core::Unit* enemy() const noexcept { return enemy_ ? &*enemy_ : nullptr; }
    [[nodiscard]] analysis::OddsCache& cache() noexcept { return cache_; }
    [[nodiscard]] const MoveWeights& weights() const noexcept { return weights_; }
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
    // A whole unit's shooting across a unit gap (the enemy's reply, our next turn). M89 (D-090): `cover`, the target
    // has the benefit of cover (13.08).
    [[nodiscard]] double ranged_rate(bool ours, core::ShootingType type, bool heavy, float gap_mm, bool cover = false);
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
    // D-083 part 2 (M80): `reply_elsewhere`, the focus replies to a partner, not to us (the shared reply).
    // M87 (D-086): `us_left`, what is left of our unit after the enemy's turn on the chosen reply (read only: no value
    // changes).
    [[nodiscard]] double hold_line(const ScoreLine& line, float gap_mm, const core::Position& our_centroid,
                                   std::string* reply = nullptr, double* p_enemy_charge = nullptr,
                                   double* p_survive = nullptr, const OtherThreat* others = nullptr,
                                   bool reply_elsewhere = false, double* us_left = nullptr);
    // The units engaged after our charge (or a combat we stay in): our fight now (`our_fighters`: the share of
    // our models that fight, we strike first), then their turn (they fight on or fall back) and ours.
    [[nodiscard]] double engaged_line(const ScoreLine& line, double our_fighters = 1.0, double* p_survive = nullptr,
                                      const OtherThreat* others = nullptr, double* us_left = nullptr);

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
    // D-077 (M68): the scenario decides on points destroyed (D-076), and whether the side plays for D-007's draw (a
    // side losing the exchange without the tiebreak: D-066's closest-target rule for the other units' replies).
    [[nodiscard]] bool points_rule() const noexcept { return points_rule_; }
    [[nodiscard]] bool plays_for_draw() const noexcept { return !points_rule_ && lean_ < 0.0; }
    // D-083 part 2 (M80): the unit's group (nullptr: it has no partners).
    [[nodiscard]] const MoveTeam* team() const noexcept { return team_ ? &*team_ : nullptr; }
    // The terms of a unit with partners (0 without): approach and cohesion, and whether the focus's reply counts on
    // the unit at `positions` (its gap to the focus `gap_mm`).
    // `line_gap_mm`: the closest pair's gap to the line's enemy after the move, when the caller has it; a unit closing
    // in alone (its focus is that enemy) reads it instead of measuring again.
    [[nodiscard]] double approach_term(const std::vector<core::Position>& positions,
                                       std::optional<float> line_gap_mm = std::nullopt) const;
    // The synced partners support the line (it follows the focus and some partner reaches it by our next turn).
    [[nodiscard]] bool supported() const noexcept { return team_ && team_->line && team_->synced; }
    [[nodiscard]] double cohesion_term(const std::vector<core::Position>& positions) const;
    [[nodiscard]] double reply_share(float gap_mm) const;
    // M87 (D-086): the objective areas as this move sees them (nullopt: no mission, objective weight 0, or no scoring
    // ahead), and whether the candidates add spots on the areas (objectives_on).
    [[nodiscard]] const std::optional<ObjectiveOutlook>& objectives() const noexcept { return objectives_; }
    [[nodiscard]] bool area_spots() const noexcept { return area_spots_; }
    // M89 (D-090): the terrain terms are on (cover_on), so the candidates add cover and hidden spots and detours.
    [[nodiscard]] bool cover_spots() const noexcept { return sight_ != nullptr; }
    // M89 (D-090): how our unit at `positions` stands against the enemy's reply in its next turn, for the rates below
    // until end_reply (score_move_plan sets it per plan; nothing happens without cover_on). `shoots`: the plan makes
    // ranged attacks this turn (then our unit is not hidden in the enemy's turn, 13.09). Returns the closest enemy
    // unit's view of us, for the reason.
    struct ReplyFacts {
        bool in_cover = false;
        bool hidden = false;
        bool unseen = false;
    };
    ReplyFacts begin_reply(const std::vector<core::Position>& positions, bool shoots);
    void end_reply() { reply_ = Reply{}; }

private:
    struct RateKey {
        bool ours;
        int type;
        bool heavy;
        std::uint64_t band;
        bool cover = false;  // M89 (D-090)
        friend auto operator<=>(const RateKey&, const RateKey&) = default;
    };
    [[nodiscard]] std::uint64_t band_of(bool ours, float gap_mm) const;
    [[nodiscard]] double our_next_turn(const ScoreLine& line, float gap_mm);
    [[nodiscard]] double enemy_turn_from(const ScoreLine& line, float from_mm, float to_mm, double* p_charge,
                                         double* p_survive, const OtherThreat* others = nullptr,
                                         double* us_left = nullptr);
    // P(the enemy's whole-unit shooting / melee / close-quarters shooting wipes out our unit as it is now).
    [[nodiscard]] double enemy_ranged_wipe(core::ShootingType type, bool heavy, float gap_mm, bool cover = false);
    // M89 (D-090): the enemy's reply on us (closest enemy) at `to_mm`, `stays` its models where they stand: the rate and
    // P(it wipes us out), blended for terrain while a reply is begun (else exactly the terrain-blind calls).
    [[nodiscard]] double reply_rate(core::ShootingType type, bool heavy, float to_mm, bool stays);
    [[nodiscard]] double reply_wipe(core::ShootingType type, bool heavy, float to_mm, bool stays);
    // Our whole unit's shooting at enemy() in our next turn, the enemy's area cover where it stands blended in.
    [[nodiscard]] double our_rate(core::ShootingType type, bool heavy, float gap_mm);
    [[nodiscard]] double blend(double plain, double terrain) const noexcept {
        return (1.0 - cover_w_) * plain + cover_w_ * terrain;
    }
    [[nodiscard]] double enemy_melee_wipe();
    [[nodiscard]] double enemy_engaged_wipe();

    // D-077 (M68): a hypothetical shot of ours (`ours`) at enemy() or of enemy() at us, [HAZARDOUS] priced.
    [[nodiscard]] analysis::ShotContext shot(bool ours, core::ShootingType type, bool heavy, float gap_mm) const;
    // D-077: enemy() engaged with another of our units still shoots us if it is a MONSTER/VEHICLE (10.06: its
    // MONSTER/VEHICLE models in close-quarters shooting, at -1): (share of us, P(it wipes us out)).
    [[nodiscard]] std::pair<double, double> enemy_elsewhere_shooting(float gap_mm);

    analysis::OddsCache& cache_;
    const core::GameState& origin_;
    MoveWeights weights_;
    bool points_rule_ = false;  // D-077: the points tiebreak (D-076)
    double rate_us_ = 0.0;      // D-077: hazard_rate of our unit against enemy(), and of enemy() against us
    double rate_them_ = 0.0;
    bool us_mv_ = false;        // D-077: our unit is a MONSTER/VEHICLE (17.03: it can be shot while engaged)
    bool enemy_mv_ = false;     // enemy() is a MONSTER/VEHICLE
    std::map<std::uint64_t, std::pair<double, double>> elsewhere_;  // band -> enemy_elsewhere_shooting
    core::Unit us_;
    std::optional<core::Unit> enemy_;
    std::vector<core::PosedModel> enemy_models_;  // D-074 part 3: with facings
    bool us_round_ = true;     // D-074 part 3: every model of ours is round (the round-only code, as before)
    bool enemy_round_ = true;  // every model of enemy() is round
    std::vector<core::BaseExtents> us_extents_;     // D-074 part 3: our models' half extents at their facings
    std::vector<core::BaseExtents> enemy_extents_;  // enemy()'s models' half extents at their facings
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
    // (model index, shooting type, heavy, band, cover) -> that model's best volley alone
    std::map<std::tuple<std::size_t, int, bool, std::uint64_t, bool>, analysis::Volley> model_volleys_;

    // M89 (D-090): terrain, under cover_on only (sight_ null otherwise: nothing below is used).
    // An enemy unit's models as the sight tests see them (where they stand now).
    struct TerrainSide {
        std::vector<std::uint32_t> masks;  // each model's areas
        std::vector<char> hidden;          // 13.09 now
        std::vector<char> area_ok;         // it takes cover and is within an area (13.08's first condition)
        bool area_cover = false;           // every model area_ok: cover against every attacker
        std::vector<std::size_t> samples;  // the models sampled against our unit (its start centroid)
    };
    [[nodiscard]] TerrainSide terrain_side(const core::Unit& unit, const std::vector<core::PosedModel>& models);
    // Our model at `at` looking at a unit `side` describes: (some sampled model visible, the target has cover).
    [[nodiscard]] std::pair<bool, bool> our_view(const core::BasePose& at, const std::vector<core::PosedModel>& models,
                                                 const TerrainSide& side);
    // The enemy unit `models` / `side` looking at our unit as reply_ holds it, from where it stands: (visible, cover).
    [[nodiscard]] std::pair<bool, bool> stays_view(const std::vector<core::PosedModel>& models, const TerrainSide& side);
    // Visible and covered at an end the enemy moved to (gap `to_mm`).
    [[nodiscard]] bool moved_visible(float to_mm) const;
    struct Reply {
        bool active = false;
        bool area_cover = false;   // every model of ours takes cover within an area
        bool all_hidden = false;   // every sampled model of ours is hidden in the enemy's turn
        std::vector<core::BasePose> poses;  // our sampled models
        std::vector<std::uint32_t> masks;
        std::vector<char> hidden;
        bool stay_visible = true;  // the closest enemy unit, where it stands
        bool stay_cover = false;
    };
    std::shared_ptr<analysis::TerrainSight> sight_;
    double cover_w_ = 0.0;
    bool us_takes_cover_ = false;
    TerrainSide enemy_side_;
    Reply reply_;

    // D-066: an enemy unit other than enemy(), with its rates against us and ours against it.
    struct Other {
        core::Unit unit;
        std::vector<core::PosedModel> models;
        bool round = true;  // D-074 part 3: every model of it is round
        analysis::TargetSnapshot snap;
        analysis::ThreatRanges threat;
        std::vector<float> ranges;  // its distinct usable ranged ranges (bands of its shooting at us)
        bool locked = false;        // engaged with another of our units: it neither shoots us nor charges (04.02)
        float other_target_mm = 0.0f;  // its gap to the closest of our other units (its other targets), or far
        bool engaged = false;       // engaged at all: we can't shoot it (04.02; D-077: unless a MONSTER/VEHICLE, 17.03)
        bool mv = false;            // D-077: a MONSTER/VEHICLE unit
        double rate_us = 0.0;       // D-077: hazard_rate of our unit against it, and of it against us
        double rate_them = 0.0;
        double melee = 0.0;         // its melee on us, shares of our unit
        double cq = 0.0;            // its close-quarters shooting on us
        double melee_wipe = 0.0;    // P(its melee wipes us out)
        // (heavy, band, cover) -> (rate, wipe)
        std::map<std::tuple<bool, std::uint64_t, bool>, std::pair<double, double>> ranged;
        // D-077: band -> its shooting at us while we are an engaged MONSTER/VEHICLE (17.03, at -1): (rate, wipe)
        std::map<std::uint64_t, std::pair<double, double>> at_engaged_us;
        // (model index, shooting type, heavy, our band, cover) -> that model's best volley at it
        std::map<std::tuple<std::size_t, int, bool, std::uint64_t, bool>, analysis::Volley> our_volleys;
        // (shooting type, heavy, every model's band, M89: every model's sight, empty terrain-blind) -> (share, models
        // destroyed)
        std::map<std::tuple<int, bool, std::vector<std::uint64_t>, std::vector<std::uint8_t>>, std::pair<double, double>>
            our_shots;
        TerrainSide terrain;  // M89 (D-090), under cover_on
    };
    [[nodiscard]] std::uint64_t other_band(const Other& o, float gap_mm) const;
    [[nodiscard]] std::pair<double, double> other_ranged(Other& o, bool heavy, float gap_mm, bool cover = false);
    // D-077: its shooting at us while we are an engaged MONSTER/VEHICLE (17.03, at -1), cached by band.
    [[nodiscard]] std::pair<double, double> other_at_engaged_us(Other& o, float gap_mm);
    [[nodiscard]] float gap_to_other(const Other& o, const std::vector<core::Position>& positions) const;
    std::vector<Other> others_;
    // D-083 part 2 (M80)
    void make_team(const ArmyPlan& plan, const core::Unit& focus, bool line);
    void make_lone_approach(const core::Unit& closest);
    std::optional<MoveTeam> team_;
    // M87 (D-086)
    void make_area_approach(const ArmyPlan& plan, const AreaSet& set);
    std::optional<ObjectiveOutlook> objectives_;
    bool area_spots_ = false;
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
