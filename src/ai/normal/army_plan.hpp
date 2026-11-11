#pragma once

// D-083 part 1 (M79): the army plan. For each of a side's units with models: an optional focus (an enemy unit) and its
// partners (our units with the same focus), from a greedy assignment of small groups to enemy units whose joint
// exchange pays more than the members get apart. A pure, deterministic function of the state (the bots stay
// stateless), recomputed by each decision that reads it (M80 moves, M81 shooting and charges, M82 Hard's army value).
//
// - Reach (per pair, no odds): our turn 0 is the current turn if it is ours, else our next one; a unit advances (M plus
//   the D6's mean, 09.06) in the turns before it attacks and makes a Normal move in the turn it attacks. It may shoot
//   once the gap is within its longest usable range, and charge once a 2D6 roll covers the gap left with P >= 0.5.
// - Modes, one per pair (the better lone exchange): Shoot (it stands at its longest range), Charge (shoot, charge,
//   engaged from then on) or Engaged (engaged with the target now).
// - The exchange of a group (1 to 3 units) with one target: MultiExchange with the roles turned round (one defender,
//   several attackers), strengths as the wounds left (model equivalents) in points-weighted shares (D-077), over three of
//   our volleys from the group's first contact and the target's replies (each the one worst for us, before contact too),
//   cut at the game's end. Fights strike both ways in every Fight phase.
// - A group pays when it has two or more members, its value is above kMinPlanValue and it beats the sum of what its
//   members get apart (max(0, alone against it, alone against their closest enemy unit)) by more than kMinPlanGain.
// - Greedy: each enemy unit's best paying group (gain per risk, the smallest size unless a bigger one adds
//   kMinPlanGain), the best of them first, until no group pays. Unassigned units keep their closest enemy unit, with no
//   partners; an engaged unit is a member of any group on the unit it is engaged with.
// - No-op (every unit: its closest enemy unit, no partners): fewer than two of our units with models, no turn left,
//   before the battle.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "core/geometry/units.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

// D-083 part 1: the plan's constants (M83 may tune them).
inline constexpr int kPlanVolleys = 3;            // our volleys from a group's first contact
inline constexpr std::size_t kMaxGroupSize = 3;   // members, the engaged ones (seeds) aside
inline constexpr std::size_t kPlanCandidates = 4;  // per target: the unassigned units that reach it soonest
inline constexpr double kMinPlanValue = 0.05;     // a group's joint value must be above this
inline constexpr double kMinPlanGain = 0.05;      // and above what its members get apart by this (each extra member too)
inline constexpr double kPlanChargeOdds = 0.5;    // a charge counts once the roll covers the gap with this chance
inline constexpr float kMeanAdvanceMm = 3.5f * core::MM_PER_INCH;  // 09.06: the D6's mean
inline constexpr int kNoArrival = 1 << 20;        // never within the game

enum class AttackMode : std::uint8_t {
    Shoot,    // shoots from its longest range, never charges
    Charge,   // advances, then shoots and charges; engaged from then on
    Engaged,  // engaged with the target now
};

struct PlanMember {
    core::UnitId unit;
    int arrival = kNoArrival;  // our turns until it can attack the target (0: this turn, or our next if it isn't ours)
    AttackMode mode = AttackMode::Shoot;
    double lone = 0.0;   // its exchange with the target alone
    double alone = 0.0;  // what it gets apart: max(0, lone, its lone exchange with its closest enemy unit); engaged: lone
    friend bool operator==(const PlanMember&, const PlanMember&) = default;
};

struct PlanGroup {
    core::UnitId target;
    std::vector<PlanMember> members;  // in unit order
    double value = 0.0;               // the joint exchange (the target's losses minus the members')
    double alone = 0.0;               // the members' alone, summed
    double gain = 0.0;                // value - alone
    double risk = 0.0;                // the members' strengths (what is left of them), summed
    int first_contact = kNoArrival;   // the earliest arrival
    std::optional<core::UnitId> reply_to;  // the member the target's first reply lands on
    [[nodiscard]] const PlanMember* member(core::UnitId unit) const;
    friend bool operator==(const PlanGroup&, const PlanGroup&) = default;
};

struct UnitPlan {
    core::UnitId unit;
    std::optional<core::UnitId> focus;   // the group's target, else the closest enemy unit (nullopt: none)
    std::vector<core::UnitId> partners;  // the group's other members, in unit order (empty: unassigned)
    std::optional<std::size_t> group;    // index into ArmyPlan::groups
    friend bool operator==(const UnitPlan&, const UnitPlan&) = default;
};

struct ArmyPlan {
    core::Side side = core::Side::A;
    bool active = false;           // false: the no-op plan (closest enemy unit, no partners for every unit)
    std::vector<UnitPlan> units;   // the side's units with models, in unit order
    std::vector<PlanGroup> groups;  // in the order they were assigned
    [[nodiscard]] const UnitPlan* find(core::UnitId unit) const;
    [[nodiscard]] const PlanGroup* group_of(core::UnitId unit) const;
    friend bool operator==(const ArmyPlan&, const ArmyPlan&) = default;
};

[[nodiscard]] ArmyPlan army_plan(analysis::OddsCache& cache, const core::GameState& state, core::Side side);

// The no-op plan (army_plan's answer before the battle or with fewer than two units): every unit of `side` with its
// closest enemy unit and no partners, inactive. D-085 (M85): what the pre-batch generation (Generation::M77) read, so the
// tests compare a unit's play with and without partners through it.
[[nodiscard]] ArmyPlan no_plan(const core::GameState& state, core::Side side);

// ---- the pieces, for tests and trace reasons ----

// One of our units against one enemy unit alone: its arrivals (kNoArrival: none), the better mode and its exchange.
struct LoneExchange {
    float gap_mm = 0.0f;
    int shoot_arrival = kNoArrival;
    int charge_arrival = kNoArrival;
    AttackMode mode = AttackMode::Shoot;
    int arrival = kNoArrival;  // the mode's
    double value = 0.0;        // 0 when it can't arrive within the game
};
[[nodiscard]] LoneExchange lone_exchange(analysis::OddsCache& cache, const core::GameState& state, core::UnitId ours,
                                         core::UnitId enemy);

// The joint exchange of `members` (our units, any order; each in its lone mode) with `target`.
struct JointExchange {
    double value = 0.0;
    int first_contact = kNoArrival;
    std::optional<core::UnitId> reply_to;
};
[[nodiscard]] JointExchange joint_exchange(analysis::OddsCache& cache, const core::GameState& state,
                                           std::span<const core::UnitId> members, core::UnitId target);

// "Legionaries 1 + Legionaries 5 on Screamer-killer 1 (joint +0.22, apart +0.00, reply on Legionaries 1); ..." or "no
// group pays" / "no plan".
[[nodiscard]] std::string describe(const core::GameState& state, const ArmyPlan& plan);
// For one unit's reasons: "with Legionaries 5 on Screamer-killer 1 (joint +0.22, apart +0.00)", or "" without partners.
[[nodiscard]] std::string describe_unit(const core::GameState& state, const ArmyPlan& plan, core::UnitId unit);

}  // namespace wh40k::ai::normal
