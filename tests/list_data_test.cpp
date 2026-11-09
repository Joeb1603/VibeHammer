// M71 / D-079: list-building data read from BSData by tools/fetch_bsdata.py: points by unit size, copy limits,
// the copy surcharge, wargear slots (02.07) and the limits per model and per unit, for the ten roster datasheets.
// The expectations are D-079's per-unit table.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/data/army_rules.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::repo_root;
using Json = nlohmann::json;
using Names = std::vector<std::string>;

namespace {

const std::vector<std::string> kFactionSheets = {"chaos_lord",   "legionaries",     "chaos_terminator_squad",
                                                 "havocs",       "termagants",      "genestealers",
                                                 "tyrant_guard", "screamer_killer", "hive_tyrant",
                                                 "tyrannofex"};

// Kept alive (std::map nodes don't move) so references into a sheet stay valid within a test.
const Datasheet& sheet(const std::string& file) {
    static std::map<std::string, Datasheet> cache;
    auto it = cache.find(file);
    if (it == cache.end()) {
        it = cache.emplace(file, load_datasheet(repo_root() / ("data/units/" + file + ".json"))).first;
    }
    return it->second;
}

const ListBuilding& lb(const std::string& file) {
    const Datasheet& ds = sheet(file);
    if (!ds.list_building) {
        throw std::logic_error(file + " has no list_building");
    }
    return *ds.list_building;
}

const ModelWargear& wargear(const std::string& file, const std::string& model_type) {
    const ModelWargear* w = lb(file).find_wargear(model_type);
    if (w == nullptr) {
        throw std::logic_error(file + " has no wargear for " + model_type);
    }
    return *w;
}

std::map<std::string, int> multiset(const Names& names) {
    std::map<std::string, int> out;
    for (const auto& n : names) ++out[n];
    return out;
}

std::string describe(const WargearLimit& l) {
    std::string names;
    for (const auto& n : l.names) names += (names.empty() ? "" : "+") + n;
    std::string s = std::string(l.counts == LimitCounts::Weapons  ? "weapons "
                                : l.counts == LimitCounts::Option ? "option "
                                                                  : "model_type ") +
                    names + " max " + std::to_string(l.max);
    if (l.below_models) {
        s += " (below " + std::to_string(l.below_models->models) + ": " + std::to_string(l.below_models->max) + ")";
    }
    if (l.per_models) {
        s += " (+" + std::to_string(l.per_models->add) + " per " + std::to_string(l.per_models->models) + ")";
    }
    return s;
}

Names describe_all(const std::vector<WargearLimit>& limits) {
    Names out;
    for (const auto& l : limits) out.push_back(describe(l));
    return out;
}

// The option names a fit chose, slot by slot.
Names chosen(const ModelWargear& w, const SlotFit& fit) {
    Names out;
    for (std::size_t s = 0; s < fit.options.size(); ++s) out.push_back(w.slots[s].options[fit.options[s]].name);
    return out;
}

struct Group {
    std::string type;
    int count;
    Names weapons;
};

// A unit as the scenario files hold it: model groups of one type and one loadout.
std::vector<Group> groups_of(const Json& unit) {
    std::vector<Group> out;
    for (const Json& g : unit.at("models")) {
        out.push_back(Group{g.at("type").get<std::string>(), g.at("count").get<int>(),
                            g.at("weapons").get<Names>()});
    }
    return out;
}

// D-079 as data: each group fits its slots (02.07), each model keeps its per-model limits and the unit its unit
// limits. Returns the problems (empty: the unit is within the list data).
Names unit_problems(const Datasheet& ds, const std::vector<Group>& groups) {
    Names problems;
    const ListBuilding& data = *ds.list_building;
    int models = 0;
    for (const Group& g : groups) models += g.count;
    std::vector<SlotFit> fits;
    for (const Group& g : groups) {
        const ModelWargear* w = data.find_wargear(g.type);
        if (w == nullptr) {
            problems.push_back("no wargear for " + g.type);
            fits.emplace_back();
            continue;
        }
        fits.push_back(loadout_fits_slots(*w, g.weapons));
        if (!fits.back().fits()) problems.push_back(fits.back().problem);
        for (const WargearLimit& l : w->limits) {
            int n = 0;
            if (l.counts == LimitCounts::Weapons) {
                for (const auto& weapon : g.weapons) n += std::count(l.names.begin(), l.names.end(), weapon) > 0;
            } else {
                for (const auto& name : chosen(*w, fits.back())) n += name == l.names.front();
            }
            if (n > l.max) problems.push_back(g.type + ": " + describe(l));
        }
    }
    for (const WargearLimit& l : data.unit_limits) {
        int n = 0;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const Group& g = groups[i];
            int per_model = 0;
            if (l.counts == LimitCounts::Weapons) {
                for (const auto& weapon : g.weapons) {
                    per_model += std::count(l.names.begin(), l.names.end(), weapon) > 0;
                }
            } else if (l.counts == LimitCounts::Option) {
                const ModelWargear* w = data.find_wargear(g.type);
                if (w != nullptr && fits[i].fits()) {
                    for (const auto& name : chosen(*w, fits[i])) per_model += name == l.names.front();
                }
            } else {
                per_model = g.type == l.names.front() ? 1 : 0;
            }
            n += per_model * g.count;
        }
        if (n > limit_max(l, models)) problems.push_back("unit: " + describe(l));
    }
    return problems;
}

// Bracket + options + the surcharge for `earlier_copies` earlier copies of the datasheet.
int unit_points(const Datasheet& ds, const std::vector<Group>& groups, int earlier_copies) {
    const ListBuilding& data = *ds.list_building;
    int models = 0;
    int options = 0;
    for (const Group& g : groups) {
        models += g.count;
        const ModelWargear& w = *data.find_wargear(g.type);
        options += g.count * fit_option_points(w, loadout_fits_slots(w, g.weapons));
    }
    const int surcharge =
        data.surcharge && earlier_copies >= data.surcharge->earlier_copies ? data.surcharge->points : 0;
    return bracket_points(data, models) + options + surcharge;
}

std::string sheet_name_of(const std::string& unit_file) {
    return std::filesystem::path(unit_file).stem().string();
}

}  // namespace

// ---- the data as read ----

TEST(D079_ListData, EveryFactionDatasheetHasListData) {
    for (const auto& file : kFactionSheets) {
        const Datasheet& ds = sheet(file);
        ASSERT_TRUE(ds.list_building.has_value()) << file;
        EXPECT_EQ(ds.list_building->wargear.size(), ds.model_types.size()) << file;
        for (std::size_t i = 0; i < ds.model_types.size(); ++i) {
            EXPECT_EQ(ds.list_building->wargear[i].model_type, ds.model_types[i].name) << file;
        }
    }
}

TEST(D079_ListData, SlotsAndFixedCoverExactlyTheFlatOptions) {
    // No exceptions: for every model type of the ten, fixed weapons plus every option's weapons are its
    // weapon_options (the scenario loader's flat 02.07 check).
    for (const auto& file : kFactionSheets) {
        const Datasheet& ds = sheet(file);
        for (const ModelTypeSheet& mt : ds.model_types) {
            const ModelWargear& w = wargear(file, mt.name);
            std::vector<std::string> covered = w.fixed;
            for (const auto& s : w.slots) {
                for (const auto& o : s.options) covered.insert(covered.end(), o.weapons.begin(), o.weapons.end());
            }
            std::sort(covered.begin(), covered.end());
            covered.erase(std::unique(covered.begin(), covered.end()), covered.end());
            std::vector<std::string> flat = mt.weapon_options;
            std::sort(flat.begin(), flat.end());
            EXPECT_EQ(covered, flat) << file << " / " << mt.name;
        }
    }
}

TEST(D079_ListData, DefaultChoicesGiveTheDefaultWeapons) {
    for (const auto& file : kFactionSheets) {
        for (const ModelTypeSheet& mt : sheet(file).model_types) {
            const ModelWargear& w = wargear(file, mt.name);
            Names weapons = w.fixed;
            for (const auto& s : w.slots) {
                ASSERT_TRUE(s.default_option.has_value()) << file << " / " << mt.name << " / " << s.name;
                const WargearOption* o = s.find_option(*s.default_option);
                ASSERT_NE(o, nullptr);
                weapons.insert(weapons.end(), o->weapons.begin(), o->weapons.end());
            }
            EXPECT_EQ(multiset(weapons), multiset(mt.default_weapons)) << file << " / " << mt.name;
            const SlotFit fit = loadout_fits_slots(w, mt.default_weapons);
            EXPECT_TRUE(fit.fits()) << fit.problem;
        }
    }
    // D-070's rule where BSData names no default: the first option.
    EXPECT_EQ(wargear("legionaries", "Legionary w/ other weapon").slots[0].default_option, "Havoc autocannon");
    EXPECT_EQ(wargear("chaos_terminator_squad", "Heavy weapon").slots[0].default_option, "Heavy flamer");
    // The flattened default of the Terminator Champion's nested slots.
    EXPECT_EQ(wargear("chaos_terminator_squad", "Terminator Champion").slots[0].default_option,
              "Accursed weapon and Combi-bolter");
}

TEST(D079_ListData, PointsByUnitSize) {
    const auto brackets = [](const std::string& file) {
        std::vector<std::pair<int, int>> out;
        for (const auto& b : lb(file).points) out.emplace_back(b.min_models, b.points);
        return out;
    };
    using B = std::vector<std::pair<int, int>>;
    EXPECT_EQ(brackets("chaos_lord"), (B{{1, 95}}));
    EXPECT_EQ(brackets("legionaries"), (B{{5, 95}, {6, 180}}));
    EXPECT_EQ(brackets("chaos_terminator_squad"), (B{{5, 185}, {6, 370}}));
    EXPECT_EQ(brackets("havocs"), (B{{5, 135}}));
    EXPECT_EQ(brackets("termagants"), (B{{10, 55}, {11, 110}}));
    EXPECT_EQ(brackets("genestealers"), (B{{5, 75}, {6, 140}}));
    EXPECT_EQ(brackets("tyrant_guard"), (B{{3, 80}, {4, 170}}));
    EXPECT_EQ(brackets("screamer_killer"), (B{{1, 125}}));
    EXPECT_EQ(brackets("hive_tyrant"), (B{{1, 195}}));
    EXPECT_EQ(brackets("tyrannofex"), (B{{1, 170}}));

    EXPECT_EQ(bracket_points(lb("legionaries"), 5), 95);
    EXPECT_EQ(bracket_points(lb("legionaries"), 6), 180);
    EXPECT_EQ(bracket_points(lb("legionaries"), 10), 180);
    EXPECT_EQ(bracket_points(lb("chaos_terminator_squad"), 5), 185);
    EXPECT_EQ(bracket_points(lb("chaos_terminator_squad"), 6), 370);  // D-079: as BSData has it, not only at 10
    EXPECT_EQ(bracket_points(lb("chaos_terminator_squad"), 10), 370);
    EXPECT_EQ(bracket_points(lb("termagants"), 10), 55);
    EXPECT_EQ(bracket_points(lb("termagants"), 11), 110);
    EXPECT_EQ(bracket_points(lb("termagants"), 20), 110);
    EXPECT_EQ(bracket_points(lb("genestealers"), 9), 140);
    EXPECT_EQ(bracket_points(lb("tyrant_guard"), 3), 80);
    EXPECT_EQ(bracket_points(lb("tyrant_guard"), 6), 170);
}

TEST(D079_ListData, WargearCosts) {
    const ModelWargear& rex = wargear("tyrannofex", "Tyrannofex");
    ASSERT_EQ(rex.slots.size(), 1u);
    const WargearSlot& hive = rex.slots[0];
    EXPECT_EQ(hive.name, "Fleshborer Hive");
    ASSERT_NE(hive.find_option("Rupture cannon"), nullptr);
    EXPECT_EQ(hive.find_option("Rupture cannon")->points, 20);
    EXPECT_EQ(hive.find_option("Acid spray")->points, 10);  // the link's own pts
    EXPECT_EQ(hive.find_option("Fleshborer hive")->points, 0);
    EXPECT_EQ(fit_option_points(rex, loadout_fits_slots(rex, {"Powerful limbs", "Stinger salvoes", "Rupture cannon"})),
              20);
    // Every other option of the ten is free.
    for (const auto& file : kFactionSheets) {
        for (const auto& w : lb(file).wargear) {
            for (const auto& s : w.slots) {
                for (const auto& o : s.options) {
                    if (file == "tyrannofex" && (o.name == "Rupture cannon" || o.name == "Acid spray")) continue;
                    EXPECT_EQ(o.points, 0) << file << " / " << o.name;
                }
            }
        }
    }
}

TEST(D079_ListData, CopyLimits) {
    for (const auto& file : kFactionSheets) {
        const bool battleline = file == "legionaries" || file == "termagants";
        EXPECT_EQ(lb(file).max_copies_default, battleline ? 6 : 3) << file;
        EXPECT_EQ(lb(file).max_copies_incursion, battleline ? 4 : 2) << file;
    }
}

TEST(D079_ListData, CopySurcharge) {
    const std::map<std::string, int> plus = {{"chaos_terminator_squad", 30}, {"havocs", 10}, {"genestealers", 10},
                                             {"screamer_killer", 10},        {"tyrannofex", 10}};
    for (const auto& file : kFactionSheets) {
        const auto& s = lb(file).surcharge;
        const auto it = plus.find(file);
        if (it == plus.end()) {
            EXPECT_FALSE(s.has_value()) << file;
            continue;
        }
        ASSERT_TRUE(s.has_value()) << file;
        EXPECT_EQ(s->earlier_copies, 2) << file;  // from the 3rd copy
        EXPECT_EQ(s->points, it->second) << file;
    }
    // The 3rd copy of a 5-model Terminator squad costs 185 + 30; the 2nd doesn't pay it.
    const std::vector<Group> five = {{"Terminator Champion", 1, {"Accursed weapon", "Combi-bolter"}},
                                     {"Accursed weapon and combi-bolter", 4, {"Accursed weapon", "Combi-bolter"}}};
    EXPECT_EQ(unit_points(sheet("chaos_terminator_squad"), five, 1), 185);
    EXPECT_EQ(unit_points(sheet("chaos_terminator_squad"), five, 2), 215);
}

TEST(D079_ListData, UnitAndModelLimitsAsRead) {
    EXPECT_EQ(describe_all(lb("legionaries").unit_limits),
              (Names{"model_type Legionary w/ other weapon max 2 (below 10: 1)", "weapons Havoc autocannon max 1",
                     "weapons Heavy bolter max 1", "weapons Lascannon max 1", "weapons Missile launcher max 1",
                     "weapons Reaper chaincannon max 1", "option Plasma pistol and Astartes chainsword max 1",
                     "weapons Meltagun max 1", "weapons Flamer max 1", "weapons Plasma gun max 1"}));
    EXPECT_EQ(describe_all(lb("chaos_terminator_squad").unit_limits),
              (Names{"weapons Power fist max 6 (below 10: 3)", "weapons Chainfist max 2 (below 10: 1)",
                     "model_type Heavy weapon max 2 (below 10: 1)",
                     "weapons Paired accursed weapons max 2 (below 10: 1)"}));
    EXPECT_EQ(describe_all(lb("termagants").unit_limits),
              (Names{"weapons Shardlauncher max 0 (+1 per 10)", "weapons Spike rifle max 0 (+1 per 10)",
                     "weapons Strangleweb max 0 (+1 per 10)"}));
    for (const char* file : {"chaos_lord", "havocs", "genestealers", "tyrant_guard", "screamer_killer", "hive_tyrant",
                             "tyrannofex"}) {
        EXPECT_TRUE(lb(file).unit_limits.empty()) << file;
    }
    // Per model: written only where they bind.
    std::map<std::string, Names> per_model;
    for (const auto& file : kFactionSheets) {
        for (const auto& w : lb(file).wargear) {
            if (!w.limits.empty()) per_model[file + " / " + w.model_type] = describe_all(w.limits);
        }
    }
    EXPECT_EQ(per_model, (std::map<std::string, Names>{
                             {"legionaries / Aspiring Champion", {"weapons Plasma pistol max 1"}},
                             {"havocs / Havoc Champion", {"weapons Accursed weapon max 1"}},
                             {"hive_tyrant / Hive Tyrant", {"weapons Heavy venom cannon+Stranglethorn cannon max 1"}}}));

    // Maxima by unit size.
    const auto& term = lb("chaos_terminator_squad").unit_limits;
    EXPECT_EQ(limit_max(term[0], 5), 3);
    EXPECT_EQ(limit_max(term[0], 9), 3);
    EXPECT_EQ(limit_max(term[0], 10), 6);
    EXPECT_EQ(limit_max(term[1], 9), 1);
    EXPECT_EQ(limit_max(term[1], 10), 2);
    EXPECT_EQ(limit_max(term[3], 5), 1);
    EXPECT_EQ(limit_max(term[3], 10), 2);
    const auto& gants = lb("termagants").unit_limits;
    EXPECT_EQ(limit_max(gants[0], 10), 1);
    EXPECT_EQ(limit_max(gants[0], 19), 1);
    EXPECT_EQ(limit_max(gants[0], 20), 2);
    EXPECT_EQ(limit_max(lb("legionaries").unit_limits[0], 9), 1);
    EXPECT_EQ(limit_max(lb("legionaries").unit_limits[0], 10), 2);

    // The Khorne Berzerkers' "Max 1 plasma pistol per 5 models" is not applied: an Aspiring Champion's plasma
    // pistol and the "Plasma pistol and Astartes chainsword" special are both allowed in one unit.
    for (const auto& l : lb("legionaries").unit_limits) {
        EXPECT_FALSE(l.counts == LimitCounts::Weapons &&
                     std::find(l.names.begin(), l.names.end(), "Plasma pistol") != l.names.end());
    }
    EXPECT_TRUE(unit_problems(sheet("legionaries"),
                              {{"Aspiring Champion", 1, {"Plasma pistol", "Boltgun", "Close combat weapon"}},
                               {"Legionary w/ other weapon",
                                1,
                                {"Bolt pistol", "Close combat weapon", "Astartes chainsword", "Plasma pistol"}},
                               {"Legionary w/ boltgun", 8, {"Boltgun", "Bolt pistol", "Close combat weapon"}}})
                    .empty());
    // But two specials need 10 models, and never the same weapon twice.
    const std::vector<Group> two_lascannons = {
        {"Aspiring Champion", 1, {"Bolt pistol", "Boltgun", "Close combat weapon"}},
        {"Legionary w/ other weapon", 2, {"Bolt pistol", "Close combat weapon", "Lascannon"}},
        {"Legionary w/ boltgun", 7, {"Boltgun", "Bolt pistol", "Close combat weapon"}}};
    EXPECT_EQ(unit_problems(sheet("legionaries"), two_lascannons), (Names{"unit: weapons Lascannon max 1"}));

    // No Terminator combi-weapon limit at any size (BSData's are scoped to Chosen).
    for (const auto& l : term) {
        EXPECT_TRUE(std::find(l.names.begin(), l.names.end(), "Combi-weapon") == l.names.end());
    }
    EXPECT_TRUE(unit_problems(sheet("chaos_terminator_squad"),
                              {{"Terminator Champion", 1, {"Accursed weapon", "Combi-weapon"}},
                               {"Accursed weapon and combi-weapon", 9, {"Accursed weapon", "Combi-weapon"}}})
                    .empty());
    // Power fists: 3 below 10 models (the champion's counts too).
    EXPECT_EQ(unit_problems(sheet("chaos_terminator_squad"),
                            {{"Terminator Champion", 1, {"Power fist", "Combi-bolter"}},
                             {"Power fist and combi-bolter", 3, {"Combi-bolter", "Power fist"}},
                             {"Accursed weapon and combi-bolter", 1, {"Combi-bolter", "Accursed weapon"}}}),
              (Names{"unit: weapons Power fist max 6 (below 10: 3)"}));

    // A Hive Tyrant may take two Monstrous scything talons, but only one of its two cannons.
    EXPECT_TRUE(unit_problems(sheet("hive_tyrant"),
                              {{"Hive Tyrant", 1, {"Monstrous scything talons", "Monstrous scything talons"}}})
                    .empty());
    EXPECT_EQ(unit_problems(sheet("hive_tyrant"), {{"Hive Tyrant", 1, {"Heavy venom cannon", "Stranglethorn cannon"}}}),
              (Names{"Hive Tyrant: weapons Heavy venom cannon+Stranglethorn cannon max 1"}));
    // The Havoc Champion: two power fists (both links lift the maximum), not two accursed weapons.
    const auto havocs = [](Names champion) {
        return std::vector<Group>{{"Havoc Champion", 1, std::move(champion)},
                                  {"Havoc w/ lascannon", 4, {"Havoc lascannon", "Close combat weapon"}}};
    };
    EXPECT_TRUE(unit_problems(sheet("havocs"), havocs({"Power fist", "Power fist"})).empty());
    EXPECT_EQ(unit_problems(sheet("havocs"), havocs({"Accursed weapon", "Accursed weapon"})),
              (Names{"Havoc Champion: weapons Accursed weapon max 1"}));
    // Termagant specials: one each per 10 models.
    const std::vector<Group> gants10 = {{"Termagants", 8, {"Fleshborer", "Chitinous claws and teeth"}},
                                        {"Termagant w/ Spike rifle", 2, {"Spike rifle", "Chitinous claws and teeth"}}};
    EXPECT_EQ(unit_problems(sheet("termagants"), gants10), (Names{"unit: weapons Spike rifle max 0 (+1 per 10)"}));
}

TEST(D079_ListData, StockRostersCost960And955) {
    const Json scenario = read_json_file(repo_root() / "data/scenarios/rosters_44x60.json");
    std::map<std::string, int> totals;
    for (const char* side : {"A", "B"}) {
        std::map<std::string, int> copies;
        for (const Json& unit : scenario.at("sides").at(side).at("units")) {
            const std::string file = sheet_name_of(unit.at("unit_file").get<std::string>());
            const Datasheet& ds = sheet(file);
            ASSERT_TRUE(ds.list_building.has_value()) << file;
            const auto groups = groups_of(unit);
            EXPECT_TRUE(unit_problems(ds, groups).empty()) << file;
            const int points = unit_points(ds, groups, copies[file]++);
            EXPECT_EQ(points, unit.at("points").get<int>()) << file;
            totals[side] += points;
        }
    }
    EXPECT_EQ(totals["A"], 960);
    EXPECT_EQ(totals["B"], 955);
}

TEST(D079_ListData, MalformedListDataIsADataError) {
    const Json terminators = read_json_file(repo_root() / "data/units/chaos_terminator_squad.json");
    const Json rex = read_json_file(repo_root() / "data/units/tyrannofex.json");
    ASSERT_NO_THROW((void)parse_datasheet(terminators));
    ASSERT_NO_THROW((void)parse_datasheet(rex));
    struct Case {
        const char* what;
        const Json* base;
        std::function<void(Json&)> mutate;
    };
    const std::vector<Case> cases = {
        {"an unknown weapon", &rex,
         [](Json& j) { j["list_building"]["wargear"][0]["slots"][0]["options"][0]["weapons"][0] = "Laser sword"; }},
        {"a weapon outside the model type's weapon_options", &terminators,
         [](Json& j) { j["list_building"]["wargear"][0]["fixed"].push_back("Power fist"); }},
        {"an unknown model type", &rex, [](Json& j) { j["list_building"]["wargear"][0]["model_type"] = "Carnifex"; }},
        {"a model type left out", &terminators, [](Json& j) { j["list_building"]["wargear"].erase(0); }},
        {"model types out of order", &terminators,
         [](Json& j) { std::swap(j["list_building"]["wargear"][0], j["list_building"]["wargear"][1]); }},
        {"an empty slot", &rex, [](Json& j) { j["list_building"]["wargear"][0]["slots"][0]["options"] = Json::array(); }},
        {"an option with no weapons", &rex,
         [](Json& j) { j["list_building"]["wargear"][0]["slots"][0]["options"][0]["weapons"] = Json::array(); }},
        {"two options of one name", &rex,
         [](Json& j) {
             auto& o = j["list_building"]["wargear"][0]["slots"][0]["options"];
             o[1]["name"] = o[0]["name"];
         }},
        {"a default that is not an option", &rex,
         [](Json& j) { j["list_building"]["wargear"][0]["slots"][0]["default"] = "Venom cannon"; }},
        {"negative option points", &rex,
         [](Json& j) { j["list_building"]["wargear"][0]["slots"][0]["options"][0]["points"] = -5; }},
        {"brackets not increasing", &terminators,
         [](Json& j) { j["list_building"]["points"][1]["min_models"] = 5; }},
        {"a first bracket past the composition's minimum", &terminators,
         [](Json& j) { j["list_building"]["points"][0]["min_models"] = 4; }},
        {"a bracket past the composition's maximum", &terminators,
         [](Json& j) { j["list_building"]["points"].push_back(Json{{"min_models", 11}, {"points", 400}}); }},
        {"a bracket with 0 points", &rex, [](Json& j) { j["list_building"]["points"][0]["points"] = 0; }},
        {"no brackets", &rex, [](Json& j) { j["list_building"]["points"] = Json::array(); }},
        {"0 copies", &rex, [](Json& j) { j["list_building"]["max_copies"]["incursion"] = 0; }},
        {"a surcharge after 0 copies", &rex,
         [](Json& j) { j["list_building"]["copy_surcharge"]["earlier_copies"] = 0; }},
        {"a limit counting two things", &terminators,
         [](Json& j) { j["list_building"]["unit_limits"][0]["option"] = "Paired accursed weapons"; }},
        {"a limit counting nothing", &terminators,
         [](Json& j) { j["list_building"]["unit_limits"][0].erase("weapons"); }},
        {"a limit on an unknown weapon", &terminators,
         [](Json& j) { j["list_building"]["unit_limits"][0]["weapons"][0] = "Lightning claws"; }},
        {"a limit on an unknown option", &terminators,
         [](Json& j) {
             j["list_building"]["unit_limits"].push_back(Json{{"option", "Power fist and Storm bolter"}, {"max", 1}});
         }},
        {"a limit on an unknown model type", &terminators,
         [](Json& j) { j["list_building"]["unit_limits"][2]["model_type"] = "Chosen"; }},
        {"a negative maximum", &terminators, [](Json& j) { j["list_building"]["unit_limits"][0]["max"] = -1; }},
        {"both below_models and per_models", &terminators,
         [](Json& j) { j["list_building"]["unit_limits"][0]["per_models"] = Json{{"models", 5}, {"add", 1}}; }},
        {"a per-model limit depending on the unit's size", &rex,
         [](Json& j) {
             j["list_building"]["wargear"][0]["limits"].push_back(
                 Json{{"weapons", Json::array({"Acid spray"})}, {"max", 1}, {"below_models", {{"models", 2}, {"max", 0}}}});
         }},
        {"a per-model limit counting a model type", &rex,
         [](Json& j) {
             j["list_building"]["wargear"][0]["limits"].push_back(Json{{"model_type", "Tyrannofex"}, {"max", 1}});
         }},
    };
    for (const Case& c : cases) {
        Json j = *c.base;
        c.mutate(j);
        try {
            (void)parse_datasheet(j);
            ADD_FAILURE() << c.what << " was accepted";
        } catch (const DataError& e) {
            EXPECT_NE(std::string(e.what()).find("D-079"), std::string::npos) << c.what << ": " << e.what();
        }
    }
}

// ---- 02.02: files without list data ----

TEST(R02_02_Datasheet, FilesWithoutListDataStillLoad) {
    const Datasheet intercessors = load_datasheet(repo_root() / "data/units/intercessor_squad.json");
    EXPECT_FALSE(intercessors.list_building.has_value());
    EXPECT_EQ(intercessors.model_types.size(), 3u);

    Json rex = read_json_file(repo_root() / "data/units/tyrannofex.json");
    rex.erase("list_building");
    const Datasheet without = parse_datasheet(rex);
    EXPECT_FALSE(without.list_building.has_value());
    const Datasheet& with = sheet("tyrannofex");
    EXPECT_EQ(without.weapons.size(), with.weapons.size());
    EXPECT_EQ(without.model_types.front().weapon_options, with.model_types.front().weapon_options);
}

// ---- 02.07: wargear slots ----

TEST(R02_07_WargearOptions, ALoadoutDecomposesIntoFixedWeaponsAndOneOptionPerSlot) {
    const ModelWargear& champion = wargear("legionaries", "Aspiring Champion");
    const SlotFit stock = loadout_fits_slots(champion, {"Astartes chainsword", "Close combat weapon", "Bolt pistol"});
    ASSERT_TRUE(stock.fits()) << stock.problem;
    EXPECT_EQ(chosen(champion, stock), (Names{"Bolt pistol", "Astartes chainsword"}));

    // Nested slots flattened into composite options.
    const ModelWargear& term = wargear("chaos_terminator_squad", "Terminator Champion");
    const SlotFit fist = loadout_fits_slots(term, {"Combi-weapon", "Power fist"});
    ASSERT_TRUE(fist.fits()) << fist.problem;
    EXPECT_EQ(chosen(term, fist), (Names{"Power fist and Combi-weapon"}));
    // A composite option of two weapons.
    const ModelWargear& special = wargear("legionaries", "Legionary w/ other weapon");
    const SlotFit pistol =
        loadout_fits_slots(special, {"Plasma pistol", "Bolt pistol", "Close combat weapon", "Astartes chainsword"});
    ASSERT_TRUE(pistol.fits()) << pistol.problem;
    EXPECT_EQ(chosen(special, pistol), (Names{"Plasma pistol and Astartes chainsword"}));
    // The same weapon in two slots.
    const ModelWargear& tyrant = wargear("hive_tyrant", "Hive Tyrant");
    const SlotFit talons = loadout_fits_slots(tyrant, {"Monstrous scything talons", "Monstrous scything talons"});
    ASSERT_TRUE(talons.fits()) << talons.problem;
    EXPECT_EQ(chosen(tyrant, talons), (Names{"Monstrous scything talons", "Monstrous scything talons"}));
    // Deterministic: the first decomposition in slot order and option order.
    const SlotFit either = loadout_fits_slots(tyrant, {"Monstrous scything talons", "Heavy venom cannon"});
    ASSERT_TRUE(either.fits());
    EXPECT_EQ(chosen(tyrant, either), (Names{"Heavy venom cannon", "Monstrous scything talons"}));
    // No slots: the fixed weapons alone.
    const SlotFit genestealer =
        loadout_fits_slots(wargear("genestealers", "Genestealer"), {"Genestealers claws and talons"});
    EXPECT_TRUE(genestealer.fits());
    EXPECT_TRUE(genestealer.options.empty());
}

TEST(R02_07_WargearOptions, ALoadoutThatDoesNotDecomposeIsRefusedWithAReason) {
    const ModelWargear& lord = wargear("chaos_lord", "Chaos Lord");
    const SlotFit one = loadout_fits_slots(lord, {"Plasma pistol"});
    EXPECT_FALSE(one.fits());
    EXPECT_NE(one.problem.find("02.07"), std::string::npos);
    EXPECT_NE(one.problem.find("D-079"), std::string::npos);
    EXPECT_TRUE(one.options.empty());
    // Two options of the same slot.
    EXPECT_FALSE(loadout_fits_slots(lord, {"Plasma pistol", "Power fist"}).fits());
    // A missing fixed weapon, and a weapon too many.
    const ModelWargear& gant = wargear("termagants", "Termagants");
    const SlotFit no_claws = loadout_fits_slots(gant, {"Fleshborer"});
    EXPECT_FALSE(no_claws.fits());
    EXPECT_NE(no_claws.problem.find("fixed weapon 'Chitinous claws and teeth'"), std::string::npos);
    EXPECT_FALSE(loadout_fits_slots(gant, {"Fleshborer", "Chitinous claws and teeth", "Fleshborer"}).fits());
    EXPECT_FALSE(loadout_fits_slots(gant, {"Fleshborer", "Chitinous claws and teeth", "Spike rifle"}).fits());
    // A composite option's weapons must come together.
    EXPECT_FALSE(loadout_fits_slots(wargear("legionaries", "Legionary w/ other weapon"),
                                    {"Bolt pistol", "Close combat weapon", "Plasma pistol"})
                     .fits());
}

TEST(R02_07_WargearOptions, EveryShippedScenarioFitsTheSlotModel) {
    // Non-fatal in the scenario loader (its flat 02.07 check is unchanged); checked here for every unit of every
    // scenario whose datasheet has list data: the slots, the limits, and the unit's points where it gives them.
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    for (const char* dir : {"data/scenarios", "tests/data/scenarios"}) {
        for (const auto& e : fs::directory_iterator(repo_root() / dir)) {
            if (e.path().extension() == ".json") files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    ASSERT_GE(files.size(), 8u);
    int checked = 0;
    for (const auto& path : files) {
        const Json scenario = read_json_file(path);
        for (const auto& [side, s] : scenario.at("sides").items()) {
            std::map<std::string, int> copies;
            for (const Json& unit : s.at("units")) {
                const std::string file = sheet_name_of(unit.at("unit_file").get<std::string>());
                const Datasheet& ds = sheet(file);
                if (!ds.list_building) continue;
                const auto groups = groups_of(unit);
                EXPECT_EQ(unit_problems(ds, groups), Names{}) << path.filename() << " side " << side << " " << file;
                const int points = unit_points(ds, groups, copies[file]++);
                if (unit.contains("points")) {
                    EXPECT_EQ(points, unit.at("points").get<int>()) << path.filename() << " " << file;
                }
                ++checked;
            }
        }
    }
    EXPECT_GE(checked, 40);  // the Termagants of V1 and the bot scenarios, Strike's, and every roster unit
}
