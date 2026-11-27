#include "core/model/weapon.hpp"

#include <cctype>
#include <algorithm>
#include <charconv>
#include <type_traits>
#include <utility>

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

// A keyword list from normalised text: "infantry", "monster/vehicle", "non vehicle" (the "non-X" form). Each keyword
// keeps its inner spaces ("adeptus astartes"). nullopt if a keyword is empty.
std::optional<ability::TargetCondition> parse_keywords(std::string keyword) {
    ability::TargetCondition out;
    constexpr std::string_view non = "non ";
    if (keyword.compare(0, non.size(), non) == 0) {
        out.non = true;
        keyword.erase(0, non.size());
    }
    std::size_t start = 0;
    while (start <= keyword.size()) {
        const std::size_t slash = std::min(keyword.find('/', start), keyword.size());
        std::string part = keyword.substr(start, slash - start);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (part.empty()) return std::nullopt;
        out.keywords.push_back(std::move(part));
        start = slash + 1;
    }
    return out;
}

// 24.03 [ANTI-X Y+] from normalised text: "anti infantry 4+", "anti monster/vehicle 2+", "anti non vehicle 3+" (the
// FAQ's non-X form). The keyword keeps its inner spaces ("anti adeptus astartes 4+"). A threshold without the "+"
// ("anti fly 2", one BSData entry) reads the same. nullopt if the text is not of that form.
std::optional<ability::Anti> parse_anti(const std::string& norm) {
    constexpr std::string_view prefix = "anti ";
    if (norm.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    const std::size_t last_space = norm.rfind(' ');
    if (last_space == std::string::npos || last_space < prefix.size()) return std::nullopt;
    std::string_view number(norm.data() + last_space + 1, norm.size() - last_space - 1);
    if (!number.empty() && number.back() == '+') number.remove_suffix(1);
    int threshold = 0;
    const auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), threshold);
    if (number.empty() || ec != std::errc{} || ptr != number.data() + number.size() || threshold < 2 || threshold > 6) {
        return std::nullopt;  // 05.02: an unmodified 1 always fails, so "1+" is not a threshold
    }
    auto keywords = parse_keywords(norm.substr(prefix.size(), last_space - prefix.size()));
    if (!keywords) return std::nullopt;
    ability::Anti out;
    out.threshold = threshold;
    out.keywords = std::move(keywords->keywords);
    out.non = keywords->non;
    return out;
}

// The abilities that can be followed by keywords (24.01; M100, D-102): those that act from the hit roll on. nullopt for
// any other text.
std::optional<WeaponAbility> parse_conditionable(const std::string& n) {
    if (n == "lethal hits") return ability::LethalHits{};
    if (n == "devastating wounds") return ability::DevastatingWounds{};
    if (const auto x = parse_suffix_int(n, "sustained hits", std::nullopt)) {
        return ability::SustainedHits{*x, {}};  // 24.36: always [SUSTAINED HITS X]; a random X stays Unknown
    }
    return std::nullopt;
}

}  // namespace

WeaponAbility parse_weapon_ability(std::string_view text) {
    const std::string n = normalise(text);
    // 24.01 (M100, D-102): "[LETHAL HITS: VEHICLE]", "Devastating Wounds: non-Monster/Vehicle". A condition on any other
    // ability is not known, so the text stays Unknown (D-071).
    if (const std::size_t colon = n.find(':'); colon != std::string::npos) {
        std::string base = n.substr(0, colon);
        while (!base.empty() && base.back() == ' ') base.pop_back();
        std::string rest = n.substr(colon + 1);
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        auto ability = parse_conditionable(base);
        auto condition = parse_keywords(rest);
        if (!ability || !condition) return ability::Unknown{std::string(text)};
        std::visit(
            [&condition](auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, ability::LethalHits> || std::is_same_v<T, ability::DevastatingWounds> ||
                              std::is_same_v<T, ability::SustainedHits>) {
                    a.only_against = std::move(*condition);
                }
            },
            *ability);
        return std::move(*ability);
    }
    if (auto conditionable = parse_conditionable(n)) return std::move(*conditionable);
    if (n == "assault") return ability::Assault{};
    if (n == "heavy") return ability::Heavy{};
    if (n == "close quarters") return ability::CloseQuarters{};
    if (n == "pistol") return ability::CloseQuarters{};  // 24.27: identical to [CLOSE-QUARTERS]
    if (n == "twin linked") return ability::TwinLinked{};
    if (n == "torrent") return ability::Torrent{};
    if (n == "hazardous") return ability::Hazardous{};
    if (n == "extra attacks") return ability::ExtraAttacks{};
    if (n == "ignores cover") return ability::IgnoresCover{};  // 24.18 (M90)
    if (n == "lance") return ability::Lance{};                 // 24.21 (M99)
    if (const auto x = parse_suffix_int(n, "melta", std::nullopt)) {
        return ability::Melta{*x};  // 24.25: always [MELTA X]
    }
    if (auto anti = parse_anti(n)) return std::move(*anti);  // 24.03: always [ANTI-X Y+]
    if (const auto x = parse_suffix_int(n, "rapid fire", std::nullopt)) {
        return ability::RapidFire{*x};  // 24.30: always [RAPID FIRE X]
    }
    if (const auto x = parse_suffix_int(n, "blast", 1)) {
        return ability::Blast{*x};  // 24.05: [BLAST] adds 1 per five models
    }
    if (n == "indirect fire") return ability::IndirectFire{};  // 24.19 (M101)
    if (n == "precision") return ability::Precision{};         // 24.28 (M101)
    if (n == "psychic") return ability::Psychic{};    // 24.29 (M100)
    if (n == "one shot") return ability::OneShot{};   // 24.26 (M100)
    if (const auto x = parse_suffix_int(n, "cleave", std::nullopt)) {
        return ability::Cleave{*x};  // 24.06: always [CLEAVE X]
    }
    return ability::Unknown{std::string(text)};
}

}  // namespace wh40k::core
