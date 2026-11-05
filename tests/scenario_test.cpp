#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::in;
using wh40k::test::load_v1_scenario;
using wh40k::test::repo_root;
using wh40k::test::unit_of;

namespace {

std::vector<std::string> weapon_names(const Model& m) {
    std::vector<std::string> out;
    for (const auto& w : m.weapons) {
        out.push_back(w.name);
    }
    return out;
}

std::vector<PlacedModel> placed(const Unit& u) {
    std::vector<PlacedModel> out;
    for (const Model& m : u.models) {
        out.push_back({m.id, m.position, m.base});
    }
    return out;
}

nlohmann::json v1_scenario_json() {
    return read_json_file(repo_root() / "data/scenarios/v1_skirmish.json");
}

GameState build(const nlohmann::json& scenario) {
    return build_game_state(scenario, [](const std::string& unit_file) {
        return load_datasheet(repo_root() / unit_file);
    });
}

}  // namespace

// ---- Scenario header ----

TEST(D003_InitialState, ScenarioHeader) {
    const GameState gs = load_v1_scenario();
    EXPECT_EQ(gs.scenario_name, "V1 Skirmish - Intercessors vs Termagants");
    EXPECT_FLOAT_EQ(gs.battlefield.width_mm, in(24));
    EXPECT_FLOAT_EQ(gs.battlefield.depth_mm, in(24));
    EXPECT_EQ(gs.battle_rounds, 5);
    EXPECT_EQ(gs.first_turn, Side::A);
    EXPECT_EQ(gs.side(Side::A).name, "Space Marines");
    EXPECT_EQ(gs.side(Side::B).name, "Tyranids");
    EXPECT_EQ(zone_rect(gs.battlefield, gs.side(Side::A).deployment_zone), (Rect{0, 0, in(24), in(6)}));
    EXPECT_EQ(zone_rect(gs.battlefield, gs.side(Side::B).deployment_zone),
              (Rect{0, in(18), in(24), in(24)}));
}

TEST(R01_02_UnitsAndModels, IdsAreDeterministic) {
    const GameState gs = load_v1_scenario();
    ASSERT_EQ(gs.units.size(), 2u);
    EXPECT_EQ(gs.units[0].id, UnitId{1});
    EXPECT_EQ(gs.units[0].side, Side::A);
    EXPECT_EQ(gs.units[1].id, UnitId{2});
    EXPECT_EQ(gs.units[1].side, Side::B);
    std::uint32_t expected = 1;
    for (const Unit& u : gs.units) {
        for (const Model& m : u.models) {
            EXPECT_EQ(m.id, ModelId{expected++});
        }
    }
    EXPECT_EQ(gs.find_unit(UnitId{2}), &gs.units[1]);
    EXPECT_EQ(gs.find_unit(UnitId{3}), nullptr);
}

// ---- Units, characteristics, weapons ----

TEST(R02_02_Profiles, FiveIntercessorsWithCorrectStats) {
    const GameState gs = load_v1_scenario();
    const Unit& u = unit_of(gs, Side::A);
    EXPECT_EQ(u.name, "Intercessor Squad");
    ASSERT_EQ(u.models.size(), 5u);
    EXPECT_EQ(u.starting_strength, 5);
    EXPECT_FALSE(u.battle_shocked);
    EXPECT_EQ(u.models[0].type_name, "Intercessor Sergeant");
    for (const Model& m : u.models) {
        EXPECT_EQ(m.base, (Base{BaseShape::Round, 32.0f, 0.0f}));
        EXPECT_FLOAT_EQ(*m.characteristics.move_mm, in(6));
        EXPECT_EQ(m.characteristics.toughness, 5);
        EXPECT_EQ(m.characteristics.save, 3);
        EXPECT_EQ(m.characteristics.wounds, 2);
        EXPECT_EQ(m.characteristics.leadership, 6);
        EXPECT_EQ(m.characteristics.objective_control, 2);
        EXPECT_EQ(m.wounds_remaining, 2);
        EXPECT_EQ(weapon_names(m),
                  (std::vector<std::string>{"Bolt Rifle", "Bolt pistol", "Knives and Fists"}));
        ASSERT_EQ(m.weapons[0].profiles.size(), 2u);  // Focused Fire + Saturation
    }
}

TEST(R02_02_Profiles, TenTermagantsWithCorrectStats) {
    const GameState gs = load_v1_scenario();
    const Unit& u = unit_of(gs, Side::B);
    EXPECT_EQ(u.name, "Termagants");
    ASSERT_EQ(u.models.size(), 10u);
    EXPECT_EQ(u.starting_strength, 10);
    for (const Model& m : u.models) {
        EXPECT_EQ(m.base, (Base{BaseShape::Round, 28.5f, 0.0f}));
        EXPECT_FLOAT_EQ(*m.characteristics.move_mm, in(6));
        EXPECT_EQ(m.characteristics.toughness, 3);
        EXPECT_EQ(m.characteristics.save, 5);
        EXPECT_EQ(m.characteristics.wounds, 1);
        EXPECT_EQ(m.characteristics.leadership, 8);
        EXPECT_EQ(m.characteristics.objective_control, 2);
        EXPECT_EQ(m.wounds_remaining, 1);
        EXPECT_EQ(weapon_names(m),
                  (std::vector<std::string>{"Fleshborer", "Chitinous claws and teeth"}));
    }
}

// ---- D-008 formation ----

TEST(D008_Formation, IntercessorsOneLineOfFiveOnZoneFront) {
    const GameState gs = load_v1_scenario();
    const Unit& u = unit_of(gs, Side::A);
    const float r = 16.0f;
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        // Front edge touches y = 6" (zone front line for the south zone).
        EXPECT_NEAR(u.models[i].position.y + r, in(6), 1e-3f);
        if (i > 0) {
            const float gap = base_distance(u.models[i - 1].position, u.models[i - 1].base,
                                            u.models[i].position, u.models[i].base);
            EXPECT_NEAR(gap, in(1), 1e-3f);
            EXPECT_GT(u.models[i].position.x, u.models[i - 1].position.x);  // left to right
        }
    }
    // Centred on x = 12".
    EXPECT_NEAR((u.models.front().position.x + u.models.back().position.x) * 0.5f, in(12), 1e-3f);
}

TEST(D008_Formation, TermagantsTwoRanksOfFiveOnZoneFront) {
    const GameState gs = load_v1_scenario();
    const Unit& u = unit_of(gs, Side::B);
    const float r = 14.25f;
    for (std::size_t i = 0; i < 5; ++i) {
        // Front rank's near edges on y = 18".
        EXPECT_NEAR(u.models[i].position.y - r, in(18), 1e-3f);
        // Rear rank directly behind, 1" edge to edge.
        const Model& rear = u.models[i + 5];
        EXPECT_NEAR(rear.position.x, u.models[i].position.x, 1e-3f);
        EXPECT_NEAR(base_distance(u.models[i].position, u.models[i].base, rear.position, rear.base),
                    in(1), 1e-3f);
        if (i > 0) {
            EXPECT_NEAR(base_distance(u.models[i - 1].position, u.models[i - 1].base,
                                      u.models[i].position, u.models[i].base),
                        in(1), 1e-3f);
        }
    }
    EXPECT_NEAR((u.models[0].position.x + u.models[4].position.x) * 0.5f, in(12), 1e-3f);
}

TEST(D008_Formation, FrontLinesExactly12InchesApart) {
    const GameState gs = load_v1_scenario();
    const Unit& a = unit_of(gs, Side::A);
    const Unit& b = unit_of(gs, Side::B);
    float closest = 1e9f;
    for (const Model& ma : a.models) {
        for (const Model& mb : b.models) {
            closest = std::min(closest, base_distance(ma.position, ma.base, mb.position, mb.base));
        }
    }
    EXPECT_NEAR(closest, in(12), 1e-2f);
}

TEST(D008_Formation, BothUnitsCoherentAndWhollyWithinZones) {
    const GameState gs = load_v1_scenario();
    for (const Unit& u : gs.units) {
        EXPECT_TRUE(check_coherency(placed(u)).ok()) << u.name;
        const Rect zone = zone_rect(gs.battlefield, gs.side(u.side).deployment_zone);
        for (const Model& m : u.models) {
            EXPECT_TRUE(wholly_within(zone, m.position, m.base)) << u.name << " model " << m.id.v;
        }
    }
}

TEST(D008_Formation, TenTermagantsInOneRankBreakCoherency) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["B"]["units"][0]["formation"]["ranks"] = 1;
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("03.03"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("9\""), std::string::npos) << e.what();
    }
}

TEST(D008_Formation, FormationOutsideZoneIsRejected) {
    nlohmann::json s = v1_scenario_json();
    // Three ranks of Intercessors (2,2,1) at 1" gaps need 3*32 mm + 2" = 5.78"; fine.
    // Four ranks of Termagants (3,3,3,1) need 4*28.5 mm + 3" = 7.49" > 6" zone depth.
    s["sides"]["B"]["units"][0]["formation"]["ranks"] = 4;
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("D-009"), std::string::npos) << e.what();
    }
}

TEST(D008_Formation, FormationOffBattlefieldEdgeIsRejected) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["A"]["units"][0]["formation"]["centre_x_in"] = 2;
    EXPECT_THROW((void)build(s), DataError);
}

// ---- 02.06 / 02.07 composition and wargear ----

TEST(R02_07_WargearOptions, WeaponNotInModelOptionsIsRejected) {
    nlohmann::json s = v1_scenario_json();
    // Power fist is a Sergeant-only option.
    s["sides"]["A"]["units"][0]["models"][1]["weapons"] = {"Power fist"};
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("02.07"), std::string::npos) << e.what();
    }
}

TEST(R02_04_Weapons, UnknownWeaponNameIsRejected) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["B"]["units"][0]["models"][0]["weapons"] = {"Laser cannon"};
    EXPECT_THROW((void)build(s), DataError);
}

TEST(R02_06_UnitComposition, TooFewModelsIsRejected) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["B"]["units"][0]["models"][0]["count"] = 6;  // Termagants min 7, unit min 10
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("02.06"), std::string::npos) << e.what();
    }
}

TEST(R02_06_UnitComposition, MissingRequiredModelTypeIsRejected) {
    nlohmann::json s = v1_scenario_json();
    // Drop the Sergeant (min 1) and make it 5 Intercessors.
    auto& models = s["sides"]["A"]["units"][0]["models"];
    models.erase(models.begin());
    models[0]["count"] = 5;
    EXPECT_THROW((void)build(s), DataError);
}

TEST(D003_InitialState, WrongJsonTypeIsDataError) {
    nlohmann::json s = v1_scenario_json();
    s["battle_rounds"] = "five";
    EXPECT_THROW((void)build(s), DataError);
}

TEST(R03_02_SetUp, UnitsSetUpEngagedAreRejected) {
    nlohmann::json s = v1_scenario_json();
    // Deep zones whose front lines are 1" apart: the units would start within 2" (03.04).
    s["sides"]["A"]["deployment_zone"]["depth_in"] = 11.5;
    s["sides"]["B"]["deployment_zone"]["depth_in"] = 11.5;
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("03.02"), std::string::npos) << e.what();
    }
}

TEST(R02_06_UnitComposition, UnknownModelTypeIsRejected) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["A"]["units"][0]["models"][0]["type"] = "Captain";
    EXPECT_THROW((void)build(s), DataError);
}

TEST(D008_Formation, ANonRoundBaseIsRejectedAsUnsupportedData) {
    // V1 geometry is round only (CLAUDE.md, Geometry): an oval base is a clean DataError, not a crash in
    // the geometry code.
    try {
        (void)build_game_state(v1_scenario_json(), [](const std::string& unit_file) {
            Datasheet d = load_datasheet(repo_root() / unit_file);
            for (auto& t : d.model_types) {
                t.base = Base{BaseShape::Oval, 28.5f, 40.0f};
            }
            return d;
        });
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("V1 supports round bases only"), std::string::npos) << e.what();
    }
}
