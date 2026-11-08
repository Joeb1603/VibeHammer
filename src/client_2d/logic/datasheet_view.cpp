#include "datasheet_view.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <variant>

#include "core/geometry/units.hpp"
#include "core/model/dice_expr.hpp"

namespace wh40k::client {

namespace {

std::string inches(float mm) {
    const float in = core::mm_to_inches(mm);
    const float rounded = std::round(in);
    char buf[32];
    if (std::fabs(in - rounded) < 0.05f) {
        std::snprintf(buf, sizeof(buf), "%d\"", static_cast<int>(rounded));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f\"", static_cast<double>(in));
    }
    return buf;
}

std::string plus(const std::optional<int>& v) { return v ? std::to_string(*v) + "+" : "-"; }
std::string number(const std::optional<int>& v) { return v ? std::to_string(*v) : "-"; }

std::string join(const std::vector<std::string>& items, std::string_view sep) {
    std::string out;
    for (const std::string& s : items) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

DatasheetWeaponRow weapon_row(const core::WeaponProfile& p) {
    DatasheetWeaponRow r;
    r.name = p.name;
    r.range = p.range_mm ? inches(*p.range_mm) : "Melee";
    r.attacks = core::to_string(p.attacks);
    r.skill = p.skill ? plus(p.skill) : "N/A";
    r.strength = std::to_string(p.strength);
    r.armour_penetration = std::to_string(p.armour_penetration);
    r.damage = core::to_string(p.damage);
    std::vector<std::string> labels;
    for (const core::WeaponAbility& a : p.abilities) {
        labels.push_back(weapon_ability_label(a));
        r.ability_help.push_back(weapon_ability_help(a));
    }
    r.abilities = join(labels, " ");
    return r;
}

DatasheetStatRow stat_row(const core::Model& m) {
    const core::Characteristics& c = m.characteristics;
    DatasheetStatRow r;
    r.model_type = m.type_name;
    r.move = c.move_mm ? inches(*c.move_mm) : "-";
    r.toughness = std::to_string(c.toughness);
    r.save = plus(c.save);
    r.invuln_save = plus(c.invuln_save);
    r.wounds = std::to_string(c.wounds);
    r.leadership = plus(c.leadership);
    r.objective_control = number(c.objective_control);
    return r;
}

}  // namespace

std::string stat_line(const core::Characteristics& c) {
    std::string out = "M " + (c.move_mm ? inches(*c.move_mm) : std::string("-"));
    out += "  T " + std::to_string(c.toughness);
    out += "  Sv " + plus(c.save);
    if (c.invuln_save) out += "  InSv " + plus(c.invuln_save);
    out += "  W " + std::to_string(c.wounds);
    out += "  Ld " + plus(c.leadership);
    out += "  OC " + number(c.objective_control);
    return out;
}

std::string weapon_ability_label(const core::WeaponAbility& ability) {
    return std::visit(
        [](const auto& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            namespace ab = core::ability;
            if constexpr (std::is_same_v<T, ab::Assault>) return "[ASSAULT]";
            else if constexpr (std::is_same_v<T, ab::Heavy>) return "[HEAVY]";
            else if constexpr (std::is_same_v<T, ab::CloseQuarters>) return "[CLOSE-QUARTERS]";
            else if constexpr (std::is_same_v<T, ab::RapidFire>) return "[RAPID FIRE " + std::to_string(a.x) + "]";
            else if constexpr (std::is_same_v<T, ab::Blast>) return "[BLAST " + std::to_string(a.x) + "]";
            else if constexpr (std::is_same_v<T, ab::TwinLinked>) return "[TWIN-LINKED]";
            else if constexpr (std::is_same_v<T, ab::Torrent>) return "[TORRENT]";
            else if constexpr (std::is_same_v<T, ab::Hazardous>) return "[HAZARDOUS]";
            else if constexpr (std::is_same_v<T, ab::DevastatingWounds>) return "[DEVASTATING WOUNDS]";
            else if constexpr (std::is_same_v<T, ab::SustainedHits>) return "[SUSTAINED HITS " + std::to_string(a.x) + "]";
            else if constexpr (std::is_same_v<T, ab::ExtraAttacks>) return "[EXTRA ATTACKS]";
            else return "[" + upper(a.text) + "]";
        },
        ability);
}

std::string clean_ability_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const std::string_view rest = text.substr(i);
        if (rest.starts_with("\xC2\xA0")) {  // U+00A0 no-break space
            out += ' ';
            i += 2;
        } else if (rest.starts_with("\xE2\x96\xAA") || rest.starts_with("\xE2\x96\xA0")) {  // U+25AA, U+25A0
            out += '-';
            i += 3;
        } else if (rest.starts_with("**") || rest.starts_with("^^")) {
            i += 2;
        } else {
            out += text[i];
            ++i;
        }
    }
    // Trailing spaces before line breaks and at the end (left by the no-break spaces).
    std::string tidy;
    tidy.reserve(out.size());
    for (const char c : out) {
        if (c == '\n') {
            while (!tidy.empty() && tidy.back() == ' ') tidy.pop_back();
        }
        tidy += c;
    }
    while (!tidy.empty() && (tidy.back() == ' ' || tidy.back() == '\n')) tidy.pop_back();
    return tidy;
}

DatasheetView datasheet_view(const core::Unit& unit) {
    DatasheetView v;
    v.name = unit.name;
    v.side = unit.side == core::Side::A ? "Side A" : "Side B";
    v.models = std::to_string(unit.models.size()) + "/" + std::to_string(unit.starting_strength) + " models";
    v.battle_shocked = unit.battle_shocked;

    // One stat row per model type and characteristics, in the order the models appear.
    std::vector<const core::Model*> row_models;
    for (const core::Model& m : unit.models) {
        const auto same = std::find_if(row_models.begin(), row_models.end(), [&](const core::Model* r) {
            return r->type_name == m.type_name && r->characteristics == m.characteristics;
        });
        if (same == row_models.end()) {
            row_models.push_back(&m);
            v.stats.push_back(stat_row(m));
            v.stats.back().count = 1;
        } else {
            ++v.stats[static_cast<std::size_t>(same - row_models.begin())].count;
        }
    }

    // Each weapon once (by name), with every profile; how many models carry it.
    std::vector<std::string> seen;
    std::vector<std::size_t> first_row;  // per seen weapon: index of its first profile row
    std::vector<bool> is_melee;
    std::vector<std::size_t> row_count;
    for (const core::Model& m : unit.models) {
        for (const core::Weapon& w : m.weapons) {
            const auto it = std::find(seen.begin(), seen.end(), w.name);
            if (it != seen.end()) {
                const auto k = static_cast<std::size_t>(it - seen.begin());
                auto& rows = is_melee[k] ? v.melee : v.ranged;
                for (std::size_t r = 0; r < row_count[k]; ++r) ++rows[first_row[k] + r].carried_by;
                continue;
            }
            const bool melee = w.kind == core::WeaponKind::Melee;
            auto& rows = melee ? v.melee : v.ranged;
            seen.push_back(w.name);
            is_melee.push_back(melee);
            first_row.push_back(rows.size());
            row_count.push_back(w.profiles.size());
            for (const core::WeaponProfile& p : w.profiles) {
                rows.push_back(weapon_row(p));
                rows.back().carried_by = 1;
            }
        }
    }

    for (const core::AbilityText& a : unit.abilities) {
        v.abilities.push_back({clean_ability_text(a.name), clean_ability_text(a.text)});
    }
    v.keywords = join(unit.keywords, ", ");
    v.faction_keywords = join(unit.faction_keywords, ", ");
    for (const std::string& k : unit.keywords) v.keyword_help.push_back(keyword_help(k));
    for (const std::string& k : unit.faction_keywords) v.faction_keyword_help.push_back(keyword_help(k, true));
    return v;
}

}  // namespace wh40k::client
