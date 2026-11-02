#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/dice/modifiers.hpp"
#include "core/serialization/json.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;

namespace {
std::vector<int> roll_n(DiceSource& d, int n) {
    std::vector<int> out;
    for (int i = 0; i < n; ++i) out.push_back(d.roll_d6());
    return out;
}
}  // namespace

// ---- ScriptedDice ----

TEST(R01_05_ScriptedDice, ReturnsRollsInOrder) {
    ScriptedDice d{3, 1, 6};
    EXPECT_EQ(d.roll_d6(), 3);
    EXPECT_EQ(d.roll_d6(), 1);
    EXPECT_EQ(d.consumed(), 2u);
    EXPECT_EQ(d.remaining(), 1u);
    EXPECT_EQ(d.roll_d6(), 6);
}

TEST(R01_05_ScriptedDice, RollingPastTheScriptIsProgrammerError) {
    ScriptedDice d{4};
    (void)d.roll_d6();
    EXPECT_THROW((void)d.roll_d6(), std::logic_error);
}

TEST(R01_05_ScriptedDice, NonD6ValuesAreRejected) {
    EXPECT_THROW((ScriptedDice{0}), std::logic_error);
    EXPECT_THROW((ScriptedDice{7}), std::logic_error);
}

TEST(R01_05_Dice, D3IsD6HalvedRoundingUp) {
    ScriptedDice d{1, 2, 3, 4, 5, 6};
    std::vector<int> got;
    for (int i = 0; i < 6; ++i) got.push_back(roll_d3(d));
    EXPECT_EQ(got, (std::vector<int>{1, 1, 2, 2, 3, 3}));
}

TEST(R01_05_Dice, TwoD6AddsBothDice) {
    ScriptedDice d{2, 5};
    const TwoD6 r = roll_2d6(d);
    EXPECT_EQ(r.dice, (std::array<int, 2>{2, 5}));
    EXPECT_EQ(r.total(), 7);
}

// ---- SeededDice ----

TEST(Determinism_SeededDice, SameSeedSameSequence) {
    DiceState a{42, 0};
    DiceState b{42, 0};
    SeededDice da(a);
    SeededDice db(b);
    EXPECT_EQ(roll_n(da, 200), roll_n(db, 200));
    EXPECT_EQ(a, b);
    EXPECT_GE(a.position, 200u);
}

TEST(Determinism_SeededDice, DifferentSeedsDiffer) {
    DiceState a{1, 0};
    DiceState b{2, 0};
    SeededDice da(a);
    SeededDice db(b);
    EXPECT_NE(roll_n(da, 50), roll_n(db, 50));
}

TEST(Determinism_SeededDice, ResumesExactlyAfterJsonRoundTrip) {
    DiceState uninterrupted{7, 0};
    SeededDice d1(uninterrupted);
    const auto first = roll_n(d1, 10);
    const auto expected_rest = roll_n(d1, 20);

    DiceState saved{7, 0};
    SeededDice d2(saved);
    EXPECT_EQ(roll_n(d2, 10), first);
    DiceState loaded = Json(saved).get<DiceState>();  // save, load
    SeededDice d3(loaded);
    EXPECT_EQ(roll_n(d3, 20), expected_rest);
}

TEST(Determinism_SeededDice, FacesAreInRangeAndRoughlyUniform) {
    DiceState s{12345, 0};
    SeededDice d(s);
    std::array<int, 7> counts{};
    for (int i = 0; i < 60000; ++i) {
        const int r = d.roll_d6();
        ASSERT_GE(r, 1);
        ASSERT_LE(r, 6);
        ++counts[static_cast<std::size_t>(r)];
    }
    for (int f = 1; f <= 6; ++f) {
        EXPECT_GT(counts[static_cast<std::size_t>(f)], 9500) << f;
        EXPECT_LT(counts[static_cast<std::size_t>(f)], 10500) << f;
    }
}

TEST(Serialization, GameStateDiceStateRoundTrips) {
    GameState gs = wh40k::test::load_v1_scenario();
    EXPECT_EQ(gs.dice, (DiceState{0, 0}));
    gs.dice = DiceState{0xFFFFFFFFFFFFFFFFull, 123456789ull};
    const GameState back = Json(gs).get<GameState>();
    EXPECT_EQ(back, gs);
    EXPECT_EQ(back.dice.seed, 0xFFFFFFFFFFFFFFFFull);
}

TEST(Serialization, ScenarioSeedIsLoaded) {
    nlohmann::json s = read_json_file(wh40k::test::repo_root() / "data/scenarios/v1_skirmish.json");
    s["seed"] = 99;
    const GameState gs = build_game_state(s, [](const std::string& f) {
        return load_datasheet(wh40k::test::repo_root() / f);
    });
    EXPECT_EQ(gs.dice, (DiceState{99, 0}));
}

// ---- D-011 Modifying Dice Rolls ----

TEST(D011_ModifyingDiceRolls, HitAndWoundModifiersAreCappedAtPlusMinusOne) {
    EXPECT_EQ(cap_hit_wound_modifier(0), 0);
    EXPECT_EQ(cap_hit_wound_modifier(1), 1);
    EXPECT_EQ(cap_hit_wound_modifier(2), 1);
    EXPECT_EQ(cap_hit_wound_modifier(-1), -1);
    EXPECT_EQ(cap_hit_wound_modifier(-5), -1);
}

TEST(D011_ModifyingDiceRolls, ResultIsFlooredAt1AndHasNoMaximum) {
    EXPECT_EQ(modified_result(1, -1), 1);
    EXPECT_EQ(modified_result(2, -3), 1);
    EXPECT_EQ(modified_result(6, 1), 7);
    EXPECT_EQ(modified_result(4, 0), 4);
}
