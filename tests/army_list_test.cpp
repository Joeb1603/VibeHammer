// M72 / D-079 part 2: army lists for custom battles (points, the list rules, refused weapons, new units, random
// armies), the auto-layout and the custom battle scenario.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/data/army_list.hpp"
#include "core/data/army_rules.hpp"
#include "core/data/formation.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/data/zone_layout.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/serialization/json.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::army_resolver;
using wh40k::test::csm;
using wh40k::test::group;
using wh40k::test::in;
using wh40k::test::repo_root;
using wh40k::test::tyranids;
using Json = nlohmann::json;
using Names = std::vector<std::string>;

namespace {

// "02.06: ...", "02.07 (D-079): ...", "D-079: ...", "02.05 (D-079): ..."
bool cited(const std::string& reason) {
    static const std::regex kCited(R"(^(\d\d\.\d\d( \(D-\d{3}\))?|D-\d{3}): .+)");
    return std::regex_match(reason, kCited);
}

const Datasheet& sheet(const std::string& name) {
    static std::map<std::string, Datasheet> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(name, army_resolver()("data/units/" + name + ".json")).first;
    return it->second;
}

std::string file(const std::string& name) { return "data/units/" + name + ".json"; }

ArmyUnit unit(const std::string& name, std::vector<ArmyModelGroup> groups) { return ArmyUnit{file(name), std::move(groups)}; }

Names problems_of(const std::string& name, std::vector<ArmyModelGroup> groups) {
    const Names p = unit_problems(sheet(name), unit(name, std::move(groups)));
    for (const std::string& r : p) EXPECT_TRUE(cited(r)) << r;
    return p;
}

bool has(const Names& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool has_prefix(const Names& v, const std::string& prefix) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return s.rfind(prefix, 0) == 0; });
}

Names reasons(const std::vector<ArmyProblem>& problems) {
    Names out;
    for (const ArmyProblem& p : problems) {
        EXPECT_TRUE(cited(p.reason)) << p.reason;
        out.push_back(p.reason);
    }
    return out;
}

std::string text_of(const std::filesystem::path& path) {
    std::ifstream in_file(path, std::ios::binary);
    std::stringstream s;
    s << in_file.rdbuf();
    return s.str();
}

ArmyList army(const std::string& faction_id, std::vector<ArmyUnit> units) {
    return ArmyList{"test", faction_id, std::move(units)};
}

ArmyUnit lord() { return *default_unit(sheet("chaos_lord"), file("chaos_lord")); }
ArmyUnit tyrant() { return *default_unit(sheet("hive_tyrant"), file("hive_tyrant")); }
ArmyUnit termagants10() { return unit("termagants", {group("Termagants", 10, {"Fleshborer", "Chitinous claws and teeth"})}); }

std::vector<Base> bases(const Base& b, int n) { return std::vector<Base>(static_cast<std::size_t>(n), b); }

const Base kTermagant{BaseShape::Round, 28.5f, 0.0f};
const Base kLegionary{BaseShape::Round, 32.0f, 0.0f};
const Base kTerminator{BaseShape::Round, 40.0f, 0.0f};
const Base kGuard{BaseShape::Round, 50.0f, 0.0f};
const Base kTyrannofex{BaseShape::Oval, 92.0f, 120.0f};

Battlefield field() { return Battlefield{in(44), in(60)}; }
DeploymentZone south() { return DeploymentZone{BattlefieldEdge::South, in(12)}; }
DeploymentZone north() { return DeploymentZone{BattlefieldEdge::North, in(12)}; }

bool on_grid(float inches) { return std::fmod(inches * 8.0f, 1.0f) == 0.0f; }

}  // namespace

// ==== Army files and factions ====

TEST(D079_ArmyList, JsonRoundTrip) {
    const ArmyList l = wh40k::test::showcase_csm();
    EXPECT_EQ(Json(l).get<ArmyList>(), l);
    EXPECT_EQ(Json::parse(army_file_text(l)).get<ArmyList>(), l);
    const Json j = Json(l);
    EXPECT_EQ(j.at("format"), 1);
    EXPECT_EQ(j.at("kind"), "vibehammer-army");
    EXPECT_FALSE(j.contains("points"));  // computed, never stored
    // Saved and loaded through a file.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "vibehammer_m72_army.json";
    save_army(l, tmp);
    EXPECT_EQ(load_army(tmp), l);
    EXPECT_EQ(text_of(tmp), army_file_text(l));
    std::filesystem::remove(tmp);
    // The stock files are written exactly as save_army writes them.
    for (const Faction& f : wh40k::test::factions()) {
        const ArmyList stock = wh40k::test::stock_army(f);
        EXPECT_EQ(text_of(repo_root() / f.default_army), army_file_text(stock)) << f.default_army;
        EXPECT_EQ(stock.faction, f.id);
    }
    // Wrong kind or format, or a malformed file.
    Json bad = j;
    bad["kind"] = "vibehammer-save";
    EXPECT_THROW((void)bad.get<ArmyList>(), DataError);
    bad = j;
    bad["format"] = 2;
    EXPECT_THROW((void)bad.get<ArmyList>(), DataError);
    EXPECT_THROW((void)load_army(repo_root() / "data/scenarios/rosters_44x60.json"), DataError);
    EXPECT_THROW((void)load_army(repo_root() / "data/armies/no_such_army.json"), DataError);
    // Factions.
    for (const Faction& f : wh40k::test::factions()) EXPECT_EQ(Json(f).get<Faction>(), f);
}

TEST(D079_ArmyList, FactionsHoldTheTenRosterDatasheets) {
    const auto& f = wh40k::test::factions();
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].id, "chaos_space_marines");
    EXPECT_EQ(f[0].name, "Chaos Space Marines");
    EXPECT_EQ(f[0].faction_keyword, "Heretic Astartes");
    EXPECT_EQ(f[0].unit_files, (Names{file("chaos_lord"), file("legionaries"), file("chaos_terminator_squad"), file("havocs")}));
    EXPECT_EQ(f[1].id, "tyranids");
    EXPECT_EQ(f[1].unit_files, (Names{file("hive_tyrant"), file("termagants"), file("genestealers"), file("tyrant_guard"),
                                      file("screamer_killer"), file("tyrannofex")}));
    for (const Faction& faction : f) {
        for (const std::string& u : faction.unit_files) {
            const Datasheet ds = army_resolver()(u);
            EXPECT_TRUE(ds.list_building.has_value()) << u;
            EXPECT_NE(std::find(ds.faction_keywords.begin(), ds.faction_keywords.end(), faction.faction_keyword),
                      ds.faction_keywords.end())
                << u;
        }
    }
    EXPECT_EQ(find_faction(f, "space_marines"), nullptr);  // the Intercessors stay for the old scenarios only
}

TEST(D079_ArmyList, StockArmiesAreLegalAt960And955) {
    const ArmyList a = wh40k::test::stock_army(csm());
    const ArmyList b = wh40k::test::stock_army(tyranids());
    EXPECT_EQ(reasons(army_problems(a, csm(), army_resolver())), Names{});
    EXPECT_EQ(reasons(army_problems(b, tyranids(), army_resolver())), Names{});
    EXPECT_EQ(army_points(a, army_resolver()), 960);
    EXPECT_EQ(army_points(b, army_resolver()), 955);
}

TEST(D079_ArmyList, StockArmiesMatchTheRostersScenarioUnitForUnit) {
    // The stock lists through custom_battle_scenario and build_game_state give the rosters scenario's game, unit for
    // unit (names, model types and counts, weapons in order, points), every model's position and facing excepted.
    const Json scenario = custom_battle_scenario(wh40k::test::stock_army(csm()), wh40k::test::stock_army(tyranids()),
                                                 Side::A, wh40k::test::factions(), army_resolver());
    GameState custom = build_game_state(scenario, army_resolver());
    const GameState rosters = wh40k::test::load_rosters_scenario();
    ASSERT_EQ(custom.units.size(), rosters.units.size());
    for (std::size_t u = 0; u < custom.units.size(); ++u) {
        Unit& c = custom.units[u];
        const Unit& r = rosters.units[u];
        EXPECT_EQ(c.name, r.name);
        EXPECT_EQ(c.points, r.points) << c.name;
        ASSERT_EQ(c.models.size(), r.models.size()) << c.name;
        for (std::size_t m = 0; m < c.models.size(); ++m) {
            EXPECT_EQ(c.models[m].type_name, r.models[m].type_name);
            ASSERT_EQ(c.models[m].weapons.size(), r.models[m].weapons.size());
            for (std::size_t w = 0; w < c.models[m].weapons.size(); ++w) {
                EXPECT_EQ(c.models[m].weapons[w].name, r.models[m].weapons[w].name);
            }
            c.models[m].position = r.models[m].position;
            c.models[m].facing_deg = r.models[m].facing_deg;
        }
    }
    EXPECT_EQ(custom.sides[0].name, "Chaos Space Marines");
    EXPECT_EQ(custom.sides[1].name, "Tyranids");
    custom.scenario_name = rosters.scenario_name;
    EXPECT_EQ(custom, rosters);  // everything else: battlefield, zones, rounds, first turn, result rule, units
}

// ==== 02.06 / 02.07 ====

TEST(R02_06_UnitComposition, ArmyCountsOutsideTheDatasheetAreRefused) {
    const auto champion = group("Aspiring Champion", 1, {"Bolt pistol", "Boltgun", "Close combat weapon"});
    const auto boltguns = [](int n) { return group("Legionary w/ boltgun", n, {"Boltgun", "Bolt pistol", "Close combat weapon"}); };
    EXPECT_EQ(problems_of("legionaries", {champion, boltguns(4)}), Names{});
    EXPECT_EQ(problems_of("legionaries", {champion, boltguns(9)}), Names{});
    // Too many of a type, and too many in the unit.
    Names p = problems_of("legionaries", {champion, boltguns(10)});
    EXPECT_TRUE(has(p, "02.06: 'Legionary w/ boltgun' count 10 outside 0-9")) << p.front();
    EXPECT_TRUE(has(p, "02.06: unit size 11 outside 5-10"));
    p = problems_of("legionaries", {champion, boltguns(3)});
    EXPECT_EQ(p, Names{"02.06: unit size 4 outside 5-10"});
    // Two champions; no champion.
    p = problems_of("legionaries", {champion, champion, boltguns(3)});
    EXPECT_TRUE(has(p, "02.06: 'Aspiring Champion' count 2 outside 1-1"));
    p = problems_of("legionaries", {boltguns(5)});
    EXPECT_TRUE(has(p, "02.06: 'Aspiring Champion' count 0 outside 1-1"));
    // A type of another datasheet, and an empty group.
    p = problems_of("legionaries", {champion, boltguns(4), group("Termagants", 1, {"Fleshborer"})});
    EXPECT_TRUE(has(p, "02.06: 'Legionaries' has no model type 'Termagants'"));
    p = problems_of("legionaries", {champion, boltguns(4), group("Legionary w/ chainsword", 0, {})});
    EXPECT_TRUE(has(p, "02.06: model group 'Legionary w/ chainsword' count must be at least 1, not 0"));
    // The Termagants' own minimum (7) inside the unit's (10).
    p = problems_of("termagants", {group("Termagants", 6, {"Fleshborer", "Chitinous claws and teeth"}),
                                   group("Termagant w/ Spike rifle", 1, {"Spike rifle", "Chitinous claws and teeth"})});
    EXPECT_TRUE(has(p, "02.06: 'Termagants' count 6 outside 7-20"));
    EXPECT_TRUE(has(p, "02.06: unit size 7 outside 10-20"));
}

TEST(R02_07_WargearOptions, ArmyWeaponsMustDecompose) {
    // Fixed weapons plus one option per slot (M71's loadout_fits_slots).
    EXPECT_EQ(problems_of("hive_tyrant", {group("Hive Tyrant", 1, {"Monstrous scything talons", "Monstrous scything talons"})}),
              Names{});
    Names p = problems_of("hive_tyrant",
                          {group("Hive Tyrant", 1, {"Monstrous bonesword and lash whip", "Monstrous bonesword and lash whip"})});
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].rfind("02.07 (D-079): 'Hive Tyrant' with ", 0), 0u) << p[0];
    p = problems_of("hive_tyrant", {group("Hive Tyrant", 1, {"Monstrous scything talons"})});  // a slot left empty
    EXPECT_TRUE(has_prefix(p, "02.07 (D-079): ")) << p.front();
    // A fixed weapon left out; a weapon of another model type.
    p = problems_of("legionaries", {group("Aspiring Champion", 1, {"Bolt pistol", "Boltgun", "Close combat weapon"}),
                                    group("Legionary w/ boltgun", 4, {"Boltgun", "Bolt pistol"})});
    EXPECT_TRUE(has(p, "02.07 (D-079): 'Legionary w/ boltgun' must carry its fixed weapon 'Close combat weapon'"));
    p = problems_of("legionaries", {group("Aspiring Champion", 1, {"Boltgun", "Boltgun", "Close combat weapon"}),
                                    group("Legionary w/ boltgun", 4, {"Boltgun", "Bolt pistol", "Close combat weapon"})});
    EXPECT_TRUE(has_prefix(p, "02.07 (D-079): 'Aspiring Champion' with "));
    p = problems_of("tyrannofex", {group("Tyrannofex", 1, {"Powerful limbs", "Stinger salvoes", "Rupture cannon", "Acid spray"})});
    EXPECT_TRUE(has_prefix(p, "02.07 (D-079): 'Tyrannofex' with "));
    // A weapon the datasheet doesn't have at all.
    p = problems_of("tyrannofex", {group("Tyrannofex", 1, {"Powerful limbs", "Stinger salvoes", "Lascannon"})});
    EXPECT_TRUE(has(p, "D-079: 'Tyrannofex' carries 'Lascannon', which can't be picked: D-079: 'Tyrannofex' has no "
                       "weapon 'Lascannon'"));
    EXPECT_TRUE(has_prefix(p, "02.07 (D-079): 'Tyrannofex' with "));
}

TEST(R02_07_WargearOptions, LimitsAreEnforced) {
    // Terminator power fists: 3 below 10 models, 6 at 10.
    const auto t_champ = group("Terminator Champion", 1, {"Accursed weapon", "Combi-bolter"});
    const auto t_champ_fist = group("Terminator Champion", 1, {"Power fist", "Combi-bolter"});
    const auto fists = [](int n) { return group("Power fist and combi-bolter", n, {"Combi-bolter", "Power fist"}); };
    const auto accursed = [](int n) { return group("Accursed weapon and combi-bolter", n, {"Combi-bolter", "Accursed weapon"}); };
    EXPECT_EQ(problems_of("chaos_terminator_squad", {t_champ, fists(3), accursed(1)}), Names{});
    EXPECT_EQ(problems_of("chaos_terminator_squad", {t_champ, fists(4)}),
              Names{"D-079: a unit of 5 models may have at most 3 'Power fist' (it has 4)"});
    EXPECT_EQ(problems_of("chaos_terminator_squad", {t_champ_fist, fists(3), accursed(1)}),
              Names{"D-079: a unit of 5 models may have at most 3 'Power fist' (it has 4)"});
    EXPECT_EQ(problems_of("chaos_terminator_squad", {t_champ, fists(6), accursed(3)}), Names{});
    EXPECT_EQ(problems_of("chaos_terminator_squad", {t_champ_fist, fists(6), accursed(3)}),
              Names{"D-079: a unit of 10 models may have at most 6 'Power fist' (it has 7)"});
    // Chainfists: 1 below 10 models, 2 at 10 (the weapon is refused today too, D-071).
    const auto chainfists = [](int n) { return group("Chainfist and combi-bolter", n, {"Chainfist", "Combi-bolter"}); };
    Names p = problems_of("chaos_terminator_squad", {t_champ, chainfists(2), accursed(2)});
    EXPECT_TRUE(has(p, "D-079: a unit of 5 models may have at most 1 'Chainfist' (it has 2)"));
    p = problems_of("chaos_terminator_squad", {t_champ, chainfists(2), accursed(7)});
    EXPECT_FALSE(has_prefix(p, "D-079: a unit of 10 models may have at most"));
    EXPECT_TRUE(has_prefix(p, "D-079: 'Chainfist and combi-bolter' carries 'Chainfist', which can't be picked: D-071: "));
    // Paired accursed weapons, the champion's option included: 1 below 10 models, 2 at 10.
    const auto paired = group("Paired accursed weapons", 1, {"Paired accursed weapons"});
    const auto champ_paired = group("Terminator Champion", 1, {"Paired accursed weapons"});
    EXPECT_EQ(problems_of("chaos_terminator_squad", {champ_paired, paired, accursed(3)}),
              Names{"D-079: a unit of 5 models may have at most 1 'Paired accursed weapons' (it has 2)"});
    EXPECT_EQ(problems_of("chaos_terminator_squad", {champ_paired, paired, accursed(8)}), Names{});

    // Legionaries: one special weapon below 10 models (Legionary w/ other weapon), two at 10, each weapon once.
    const auto l_champ = group("Aspiring Champion", 1, {"Bolt pistol", "Boltgun", "Close combat weapon"});
    const auto other = [](const std::string& w) {
        return group("Legionary w/ other weapon", 1, {"Bolt pistol", "Close combat weapon", w});
    };
    const auto chainswords = [](int n) {
        return group("Legionary w/ chainsword", n, {"Astartes chainsword", "Bolt pistol", "Close combat weapon"});
    };
    EXPECT_EQ(problems_of("legionaries", {l_champ, other("Lascannon"), chainswords(3)}), Names{});
    EXPECT_EQ(problems_of("legionaries", {l_champ, other("Lascannon"), other("Heavy bolter"), chainswords(2)}),
              Names{"D-079: a unit of 5 models may have at most 1 'Legionary w/ other weapon' model(s) (it has 2)"});
    EXPECT_EQ(problems_of("legionaries", {l_champ, other("Lascannon"), other("Heavy bolter"), chainswords(7)}), Names{});
    EXPECT_EQ(problems_of("legionaries", {l_champ, other("Lascannon"), other("Lascannon"), chainswords(7)}),
              Names{"D-079: a unit of 10 models may have at most 1 'Lascannon' (it has 2)"});
    // The champion's plasma pistol and the special "Plasma pistol and Astartes chainsword" together (D-079 part 1), but
    // the special only once and the champion one plasma pistol.
    const auto pp_special = group("Legionary w/ other weapon", 1,
                                  {"Bolt pistol", "Close combat weapon", "Plasma pistol", "Astartes chainsword"});
    const auto pp_champ = group("Aspiring Champion", 1, {"Plasma pistol", "Boltgun", "Close combat weapon"});
    EXPECT_EQ(problems_of("legionaries", {pp_champ, pp_special, chainswords(8)}), Names{});
    EXPECT_EQ(problems_of("legionaries", {pp_champ, pp_special, pp_special, chainswords(7)}),
              Names{"D-079: a unit of 10 models may have at most 1 model(s) with the option 'Plasma pistol and "
                    "Astartes chainsword' (it has 2)"});
    EXPECT_EQ(problems_of("legionaries", {group("Aspiring Champion", 1, {"Plasma pistol", "Plasma pistol", "Close combat weapon"}),
                                          chainswords(4)}),
              Names{"D-079: each 'Aspiring Champion' may carry at most 1 'Plasma pistol' (it carries 2)"});

    // Termagant specials: 0 + 1 per 10 models, each.
    const auto gants = [](int n) { return group("Termagants", n, {"Fleshborer", "Chitinous claws and teeth"}); };
    const auto shard = [](int n) { return group("Termagant w/ Shardlauncher", n, {"Shardlauncher", "Chitinous claws and teeth"}); };
    const auto spike = [](int n) { return group("Termagant w/ Spike rifle", n, {"Spike rifle", "Chitinous claws and teeth"}); };
    EXPECT_EQ(problems_of("termagants", {gants(8), shard(1), spike(1)}), Names{});
    EXPECT_EQ(problems_of("termagants", {gants(8), shard(2)}),
              Names{"D-079: a unit of 10 models may have at most 1 'Shardlauncher' (it has 2)"});
    EXPECT_EQ(problems_of("termagants", {gants(16), shard(2), spike(2)}), Names{});
    EXPECT_EQ(problems_of("termagants", {gants(18), shard(1)}), Names{});

    // The Hive Tyrant: one cannon at most, two scything talons allowed.
    EXPECT_EQ(problems_of("hive_tyrant", {group("Hive Tyrant", 1, {"Heavy venom cannon", "Monstrous scything talons"})}),
              Names{});
    EXPECT_EQ(problems_of("hive_tyrant", {group("Hive Tyrant", 1, {"Heavy venom cannon", "Stranglethorn cannon"})}),
              Names{"D-079: each 'Hive Tyrant' may carry at most 1 'Heavy venom cannon' and 'Stranglethorn cannon' in "
                    "all (it carries 2)"});
    EXPECT_EQ(problems_of("hive_tyrant", {group("Hive Tyrant", 1, {"Heavy venom cannon", "Heavy venom cannon"})}),
              Names{"D-079: each 'Hive Tyrant' may carry at most 1 'Heavy venom cannon' and 'Stranglethorn cannon' in "
                    "all (it carries 2)"});
    // The Havoc Champion: two power fists, but one accursed weapon.
    const auto havocs = [](const Names& champion) {
        return std::vector<ArmyModelGroup>{group("Havoc Champion", 1, champion),
                                           group("Havoc w/ lascannon", 4, {"Havoc lascannon", "Close combat weapon"})};
    };
    EXPECT_EQ(problems_of("havocs", havocs({"Power fist", "Power fist"})), Names{});
    EXPECT_EQ(problems_of("havocs", havocs({"Accursed weapon", "Accursed weapon"})),
              Names{"D-079: each 'Havoc Champion' may carry at most 1 'Accursed weapon' (it carries 2)"});
}

// ==== Points ====

TEST(D079_Points, BracketWargearAndSurcharge) {
    const auto legionaries = [](int n) {
        return unit("legionaries", {group("Aspiring Champion", 1, {"Bolt pistol", "Boltgun", "Close combat weapon"}),
                                    group("Legionary w/ boltgun", n - 1, {"Boltgun", "Bolt pistol", "Close combat weapon"})});
    };
    EXPECT_EQ(unit_points(sheet("legionaries"), legionaries(5), 0), 95);
    EXPECT_EQ(unit_points(sheet("legionaries"), legionaries(6), 0), 180);
    EXPECT_EQ(unit_points(sheet("legionaries"), legionaries(10), 0), 180);
    EXPECT_EQ(unit_points(sheet("legionaries"), legionaries(10), 5), 180);  // no surcharge
    const auto terminators = [](int n) {
        return unit("chaos_terminator_squad",
                    {group("Terminator Champion", 1, {"Accursed weapon", "Combi-bolter"}),
                     group("Accursed weapon and combi-bolter", n - 1, {"Combi-bolter", "Accursed weapon"})});
    };
    EXPECT_EQ(unit_points(sheet("chaos_terminator_squad"), terminators(5), 0), 185);
    EXPECT_EQ(unit_points(sheet("chaos_terminator_squad"), terminators(6), 0), 370);
    EXPECT_EQ(unit_points(sheet("chaos_terminator_squad"), terminators(5), 1), 185);
    EXPECT_EQ(unit_points(sheet("chaos_terminator_squad"), terminators(5), 2), 215);  // the third copy: +30
    const auto fex = [](const std::string& w) {
        return unit("tyrannofex", {group("Tyrannofex", 1, {"Powerful limbs", "Stinger salvoes", w})});
    };
    EXPECT_EQ(unit_points(sheet("tyrannofex"), fex("Fleshborer hive"), 0), 170);
    EXPECT_EQ(unit_points(sheet("tyrannofex"), fex("Rupture cannon"), 0), 190);
    EXPECT_EQ(unit_points(sheet("tyrannofex"), fex("Acid spray"), 0), 180);
    EXPECT_EQ(unit_points(sheet("tyrannofex"), fex("Acid spray"), 2), 190);  // +10 from the third copy
    // Earlier copies are counted in list order (the third Havocs pays +10; the list is over its copy limit too).
    const ArmyUnit havocs = *default_unit(sheet("havocs"), file("havocs"));
    EXPECT_EQ(army_points(army("chaos_space_marines", {havocs, lord(), havocs, havocs}), army_resolver()),
              135 + 95 + 135 + 145);
    EXPECT_EQ(army_points(wh40k::test::showcase_csm(), army_resolver()), 875);
    EXPECT_EQ(army_points(wh40k::test::showcase_tyranids(), army_resolver()), 900);
}

TEST(D079_Points, OverTheLimitIsRefused) {
    ArmyList a = wh40k::test::stock_army(csm());
    a.units.push_back(a.units.front());  // another 10 chainsword Legionaries (180 points)
    EXPECT_EQ(army_points(a, army_resolver()), 1140);
    Names p = reasons(army_problems(a, csm(), army_resolver()));
    EXPECT_EQ(p, Names{"D-079: the army costs 1140 points, over the limit of 1000"});
    EXPECT_EQ(army_problems(a, csm(), army_resolver(), 1140), std::vector<ArmyProblem>{});
    const std::vector<ArmyProblem> problems = army_problems(a, csm(), army_resolver());
    EXPECT_FALSE(problems.front().unit.has_value());  // the army's problem, not a unit's
}

// ==== Copies ====

TEST(D079_Copies, ThirdCopyRefused) {
    const ArmyList a = army("chaos_space_marines", {lord(), lord(), lord()});
    const std::vector<ArmyProblem> problems = army_problems(a, csm(), army_resolver());
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].unit, std::optional<std::size_t>(2));
    EXPECT_EQ(problems[0].reason, "D-079: an Incursion army may include at most 2 'Chaos Lord' units (this is the 3rd)");
    EXPECT_TRUE(army_problems(army("chaos_space_marines", {lord(), lord()}), csm(), army_resolver()).empty());
}

TEST(D079_Copies, BattlelineFour) {
    // Termagants and Legionaries are BATTLELINE: 4 copies at Incursion.
    ArmyList t = army("tyranids", {tyrant(), termagants10(), termagants10(), termagants10(), termagants10()});
    EXPECT_EQ(reasons(army_problems(t, tyranids(), army_resolver())), Names{});
    t.units.push_back(termagants10());
    const std::vector<ArmyProblem> problems = army_problems(t, tyranids(), army_resolver());
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].unit, std::optional<std::size_t>(5));
    EXPECT_EQ(problems[0].reason, "D-079: an Incursion army may include at most 4 'Termagants' units (this is the 5th)");
    const ArmyUnit legionaries = *default_unit(sheet("legionaries"), file("legionaries"));
    ArmyList c = army("chaos_space_marines", {lord(), legionaries, legionaries, legionaries, legionaries});
    EXPECT_EQ(reasons(army_problems(c, csm(), army_resolver())), Names{});
    c.units.push_back(legionaries);
    EXPECT_EQ(reasons(army_problems(c, csm(), army_resolver())),
              Names{"D-079: an Incursion army may include at most 4 'Legionaries' units (this is the 5th)"});
}

// ==== Factions ====

TEST(D079_Faction, OtherFactionsUnitsRefused) {
    ArmyList a = army("chaos_space_marines", {lord(), termagants10()});
    std::vector<ArmyProblem> problems = army_problems(a, csm(), army_resolver());
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].unit, std::optional<std::size_t>(1));
    EXPECT_EQ(problems[0].reason, "D-079: 'data/units/termagants.json' is not a Chaos Space Marines datasheet");
    a.units[1] = ArmyUnit{"data/units/intercessor_squad.json", {}};
    problems = army_problems(a, csm(), army_resolver());
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].reason, "D-079: 'data/units/intercessor_squad.json' is not a Chaos Space Marines datasheet");
    // A list of the other faction.
    EXPECT_TRUE(has(reasons(army_problems(wh40k::test::stock_army(tyranids()), csm(), army_resolver())),
                    "D-079: the list is for faction 'tyranids', not Chaos Space Marines"));
}

TEST(D079_Faction, OpposingFactionsOnly) {
    const ArmyList a = wh40k::test::stock_army(csm());
    const ArmyList b = wh40k::test::stock_army(tyranids());
    EXPECT_TRUE(battle_problems(a, b).empty());
    EXPECT_TRUE(battle_problems(b, a).empty());
    const std::vector<std::string> mirror = battle_problems(a, a);
    ASSERT_EQ(mirror.size(), 1u);
    EXPECT_EQ(mirror[0], "D-079: both armies are 'chaos_space_marines'; a custom battle needs two different factions");
    EXPECT_THROW((void)custom_battle_scenario(a, a, Side::A, wh40k::test::factions(), army_resolver()), DataError);
    EXPECT_THROW((void)custom_battle_scenario(b, b, Side::A, wh40k::test::factions(), army_resolver()), DataError);
    EXPECT_NO_THROW((void)custom_battle_scenario(b, a, Side::A, wh40k::test::factions(), army_resolver()));
}

// ==== CHARACTER (agreed with the user) ====

TEST(D079_Character, AnArmyNeedsACharacter) {
    const ArmyUnit legionaries = *default_unit(sheet("legionaries"), file("legionaries"));
    EXPECT_EQ(reasons(army_problems(army("chaos_space_marines", {legionaries}), csm(), army_resolver())),
              Names{"D-079: an army needs at least one CHARACTER unit ('Chaos Lord')"});
    EXPECT_EQ(reasons(army_problems(army("tyranids", {termagants10()}), tyranids(), army_resolver())),
              Names{"D-079: an army needs at least one CHARACTER unit ('Hive Tyrant')"});
    EXPECT_TRUE(army_problems(army("chaos_space_marines", {legionaries, lord()}), csm(), army_resolver()).empty());
    EXPECT_EQ(reasons(army_problems(army("tyranids", {}), tyranids(), army_resolver())),
              Names{"D-079: an army needs at least one unit"});
    // The custom battle refuses a side without one.
    EXPECT_THROW((void)custom_battle_scenario(army("chaos_space_marines", {legionaries}), wh40k::test::stock_army(tyranids()),
                                              Side::A, wh40k::test::factions(), army_resolver()),
                 DataError);
}

// ==== Refused weapons (agreed: greyed out with the reason) ====

TEST(D079_Refused, UnplayableWeaponsCantBePickedWithTheReason) {
    // D-079 part 1's list: refused today from their abilities (D-026, D-071).
    const std::vector<std::pair<std::string, std::string>> refused = {
        {"chaos_lord", "Daemon hammer"},          {"chaos_terminator_squad", "Combi-weapon"},
        {"chaos_terminator_squad", "Chainfist"},  {"chaos_terminator_squad", "Heavy flamer"},
        {"chaos_terminator_squad", "Reaper autocannon"}, {"legionaries", "Meltagun"},
        {"legionaries", "Flamer"},                {"havocs", "Meltagun"},
        {"havocs", "Flamer"},                     {"legionaries", "Balefire tome"},
        {"termagants", "Strangleweb"}};
    for (const auto& [ds, weapon] : refused) {
        const std::optional<std::string> r = weapon_refusal(sheet(ds), weapon);
        ASSERT_TRUE(r.has_value()) << ds << " " << weapon;
        EXPECT_TRUE(cited(*r)) << *r;
        // The engine's own reason for the first refused profile.
        std::optional<std::string> engine;
        for (const WeaponProfile& p : sheet(ds).find_weapon(weapon)->profiles) {
            if (!engine) engine = unimplemented_ability_problem(p);
        }
        EXPECT_EQ(r, engine);
    }
    // Every other weapon of the ten is pickable: each of its profiles is playable.
    std::set<std::string> refused_names;
    for (const auto& [ds, weapon] : refused) refused_names.insert(weapon);
    for (const Faction& f : wh40k::test::factions()) {
        for (const std::string& u : f.unit_files) {
            const Datasheet ds = army_resolver()(u);
            for (const Weapon& w : ds.weapons) {
                if (refused_names.count(w.name) > 0) continue;
                EXPECT_EQ(weapon_refusal(ds, w.name), std::nullopt) << u << " " << w.name;
            }
        }
    }
    // An option with a refused weapon is refused for it; a list with it can't be taken.
    const ModelWargear* champ = sheet("chaos_terminator_squad").list_building->find_wargear("Terminator Champion");
    const WargearOption* fist_combi = champ->slots[0].find_option("Power fist and Combi-weapon");
    ASSERT_NE(fist_combi, nullptr);
    EXPECT_EQ(option_refusal(sheet("chaos_terminator_squad"), *fist_combi),
              weapon_refusal(sheet("chaos_terminator_squad"), "Combi-weapon"));
    EXPECT_EQ(option_refusal(sheet("chaos_terminator_squad"), *champ->slots[0].find_option("Accursed weapon and Combi-bolter")),
              std::nullopt);
    const Names p = problems_of("chaos_lord", {group("Chaos Lord", 1, {"Daemon hammer", "Plasma pistol"})});
    EXPECT_EQ(p, Names{"D-079: 'Chaos Lord' carries 'Daemon hammer', which can't be picked: " +
                       *weapon_refusal(sheet("chaos_lord"), "Daemon hammer")});
}

// ==== New units ====

TEST(D079_Defaults, NewUnitsSkipRefusedWeapons) {
    const auto expect_unit = [](const std::string& name, const std::vector<ArmyModelGroup>& groups, int points) {
        const std::optional<ArmyUnit> u = default_unit(sheet(name), file(name));
        ASSERT_TRUE(u.has_value()) << name;
        EXPECT_EQ(u->unit_file, file(name));
        EXPECT_EQ(u->models, groups) << name;
        EXPECT_EQ(unit_problems(sheet(name), *u), Names{}) << name;
        EXPECT_EQ(unit_points(sheet(name), *u, 0), points) << name;
    };
    // The Lord's default Daemon hammer is refused: the first pickable option of that slot, the Accursed weapon.
    expect_unit("chaos_lord", {group("Chaos Lord", 1, {"Accursed weapon", "Plasma pistol"})}, 95);
    // The Havoc Champion's default Flamer is refused: the first pickable ranged option, the Plasma pistol.
    expect_unit("havocs",
                {group("Havoc Champion", 1, {"Astartes chainsword", "Plasma pistol"}),
                 group("Havoc w/ autocannon", 4, {"Havoc autocannon", "Close combat weapon"})},
                135);
    expect_unit("chaos_terminator_squad",
                {group("Accursed weapon and combi-bolter", 4, {"Combi-bolter", "Accursed weapon"}),
                 group("Terminator Champion", 1, {"Accursed weapon", "Combi-bolter"})},
                185);
    expect_unit("legionaries",
                {group("Aspiring Champion", 1, {"Close combat weapon", "Bolt pistol", "Boltgun"}),
                 group("Legionary w/ boltgun", 4, {"Boltgun", "Bolt pistol", "Close combat weapon"})},
                95);
    expect_unit("termagants", {group("Termagants", 10, {"Chitinous claws and teeth", "Fleshborer"})}, 55);
    expect_unit("genestealers", {group("Genestealer", 5, {"Genestealers claws and talons"})}, 75);
    expect_unit("tyrant_guard", {group("Tyrant Guard", 3, {"Scything talons and rending claws"})}, 80);
    expect_unit("screamer_killer", {group("Screamer-killer", 1, {"Bio-plasmic scream", "Screamer-killer talons"})}, 125);
    expect_unit("hive_tyrant", {group("Hive Tyrant", 1, {"Monstrous bonesword and lash whip", "Monstrous scything talons"})},
                195);
    expect_unit("tyrannofex", {group("Tyrannofex", 1, {"Powerful limbs", "Stinger salvoes", "Fleshborer hive"})}, 170);
    // No list data: no new unit.
    EXPECT_EQ(default_unit(army_resolver()("data/units/intercessor_squad.json"), "data/units/intercessor_squad.json"),
              std::nullopt);
}

TEST(D079_Defaults, UnavailableModelTypes) {
    EXPECT_EQ(unavailable_model_types(sheet("chaos_terminator_squad")),
              (Names{"Accursed weapon and combi-weapon", "Power fist and combi-weapon", "Chainfist and combi-bolter",
                     "Heavy weapon", "Chainfist and combi-weapon"}));
    EXPECT_EQ(unavailable_model_types(sheet("legionaries")), Names{"Legionary w/ balefire tome"});
    EXPECT_EQ(unavailable_model_types(sheet("termagants")), Names{"Termagant w/ Strangleweb"});
    for (const std::string& name : {"chaos_lord", "havocs", "genestealers", "tyrant_guard", "screamer_killer",
                                    "hive_tyrant", "tyrannofex"}) {
        EXPECT_EQ(unavailable_model_types(sheet(name)), Names{}) << name;
    }
}

// ==== The auto-layout ====

TEST(D079_Layout, FewestCoherentRanks) {
    // 03.03: 20 Termagants in 2 ranks of 10 are 9.04" corner to corner (more than 9"), so 3 ranks.
    {
        Unit u;
        u.models.resize(20);
        for (std::size_t i = 0; i < 20; ++i) {
            u.models[i].id = ModelId{static_cast<std::uint32_t>(i + 1)};
            u.models[i].base = kTermagant;
        }
        Formation f;
        f.ranks = 2;
        f.centre_x_mm = in(22);
        place_formation(u.models, f, field(), south(), "test");
        const float corner = base_distance(u.models[0].position, kTermagant, u.models[19].position, kTermagant);
        EXPECT_NEAR(mm_to_inches(corner), 9.04f, 0.005f);
        EXPECT_FALSE(unit_coherency(u).ok());
    }
    EXPECT_EQ(fewest_coherent_ranks(bases(kTermagant, 20), field(), south()), 3);
    EXPECT_EQ(fewest_coherent_ranks(bases(kTermagant, 10), field(), south()), 1);  // 8.98" end to end
    EXPECT_EQ(fewest_coherent_ranks(bases(kLegionary, 10), field(), south()), 2);
    EXPECT_EQ(fewest_coherent_ranks(bases(kLegionary, 5), field(), south()), 1);
    EXPECT_EQ(fewest_coherent_ranks(bases(kTerminator, 10), field(), south()), 2);
    EXPECT_EQ(fewest_coherent_ranks(bases(kGuard, 6), field(), south()), 1);
    EXPECT_EQ(fewest_coherent_ranks(bases(kTyrannofex, 1), field(), north()), 1);
    EXPECT_EQ(fewest_coherent_ranks({}, field(), south()), std::nullopt);
    // A base deeper than the zone fits no ranks.
    EXPECT_EQ(fewest_coherent_ranks(bases(Base{BaseShape::Round, in(13), 0.0f}, 1), field(), south()), std::nullopt);
}

TEST(D079_Layout, ShelvesStayInTheZone) {
    // The stock Chaos Space Marines on one shelf, in list order, centred, at the zone front, 1" apart.
    const std::vector<std::vector<Base>> stock = {bases(kLegionary, 10), bases(Base{BaseShape::Round, 40.0f, 0.0f}, 1),
                                                  bases(kTerminator, 10), bases(kTerminator, 5), bases(kLegionary, 10)};
    const ZoneLayout one = layout_zone(stock, field(), south());
    ASSERT_TRUE(one.fits()) << one.problem;
    ASSERT_EQ(one.slots.size(), 5u);
    const std::vector<int> ranks{2, 1, 2, 1, 2};
    float total_in = 4.0f * kLayoutGapIn;
    const std::vector<float> widths_in{mm_to_inches(5 * 32.0f), mm_to_inches(40.0f), mm_to_inches(5 * 40.0f),
                                       mm_to_inches(5 * 40.0f), mm_to_inches(5 * 32.0f)};
    for (float w : widths_in) total_in += w;
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(one.slots[i].ranks, ranks[i]);
        EXPECT_EQ(one.slots[i].depth_in, 0.0f);
        EXPECT_TRUE(on_grid(one.slots[i].centre_x_in)) << one.slots[i].centre_x_in;
        if (i > 0) {
            const float gap = (one.slots[i].centre_x_in - widths_in[i] / 2) - (one.slots[i - 1].centre_x_in + widths_in[i - 1] / 2);
            EXPECT_GE(gap, 1.0f - kLayoutGridIn) << i;  // 1" apart, give or take the grid
            EXPECT_LE(gap, 1.0f + kLayoutGridIn) << i;
        }
    }
    const float west = one.slots[0].centre_x_in - widths_in[0] / 2;
    const float east = one.slots[4].centre_x_in + widths_in[4] / 2;
    EXPECT_NEAR(west, (44.0f - total_in) / 2, 0.2f);  // centred
    EXPECT_NEAR(44.0f - east, (44.0f - total_in) / 2, 0.2f);

    // A second shelf when the first is full: 1" behind its deepest unit (3 ranks of Termagants, 3.37"), rounded up
    // to the grid; the same depths in the north zone.
    std::vector<std::vector<Base>> many(4, bases(kTermagant, 20));
    many.push_back(bases(Base{BaseShape::Round, 60.0f, 0.0f}, 1));
    many.push_back(bases(kGuard, 6));
    for (const DeploymentZone& zone : {south(), north()}) {
        const ZoneLayout two = layout_zone(many, field(), zone);
        ASSERT_TRUE(two.fits()) << two.problem;
        for (std::size_t i = 0; i < 5; ++i) EXPECT_EQ(two.slots[i].depth_in, 0.0f) << i;
        EXPECT_EQ(two.slots[5].depth_in, 4.375f);  // 3 x 28.5 mm = 3.366" + 1" -> 4.375"
        EXPECT_EQ(two.slots[5].centre_x_in, 22.0f);  // alone on its shelf, centred
        EXPECT_EQ(two.slots[5].ranks, 1);
        for (std::size_t i = 0; i + 1 < 5; ++i) EXPECT_LT(two.slots[i].centre_x_in, two.slots[i + 1].centre_x_in);
    }

    // Fallback: first-fit decreasing by width. In list order [A, A, B, B] (A 25" wide and 12.5" deep, B 15" and 15")
    // takes three shelves, 44.5" deep; widest first, [A, B] twice takes two, 31" deep.
    const DeploymentZone deep{BattlefieldEdge::South, in(40)};
    const Battlefield big{in(44), in(100)};
    const std::vector<Base> a = bases(Base{BaseShape::Round, in(12.5f), 0.0f}, 2);
    const std::vector<Base> b = bases(Base{BaseShape::Round, in(15), 0.0f}, 1);
    const ZoneLayout ffd = layout_zone({a, a, b, b}, big, deep);
    ASSERT_TRUE(ffd.fits()) << ffd.problem;
    EXPECT_EQ(ffd.slots[0].depth_in, 0.0f);
    EXPECT_EQ(ffd.slots[2].depth_in, 0.0f);
    EXPECT_EQ(ffd.slots[1].depth_in, 16.0f);
    EXPECT_EQ(ffd.slots[3].depth_in, 16.0f);
    // Else the army does not fit.
    const ZoneLayout none = layout_zone({a, a, b, b, b}, big, deep);
    EXPECT_FALSE(none.fits());
    EXPECT_EQ(none.problem, "D-079: the army does not fit in its 44\"x40\" deployment zone");
    EXPECT_EQ(layout_zone({bases(Base{BaseShape::Round, in(13), 0.0f}, 1)}, field(), south()).problem,
              "D-079: the army does not fit in its 44\"x12\" deployment zone");
}

TEST(D079_Layout, LayoutIsALegalSetUp) {
    // The showcase and stock armies and some random ones: build_game_state accepts the scenario (coherency, wholly
    // within, no overlap, unengaged), and every model stands exactly where the slot's formation puts it (the 1/8"
    // grid makes inches -> JSON -> mm exact).
    std::vector<std::pair<ArmyList, ArmyList>> battles = {
        {wh40k::test::showcase_csm(), wh40k::test::showcase_tyranids()},
        {wh40k::test::stock_army(csm()), wh40k::test::stock_army(tyranids())}};
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        battles.emplace_back(random_army(csm(), army_resolver(), seed), random_army(tyranids(), army_resolver(), seed));
    }
    for (const auto& [a, b] : battles) {
        const Json scenario = custom_battle_scenario(a, b, Side::A, wh40k::test::factions(), army_resolver());
        GameState gs;
        ASSERT_NO_THROW(gs = build_game_state(scenario, army_resolver())) << scenario.dump();
        std::size_t u = 0;
        for (const char* side : {"A", "B"}) {
            for (const Json& uj : scenario.at("sides").at(side).at("units")) {
                const Json& f = uj.at("formation");
                const LayoutSlot slot{f.at("ranks").get<int>(), f.at("centre_x_in").get<float>(),
                                      f.value("depth_in", 0.0f)};
                EXPECT_TRUE(on_grid(slot.centre_x_in) && on_grid(slot.depth_in));
                EXPECT_EQ(f.at("gap_in"), 0);
                std::vector<Model> models = gs.units[u].models;
                place_formation(models, slot_formation(slot), gs.battlefield,
                                gs.sides[side == std::string("A") ? 0 : 1].deployment_zone, "test");
                for (std::size_t m = 0; m < models.size(); ++m) {
                    EXPECT_EQ(models[m].position, gs.units[u].models[m].position);
                    EXPECT_EQ(gs.units[u].models[m].facing_deg, 0.0f);
                }
                ++u;
            }
        }
        // Deterministic.
        EXPECT_EQ(custom_battle_scenario(a, b, Side::A, wh40k::test::factions(), army_resolver()), scenario);
    }
}

namespace {

// Draws `count` random armies per faction (seeds 1..count) and checks each fits (army_problems never refused one for
// the zone) and builds as a custom battle.
void every_random_army_fits(std::uint64_t count) {
    RandomArmyStats stats[2];
    int models_max[2] = {0, 0};
    for (std::uint64_t seed = 1; seed <= count; ++seed) {
        const ArmyList a = random_army(csm(), army_resolver(), seed, kIncursionPointsLimit, &stats[0]);
        const ArmyList b = random_army(tyranids(), army_resolver(), seed, kIncursionPointsLimit, &stats[1]);
        const GameState gs = wh40k::test::custom_battle(a, b);
        int n[2] = {0, 0};
        for (const Unit& u : gs.units) n[side_index(u.side)] += static_cast<int>(u.models.size());
        models_max[0] = std::max(models_max[0], n[0]);
        models_max[1] = std::max(models_max[1], n[1]);
    }
    EXPECT_EQ(stats[0].did_not_fit, 0);
    EXPECT_EQ(stats[1].did_not_fit, 0);
    std::printf("random armies: %llu per faction; attempts %d / %d, rejected %d / %d; most models %d / %d\n",
                static_cast<unsigned long long>(count), stats[0].attempts, stats[1].attempts, stats[0].rejected,
                stats[1].rejected, models_max[0], models_max[1]);
}

}  // namespace

TEST(D079_Layout, EveryRandomLegalArmyFits) {
    // 2,000 random legal armies per faction (D-079 part 2).
    every_random_army_fits(2000);
}

// ==== Random armies ====

TEST(D079_RandomArmy, LegalAndDeterministic) {
    std::set<std::string> weapons;
    std::set<std::string> lists;
    std::map<std::string, int> sizes;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        for (const Faction* f : {&csm(), &tyranids()}) {
            const ArmyList l = random_army(*f, army_resolver(), seed);
            EXPECT_EQ(l, random_army(*f, army_resolver(), seed));  // deterministic
            EXPECT_EQ(l.faction, f->id);
            EXPECT_EQ(l.name, f->name);
            EXPECT_EQ(reasons(army_problems(l, *f, army_resolver())), Names{}) << "seed " << seed;
            EXPECT_LE(army_points(l, army_resolver()), 1000);
            lists.insert(Json(l).dump());
            ++sizes[f->id + " " + std::to_string(l.units.size())];
            for (const ArmyUnit& u : l.units) {
                for (const ArmyModelGroup& g : u.models) weapons.insert(g.weapons.begin(), g.weapons.end());
            }
        }
    }
    EXPECT_GT(lists.size(), 390u);  // nearly all different
    // The loadouts no shipped scenario plays turn up.
    for (const char* w : {"Havoc autocannon", "Havoc reaper chaincannon", "Havoc missile launcher", "Power fist",
                          "Paired accursed weapons", "Reaper chaincannon", "Missile launcher", "Lascannon", "Shardlauncher",
                          "Spike rifle", "Termagant devourer", "Rupture cannon", "Acid spray", "Heavy venom cannon",
                          "Stranglethorn cannon", "Bone cleaver, lash whip and rending claws"}) {
        EXPECT_EQ(weapons.count(w), 1u) << w;
    }
    for (const char* w : {"Daemon hammer", "Combi-weapon", "Chainfist", "Heavy flamer", "Reaper autocannon", "Meltagun",
                          "Flamer", "Balefire tome", "Strangleweb"}) {
        EXPECT_EQ(weapons.count(w), 0u) << w;  // refused weapons are never picked
    }
    EXPECT_NE(random_army(csm(), army_resolver(), 1), random_army(csm(), army_resolver(), 2));
    // A lower limit is kept too.
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const ArmyList l = random_army(tyranids(), army_resolver(), seed, 500);
        EXPECT_LE(army_points(l, army_resolver()), 500);
        EXPECT_TRUE(army_problems(l, tyranids(), army_resolver(), 500).empty());
    }
}

// ==== The custom battle ====

TEST(D079_CustomBattle, ScenarioBuildsAPointsGame) {
    const ArmyList a = wh40k::test::showcase_csm();
    const ArmyList b = wh40k::test::showcase_tyranids();
    const Json scenario = custom_battle_scenario(a, b, Side::B, wh40k::test::factions(), army_resolver());
    const GameState gs = build_game_state(scenario, army_resolver());
    EXPECT_EQ(gs.scenario_name, "Custom battle - Chaos Space Marines (showcase) vs Tyranids (showcase)");
    EXPECT_EQ(gs.battlefield, (Battlefield{in(44), in(60)}));
    EXPECT_EQ(gs.sides[0].deployment_zone, south());
    EXPECT_EQ(gs.sides[1].deployment_zone, north());
    EXPECT_EQ(gs.sides[0].name, "Chaos Space Marines (showcase)");
    EXPECT_EQ(gs.sides[1].name, "Tyranids (showcase)");
    EXPECT_EQ(gs.battle_rounds, 5);
    EXPECT_EQ(gs.first_turn, Side::B);
    EXPECT_EQ(gs.result_rule, ResultRule::PointsDestroyed);
    ASSERT_EQ(gs.units.size(), a.units.size() + b.units.size());
    int points[2] = {0, 0};
    for (const Unit& u : gs.units) {
        EXPECT_GT(u.points, 0);
        points[side_index(u.side)] += u.points;
    }
    EXPECT_EQ(points[0], 875);
    EXPECT_EQ(points[1], 900);
    EXPECT_EQ(gs.units[0].name, "Chaos Lord");
    EXPECT_EQ(gs.units[5].name, "Hive Tyrant");
    EXPECT_EQ(gs.units[5].side, Side::B);
    // A side without an army name takes its faction's name; the state round-trips; the game starts.
    ArmyList unnamed = b;
    unnamed.name.clear();
    EXPECT_EQ(wh40k::test::custom_battle(a, unnamed).sides[1].name, "Tyranids");
    EXPECT_EQ(Json(gs).get<GameState>(), gs);
    GameState started = gs;
    start_game(started);
    EXPECT_TRUE(started.pending.has_value());
    EXPECT_EQ(started.pending->side, Side::B);
}

// ==== 24.11 [EXTRA ATTACKS] with two scything talons ====

TEST(R24_11_ExtraAttacks, ATyrantWithTwoScythingTalonsSelectsBoth) {
    // 24.11: "you must select all of that model's [EXTRA ATTACKS] weapons" and one other melee weapon "if possible". A
    // Hive Tyrant with two Monstrous scything talons (a legal army loadout, D-079 part 1) has no other melee weapon, so
    // it selects both talons: one alone is refused, both are valid.
    const ArmyUnit two_talons{file("hive_tyrant"),
                              {group("Hive Tyrant", 1, {"Monstrous scything talons", "Monstrous scything talons"})}};
    ASSERT_EQ(unit_problems(sheet("hive_tyrant"), two_talons), Names{});
    ArmyList b = army("tyranids", {two_talons});
    GameState gs = wh40k::test::custom_battle(wh40k::test::stock_army(csm()), b, Side::B);
    // The Tyrant (unit 6) in base contact with the chainsword Legionaries' front rank (unit 1).
    Unit& t = gs.units[5];
    ASSERT_EQ(t.name, "Hive Tyrant");
    ASSERT_EQ(t.models[0].weapons.size(), 2u);
    const Model& legionary = *std::max_element(gs.units[0].models.begin(), gs.units[0].models.end(),
                                               [](const Model& x, const Model& y) { return x.position.y < y.position.y; });
    t.models[0].position = Position{legionary.position.x, legionary.position.y + 16.0f + 30.0f, 0.0f};  // in contact
    start_game(gs);
    // Every unit remains stationary, nothing shoots or charges; on to Side B's choice of a unit to fight.
    for (int guard = 0; guard < 200 && gs.pending; ++guard) {
        if (std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind) && gs.pending->side == Side::B) break;
        std::optional<GameAction> pick;
        if (std::holds_alternative<decision::MoveUnits>(gs.pending->kind)) {
            for (const GameAction& a : legal_actions(gs, gs.pending->side)) {
                const auto* m = std::get_if<action::StartMove>(&a);
                if (!pick && m != nullptr && m->move_type == MoveType::RemainStationary) pick = a;
            }
        }
        apply_action(gs, gs.pending->side, pick ? *pick : wh40k::test::neutral_action(gs));
    }
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind));
    const UnitId tyrant_id = gs.units[5].id;
    ASSERT_TRUE(validate_action(gs, Side::B, action::SelectToFight{tyrant_id, FightType::Normal}).ok());
    apply_action(gs, Side::B, action::SelectToFight{tyrant_id, FightType::Normal});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::DeclareMeleeAttacks{tyrant_id}}));
    const Unit& tyrant = *gs.find_unit(tyrant_id);
    const Model& m = tyrant.models[0];
    ASSERT_TRUE(is_extra_attacks_weapon(m.weapons[0]) && is_extra_attacks_weapon(m.weapons[1]));
    const UnitId target = gs.units[0].id;
    const auto talon = [&](std::size_t w) { return MeleeDeclaration{m.id, w, 0, {MeleeTarget{target, 4}}}; };
    const auto one = wh40k::test::rejection(gs, Side::B, action::Fight{tyrant_id, {talon(0)}});
    EXPECT_TRUE(wh40k::test::contains(one, "24.11: model " + std::to_string(m.id.v) +
                                               " must select all of its [EXTRA ATTACKS] weapons ('Monstrous scything "
                                               "talons' is not selected)"))
        << one.front();
    wh40k::test::expect_valid(gs, Side::B, action::Fight{tyrant_id, {talon(0), talon(1)}});
    // The test helper's neutral fill selects both as well.
    EXPECT_EQ(wh40k::test::first_melee_options(gs, tyrant_id).declarations.size(), 2u);
}
