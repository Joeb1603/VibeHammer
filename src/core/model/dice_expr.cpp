#include "core/model/dice_expr.hpp"

#include <cctype>
#include <optional>
#include <stdexcept>

#include "core/data/load_error.hpp"

namespace wh40k::core {

namespace {

// Parses a run of decimal digits at `pos`; advances `pos`. Returns nullopt if none.
std::optional<int> parse_uint(std::string_view s, std::size_t& pos) {
    const std::size_t start = pos;
    int value = 0;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])) != 0) {
        value = value * 10 + (s[pos] - '0');
        if (value > 1000) {
            return std::nullopt;
        }
        ++pos;
    }
    if (pos == start) {
        return std::nullopt;
    }
    return value;
}

[[noreturn]] void malformed(std::string_view text) {
    throw DataError("01.05: malformed dice expression '" + std::string(text) + "'");
}

}  // namespace

int DiceExpr::fixed_value() const {
    if (!is_fixed()) {
        // D-069: a random value is rolled (roll_characteristic); asking for a fixed one is a programmer error.
        throw std::logic_error("D-069: random characteristic " + to_string(*this) +
                               " used where a fixed value is required");
    }
    return modifier;
}

DiceExpr parse_dice_expr(std::string_view text) {
    // Strip surrounding whitespace.
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        malformed(text);
    }

    std::size_t pos = 0;
    const std::optional<int> lead = parse_uint(text, pos);
    if (pos == text.size()) {
        if (!lead) {
            malformed(text);
        }
        return DiceExpr::fixed(*lead);  // plain integer
    }
    if (text[pos] != 'D' && text[pos] != 'd') {
        malformed(text);
    }
    ++pos;
    const int num_dice = lead.value_or(1);
    const std::optional<int> sides = parse_uint(text, pos);
    if (!sides || (*sides != 3 && *sides != 6) || num_dice < 1 || num_dice > 20) {
        malformed(text);
    }
    int modifier = 0;
    if (pos < text.size()) {
        if (text[pos] != '+') {
            malformed(text);
        }
        ++pos;
        const std::optional<int> mod = parse_uint(text, pos);
        if (!mod || pos != text.size()) {
            malformed(text);
        }
        modifier = *mod;
    }
    return DiceExpr{static_cast<std::uint8_t>(num_dice), static_cast<std::uint8_t>(*sides),
                    static_cast<std::int16_t>(modifier)};
}

std::string to_string(const DiceExpr& expr) {
    if (expr.is_fixed()) {
        return std::to_string(expr.modifier);
    }
    std::string out;
    if (expr.num_dice > 1) {
        out += std::to_string(expr.num_dice);
    }
    out += 'D';
    out += std::to_string(expr.sides);
    if (expr.modifier > 0) {
        out += '+';
        out += std::to_string(expr.modifier);
    }
    return out;
}

}  // namespace wh40k::core
