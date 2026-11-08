#pragma once

// D-076 (M67, amends D-007): the points tiebreak. A wipe-out still wins at once (D-007). Otherwise, after the End of
// Battle Round step of the last battle round (07.03), the side that destroyed more points wins; equal points is a draw.
//
// Points destroyed by a side: for each enemy unit, its full points if it is destroyed, otherwise
// points x wounds lost / starting wounds. Starting wounds are the sum of the W characteristic of every model in the
// unit as set up (Starting Strength, Rules Appendix); wounds lost are the starting wounds minus the wounds remaining
// on the models still on the battlefield.
//
// The totals are exact: both are numerators over one common denominator D, the lcm of every unit's starting wounds,
// so they compare as integers. check_points_data bounds the input so that no step can overflow (D-076):
//   every unit's points are 1..kMaxUnitPoints, its starting wounds a positive int, and
//   D x (the sum of every unit's points) <= kPointsLimit (2^59).
// Then each numerator is at most D x (that sum) <= 2^59, every product on the way is at most that, and the decimal
// display's long division (remainder x 10 < 10 x 2^59 < 2^63) stays in range.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/model/unit.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

inline constexpr int kMaxUnitPoints = 100000;
inline constexpr std::int64_t kPointsLimit = std::int64_t{1} << 59;

// The sum of the W characteristic of the unit's models (as they are now: at set-up, its starting wounds).
[[nodiscard]] std::int64_t sum_of_wounds_characteristics(const Unit& unit);

// Starting wounds minus the wounds remaining on the unit's models, clamped to 0..starting wounds.
[[nodiscard]] int wounds_lost(const Unit& unit);

// D-076: refuses (DataError, naming `where`) points or starting wounds out of range, a unit with one but not the
// other, a PointsDestroyed game with a unit without points, and a set of units whose denominator breaks the bound.
void check_points_data(const GameState& state, std::string_view where);

// D-076: the common denominator, the lcm of the starting wounds of every unit with points (1 when none has any).
// Precondition: check_points_data passed.
[[nodiscard]] std::int64_t points_denominator(const GameState& state);

// D-076: the points each side has destroyed so far, exactly, with their display forms; battle_round is the current
// one. Units without points count 0. Precondition: check_points_data passed (the loader and the state JSON check it).
[[nodiscard]] PointsTally points_destroyed(const GameState& state);

// numerator / denominator as a decimal with `decimals` places, rounded half up, without trailing zeros
// ("412.5", "380", "18.33"). Precondition: 0 <= numerator, 0 < denominator <= kPointsLimit.
[[nodiscard]] std::string points_decimal(std::int64_t numerator, std::int64_t denominator, int decimals);

// D-076: both totals as decimals: 2 places, or more where two unequal totals would read alike at 2 (never two
// unequal totals shown as equal), without trailing zeros.
[[nodiscard]] std::array<std::string, 2> points_display(const std::array<std::int64_t, 2>& numerators,
                                                        std::int64_t denominator);

}  // namespace wh40k::core
