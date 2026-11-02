#include <gtest/gtest.h>

#include "core/data/load_error.hpp"
#include "core/serialization/json.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::in;
using wh40k::test::load_v1_scenario;

namespace {

template <typename T>
T round_trip(const T& value) {
    const Json j = value;
    return j.get<T>();
}

WeaponAbility round_trip_ability(const WeaponAbility& a) {
    Json j;
    ability::to_json(j, a);
    WeaponAbility out;
    ability::from_json(j, out);
    return out;
}

}  // namespace

TEST(Serialization, LoadedGameStateRoundTripsEqual) {
    const GameState gs = load_v1_scenario();
    const Json j = gs;
    const GameState back = j.get<GameState>();
    EXPECT_EQ(back, gs);
    // A second round trip produces byte-identical JSON.
    EXPECT_EQ(Json(back).dump(), j.dump());
}

TEST(Serialization, RoundTripSurvivesTextDump) {
    const GameState gs = load_v1_scenario();
    const std::string text = Json(gs).dump(2);
    EXPECT_EQ(Json::parse(text).get<GameState>(), gs);
}

TEST(Serialization, ModifiedStateFieldsRoundTrip) {
    GameState gs = load_v1_scenario();
    Unit& u = gs.units[1];
    u.battle_shocked = true;
    u.turn_flags = UnitTurnFlags{true, true, true};
    u.models[3].wounds_remaining = 0;
    u.models[3].position = Position{in(3.25f), in(20.5f), 0.0f};
    gs.first_turn = Side::B;
    gs.battle_rounds = 3;
    EXPECT_EQ(round_trip(gs), gs);
    EXPECT_NE(round_trip(gs), load_v1_scenario());
}

TEST(Serialization, EveryWeaponAbilityRoundTrips) {
    const std::vector<WeaponAbility> all{
        ability::Assault{},   ability::Heavy{},     ability::CloseQuarters{},
        ability::RapidFire{2}, ability::Blast{3},   ability::TwinLinked{},
        ability::Torrent{},   ability::Hazardous{}, ability::DevastatingWounds{},
        ability::Unknown{"Anti-Infantry 4+"}};
    // Guard: the list above covers every alternative of the variant.
    ASSERT_EQ(all.size(), std::variant_size_v<WeaponAbility>);
    for (const auto& a : all) {
        EXPECT_EQ(round_trip_ability(a), a);
    }
}

TEST(Serialization, CharacteristicsOptionalsSetAndUnset) {
    Characteristics full{in(6), 5, 3, 4, 2, 6, 2};
    EXPECT_EQ(round_trip(full), full);
    Characteristics empty{std::nullopt, 8, std::nullopt, std::nullopt, 10, std::nullopt, std::nullopt};
    EXPECT_EQ(round_trip(empty), empty);
    EXPECT_TRUE(Json(empty)["OC"].is_null());
}

TEST(Serialization, WeaponProfileWithDiceAndNoSkill) {
    WeaponProfile p;
    p.name = "Strangleweb";
    p.kind = WeaponKind::Ranged;
    p.range_mm = in(18);
    p.attacks = DiceExpr{1, 6, 0};
    p.skill = std::nullopt;
    p.strength = 2;
    p.armour_penetration = 0;
    p.damage = DiceExpr{2, 3, 1};
    p.abilities = {ability::Torrent{}, ability::Unknown{"weird"}};
    const Json j = p;
    EXPECT_EQ(j["A"], "D6");
    EXPECT_EQ(j["D"], "2D3+1");
    EXPECT_TRUE(j["skill"].is_null());
    EXPECT_EQ(j.get<WeaponProfile>(), p);

    WeaponProfile melee = p;
    melee.kind = WeaponKind::Melee;
    melee.range_mm = std::nullopt;
    melee.skill = 4;
    EXPECT_EQ(round_trip(melee), melee);
}

TEST(Serialization, EnumsAndIds) {
    for (const Side s : {Side::A, Side::B}) EXPECT_EQ(round_trip(s), s);
    for (const auto e : {BattlefieldEdge::North, BattlefieldEdge::South, BattlefieldEdge::East,
                         BattlefieldEdge::West}) {
        EXPECT_EQ(round_trip(e), e);
    }
    EXPECT_EQ(round_trip(Base{BaseShape::Oval, 60.0f, 35.0f}), (Base{BaseShape::Oval, 60.0f, 35.0f}));
    EXPECT_EQ(round_trip(UnitId{42}), UnitId{42});
    EXPECT_EQ(round_trip(ModelId{7}), ModelId{7});
    EXPECT_EQ(round_trip(Rect{1, 2, 3, 4}), (Rect{1, 2, 3, 4}));
}

TEST(Serialization, InvalidEnumIsDataError) {
    EXPECT_THROW((void)Json("C").get<Side>(), DataError);
    Json j = Json(load_v1_scenario());
    j["sides"][0]["deployment_zone"]["edge"] = "up";
    EXPECT_THROW((void)j.get<GameState>(), DataError);
}
