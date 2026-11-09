// M75 (D-082 part 1): the army builder's logic (ArmyBuilder), the custom battle in the settings, the battle it builds
// and the --builder / --army-a / --army-b / --first / --deploy / --open-slot flags. The screen itself is drawing code
// (army_builder_screen.cpp) and was checked by screenshots.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "army_builder.hpp"
#include "core/data/army_list.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/serialization/json.hpp"
#include "launch_options.hpp"
#include "settings.hpp"
#include "test_helpers.hpp"

using namespace wh40k::client;
using wh40k::core::ArmyList;
using wh40k::core::DeploymentMode;
using wh40k::core::Side;
namespace fs = std::filesystem;
namespace core = wh40k::core;

namespace {

ArmyBuilder builder() { return ArmyBuilder(wh40k::test::repo_root()); }

class TempDir {
public:
    TempDir()
        : dir_(fs::temp_directory_path() /
               ("vibehammer_m75_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()))) {
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    fs::path file(const std::string& name) const { return dir_ / name; }

private:
    fs::path dir_;
};

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// The index of the first unit of the side's list from `unit_file`.
std::size_t unit_of(const ArmyBuilder& b, Side side, const std::string& unit_file) {
    const auto& units = b.list(side).units;
    for (std::size_t i = 0; i < units.size(); ++i) {
        if (units[i].unit_file == unit_file) return i;
    }
    throw std::logic_error("no " + unit_file);
}

bool carries(const core::ArmyUnit& u, const std::string& weapon) {
    return std::any_of(u.models.begin(), u.models.end(), [&](const core::ArmyModelGroup& g) {
        return std::find(g.weapons.begin(), g.weapons.end(), weapon) != g.weapons.end();
    });
}

const ModelTypeView& type_view(const std::vector<ModelTypeView>& types, const std::string& name) {
    for (const ModelTypeView& t : types) {
        if (t.name == name) return t;
    }
    throw std::logic_error("no model type " + name);
}

bool has_problem(const std::vector<BuilderProblem>& problems, const std::string& part) {
    return std::any_of(problems.begin(), problems.end(),
                       [&](const BuilderProblem& p) { return p.text.find(part) != std::string::npos; });
}

}  // namespace

// ==== M75_ArmyBuilder ====

TEST(M75_ArmyBuilder, StartsWithStockRosters960And955) {
    const ArmyBuilder b = builder();
    EXPECT_EQ(b.list(Side::A).faction, "chaos_space_marines");
    EXPECT_EQ(b.list(Side::B).faction, "tyranids");
    EXPECT_EQ(b.list(Side::A), core::load_army(wh40k::test::repo_root() / "data/armies/chaos_space_marines_incursion.json"));
    EXPECT_EQ(b.list(Side::B), core::load_army(wh40k::test::repo_root() / "data/armies/tyranids_incursion.json"));
    EXPECT_EQ(b.points(Side::A), 960);
    EXPECT_EQ(b.points(Side::B), 955);
    int sum = 0;
    for (std::size_t i = 0; i < b.list(Side::B).units.size(); ++i) sum += b.unit_points(Side::B, i);
    EXPECT_EQ(sum, 955);
    EXPECT_EQ(b.unit_points(Side::A, 2), 370);  // 10 Chaos Terminators
    EXPECT_TRUE(b.problems(Side::A).empty());
    EXPECT_TRUE(b.problems(Side::B).empty());
    EXPECT_TRUE(b.can_start());
    EXPECT_EQ(b.first_turn(), Side::A);
    EXPECT_EQ(b.deployment(), DeploymentMode::Auto);
    EXPECT_EQ(b.play_mode(), GameMode::Hotseat);
    EXPECT_FALSE(b.edited(Side::A));
    EXPECT_EQ(b.source(Side::B), "data/armies/tyranids_incursion.json");
    EXPECT_EQ(b.settings(), CustomBattleSettings{});  // the stock armies are left out of the settings
    // The add-unit list: every datasheet of the faction, with its copies and the Incursion limit (Battleline 4).
    const std::vector<AddUnitView> add = b.add_list(Side::A);
    ASSERT_EQ(add.size(), 4u);
    EXPECT_EQ(add[1].name, "Legionaries");
    EXPECT_EQ(add[1].copies, 2);
    EXPECT_EQ(add[1].limit, 4);
    EXPECT_EQ(add[1].points, 95);  // its 5-model default
    EXPECT_FALSE(add[1].refusal);
    EXPECT_EQ(add[0].name, "Chaos Lord");
    EXPECT_EQ(add[0].limit, 2);
}

TEST(M75_ArmyBuilder, AddUnitUsesPlayableDefaults) {
    ArmyBuilder b = builder();
    const std::size_t before = b.list(Side::A).units.size();
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/chaos_lord.json"));
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/havocs.json"));
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/chaos_terminator_squad.json"));
    const auto& units = b.list(Side::A).units;
    ASSERT_EQ(units.size(), before + 3);
    EXPECT_EQ(b.selected_side(), Side::A);
    EXPECT_EQ(b.selected_unit(), before + 2);  // the unit just added is selected
    EXPECT_TRUE(b.edited(Side::A));
    // Chaos Lord: no Daemon hammer (its BSData default, refused); Havoc Champion: no Flamer; Terminators: no combi-weapon.
    EXPECT_FALSE(carries(units[before], "Daemon hammer"));
    EXPECT_FALSE(carries(units[before + 1], "Flamer"));
    EXPECT_FALSE(carries(units[before + 2], "Combi-weapon"));
    for (std::size_t i = before; i < units.size(); ++i) {
        const core::Datasheet sheet = *b.datasheet(Side::A, i);
        EXPECT_EQ(units[i], *core::default_unit(sheet, units[i].unit_file));
        EXPECT_TRUE(core::unit_problems(sheet, units[i]).empty());
        EXPECT_TRUE(b.unit_problems(Side::A, i).empty() ||
                    b.unit_problems(Side::A, i).front().find("Incursion army may include") != std::string::npos);
    }
    // A datasheet of the other faction is refused, and nothing changes.
    const ArmyList list = b.list(Side::A);
    const auto refused = b.add_unit(Side::A, "data/units/termagants.json");
    ASSERT_TRUE(refused);
    EXPECT_NE(refused->find("D-079"), std::string::npos);
    EXPECT_EQ(b.list(Side::A), list);
}

TEST(M75_ArmyBuilder, RefusedOptionsAreGreyedWithTheReason) {
    ArmyBuilder b = builder();
    const std::size_t lord = unit_of(b, Side::A, "data/units/chaos_lord.json");
    const std::vector<GroupView> groups = b.groups(Side::A, lord);
    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].slots.size(), 2u);
    const SlotView& melee = groups[0].slots[0];
    ASSERT_EQ(melee.options.size(), 3u);
    EXPECT_EQ(melee.options[0].name, "Daemon hammer");
    ASSERT_TRUE(melee.options[0].refusal);  // greyed, with the engine's rule-cited reason
    EXPECT_EQ(*melee.options[0].refusal, *core::option_refusal(*b.datasheet(Side::A, lord),
                                                               b.datasheet(Side::A, lord)->list_building->wargear[0].slots[0].options[0]));
    EXPECT_FALSE(melee.options[1].refusal);
    EXPECT_FALSE(melee.options[2].refusal);
    EXPECT_EQ(melee.chosen, 2u);  // the stock Lord's Astartes chainblade
    // Picking it is refused with that reason, and nothing changes.
    const ArmyList list = b.list(Side::A);
    const auto refused = b.set_option(Side::A, lord, 0, 0, 0);
    ASSERT_TRUE(refused);
    EXPECT_EQ(*refused, *melee.options[0].refusal);
    EXPECT_EQ(b.list(Side::A), list);
    EXPECT_FALSE(b.edited(Side::A));
    // A pickable option is taken: the weapons are rebuilt (fixed weapons, then the options in slot order).
    ASSERT_FALSE(b.set_option(Side::A, lord, 0, 0, 1));
    EXPECT_EQ(b.list(Side::A).units[lord].models[0].weapons, (std::vector<std::string>{"Accursed weapon", "Plasma pistol"}));
    EXPECT_EQ(b.groups(Side::A, lord)[0].slots[0].chosen, 1u);
    EXPECT_TRUE(b.edited(Side::A));
    // The Havoc Champion's Flamer and Meltagun are refused; the rest of that slot is pickable.
    const std::size_t havocs = unit_of(b, Side::A, "data/units/havocs.json");
    const std::vector<GroupView> havoc_groups = b.groups(Side::A, havocs);
    const SlotView& ranged = havoc_groups[0].slots[1];
    for (const OptionView& o : ranged.options) {
        EXPECT_EQ(o.refusal.has_value(), o.name == "Flamer" || o.name == "Meltagun") << o.name;
    }
    // Option points: the Tyrannofex's Rupture cannon (+20) and Acid spray (+10).
    ArmyBuilder t = builder();
    const std::size_t rex = unit_of(t, Side::B, "data/units/tyrannofex.json");
    const std::vector<GroupView> rex_groups = t.groups(Side::B, rex);
    const SlotView& cannon = rex_groups[0].slots[0];
    EXPECT_EQ(cannon.options[0].name, "Rupture cannon");
    EXPECT_EQ(cannon.options[0].points, 20);
    EXPECT_EQ(cannon.options[2].points, 10);
    const int before = t.points(Side::B);
    ASSERT_FALSE(t.set_option(Side::B, rex, 0, 0, 0));
    EXPECT_EQ(t.points(Side::B), before + 20);
    EXPECT_EQ(t.unit_points(Side::B, rex), 190);
}

TEST(M75_ArmyBuilder, UnavailableModelTypesHaveReasons) {
    ArmyBuilder b = builder();
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/chaos_terminator_squad.json"));
    const std::size_t terminators = b.list(Side::A).units.size() - 1;
    const std::vector<ModelTypeView> types = b.model_types(Side::A, terminators);
    const core::Datasheet sheet = *b.datasheet(Side::A, terminators);
    const std::vector<std::string> unavailable = core::unavailable_model_types(sheet);
    EXPECT_FALSE(unavailable.empty());
    for (const ModelTypeView& t : types) {
        const bool listed = std::find(unavailable.begin(), unavailable.end(), t.name) != unavailable.end();
        EXPECT_EQ(t.unavailable.has_value(), listed) << t.name;
        if (t.unavailable) {
            EXPECT_EQ(t.unavailable->rfind("D-079: ", 0), 0u) << *t.unavailable;
            const auto refused = b.add_model_type(Side::A, terminators, t.name);
            ASSERT_TRUE(refused) << t.name;
            EXPECT_EQ(*refused, *t.unavailable);
        }
    }
    for (const char* name : {"Heavy weapon", "Chainfist and combi-bolter", "Chainfist and combi-weapon",
                             "Accursed weapon and combi-weapon", "Power fist and combi-weapon"}) {
        EXPECT_TRUE(type_view(types, name).unavailable) << name;
    }
    EXPECT_NE(type_view(types, "Chainfist and combi-bolter").unavailable->find("Chainfist"), std::string::npos);
    EXPECT_NE(type_view(types, "Heavy weapon").unavailable->find("Ranged weapon"), std::string::npos);
    EXPECT_FALSE(type_view(types, "Power fist and combi-bolter").unavailable);
    EXPECT_TRUE(type_view(types, "Terminator Champion").has_options);
    EXPECT_FALSE(type_view(types, "Paired accursed weapons").has_options);
    // The Tyranid and Legionary ones.
    const std::size_t gants = unit_of(b, Side::B, "data/units/termagants.json");
    EXPECT_TRUE(type_view(b.model_types(Side::B, gants), "Termagant w/ Strangleweb").unavailable);
    const std::size_t legionaries = unit_of(b, Side::A, "data/units/legionaries.json");
    EXPECT_TRUE(type_view(b.model_types(Side::A, legionaries), "Legionary w/ balefire tome").unavailable);
    EXPECT_FALSE(type_view(b.model_types(Side::A, legionaries), "Legionary w/ other weapon").unavailable);
}

TEST(M75_ArmyBuilder, CountsStayWithinTheDatasheet) {
    ArmyBuilder b = builder();
    const std::size_t legionaries = 0;  // Aspiring Champion 1, Legionary w/ chainsword 9
    EXPECT_TRUE(b.set_count(Side::A, legionaries, 1, 10));  // 11 models: the unit's maximum is 10
    EXPECT_TRUE(b.set_count(Side::A, legionaries, 0, 2));   // the champion is 1-1
    EXPECT_TRUE(b.set_count(Side::A, legionaries, 0, 0));
    EXPECT_TRUE(b.set_count(Side::A, legionaries, 1, 0));   // a group keeps a model (remove it instead)
    EXPECT_FALSE(b.edited(Side::A));
    ASSERT_FALSE(b.set_count(Side::A, legionaries, 1, 4));  // 5 models
    EXPECT_EQ(b.models(Side::A, legionaries), 5);
    EXPECT_EQ(b.unit_points(Side::A, legionaries), 95);
    EXPECT_TRUE(b.edited(Side::A));
    // The model type's own range: Havoc w/ lascannon is 0-4.
    const std::size_t havocs = unit_of(b, Side::A, "data/units/havocs.json");
    EXPECT_TRUE(b.set_count(Side::A, havocs, 1, 5));
    // Below the unit's minimum is allowed while editing (to swap a type), as a problem until it's fixed.
    ASSERT_FALSE(b.set_count(Side::A, havocs, 1, 3));
    EXPECT_TRUE(has_problem(b.problems(Side::A), "02.06: unit size 4 outside 5-5"));
    EXPECT_FALSE(b.can_start());
    ASSERT_FALSE(b.add_model_type(Side::A, havocs, "Havoc w/ autocannon"));
    EXPECT_TRUE(b.problems(Side::A).empty());
    EXPECT_TRUE(b.add_model_type(Side::A, havocs, "Havoc w/ missile launcher"));  // a 6th model
    // Removing a group can't take a type under its minimum (the champion).
    EXPECT_TRUE(b.remove_group(Side::A, havocs, 0));
    // Model types are kept in datasheet order: the autocannon group comes before the lascannons.
    ASSERT_EQ(b.list(Side::A).units[havocs].models[1].type, "Havoc w/ autocannon");
    ASSERT_FALSE(b.remove_group(Side::A, havocs, 1));
    EXPECT_EQ(b.models(Side::A, havocs), 4);
    // Adding a model of a type that has a group with its default loadout joins that group.
    ASSERT_FALSE(b.add_model_type(Side::B, 0, "Termagants"));
    ASSERT_EQ(b.list(Side::B).units[0].models.size(), 1u);
    EXPECT_EQ(b.list(Side::B).units[0].models[0].count, 11);
    EXPECT_EQ(b.unit_points(Side::B, 0), 110);  // the 11-20 bracket
    // Every count in the list stays within its datasheet's ranges.
    for (std::size_t u = 0; u < b.list(Side::A).units.size(); ++u) {
        for (const ModelTypeView& t : b.model_types(Side::A, u)) {
            EXPECT_GE(t.count, t.count == 0 ? 0 : t.min);
            EXPECT_LE(t.count, t.max);
        }
        EXPECT_LE(b.models(Side::A, u), b.datasheet(Side::A, u)->max_models);
    }
}

TEST(M75_ArmyBuilder, SplitGroupGivesTwoSpecials) {
    ArmyBuilder b = builder();
    // Legionaries with boltguns (unit 5): one boltgun out, a special in, then a second loadout of it.
    const std::size_t u = 4;
    ASSERT_EQ(b.list(Side::A).units[u].models[1].type, "Legionary w/ boltgun");
    ASSERT_FALSE(b.set_count(Side::A, u, 1, 7));
    ASSERT_FALSE(b.add_model_type(Side::A, u, "Legionary w/ other weapon"));
    ASSERT_EQ(b.list(Side::A).units[u].models.size(), 3u);
    EXPECT_EQ(b.list(Side::A).units[u].models[2].weapons,
              (std::vector<std::string>{"Bolt pistol", "Close combat weapon", "Havoc autocannon"}));  // its BSData default
    EXPECT_TRUE(b.split_group(Side::A, u, 2));  // one model: nothing to split
    EXPECT_TRUE(b.split_group(Side::A, u, 1));  // boltguns have no options to vary
    ASSERT_FALSE(b.set_count(Side::A, u, 2, 2));
    // Two of the same special break the unit limit (one autocannon a unit).
    EXPECT_TRUE(has_problem(b.problems(Side::A), "at most 1 'Havoc autocannon'"));
    ASSERT_FALSE(b.split_group(Side::A, u, 2));
    ASSERT_EQ(b.list(Side::A).units[u].models.size(), 4u);
    EXPECT_EQ(b.list(Side::A).units[u].models[2].count, 1);
    EXPECT_EQ(b.list(Side::A).units[u].models[3].count, 1);
    EXPECT_EQ(b.list(Side::A).units[u].models[3].weapons, b.list(Side::A).units[u].models[2].weapons);
    // The second loadout takes a plasma gun: two different specials, no problem.
    const std::vector<GroupView> groups = b.groups(Side::A, u);
    std::size_t plasma = 0;
    while (groups[3].slots[0].options[plasma].name != "Plasma gun") ++plasma;
    ASSERT_FALSE(b.set_option(Side::A, u, 3, 0, plasma));
    EXPECT_TRUE(b.problems(Side::A).empty());
    EXPECT_TRUE(carries(b.list(Side::A).units[u], "Havoc autocannon"));
    EXPECT_TRUE(carries(b.list(Side::A).units[u], "Plasma gun"));
    EXPECT_EQ(b.models(Side::A, u), 10);
    // Merged back, both models take the first loadout (and break the limit again).
    ASSERT_FALSE(b.merge_group(Side::A, u, 3));
    EXPECT_EQ(b.list(Side::A).units[u].models.size(), 3u);
    EXPECT_EQ(b.list(Side::A).units[u].models[2].count, 2);
    EXPECT_TRUE(b.merge_group(Side::A, u, 2));  // no other group of that type
    // Termagants: 9 with Fleshborers and 1 with a spinefist.
    const std::size_t gants = 0;
    ASSERT_FALSE(b.split_group(Side::B, gants, 0));
    ASSERT_FALSE(b.set_option(Side::B, gants, 1, 0, 1));
    EXPECT_EQ(b.list(Side::B).units[gants].models[0].count, 9);
    EXPECT_EQ(b.list(Side::B).units[gants].models[1].weapons,
              (std::vector<std::string>{"Chitinous claws and teeth", "Termagant spinefist"}));
    EXPECT_TRUE(b.problems(Side::B).empty());
}

TEST(M75_ArmyBuilder, ProblemsShowCopyPointsAndCharacterLimits) {
    ArmyBuilder b = builder();
    // Copies: two more Legionaries (Battleline: 4 at Incursion), then the add-unit list and Copy refuse.
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/legionaries.json"));
    ASSERT_FALSE(b.duplicate_unit(Side::A, 0));
    const auto refused = b.add_unit(Side::A, "data/units/legionaries.json");
    ASSERT_TRUE(refused);
    EXPECT_NE(refused->find("at most 4 'Legionaries'"), std::string::npos);
    EXPECT_TRUE(b.duplicate_unit(Side::A, 0));
    EXPECT_TRUE(b.add_list(Side::A)[1].refusal);
    // Points: over 1000 is a problem of the army.
    EXPECT_GT(b.points(Side::A), 1000);
    EXPECT_TRUE(has_problem(b.problems(Side::A), "D-079: the army costs"));
    // A CHARACTER: without the Chaos Lord the army has none.
    ASSERT_FALSE(b.remove_unit(Side::A, unit_of(b, Side::A, "data/units/chaos_lord.json")));
    EXPECT_TRUE(has_problem(b.problems(Side::A), "at least one CHARACTER"));
    // A list with a third Chaos Lord (loaded, as the builder can't add it): the copy problem, on that unit.
    TempDir dir;
    ArmyList lords = b.list(Side::A);
    lords.units.assign(3, core::ArmyUnit{"data/units/chaos_lord.json",
                                         {{"Chaos Lord", 1, {"Plasma pistol", "Astartes chainblade"}}}});
    core::save_army(lords, dir.file("lords.json"));
    ASSERT_FALSE(b.load(Side::A, dir.file("lords.json")));
    const std::vector<BuilderProblem> problems = b.problems(Side::A);
    ASSERT_FALSE(problems.empty());
    EXPECT_EQ(problems.front().unit, 2u);
    EXPECT_NE(problems.front().text.find("at most 2 'Chaos Lord' units (this is the 3rd)"), std::string::npos);
    EXPECT_EQ(b.unit_problems(Side::A, 2), std::vector<std::string>{problems.front().text});
    EXPECT_TRUE(b.unit_problems(Side::A, 0).empty());
    // The plain words first, the rule after.
    EXPECT_EQ(plain_first("02.06: unit size 4 outside 5-5"), "Unit size 4 outside 5-5 (02.06)");
    EXPECT_EQ(plain_first("02.07 (D-079): 'x' doesn't fit"), "'x' doesn't fit (02.07 (D-079))");
    EXPECT_EQ(plain_first("no citation: here"), "no citation: here");
}

TEST(M75_ArmyBuilder, OpposingFactionsOnly) {
    ArmyBuilder b = builder();
    const ArmyList csm = b.list(Side::A);
    const ArmyList nids = b.list(Side::B);
    ASSERT_FALSE(b.rename(Side::A, "Black Legion"));
    // Picking the other side's faction swaps the two armies (D-082): never a mirror match.
    ASSERT_FALSE(b.set_faction(Side::A, "tyranids"));
    EXPECT_EQ(b.list(Side::A), nids);
    EXPECT_EQ(b.list(Side::B).name, "Black Legion");
    EXPECT_TRUE(b.edited(Side::B));
    EXPECT_FALSE(b.edited(Side::A));
    EXPECT_TRUE(b.can_start());
    EXPECT_EQ(b.add_list(Side::A).size(), 6u);  // the Tyranid datasheets now
    ASSERT_FALSE(b.set_faction(Side::A, "tyranids"));  // already: nothing changes
    EXPECT_EQ(b.list(Side::A), nids);
    EXPECT_TRUE(b.set_faction(Side::A, "space_marines"));  // not a custom-battle faction
    // Loading a list of the other side's faction is refused.
    const auto refused = b.load(Side::B, "data/armies/tyranids_incursion.json");
    ASSERT_TRUE(refused);
    EXPECT_NE(refused->find("two different factions"), std::string::npos);
    EXPECT_EQ(b.list(Side::B).name, "Black Legion");
    ASSERT_FALSE(b.load(Side::B, "data/armies/chaos_space_marines_incursion.json"));
    EXPECT_EQ(b.list(Side::B), csm);
    // The Load combo offers each side its own faction's lists.
    for (const Side side : {Side::A, Side::B}) {
        for (const ArmyFile& f : b.army_files(side)) EXPECT_EQ(f.faction, b.list(side).faction);
    }
    EXPECT_FALSE(b.army_files(Side::A).empty());
    // battle_problems is shown to both sides (a hand-written settings file could hold a mirror match).
    CustomBattleSettings mirror;
    mirror.army_b = csm;
    b.apply(mirror);
    EXPECT_TRUE(has_problem(b.problems(Side::A), "two different factions"));
    EXPECT_TRUE(has_problem(b.problems(Side::B), "two different factions"));
    EXPECT_FALSE(b.can_start());
}

TEST(M75_ArmyBuilder, SaveLoadRoundTrip) {
    TempDir dir;
    ArmyBuilder b = builder();
    ASSERT_FALSE(b.rename(Side::B, "Hive Fleet: Leviathan!"));
    ASSERT_FALSE(b.split_group(Side::B, 0, 0));
    ASSERT_FALSE(b.set_option(Side::B, 0, 1, 0, 2));
    EXPECT_EQ(b.user_army_path(Side::B), wh40k::test::repo_root() / "armies" / "hive_fleet_leviathan.json");
    const fs::path file = dir.file("sub/leviathan.json");  // the folder is created
    ASSERT_FALSE(b.save(Side::B, file));
    EXPECT_FALSE(b.edited(Side::B));
    EXPECT_EQ(read_text(file), core::army_file_text(b.list(Side::B)));
    const ArmyList saved = b.list(Side::B);
    b.reset_to_stock(Side::B);
    EXPECT_NE(b.list(Side::B), saved);
    ASSERT_FALSE(b.load(Side::B, file));
    EXPECT_EQ(b.list(Side::B), saved);
    EXPECT_FALSE(b.edited(Side::B));
    EXPECT_EQ(b.source(Side::B), file.generic_string());
    // A file that isn't an army list is refused, and nothing changes.
    std::ofstream(dir.file("bad.json")) << "{\"kind\": \"something else\"}";
    EXPECT_TRUE(b.load(Side::B, dir.file("bad.json")));
    EXPECT_TRUE(b.load(Side::B, dir.file("missing.json")));
    EXPECT_EQ(b.list(Side::B), saved);
}

TEST(M75_ArmyBuilder, StockFilesAreNeverOverwritten) {
    ArmyBuilder b = builder();
    const fs::path stock = wh40k::test::repo_root() / "data/armies/chaos_space_marines_incursion.json";
    const std::string before = read_text(stock);
    ASSERT_FALSE(b.rename(Side::A, "Changed"));
    const auto refused = b.save(Side::A, stock);
    ASSERT_TRUE(refused);
    EXPECT_NE(refused->find("never overwritten"), std::string::npos);
    EXPECT_TRUE(b.save(Side::A, wh40k::test::repo_root() / "data/armies/new_list.json"));  // nothing new there either
    EXPECT_TRUE(b.save(Side::A, wh40k::test::repo_root() / "data/armies/../armies/x.json"));
    EXPECT_FALSE(fs::exists(wh40k::test::repo_root() / "data/armies/new_list.json"));
    EXPECT_EQ(read_text(stock), before);
    EXPECT_TRUE(b.edited(Side::A));
    // The user's lists go under <data root>/armies, never data/armies.
    EXPECT_EQ(b.user_army_path(Side::A).parent_path(), wh40k::test::repo_root() / "armies");
}

TEST(M75_ArmyBuilder, CanStartOnlyWithoutProblems) {
    ArmyBuilder b = builder();
    EXPECT_TRUE(b.can_start());
    EXPECT_FALSE(b.start_problem());
    while (!b.list(Side::B).units.empty()) ASSERT_FALSE(b.remove_unit(Side::B, 0));
    EXPECT_FALSE(b.can_start());
    ASSERT_TRUE(b.start_problem());
    EXPECT_EQ(b.start_problem()->rfind("Side B: ", 0), 0u) << *b.start_problem();
    EXPECT_THROW((void)b.battle_state(), core::DataError);
    b.reset_to_stock(Side::B);
    EXPECT_TRUE(b.can_start());
    EXPECT_NO_THROW((void)b.battle_state());
    // A model type swap in progress (a Havoc short) blocks the start until it is finished.
    const std::size_t havocs = unit_of(b, Side::A, "data/units/havocs.json");
    ASSERT_FALSE(b.set_count(Side::A, havocs, 1, 3));
    EXPECT_FALSE(b.can_start());
    EXPECT_EQ(b.start_problem()->rfind("Side A: ", 0), 0u);
    ASSERT_FALSE(b.add_model_type(Side::A, havocs, "Havoc w/ heavy bolter"));
    EXPECT_TRUE(b.can_start());
}

TEST(M75_ArmyBuilder, ThePreviewIsTheUnitAsTheBattleBuildsIt) {
    ArmyBuilder b = builder();
    const std::optional<core::Unit> rex = b.preview_unit(Side::B, 5);
    ASSERT_TRUE(rex);
    EXPECT_EQ(rex->name, "Tyrannofex");
    EXPECT_EQ(rex->side, Side::B);
    const core::GameState battle = b.battle_state();
    const core::Unit& in_battle = battle.units[b.list(Side::A).units.size() + 5];
    EXPECT_EQ(rex->models.size(), in_battle.models.size());
    EXPECT_EQ(rex->models[0].weapons, in_battle.models[0].weapons);
    EXPECT_EQ(rex->keywords, in_battle.keywords);
    // Not while the unit has problems of its own.
    const std::size_t havocs = unit_of(b, Side::A, "data/units/havocs.json");
    ASSERT_FALSE(b.set_count(Side::A, havocs, 1, 3));
    EXPECT_FALSE(b.preview_unit(Side::A, havocs));
}

// ==== M75_Settings ====

TEST(M75_Settings, CustomBattleRoundTripsAndOldFilesLoad) {
    // At its defaults nothing is written, so a settings file from before M75 saves byte for byte.
    const AppSettings defaults;
    EXPECT_FALSE(settings_to_json(defaults).contains("custom_battle"));
    const nlohmann::json old = settings_to_json(defaults);
    EXPECT_EQ(settings_from_json(old), defaults);
    EXPECT_EQ(settings_to_json(settings_from_json(old)).dump(2), old.dump(2));

    ArmyBuilder b = builder();
    ASSERT_FALSE(b.rename(Side::A, "Black Legion"));
    ASSERT_FALSE(b.set_option(Side::B, 5, 0, 0, 0));
    b.set_first_turn(Side::B);
    b.set_deployment(DeploymentMode::Hand);
    b.set_play_mode(GameMode::VsBot);
    AppSettings s;
    s.custom_battle = b.settings();
    ASSERT_TRUE(s.custom_battle.army_a);
    ASSERT_TRUE(s.custom_battle.army_b);
    const nlohmann::json j = settings_to_json(s);
    ASSERT_TRUE(j.contains("custom_battle"));
    EXPECT_EQ(j["custom_battle"]["first_turn"], "B");
    EXPECT_EQ(j["custom_battle"]["deployment"], "hand");
    EXPECT_EQ(j["custom_battle"]["play_mode"], "vs_bot");
    EXPECT_EQ(j["custom_battle"]["army_a"]["kind"], "vibehammer-army");  // the army embedded as an army file
    EXPECT_EQ(settings_from_json(j), s);
    EXPECT_EQ(settings_from_json(nlohmann::json::parse(j.dump())), s);
    // The builder opens on it.
    ArmyBuilder reopened = builder();
    reopened.apply(settings_from_json(j).custom_battle);
    EXPECT_EQ(reopened.setup(), b.setup());
    EXPECT_EQ(reopened.play_mode(), GameMode::VsBot);
    // Only one side edited: the other is left out (its stock army).
    CustomBattleSettings one;
    one.army_b = b.list(Side::B);
    reopened.apply(one);
    EXPECT_EQ(reopened.list(Side::A), builder().list(Side::A));
    EXPECT_EQ(reopened.settings(), one);
    // Bad keys keep their defaults; an army that isn't an army list is the stock one.
    nlohmann::json bad = j;
    bad["custom_battle"]["first_turn"] = "C";
    bad["custom_battle"]["deployment"] = 3;
    bad["custom_battle"]["play_mode"] = "chess";
    bad["custom_battle"]["army_a"] = {{"kind", "vibehammer-army"}, {"format", 2}};
    bad["custom_battle"]["army_b"] = "not a list";
    const AppSettings read = settings_from_json(bad);
    EXPECT_EQ(read.custom_battle, CustomBattleSettings{});
    EXPECT_EQ(read.scenario, s.scenario);
    nlohmann::json not_object = j;
    not_object["custom_battle"] = 7;
    EXPECT_EQ(settings_from_json(not_object).custom_battle, CustomBattleSettings{});
    // An army of a faction the data doesn't have opens as the stock army.
    CustomBattleSettings unknown;
    unknown.army_a = b.list(Side::A);
    unknown.army_a->faction = "orks";
    reopened.apply(unknown);
    EXPECT_EQ(reopened.list(Side::A), builder().list(Side::A));
    // Through a file too.
    TempDir dir;
    ASSERT_TRUE(save_settings(dir.file("vibehammer_settings.json"), s));
    EXPECT_EQ(load_settings(dir.file("vibehammer_settings.json")), s);
}

// ==== M75_CustomBattle ====

TEST(M75_CustomBattle, NewGameEqualsCoreScenario) {
    ArmyBuilder b = builder();
    for (const DeploymentMode mode : {DeploymentMode::Auto, DeploymentMode::Hand}) {
        for (const Side first : {Side::A, Side::B}) {
            b.set_deployment(mode);
            b.set_first_turn(first);
            const core::GameState expected = core::build_game_state(
                core::custom_battle_scenario(b.list(Side::A), b.list(Side::B), first, b.factions(), b.resolver(), mode),
                b.resolver());
            EXPECT_EQ(b.battle_state(), expected);
            EXPECT_EQ(custom_battle_state(b.setup(), b.factions(), b.resolver()), expected);
            EXPECT_EQ(expected.undeployed.empty(), mode == DeploymentMode::Auto);
            EXPECT_EQ(expected.first_turn, first);
        }
    }
    // With edits: the setup carries them.
    ASSERT_FALSE(b.split_group(Side::B, 0, 0));
    ASSERT_FALSE(b.set_option(Side::B, 0, 1, 0, 1));
    ASSERT_FALSE(b.remove_unit(Side::A, 4));
    b.set_deployment(DeploymentMode::Auto);
    const CustomBattleSetup setup = b.setup();
    EXPECT_EQ(setup.armies[0], b.list(Side::A));
    EXPECT_EQ(setup.armies[1], b.list(Side::B));
    const core::GameState gs = b.battle_state();
    EXPECT_EQ(gs, core::build_game_state(core::custom_battle_scenario(setup.armies[0], setup.armies[1], setup.first_turn,
                                                                      b.factions(), b.resolver(), setup.deployment),
                                         b.resolver()));
    EXPECT_EQ(gs.units.size(), 4u + 9u);
    EXPECT_EQ(gs.result_rule, core::ResultRule::PointsDestroyed);
    // A new game from the same setup is the same battle (New game rebuilds it from the setup).
    EXPECT_EQ(custom_battle_state(setup, b.factions(), b.resolver()), gs);
}

// ==== M75_LaunchOptions ====

TEST(M75_LaunchOptions, ParsesTheBuilderFlags) {
    const LaunchOptions none = parse_launch_args({});
    EXPECT_FALSE(none.builder);
    EXPECT_FALSE(none.custom_battle());
    const LaunchOptions o = parse_launch_args({"--builder", "1", "--army-a", "a.json", "--army-b", "b.json", "--first",
                                               "B", "--deploy", "hand", "--open-slot", "1:0"});
    EXPECT_TRUE(o.builder);
    EXPECT_EQ(o.army_a, "a.json");
    EXPECT_EQ(o.army_b, "b.json");
    EXPECT_EQ(o.first, Side::B);
    EXPECT_EQ(o.deploy, DeploymentMode::Hand);
    EXPECT_EQ(o.open_slot, (std::pair<std::size_t, std::size_t>{1, 0}));
    EXPECT_TRUE(o.custom_battle());
    EXPECT_TRUE(parse_launch_args({"--first", "A"}).custom_battle());
    EXPECT_TRUE(parse_launch_args({"--deploy", "auto"}).custom_battle());
    EXPECT_FALSE(parse_launch_args({"--builder", "0"}).builder);
    for (const auto& [flag, bad] : std::vector<std::pair<std::string, std::string>>{
             {"--builder", "2"}, {"--builder", "yes"}, {"--army-a", ""}, {"--army-b", ""}, {"--first", "C"},
             {"--first", "a"}, {"--deploy", "manual"}, {"--deploy", "Hand"}, {"--open-slot", "1"},
             {"--open-slot", "a:1"}, {"--open-slot", "1:"}, {"--open-slot", "-1:0"}}) {
        EXPECT_THROW((void)parse_launch_args({flag, bad}), std::invalid_argument) << flag << " " << bad;
    }
}

TEST(M75_LaunchOptions, NoScenarioReplayOrSaveWithACustomBattle) {
    for (const std::vector<std::string>& custom : {std::vector<std::string>{"--builder", "1"},
                                                   std::vector<std::string>{"--army-a", "a.json"},
                                                   std::vector<std::string>{"--army-b", "b.json"},
                                                   std::vector<std::string>{"--first", "B"},
                                                   std::vector<std::string>{"--deploy", "hand"}}) {
        for (const std::vector<std::string>& other : {std::vector<std::string>{"--scenario", "s.json"},
                                                      std::vector<std::string>{"--replay", "r.json"},
                                                      std::vector<std::string>{"--load", "g.json"}}) {
            std::vector<std::string> args = custom;
            args.insert(args.end(), other.begin(), other.end());
            EXPECT_THROW((void)parse_launch_args(args), std::invalid_argument) << args[0] << " " << other[0];
        }
    }
    EXPECT_THROW((void)parse_launch_args({"--builder", "1", "--menu", "1"}), std::invalid_argument);
    EXPECT_NO_THROW((void)parse_launch_args({"--army-a", "a.json", "--menu", "1"}));
    EXPECT_NO_THROW((void)parse_launch_args({"--builder", "0", "--scenario", "s.json"}));
    EXPECT_NO_THROW((void)parse_launch_args({"--army-a", "a.json", "--seed", "4", "--bot", "both"}));
}

TEST(M75_LaunchOptions, ArmyFilesGoIntoTheBuilder) {
    TempDir dir;
    // --army-b with a Chaos list: side A takes the other faction's stock army (the sides swap factions).
    ArmyBuilder b = builder();
    const fs::path csm_file = dir.file("csm.json");
    ArmyList csm = b.list(Side::A);
    csm.name = "Black Legion";
    csm.units.pop_back();
    core::save_army(csm, csm_file);
    apply_launch_armies(parse_launch_args({"--army-b", csm_file.string(), "--first", "B", "--deploy", "hand"}), b);
    EXPECT_EQ(b.list(Side::B), csm);
    EXPECT_EQ(b.list(Side::A), builder().list(Side::B));
    EXPECT_EQ(b.first_turn(), Side::B);
    EXPECT_EQ(b.deployment(), DeploymentMode::Hand);
    // Both, relative to the data root.
    ArmyBuilder both = builder();
    apply_launch_armies(parse_launch_args({"--army-a", "data/armies/tyranids_incursion.json", "--army-b",
                                           "data/armies/chaos_space_marines_incursion.json"}),
                        both);
    EXPECT_EQ(both.list(Side::A).faction, "tyranids");
    EXPECT_EQ(both.list(Side::B).faction, "chaos_space_marines");
    // The same faction twice, a missing file and a file that isn't a list exit with code 2 (std::invalid_argument).
    ArmyBuilder c = builder();
    EXPECT_THROW(apply_launch_armies(parse_launch_args({"--army-a", "data/armies/tyranids_incursion.json", "--army-b",
                                                        "data/armies/tyranids_incursion.json"}),
                                     c),
                 std::invalid_argument);
    EXPECT_THROW(apply_launch_armies(parse_launch_args({"--army-a", dir.file("missing.json").string()}), c),
                 std::invalid_argument);
    std::ofstream(dir.file("bad.json")) << "[]";
    EXPECT_THROW(apply_launch_armies(parse_launch_args({"--army-b", dir.file("bad.json").string()}), c),
                 std::invalid_argument);
    // A list with a problem: refused for a battle, preloaded into the builder.
    ArmyList none = csm;
    none.units.clear();
    core::save_army(none, dir.file("empty.json"));
    ArmyBuilder d = builder();
    try {
        apply_launch_armies(parse_launch_args({"--army-a", dir.file("empty.json").string()}), d);
        ADD_FAILURE() << "an empty army started";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("Side A"), std::string::npos) << e.what();
    }
    ArmyBuilder e = builder();
    EXPECT_NO_THROW(apply_launch_armies(parse_launch_args({"--builder", "1", "--army-a", dir.file("empty.json").string()}), e));
    EXPECT_TRUE(e.list(Side::A).units.empty());
    EXPECT_FALSE(e.can_start());
}
