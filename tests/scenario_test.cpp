#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/serialization/json.hpp"
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
            EXPECT_NEAR(gap, 0.0f, 1e-3f);  // base to base (M14)
            EXPECT_TRUE(in_base_contact(gap));
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
        // Rear rank directly behind, in base contact (M14).
        const Model& rear = u.models[i + 5];
        EXPECT_NEAR(rear.position.x, u.models[i].position.x, 1e-3f);
        EXPECT_NEAR(base_distance(u.models[i].position, u.models[i].base, rear.position, rear.base),
                    0.0f, 1e-3f);
        if (i > 0) {
            EXPECT_NEAR(base_distance(u.models[i - 1].position, u.models[i - 1].base,
                                      u.models[i].position, u.models[i].base),
                        0.0f, 1e-3f);
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
    // At 1" gaps a line of ten is about 20" wide (in base contact it would be 8.98" end to end, inside 9").
    s["sides"]["B"]["units"][0]["formation"]["ranks"] = 1;
    s["sides"]["B"]["units"][0]["formation"]["gap_in"] = 1;
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
    s["sides"]["B"]["units"][0]["formation"]["gap_in"] = 1;
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

// ---- 03.02 set-up / D-064: depth offset, overlap, several units per side (M53) ----

namespace {

nlohmann::json scenario_json(const char* file) {
    return read_json_file(repo_root() / "data/scenarios" / file);
}

GameState load_strike() { return load_scenario(repo_root() / "data/scenarios/strike_44x60.json", repo_root()); }

void expect_data_error_containing(const nlohmann::json& s, const std::vector<std::string>& parts) {
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        for (const std::string& p : parts) {
            EXPECT_NE(std::string(e.what()).find(p), std::string::npos) << "missing '" << p << "' in " << e.what();
        }
    }
}

}  // namespace

TEST(R03_02_SetUp, DepthOffsetMovesTheFormationBackExactly) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["A"]["units"][0]["formation"]["depth_in"] = 2;  // south zone: towards y = 0
    s["sides"]["B"]["units"][0]["formation"]["depth_in"] = 1.5;  // north zone: towards y = 24"
    const GameState gs = build(s);
    const GameState v1 = wh40k::test::load_v1_scenario();
    const Unit& a = unit_of(gs, Side::A);
    const Unit& b = unit_of(gs, Side::B);
    for (std::size_t i = 0; i < a.models.size(); ++i) {
        EXPECT_NEAR(a.models[i].position.y + 16.0f, in(6) - in(2), 1e-3f);  // front edge 2" behind y = 6"
        EXPECT_FLOAT_EQ(a.models[i].position.x, unit_of(v1, Side::A).models[i].position.x);
    }
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_NEAR(b.models[i].position.y - 14.25f, in(18) + in(1.5f), 1e-3f);  // front rank 1.5" behind y = 18"
        EXPECT_NEAR(b.models[i + 5].position.y - b.models[i].position.y, 28.5f, 1e-3f);  // rear rank in contact
        EXPECT_FLOAT_EQ(b.models[i].position.x, unit_of(v1, Side::B).models[i].position.x);
    }
}

TEST(R03_02_SetUp, OffsetFormationMustStayWhollyWithinItsZone) {
    nlohmann::json s = v1_scenario_json();
    // Two ranks of Termagants are 57 mm (2.24") deep: an offset of 4" puts the rear rank 6.24" behind the
    // front line of a 6" zone, over the battlefield edge.
    s["sides"]["B"]["units"][0]["formation"]["depth_in"] = 4;
    expect_data_error_containing(s, {"D-009", "wholly within"});
    // 3.7" still fits (5.94" of 6").
    s["sides"]["B"]["units"][0]["formation"]["depth_in"] = 3.7;
    EXPECT_NO_THROW((void)build(s));
}

TEST(R03_02_SetUp, NegativeDepthOffsetIsRejected) {
    nlohmann::json s = v1_scenario_json();
    s["sides"]["A"]["units"][0]["formation"]["depth_in"] = -1;
    expect_data_error_containing(s, {"D-064", "depth_in"});
    s["sides"]["A"]["units"][0]["formation"]["depth_in"] = "far";
    EXPECT_THROW((void)build(s), DataError);
}

TEST(R03_02_SetUp, OverlappingFriendlyUnitsAreRejected) {
    nlohmann::json s = v1_scenario_json();
    // A second squad centred 3" east of the first: their lines overlap.
    nlohmann::json second = s["sides"]["A"]["units"][0];
    second["formation"]["centre_x_in"] = 15;
    s["sides"]["A"]["units"].push_back(second);
    // Its models are 6-10 and the Termagants 11-20; the first squad's model 3 is at x = 12", the second's
    // model 6 at 15" - 2.52" = 12.48": centres 0.48" apart.
    expect_data_error_containing(s, {"03.02", "D-064", "overlaps", "unit 1", "unit 2", "model 6"});
}

TEST(R03_02_SetUp, OverlappingEnemyModelsAreRejected) {
    nlohmann::json s = v1_scenario_json();
    // Zones 12" deep on a 24" board: both front lines on y = 12", so the two front ranks intersect.
    s["sides"]["A"]["deployment_zone"]["depth_in"] = 12.5;
    s["sides"]["B"]["deployment_zone"]["depth_in"] = 12.5;
    expect_data_error_containing(s, {"03.02", "overlaps", "'Intercessor Squad'", "'Termagants'"});
}

TEST(R03_02_SetUp, BasesInContactAcrossUnitsAreAllowed) {
    nlohmann::json s = v1_scenario_json();
    // A second squad whose west model touches the first squad's east model: 5 bases of 32 mm = 160 mm wide,
    // so centres 160 mm apart put the two lines edge to edge.
    nlohmann::json second = s["sides"]["A"]["units"][0];
    second["formation"]["centre_x_in"] = 12.0f + 160.0f / 25.4f;
    s["sides"]["A"]["units"].push_back(second);
    const GameState gs = build(s);
    ASSERT_EQ(gs.units.size(), 3u);
    const Model& east = gs.units[0].models.back();
    const Model& west = gs.units[1].models.front();
    EXPECT_TRUE(in_base_contact(base_distance(east.position, east.base, west.position, west.base)));
}

TEST(R03_02_SetUp, StrikeScenarioLoadsSevenUnitsOnA60By44Field) {
    const GameState gs = load_strike();
    EXPECT_EQ(gs.scenario_name, "Strike - 3 Intercessor Squads vs 4 Termagant units (44x60)");
    EXPECT_FLOAT_EQ(gs.battlefield.width_mm, in(60));
    EXPECT_FLOAT_EQ(gs.battlefield.depth_mm, in(44));
    EXPECT_EQ(gs.battle_rounds, 5);
    EXPECT_EQ(gs.first_turn, Side::A);
    EXPECT_EQ(gs.side(Side::A).name, "Space Marines");
    EXPECT_EQ(gs.side(Side::B).name, "Tyranids");
    EXPECT_EQ(zone_rect(gs.battlefield, gs.side(Side::A).deployment_zone), (Rect{0, 0, in(60), in(12)}));
    EXPECT_EQ(zone_rect(gs.battlefield, gs.side(Side::B).deployment_zone), (Rect{0, in(32), in(60), in(44)}));
    ASSERT_EQ(gs.units.size(), 7u);
    for (std::size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(gs.units[i].side, i < 3 ? Side::A : Side::B) << i;
        EXPECT_EQ(gs.units[i].name, i < 3 ? "Intercessor Squad" : "Termagants") << i;
        EXPECT_EQ(gs.units[i].models.size(), i < 3 ? 5u : 10u) << i;
    }
}

TEST(R03_02_SetUp, StrikeScenarioIdsRunInListedOrder) {
    const GameState gs = load_strike();
    std::uint32_t unit = 1;
    std::uint32_t model = 1;
    float last_x = -1.0f;
    Side last_side = Side::A;
    for (const Unit& u : gs.units) {
        EXPECT_EQ(u.id, UnitId{unit++});
        for (const Model& m : u.models) {
            EXPECT_EQ(m.id, ModelId{model++});
        }
        // Listed west to east within each side.
        if (u.side != last_side) last_x = -1.0f;
        EXPECT_GT(u.models.front().position.x, last_x) << u.id.v;
        last_x = u.models.front().position.x;
        last_side = u.side;
    }
    EXPECT_EQ(model, 56u);  // 15 Intercessors + 40 Termagants
}

TEST(R03_02_SetUp, StrikeScenarioIsALegalSetUp) {
    const GameState gs = load_strike();
    for (const Unit& u : gs.units) {
        EXPECT_TRUE(check_coherency(placed(u)).ok()) << u.id.v;  // 03.03, both clauses
        const Rect zone = zone_rect(gs.battlefield, gs.side(u.side).deployment_zone);
        for (const Model& m : u.models) {
            EXPECT_TRUE(wholly_within(zone, m.position, m.base)) << "model " << m.id.v;
        }
    }
    float closest_enemy = 1e9f;
    for (const Unit& u : gs.units) {
        for (const Unit& v : gs.units) {
            for (const Model& a : u.models) {
                for (const Model& b : v.models) {
                    if (a.id == b.id) continue;
                    EXPECT_FALSE(bases_overlap(a.position, a.base, b.position, b.base)) << a.id.v << " " << b.id.v;
                    if (u.side != v.side) {
                        closest_enemy = std::min(closest_enemy, base_distance(a.position, a.base, b.position, b.base));
                    }
                }
            }
        }
    }
    // 20" between the zones; the closest pairs are slightly off-axis (unit 1 and unit 4 overlap in x).
    EXPECT_GE(closest_enemy, in(20) - EPS_MM);
    EXPECT_NEAR(closest_enemy, in(20), in(0.01f));
}

TEST(D064_Formation, HeldBackTermagantsStandThreeInchesBehindTheFront) {
    const GameState gs = load_strike();
    const float front = in(44) - in(12);  // north zone front line, y = 32"
    for (std::size_t i = 3; i < 7; ++i) {
        const Unit& u = gs.units[i];
        const float offset = i == 5 ? in(3) : 0.0f;  // unit 6 (x = 37") has depth_in 3
        for (std::size_t k = 0; k < 5; ++k) {
            EXPECT_NEAR(u.models[k].position.y - 14.25f, front + offset, 1e-2f) << u.id.v;
        }
    }
}

TEST(D064_Formation, AbsentDepthIsTheSameStateAsZero) {
    // Files without depth_in load exactly as before (D-008): adding depth_in 0 to every formation gives an
    // identical GameState, bit for bit.
    for (const char* file : {"v1_skirmish.json", "bot_termagants_first.json", "bot_20_termagants.json"}) {
        SCOPED_TRACE(file);
        const nlohmann::json original = scenario_json(file);
        nlohmann::json zeroed = original;
        for (const char* side : {"A", "B"}) {
            for (auto& u : zeroed["sides"][side]["units"]) {
                ASSERT_FALSE(u["formation"].contains("depth_in"));
                u["formation"]["depth_in"] = 0;
            }
        }
        const GameState a = build(original);
        const GameState b = build(zeroed);
        EXPECT_EQ(a, b);
        EXPECT_EQ(nlohmann::json(a).dump(), nlohmann::json(b).dump());
    }
}

// ---- M59 / D-070: the roster fixture scenarios (tests/data/scenarios) ----

TEST(R02_06_UnitComposition, RostersFixtureLoadsThirteenUnitsWithTheRosterModelCounts) {
    const GameState gs = wh40k::test::load_rosters_fixture();
    EXPECT_FLOAT_EQ(gs.battlefield.width_mm, in(44));  // portrait: 44" wide, 60" deep
    EXPECT_FLOAT_EQ(gs.battlefield.depth_mm, in(60));
    EXPECT_EQ(gs.first_turn, Side::A);
    EXPECT_EQ(gs.side(Side::A).name, "Chaos Space Marines");
    EXPECT_EQ(gs.side(Side::B).name, "Tyranids");
    EXPECT_EQ(zone_rect(gs.battlefield, gs.side(Side::A).deployment_zone), (Rect{0, 0, in(44), in(12)}));
    EXPECT_EQ(zone_rect(gs.battlefield, gs.side(Side::B).deployment_zone), (Rect{0, in(48), in(44), in(60)}));
    struct Expected {
        Side side;
        const char* name;
        std::size_t models;
    };
    const std::vector<Expected> expected{
        {Side::A, "Chaos Lord", 1},   {Side::A, "Legionaries", 10},  {Side::A, "Legionaries", 10},
        {Side::A, "Chaos Terminator Squad", 10},                     {Side::A, "Havocs", 5},
        {Side::B, "Hive Tyrant", 1},  {Side::B, "Termagants", 10},   {Side::B, "Termagants", 10},
        {Side::B, "Genestealers", 5}, {Side::B, "Genestealers", 5},  {Side::B, "Tyrant Guard", 3},
        {Side::B, "Screamer-killer", 1},                             {Side::B, "Screamer-killer", 1}};
    ASSERT_EQ(gs.units.size(), 13u);
    std::uint32_t model_id = 1;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const Unit& u = gs.units[i];
        EXPECT_EQ(u.id, UnitId{static_cast<std::uint32_t>(i + 1)});
        EXPECT_EQ(u.side, expected[i].side) << i;
        EXPECT_EQ(u.name, expected[i].name) << i;
        EXPECT_EQ(u.models.size(), expected[i].models) << i;
        EXPECT_EQ(u.starting_strength, static_cast<int>(expected[i].models)) << i;
        for (const Model& m : u.models) {
            EXPECT_EQ(m.id, ModelId{model_id++});
            EXPECT_EQ(m.base.shape, BaseShape::Round);  // no Tyrannofex (oval) until M65
        }
    }
    EXPECT_EQ(model_id - 1, 72u);  // 36 Chaos Space Marines + 36 Tyranids
}

TEST(R02_07_WargearOptions, RostersFixtureCarriesTheRosterLoadouts) {
    const GameState gs = wh40k::test::load_rosters_fixture();
    auto names = [&](std::uint32_t unit, std::size_t model) { return weapon_names(gs.units[unit - 1].models[model]); };
    using V = std::vector<std::string>;
    EXPECT_EQ(names(1, 0), (V{"Plasma pistol", "Astartes chainblade"}));
    EXPECT_EQ(gs.units[1].models[0].type_name, "Aspiring Champion");
    EXPECT_EQ(names(2, 0), (V{"Bolt pistol", "Astartes chainsword", "Close combat weapon"}));
    EXPECT_EQ(names(2, 9), (V{"Bolt pistol", "Astartes chainsword", "Close combat weapon"}));
    EXPECT_EQ(names(3, 0), (V{"Bolt pistol", "Astartes chainsword", "Close combat weapon"}));
    EXPECT_EQ(names(3, 9), (V{"Boltgun", "Bolt pistol", "Close combat weapon"}));
    EXPECT_EQ(names(4, 0), (V{"Combi-bolter", "Accursed weapon"}));
    EXPECT_EQ(names(4, 9), (V{"Combi-bolter", "Accursed weapon"}));
    EXPECT_EQ(names(5, 0), (V{"Plasma gun", "Astartes chainsword"}));
    EXPECT_EQ(names(5, 4), (V{"Havoc lascannon", "Close combat weapon"}));
    EXPECT_EQ(names(6, 0), (V{"Monstrous bonesword and lash whip", "Monstrous scything talons"}));
    EXPECT_EQ(names(7, 0), (V{"Fleshborer", "Chitinous claws and teeth"}));
    EXPECT_EQ(names(9, 0), (V{"Genestealers claws and talons"}));
    EXPECT_EQ(names(11, 0), (V{"Scything talons and rending claws"}));
    EXPECT_EQ(names(12, 0), (V{"Bio-plasmic scream", "Screamer-killer talons"}));
    // D-070: the Havoc Champion's chainsword has A5 (BSData modifier), the Legionaries' A4.
    EXPECT_EQ(gs.units[4].models[0].weapons[1].profiles[0].attacks, DiceExpr::fixed(5));
    EXPECT_EQ(gs.units[1].models[0].weapons[1].profiles[0].attacks, DiceExpr::fixed(4));
}

TEST(R03_02_SetUp, RostersFixtureIsALegalSetUp) {
    const GameState gs = wh40k::test::load_rosters_fixture();
    float closest_enemy = 1e9f;
    for (const Unit& u : gs.units) {
        EXPECT_TRUE(check_coherency(placed(u)).ok()) << u.id.v;  // 03.03
        const Rect zone = zone_rect(gs.battlefield, gs.side(u.side).deployment_zone);
        for (const Model& m : u.models) {
            EXPECT_TRUE(wholly_within(zone, m.position, m.base)) << "model " << m.id.v;  // D-009
        }
        for (const Unit& v : gs.units) {
            for (const Model& a : u.models) {
                for (const Model& b : v.models) {
                    if (a.id == b.id) continue;
                    EXPECT_FALSE(bases_overlap(a.position, a.base, b.position, b.base)) << a.id.v << " " << b.id.v;
                    if (u.side != v.side) {
                        closest_enemy = std::min(closest_enemy, base_distance(a.position, a.base, b.position, b.base));
                    }
                }
            }
        }
    }
    EXPECT_GE(closest_enemy, in(36) - EPS_MM);  // 36" between the zones
}

TEST(R03_02_SetUp, HavocsVsHiveTyrantFixtureLoads) {
    const GameState gs = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    ASSERT_EQ(gs.units.size(), 2u);
    EXPECT_EQ(gs.units[0].name, "Havocs");
    EXPECT_EQ(gs.units[0].models.size(), 5u);
    EXPECT_EQ(gs.units[1].name, "Hive Tyrant");
    const float gap = base_distance(gs.units[0].models[2].position, gs.units[0].models[2].base,
                                    gs.units[1].models[0].position, gs.units[1].models[0].base);
    EXPECT_NEAR(gap, in(8), 0.01f);  // 8" between the front lines
}

TEST(D008_Formation, TheTyrannofexOvalBaseIsStillRefusedAtPlacement) {
    // D-070: the Tyrannofex file loads (oval 120x92mm), but placement refuses non-round bases until M65.
    nlohmann::json s = read_json_file(repo_root() / wh40k::test::kRostersFixture);
    s["sides"]["B"]["units"].push_back(nlohmann::json::parse(R"({
        "unit_file": "data/units/tyrannofex.json",
        "models": [{"type": "Tyrannofex", "count": 1,
                    "weapons": ["Stinger salvoes", "Fleshborer hive", "Powerful limbs"]}],
        "formation": {"ranks": 1, "gap_in": 0, "centre_x_in": 22, "depth_in": 6, "anchor": "zone_front"}})"));
    try {
        (void)build(s);
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("V1 supports round bases only"), std::string::npos) << e.what();
    }
}
