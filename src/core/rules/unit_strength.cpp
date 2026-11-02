#include "core/rules/unit_strength.hpp"

#include <stdexcept>

#include "core/model/keywords.hpp"

namespace wh40k::core {

namespace {

// The quantity the Appendix compares, and its full value: wounds of the single model (starting strength
// of 1) or models in the unit (starting strength of 2 or more). A destroyed unit has 0 of either.
struct Strength {
    int remaining = 0;
    int full = 0;
};

Strength strength(const Unit& unit) {
    if (unit.starting_strength < 1) {
        throw std::logic_error("Appendix: unit " + std::to_string(unit.id.v) + " has no starting strength");
    }
    if (unit.starting_strength == 1) {
        if (is_destroyed(unit)) return {0, 1};
        const Model& m = unit.models.front();
        return {m.wounds_remaining, m.characteristics.wounds};
    }
    return {static_cast<int>(unit.models.size()), unit.starting_strength};
}

}  // namespace

bool below_starting_strength(const Unit& unit) {
    const Strength s = strength(unit);
    return s.remaining < s.full;
}

bool at_half_strength(const Unit& unit) {
    const Strength s = strength(unit);
    return s.full % 2 == 0 && s.remaining * 2 == s.full;  // odd values can't be halved evenly
}

bool below_half_strength(const Unit& unit) {
    const Strength s = strength(unit);
    return s.remaining * 2 < s.full;
}

bool at_or_below_half_strength(const Unit& unit) { return at_half_strength(unit) || below_half_strength(unit); }

}  // namespace wh40k::core
