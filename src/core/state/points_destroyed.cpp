#include "core/state/points_destroyed.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/model/keywords.hpp"

namespace wh40k::core {

namespace {

[[noreturn]] void fail(std::string_view where, const std::string& what) {
    throw DataError(std::string(where) + ": D-076: " + what);
}

std::string unit_label(const Unit& u) { return "unit " + std::to_string(u.id.v) + " '" + u.name + "'"; }

}  // namespace

std::int64_t sum_of_wounds_characteristics(const Unit& unit) {
    std::int64_t sum = 0;
    for (const Model& m : unit.models) sum += std::max(0, m.characteristics.wounds);
    return sum;
}

int wounds_lost(const Unit& unit) {
    std::int64_t remaining = 0;
    for (const Model& m : unit.models) remaining += std::max(0, m.wounds_remaining);
    const std::int64_t lost = std::int64_t{unit.starting_wounds} - remaining;
    return static_cast<int>(std::clamp<std::int64_t>(lost, 0, unit.starting_wounds));
}

void check_points_data(const GameState& state, std::string_view where) {
    std::int64_t total = 0;
    std::int64_t denominator = 1;
    // D-081 (M74): the units not yet set up count too (a hand-deployment scenario as loaded has every unit there).
    std::vector<const Unit*> all;
    all.reserve(state.units.size() + state.undeployed.size());
    for (const Unit& u : state.units) all.push_back(&u);
    for (const Unit& u : state.undeployed) all.push_back(&u);
    for (const Unit* unit : all) {
        const Unit& u = *unit;
        if (u.points < 0 || u.points > kMaxUnitPoints) {
            fail(where, unit_label(u) + " has points " + std::to_string(u.points) + "; points must be a positive integer of at most " +
                            std::to_string(kMaxUnitPoints));
        }
        if (u.starting_wounds < 0) {
            fail(where, unit_label(u) + " has starting wounds " + std::to_string(u.starting_wounds) + "; they must be positive");
        }
        if ((u.points == 0) != (u.starting_wounds == 0)) {
            fail(where, unit_label(u) + " has points " + std::to_string(u.points) + " and starting wounds " +
                            std::to_string(u.starting_wounds) + "; a unit has both or neither");
        }
        if (u.points == 0) {
            if (state.result_rule == ResultRule::PointsDestroyed) {
                fail(where, unit_label(u) + " has no points; result \"points_destroyed\" needs the points of every unit");
            }
            continue;
        }
        total += u.points;
        // The lcm, refused before it could pass the bound (so it never overflows).
        const std::int64_t step = u.starting_wounds / std::gcd(denominator, std::int64_t{u.starting_wounds});
        if (denominator > kPointsLimit / step) {
            fail(where, "the units' starting wounds have a least common multiple over 2^59, too large to count points exactly");
        }
        denominator *= step;
    }
    if (total > 0 && denominator > kPointsLimit / total) {
        fail(where, "the least common multiple of the units' starting wounds (" + std::to_string(denominator) +
                        ") times their total points (" + std::to_string(total) +
                        ") is over 2^59, too large to count points exactly");
    }
}

std::int64_t points_denominator(const GameState& state) {
    std::int64_t denominator = 1;
    for (const Unit& u : state.units) {
        if (u.points <= 0 || u.starting_wounds <= 0) continue;
        denominator = std::lcm(denominator, std::int64_t{u.starting_wounds});
    }
    return denominator;
}

PointsTally points_destroyed(const GameState& state) {
    PointsTally t;
    t.denominator = points_denominator(state);
    t.battle_round = state.flow.battle_round;
    for (const Unit& u : state.units) {
        if (u.points <= 0 || u.starting_wounds <= 0) continue;
        // D-076: a destroyed unit counts its full points; any other unit points x wounds lost / starting wounds, here
        // over the common denominator: points x (D / starting wounds) x wounds lost, at most points x D.
        const std::int64_t lost = is_destroyed(u) ? std::int64_t{u.starting_wounds} : std::int64_t{wounds_lost(u)};
        const std::int64_t scale = t.denominator / u.starting_wounds;  // exact: D is a multiple of it
        t.destroyed[side_index(opponent(u.side))] += std::int64_t{u.points} * scale * lost;
    }
    t.display = points_display(t.destroyed, t.denominator);
    return t;
}

std::string points_decimal(std::int64_t numerator, std::int64_t denominator, int decimals) {
    std::int64_t whole = numerator / denominator;
    std::int64_t rest = numerator % denominator;
    std::vector<int> digits;
    for (int i = 0; i < decimals; ++i) {
        rest *= 10;  // < 10 x denominator <= 10 x 2^59 < 2^63
        digits.push_back(static_cast<int>(rest / denominator));
        rest %= denominator;
    }
    if (2 * rest >= denominator) {  // round half up, carrying into the whole part
        std::size_t i = digits.size();
        while (i > 0 && digits[i - 1] == 9) digits[--i] = 0;
        if (i > 0) {
            ++digits[i - 1];
        } else {
            ++whole;
        }
    }
    while (!digits.empty() && digits.back() == 0) digits.pop_back();
    std::string out = std::to_string(whole);
    if (!digits.empty()) {
        out += '.';
        for (const int d : digits) out += static_cast<char>('0' + d);
    }
    return out;
}

std::array<std::string, 2> points_display(const std::array<std::int64_t, 2>& numerators, std::int64_t denominator) {
    // Two unequal totals differ by at least 1 / denominator >= 2^-59 > 10^-18, so by 18 places they read apart.
    for (int decimals = 2;; ++decimals) {
        std::array<std::string, 2> out{points_decimal(numerators[0], denominator, decimals),
                                       points_decimal(numerators[1], denominator, decimals)};
        if (numerators[0] == numerators[1] || out[0] != out[1] || decimals >= 19) return out;
    }
}

}  // namespace wh40k::core
