#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::load_v1_scenario;

namespace {

struct RecordingChooser {
    std::vector<std::vector<ModelId>> calls;
    ModelChooser fn() {
        return [this](const Unit&, std::span<const ModelId> options) {
            calls.emplace_back(options.begin(), options.end());
            return options.front();
        };
    }
};

}  // namespace

// ---- 06.02 Mortal Wounds ----

TEST(R06_02_MortalWounds, EachMortalWoundRemovesOneWound) {
    GameState gs = load_v1_scenario();
    Unit& gants = gs.units[1];
    RecordingChooser chooser;
    const MortalWoundsResult r = inflict_mortal_wounds(gants, 3, chooser.fn());
    EXPECT_EQ(r.steps.size(), 3u);
    EXPECT_EQ(r.models_destroyed, 3);
    EXPECT_EQ(r.not_inflicted, 0);
    EXPECT_EQ(gants.models.size(), 7u);
    EXPECT_EQ(chooser.calls.size(), 3u);
}

TEST(R06_02_MortalWounds, WoundedNonCharacterModelMustBeSelected) {
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[0];
    marines.models[3].wounds_remaining = 1;
    const ModelId wounded = marines.models[3].id;
    EXPECT_EQ(mortal_wound_candidates(marines), (std::vector<ModelId>{wounded}));
    RecordingChooser chooser;
    const MortalWoundsResult r = inflict_mortal_wounds(marines, 1, chooser.fn());
    ASSERT_EQ(r.steps.size(), 1u);
    EXPECT_EQ(r.steps[0].model, wounded);
    EXPECT_TRUE(r.steps[0].destroyed);
    EXPECT_TRUE(chooser.calls.empty());
}

TEST(R06_02_MortalWounds, SecondMortalWoundFollowsTheFirst) {
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[0];
    RecordingChooser chooser;
    const MortalWoundsResult r = inflict_mortal_wounds(marines, 2, chooser.fn());
    ASSERT_EQ(r.steps.size(), 2u);
    EXPECT_EQ(r.steps[0].model, r.steps[1].model);
    EXPECT_FALSE(r.steps[0].destroyed);
    EXPECT_TRUE(r.steps[1].destroyed);
    EXPECT_EQ(chooser.calls.size(), 1u);  // the second had exactly one legal model
    EXPECT_EQ(marines.models.size(), 4u);
}

TEST(R06_02_MortalWounds, StopsWhenTheUnitIsDestroyed) {
    GameState gs = load_v1_scenario();
    Unit& gants = gs.units[1];
    RecordingChooser chooser;
    const MortalWoundsResult r = inflict_mortal_wounds(gants, 13, chooser.fn());
    EXPECT_EQ(r.models_destroyed, 10);
    EXPECT_EQ(r.not_inflicted, 3);
    EXPECT_TRUE(gants.models.empty());
}

TEST(R06_02_MortalWounds, CharacterUnitWoundedCharacterFirst) {
    GameState gs = load_v1_scenario();
    Unit u = gs.units[0];
    u.keywords.push_back("Character");
    EXPECT_EQ(mortal_wound_candidates(u).size(), 5u);  // tier 4: any CHARACTER model
    u.models[2].wounds_remaining = 1;
    EXPECT_EQ(mortal_wound_candidates(u), (std::vector<ModelId>{u.models[2].id}));  // tier 3
}

// ---- 06.03 Hazard Rolls ----

TEST(R06_03_HazardRolls, OneAndTwoFailForOneMortalWoundEach) {
    const GameState gs = load_v1_scenario();
    ScriptedDice dice{1, 2, 3, 6};
    const HazardRolls r = make_hazard_rolls(dice, gs.units[0], 4);
    EXPECT_EQ(r.rolls, (std::vector<int>{1, 2, 3, 6}));
    EXPECT_EQ(r.failures, 2);
    EXPECT_EQ(r.mortal_wounds, 2);
}

TEST(R06_03_HazardRolls, MonsterOrVehicleUnitsSufferThreePerFailure) {
    GameState gs = load_v1_scenario();
    Unit u = gs.units[0];
    u.keywords.push_back("Vehicle");
    ScriptedDice dice{2, 5};
    const HazardRolls r = make_hazard_rolls(dice, u, 2);
    EXPECT_EQ(r.failures, 1);
    EXPECT_EQ(r.mortal_wounds, 3);
}

TEST(R06_03_HazardRolls, ZeroRolls) {
    const GameState gs = load_v1_scenario();
    ScriptedDice dice(std::vector<int>{});
    EXPECT_EQ(make_hazard_rolls(dice, gs.units[0], 0), HazardRolls{});
}
