// M75 (D-082 part 1): the builder's battle. A custom battle made through ArmyBuilder edits (the client's army builder: a
// non-stock Tyranid list with a split loadout group and a Rupture cannon, against an edited Chaos Space Marine list,
// Tyranids first), auto-placed, saved as a scenario file. D-085 (M85): the replay of it, played by the Easy bot on both
// sides through battle round 1 (tests/replays/m75_builder_battle.json), went with level Easy; the builder's checks stay.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "army_builder.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::client::ArmyBuilder;
using wh40k::test::repo_root;

namespace {

// The battle as a scenario file (written by the regenerate test).
constexpr const char* kBattleScenario = "tests/data/scenarios/m75_builder_battle_44x60.json";

void edit(const std::optional<std::string>& refused) {
    if (refused) throw std::logic_error("the builder refused an edit: " + *refused);
}

// The battle as a player would build it in the army builder (each line one click or a few).
ArmyBuilder edited_builder() {
    ArmyBuilder b(repo_root());
    // Side A, Chaos Space Marines: renamed; the boltgun Legionaries (unit 5) trade a boltgun for a heavy melee weapon.
    edit(b.rename(Side::A, "Black Legion"));
    edit(b.set_count(Side::A, 4, 1, 8));
    edit(b.add_model_type(Side::A, 4, "Legionary w/ heavy melee weapon"));
    // Side B, Tyranids: the first Termagants split into 9 with Fleshborers and 1 with a spinefist ("+ another
    // loadout"); the Tyrannofex takes the Rupture cannon (+20 points).
    edit(b.rename(Side::B, "Hive Fleet Leviathan"));
    edit(b.split_group(Side::B, 0, 0));
    edit(b.set_option(Side::B, 0, 1, 0, 1));
    edit(b.set_option(Side::B, 5, 0, 0, 0));
    b.set_first_turn(Side::B);
    return b;
}

}  // namespace

TEST(M75_Builder, TheScenarioFileStartsTheBuildersBattle) {
    // The scenario file with a dice seed is the builder's battle for those edits with that seed.
    Replay r;
    r.initial.scenario = kBattleScenario;
    r.initial.seed = 1;
    const ArmyBuilder b = edited_builder();
    GameState expected = b.battle_state();
    expected.dice = DiceState{1, 0};
    const GameState initial = replay_initial_state(r, repo_root());
    EXPECT_EQ(initial, expected);
    EXPECT_EQ(initial.first_turn, Side::B);
    EXPECT_EQ(initial.sides[1].name, "Hive Fleet Leviathan");
    EXPECT_EQ(b.points(Side::A), 960);
    EXPECT_EQ(b.points(Side::B), 975);
}

TEST(M75_Replay, TheScenarioIsTheBuildersBattle) {
    // The scenario file is the builder's battle_scenario for the edits (written by the regenerate test).
    EXPECT_EQ(read_json_file(repo_root() / kBattleScenario), edited_builder().battle_scenario());
}

// Writes tests/data/scenarios/m75_builder_battle_44x60.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M75_Builder.DISABLED_RegenerateBuilderBattleScenario
TEST(M75_Builder, DISABLED_RegenerateBuilderBattleScenario) {
    std::ofstream out(repo_root() / kBattleScenario, std::ios::binary);
    out << edited_builder().battle_scenario().dump(2) << "\n";
}
