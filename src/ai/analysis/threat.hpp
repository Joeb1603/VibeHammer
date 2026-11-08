#pragma once

// M40 (D-054): threat ranges, unit roles and the per-range-band matchup table (M43's whole-turn scoring, M44's
// Markov chain over range bands).
//
// Two kinds of knowledge are kept apart:
// - The live state is always asked through the engine (current_options: eligible_shooting_types,
//   shooting_context, declare_charge_problem).
// - Hypothetical future turns use strategic knowledge (allowance_after): an Advance means Assault shooting only and
//   no charge (10.05, 11.02), a Fall Back means no shooting and no charge (09.07, 11.02), moving more than 3" loses
//   [HEAVY] (24.16), an advance adds a D6 to M (09.06), a charge is M + 2D6 away (11.02, 11.04). Tests check that
//   the table agrees with the engine's queries.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::analysis {

// ---- what a unit may still do ----

// The engine's answer for `unit` in `state` now.
struct TurnOptions {
    std::vector<core::ShootingType> shooting_types;  // eligible_shooting_types (empty: can't shoot)
    bool heavy = false;                              // shooting_context: 24.16 applies
    std::optional<std::string> charge_problem;       // declare_charge_problem (nullopt: may declare now)
};
[[nodiscard]] TurnOptions current_options(const core::GameState& state, const core::Unit& unit);

// A movement-phase choice for a hypothetical turn.
enum class MovePlan : std::uint8_t {
    RemainStationary,  // 09.04
    NormalUpTo3,       // a Normal move of 3" or less (keeps [HEAVY])
    Normal,            // 09.05, more than 3"
    Advance,           // 09.06
    FallBack,          // 09.07
};
struct TurnAllowance {
    bool shoot = false;
    core::ShootingType type = core::ShootingType::Normal;  // when unengaged after the move
    bool heavy = false;
    bool charge = false;
};
// Strategic knowledge (see the file comment); consistency-tested against the engine.
[[nodiscard]] TurnAllowance allowance_after(MovePlan plan);

// ---- threat ranges ----

struct ThreatRanges {
    float move_mm = 0.0f;                 // M (0 for M '-')
    std::array<float, 7> advance_mm{};    // [f]: M + f" after an advance roll of f (09.06)
    float max_range_mm = 0.0f;            // longest usable ranged profile (D-026, D-071)
    float max_assault_range_mm = 0.0f;    // longest usable [ASSAULT] profile (shooting after an Advance)
    float max_close_quarters_range_mm = 0.0f;

    // The gap this unit can shoot across next turn: a Normal move then its longest weapon.
    [[nodiscard]] float shooting_reach_mm() const noexcept { return move_mm + max_range_mm; }
    // P(an Advance then an [ASSAULT] weapon reaches across `gap_mm`), over the advance roll.
    [[nodiscard]] double p_advance_shoot(float gap_mm) const;
    // P(a Normal move of up to M, ending outside engagement range, then a charge roll covers the rest: within the
    // roll and within 12"). The move's path and the placement are not checked (the quick bound).
    [[nodiscard]] double p_charge_after_move(float gap_mm) const;
    // P(the charge roll alone covers `gap_mm`, e.g. in the Charge phase after this turn's move).
    [[nodiscard]] static double p_charge_now(float gap_mm);
};
[[nodiscard]] ThreatRanges threat_ranges(const core::Unit& unit);

// ---- roles ----

enum class Lean : std::uint8_t { Ranged, Balanced, Melee };

// How `unit` deals with `enemy` (values in model equivalents, AttackOutcome::model_equivalents):
// - ranged: its best shooting (unengaged, [HEAVY] applies, within half range of its longest weapon);
// - melee: every model fighting with its best melee weapon;
// - ranged_share = ranged / (ranged + melee); lean Ranged above kLeanMargin over 0.5, Melee below it;
// - strong_*: the value is at least kStrongShare of the enemy unit's models.
struct UnitRole {
    double ranged = 0.0;
    double melee = 0.0;
    double ranged_share = 0.5;
    double ranged_vs_enemy = 0.0;  // ranged / enemy models
    double melee_vs_enemy = 0.0;
    Lean lean = Lean::Balanced;
    bool strong_ranged = false;
    bool strong_melee = false;
};
inline constexpr double kLeanMargin = 0.05;
inline constexpr double kStrongShare = 0.25;
[[nodiscard]] UnitRole unit_role(OddsCache& cache, const core::Unit& unit, const core::Unit& enemy);

// ---- matchup per range band ----

enum class RangeBand : std::uint8_t {
    Engaged,     // close-quarters shooting, then the fight
    HalfRange,   // at half the attacker's longest range: [RAPID FIRE] applies to its longest weapons
    InRange,     // at the attacker's longest range
    OutOfRange,  // nothing reaches
};
inline constexpr std::size_t kRangeBands = 4;

struct BandValue {
    RangeBand band = RangeBand::OutOfRange;
    float gap_mm = 0.0f;              // the representative gap the band was evaluated at
    AttackOutcome stationary;         // shooting with [HEAVY] (unengaged bands; close-quarters when engaged)
    AttackOutcome moved;              // shooting after moving more than 3"
    AttackOutcome advanced;           // [ASSAULT] shooting after an Advance
    AttackOutcome melee;              // the fight (Engaged only)
    AttackOutcome turn;               // the best turn here: stationary shooting, plus the fight when engaged
};

struct DirectionTable {
    core::UnitId attacker;
    core::UnitId target;
    std::array<BandValue, kRangeBands> bands;  // indexed by RangeBand
    [[nodiscard]] const BandValue& at(RangeBand b) const { return bands[static_cast<std::size_t>(b)]; }
};

struct Matchup {
    DirectionTable a_to_b;
    DirectionTable b_to_a;
};

[[nodiscard]] DirectionTable direction_table(OddsCache& cache, const core::Unit& attacker, const core::Unit& target);
[[nodiscard]] Matchup matchup(OddsCache& cache, const core::Unit& a, const core::Unit& b);

}  // namespace wh40k::ai::analysis
