#include "core/model/weapon.hpp"

#include <cctype>
#include <charconv>

namespace wh40k::core {

namespace {

// Lower-cases ASCII, maps every hyphen variant (including U+2010/U+2011/U+2012/U+2013 in UTF-8)
// to a space, and collapses whitespace. "Rapid Fire 1" -> "rapid fire 1",
// "Twin‑linked" -> "twin linked".
std::string normalise(std::string_view text) {
    std::string out;
    bool pending_space = false;
    auto put = [&](char c) {
        if (c == ' ') {
            pending_space = !out.empty();
            return;
        }
        if (pending_space) {
            out += ' ';
            pending_space = false;
        }
        out += c;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        // UTF-8 E2 80 90..93: hyphen, non-breaking hyphen, figure dash, en dash.
        if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80) {
            const auto c3 = static_cast<unsigned char>(text[i + 2]);
            if (c3 >= 0x90 && c3 <= 0x93) {
                put(' ');
                i += 2;
                continue;
            }
        }
        if (c == '-' || c == '_' || c == '[' || c == ']' || std::isspace(c) != 0) {
            put(' ');
            continue;
        }
        put(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// "rapid fire 1" with prefix "rapid fire" -> 1; "blast" with prefix "blast" -> default.
std::optional<int> parse_suffix_int(const std::string& norm, std::string_view prefix,
                                    std::optional<int> if_absent) {
    if (norm == prefix) {
        return if_absent;
    }
    if (norm.size() <= prefix.size() + 1 || norm.compare(0, prefix.size(), prefix) != 0 ||
        norm[prefix.size()] != ' ') {
        return std::nullopt;
    }
    const char* first = norm.data() + prefix.size() + 1;
    const char* last = norm.data() + norm.size();
    int value = 0;
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last || value < 1) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

WeaponAbility parse_weapon_ability(std::string_view text) {
    const std::string n = normalise(text);
    if (n == "assault") return ability::Assault{};
    if (n == "heavy") return ability::Heavy{};
    if (n == "close quarters") return ability::CloseQuarters{};
    if (n == "pistol") return ability::CloseQuarters{};  // 24.27: identical to [CLOSE-QUARTERS]
    if (n == "twin linked") return ability::TwinLinked{};
    if (n == "torrent") return ability::Torrent{};
    if (n == "hazardous") return ability::Hazardous{};
    if (n == "devastating wounds") return ability::DevastatingWounds{};
    if (const auto x = parse_suffix_int(n, "rapid fire", std::nullopt)) {
        return ability::RapidFire{*x};  // 24.30: always [RAPID FIRE X]
    }
    if (const auto x = parse_suffix_int(n, "blast", 1)) {
        return ability::Blast{*x};  // 24.05: [BLAST] adds 1 per five models
    }
    return ability::Unknown{std::string(text)};
}

}  // namespace wh40k::core
