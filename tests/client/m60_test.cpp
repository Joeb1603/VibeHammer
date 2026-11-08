// M60 (D-071): the client's default shooting and melee plans for mixed loadouts ([HAZARDOUS] never by default,
// [EXTRA ATTACKS] declared with the main weapon), unchanged plans for single-loadout units, and the log text and
// dice cards for Sustained Hits and the hazard rolls after a unit's attacks.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "casualty_plan.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "dice_cards.hpp"
#include "event_text.hpp"
#include "melee_plan.hpp"
#include "session.hpp"
#include "shot_plan.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::decider;
using wh40k::test::repo_root;

namespace {

// ---- the M12 defaults, as they were before M60, for comparison ----

// The first option's (weapon, profile, target), for every model that has exactly that option.
action::Shoot m12_shot_default(const GameState& gs, UnitId unit, ShootingType type) {
    const Unit& u = *gs.find_unit(unit);
    const auto opts = shot_options(gs, u, type);
    action::Shoot a{unit, type, {}};
    if (opts.empty()) return a;
    const ShotDeclaration& f = opts.front();
    const std::string w0 = find_model(u, f.model)->weapons[f.weapon_index].name;
    for (const Model& m : u.models) {
        for (const ShotDeclaration& o : opts) {
            if (o.model == m.id && m.weapons[o.weapon_index].name == w0 && o.profile_index == f.profile_index &&
                o.target == f.target) {
                a.shots.push_back(o);
                break;
            }
        }
    }
    return a;
}

// The first option for every model that has it, else the model's own first option.
action::Fight m12_melee_default(const GameState& gs, UnitId unit) {
    const Unit& u = *gs.find_unit(unit);
    const auto opts = melee_options(gs, u);
    action::Fight a{unit, {}};
    if (opts.empty()) return a;
    const MeleeDeclaration& f = opts.front();
    const std::string w0 = find_model(u, f.model)->weapons[f.weapon_index].name;
    for (const Model& m : u.models) {
        std::vector<MeleeDeclaration> mine;
        for (const MeleeDeclaration& o : opts) {
            if (o.model == m.id) mine.push_back(o);
        }
        if (mine.empty()) continue;
        const auto exact = std::find_if(mine.begin(), mine.end(), [&](const MeleeDeclaration& o) {
            return m.weapons[o.weapon_index].name == w0 && o.profile_index == f.profile_index &&
                   o.targets.front().unit == f.targets.front().unit;
        });
        a.declarations.push_back(exact != mine.end() ? *exact : mine.front());
    }
    return a;
}

// Remains stationary with every unit of the side whose turn it is (from the start of the game).
void to_shoot_step(GameState& gs) {
    start_game(gs);
    while (gs.pending && std::holds_alternative<decision::MoveUnits>(gs.pending->kind)) {
        for (const GameAction& a : legal_actions(gs, decider(gs))) {
            const auto* m = std::get_if<action::StartMove>(&a);
            if (m != nullptr && m->move_type == MoveType::RemainStationary) {
                apply_action(gs, decider(gs), a);
                break;
            }
        }
    }
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Shoot>(gs.pending->kind));
}

// Then no shooting, charges or pile-ins, to the first SelectUnitToFight decision.
void to_fight_step(GameState& gs) {
    to_shoot_step(gs);
    for (int guard = 0; guard < 30 && gs.pending; ++guard) {
        const DecisionKind& k = gs.pending->kind;
        if (std::holds_alternative<decision::SelectUnitToFight>(k)) return;
        if (std::holds_alternative<decision::Shoot>(k)) apply_action(gs, decider(gs), action::EndShootStep{});
        else if (std::holds_alternative<decision::Charge>(k)) apply_action(gs, decider(gs), action::EndChargeStep{});
        else if (std::holds_alternative<decision::PileIn>(k)) apply_action(gs, decider(gs), action::EndPileIns{});
        else apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    ADD_FAILURE() << "did not reach the Fight step";
}

GameState havocs_fixture() {
    return load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
}

const WeaponProfile& profile_of(const GameState& gs, const ShotDeclaration& d) {
    return find_model(gs, d.model)->weapons.at(d.weapon_index).profiles.at(d.profile_index);
}

const std::string& weapon_of(const GameState& gs, ModelId model, std::size_t weapon) {
    return find_model(gs, model)->weapons.at(weapon).name;
}

// The Hive Tyrant in base contact with the middle Havoc, Side B first, at its DeclareMeleeAttacks decision.
GameState tyrant_declaring(bool usable_bonesword) {
    GameState gs = havocs_fixture();
    gs.first_turn = Side::B;
    if (usable_bonesword) gs.units[1].models[0].weapons[0].profiles[0].abilities.clear();  // stand-in: no [TWIN-LINKED]
    const Model& havoc = gs.units[0].models[2];
    Model& tyrant = gs.units[1].models[0];
    tyrant.position = Position{havoc.position.x, havoc.position.y + base_radius(havoc.base) + base_radius(tyrant.base), 0};
    to_fight_step(gs);
    apply_action(gs, Side::B, action::SelectToFight{gs.units[1].id, FightType::Normal});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::DeclareMeleeAttacks>(gs.pending->kind));
    return gs;
}

}  // namespace

TEST(M60_DefaultPlans, SingleLoadoutUnitsPlanExactlyAsBefore) {
    // D-071: every unit in V1 and Strike has one loadout, so its default shooting and melee plans are the M12 ones,
    // in every Shoot and DeclareMeleeAttacks decision random play reaches.
    int shots = 0;
    int fights = 0;
    {
        // After the M10 charge: the Intercessors, then the Termagants, declare their melee attacks.
        GameState gs = wh40k::test::after_successful_charge();
        apply_action(gs, Side::A, action::EndChargeStep{});
        while (gs.flow.stage == Stage::PileIn) apply_action(gs, decider(gs), action::EndPileIns{});
        for (const UnitId unit : {gs.units[0].id, gs.units[1].id}) {
            apply_action(gs, decider(gs), action::SelectToFight{unit, FightType::Normal});
            ASSERT_TRUE(gs.pending && std::holds_alternative<decision::DeclareMeleeAttacks>(gs.pending->kind));
            const action::Fight plan = MeleePlan(gs, unit).expand();
            EXPECT_EQ(plan, m12_melee_default(gs, unit));
            EXPECT_FALSE(plan.declarations.empty());
            ++fights;
            ScriptedDice misses(std::vector<int>(64, 1));
            apply_action(gs, decider(gs), plan, misses);
        }
    }
    for (std::uint64_t seed = 1; seed <= 16; ++seed) {
        const bool strike = seed > 10;
        GameState gs = strike ? load_scenario(repo_root() / "data/scenarios/strike_44x60.json", repo_root())
                              : wh40k::test::load_v1_scenario_closer();
        gs.dice.seed = seed;
        start_game(gs);
        wh40k::test::TestRng rng(seed);
        for (int i = 0; i < 1500 && gs.pending; ++i) {
            if (std::holds_alternative<decision::Shoot>(gs.pending->kind)) {
                for (const Unit& u : gs.units) {
                    if (u.side != gs.pending->side) continue;
                    for (const ShootingType t : eligible_shooting_types(gs, u)) {
                        const ShotPlan plan(gs, u.id, t);
                        EXPECT_FALSE(plan.mixed_loadout());
                        EXPECT_EQ(plan.expand(), m12_shot_default(gs, u.id, t)) << "seed " << seed;
                        ++shots;
                    }
                }
            }
            if (const auto* d = std::get_if<decision::DeclareMeleeAttacks>(&gs.pending->kind)) {
                EXPECT_EQ(MeleePlan(gs, d->unit).expand(), m12_melee_default(gs, d->unit)) << "seed " << seed;
                ++fights;
            }
            const GameAction a = wh40k::test::random_action(gs, rng);
            apply_action(gs, decider(gs), a);
        }
    }
    EXPECT_GT(shots, 20);
    EXPECT_GE(fights, 2);  // random play rarely charges: the two M10 fights above at least
}

TEST(M60_DefaultPlans, TheHavocChampionFiresItsPlasmaGunOnTheStandardProfile) {
    // The lascannons (D6+1 damage, D-002) can't fire until M62; the champion's plasma gun is offered on both
    // profiles, and the default never picks supercharge ([HAZARDOUS]).
    GameState gs = havocs_fixture();
    to_shoot_step(gs);
    const ShotPlan plan(gs, gs.units[0].id, ShootingType::Normal);
    EXPECT_TRUE(plan.mixed_loadout());
    bool supercharge_offered = false;
    for (const ShotOption& o : plan.options()) supercharge_offered = supercharge_offered || o.hazardous;
    EXPECT_TRUE(supercharge_offered);
    const action::Shoot shoot = plan.expand();
    ASSERT_EQ(shoot.shots.size(), 1u);
    EXPECT_EQ(shoot.shots[0].model, gs.units[0].models[0].id);
    EXPECT_EQ(profile_of(gs, shoot.shots[0]).name, "Plasma gun - standard");
    EXPECT_TRUE(validate_action(gs, Side::A, shoot).ok());
}

TEST(M60_DefaultPlans, EveryHavocFiresItsOwnWeaponAtTheSameTarget) {
    // Stand-in: fixed-damage lascannons, so every model of the mixed loadout has a weapon to fire. Before M60 only
    // the champion fired (the unit-wide choice was its plasma gun).
    GameState gs = havocs_fixture();
    for (Model& m : gs.units[0].models) {
        for (Weapon& w : m.weapons) {
            if (w.name == "Havoc lascannon") w.profiles[0].damage = DiceExpr::fixed(4);
        }
    }
    to_shoot_step(gs);
    const UnitId havocs = gs.units[0].id;
    EXPECT_EQ(m12_shot_default(gs, havocs, ShootingType::Normal).shots.size(), 1u);
    const action::Shoot shoot = ShotPlan(gs, havocs, ShootingType::Normal).expand();
    ASSERT_EQ(shoot.shots.size(), 5u);
    for (const ShotDeclaration& d : shoot.shots) {
        EXPECT_EQ(d.target, gs.units[1].id);
        EXPECT_FALSE(profile_of(gs, d).has<ability::Hazardous>());
        EXPECT_EQ(weapon_of(gs, d.model, d.weapon_index), d.model == gs.units[0].models[0].id ? "Plasma gun" : "Havoc lascannon");
    }
    EXPECT_TRUE(validate_action(gs, Side::A, shoot).ok());
}

TEST(M60_DefaultPlans, BoltgunLegionariesFireTheirBoltgunsAndTheirChampionItsPistol) {
    // The champion's first option is its bolt pistol, which every Legionary also carries. Before M60 all ten fired
    // bolt pistols; now each model fires its own weapons (24.07: the boltgun, not the pistol, for the others).
    GameState gs = wh40k::test::load_rosters_fixture_closer(28.0f);  // 8" between the zones' front lines
    to_shoot_step(gs);
    const Unit& bolters = *gs.find_unit(UnitId{3});
    ASSERT_EQ(bolters.name, "Legionaries");
    const ShotPlan plan(gs, bolters.id, ShootingType::Normal);
    EXPECT_TRUE(plan.mixed_loadout());
    const action::Shoot old = m12_shot_default(gs, bolters.id, ShootingType::Normal);
    const action::Shoot shoot = plan.expand();
    ASSERT_FALSE(shoot.shots.empty());
    int boltguns = 0;
    for (const ShotDeclaration& d : old.shots) EXPECT_EQ(weapon_of(gs, d.model, d.weapon_index), "Bolt pistol");
    for (const ShotDeclaration& d : shoot.shots) {
        const std::string& w = weapon_of(gs, d.model, d.weapon_index);
        if (d.model == bolters.models[0].id) {
            EXPECT_EQ(w, "Bolt pistol");  // the champion has no other ranged weapon
        } else {
            EXPECT_EQ(w, "Boltgun");
            ++boltguns;
        }
        EXPECT_EQ(d.target, shoot.shots.front().target);
    }
    EXPECT_EQ(boltguns, 9);
    EXPECT_TRUE(validate_action(gs, Side::A, shoot).ok());
}

TEST(M60_DefaultPlans, NeverDefaultsToAHazardousProfileEvenWhenItIsListedFirst) {
    // BSData lists the Intercessors' "Plasma pistol - supercharge" before "- standard". Stand-in: every Intercessor
    // carries one instead of its Bolt Rifle and bolt pistol (a single loadout).
    GameState gs = wh40k::test::shoot_step_state();
    const Datasheet ds = load_datasheet(repo_root() / "data/units/intercessor_squad.json");
    const Weapon* pistol = ds.find_weapon("Plasma pistol");
    ASSERT_NE(pistol, nullptr);
    ASSERT_TRUE(pistol->profiles[0].has<ability::Hazardous>());
    for (Model& m : gs.units[0].models) m.weapons = {*pistol};
    const ShotPlan plan(gs, gs.units[0].id, ShootingType::Normal);
    EXPECT_FALSE(plan.mixed_loadout());
    const action::Shoot shoot = plan.expand();
    ASSERT_EQ(shoot.shots.size(), 5u);
    for (const ShotDeclaration& d : shoot.shots) EXPECT_EQ(profile_of(gs, d).name, "Plasma pistol - standard");
    EXPECT_EQ(m12_shot_default(gs, gs.units[0].id, ShootingType::Normal).shots.front().profile_index, 0u);  // was supercharge
    EXPECT_TRUE(validate_action(gs, Side::A, shoot).ok());
}

TEST(M60_DefaultPlans, TheHiveTyrantFightsWithItsTalonsWhileItsBoneswordIsRefused) {
    const GameState gs = tyrant_declaring(false);
    const MeleePlan plan(gs, gs.units[1].id);
    const action::Fight fight = plan.expand();
    ASSERT_EQ(fight.declarations.size(), 1u);
    EXPECT_EQ(weapon_of(gs, fight.declarations[0].model, fight.declarations[0].weapon_index), "Monstrous scything talons");
    EXPECT_TRUE(plan.extra_declarations_for(gs.units[1].models[0].id).empty());
    EXPECT_TRUE(validate_action(gs, Side::B, fight).ok());
}

TEST(M60_DefaultPlans, TheHiveTyrantFightsWithBothWeaponsWhenItCan) {
    // Stand-in: a bonesword without [TWIN-LINKED] (M61). 24.11: the talons are added to the main weapon.
    const GameState gs = tyrant_declaring(true);
    const ModelId tyrant = gs.units[1].models[0].id;
    MeleePlan plan(gs, gs.units[1].id);
    const auto mains = plan.main_options_for(tyrant);
    ASSERT_FALSE(mains.empty());
    for (const MeleeOption& o : mains) EXPECT_FALSE(o.extra_attacks);  // the bonesword only
    const action::Fight fight = plan.expand();
    ASSERT_EQ(fight.declarations.size(), 2u);
    EXPECT_EQ(weapon_of(gs, tyrant, fight.declarations[0].weapon_index), "Monstrous bonesword and lash whip");
    EXPECT_EQ(weapon_of(gs, tyrant, fight.declarations[1].weapon_index), "Monstrous scything talons");
    EXPECT_TRUE(validate_action(gs, Side::B, fight).ok());
    for (const MeleeChoice& c : plan.choices()) EXPECT_NE(c.weapon, "Monstrous scything talons");
    // An override of the main weapon keeps the talons.
    MeleeDeclaration main = fight.declarations[0];
    plan.override_model(tyrant, main);
    EXPECT_EQ(plan.expand(), fight);
}

TEST(M60_DefaultPlans, MixedMeleeLoadoutsFightAtTheDefaultTarget) {
    // The armies 0.5" apart, engaged from the start. The boltgun Legionaries' champion fights with its chainsword,
    // the others with close combat weapons; each at the unit default's target if engaged with it (D-071).
    GameState gs = wh40k::test::load_rosters_fixture_closer(35.5f);
    to_fight_step(gs);
    ASSERT_EQ(gs.pending->side, Side::A);
    const UnitId bolters{3};
    apply_action(gs, Side::A, action::SelectToFight{bolters, FightType::Normal});
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::DeclareMeleeAttacks>(gs.pending->kind));
    const MeleePlan plan(gs, bolters);
    ASSERT_TRUE(plan.unit_default().has_value());
    const UnitId target = plan.unit_default()->target;
    const action::Fight fight = plan.expand();
    ASSERT_FALSE(fight.declarations.empty());
    const Unit& unit = *gs.find_unit(bolters);
    for (const MeleeDeclaration& d : fight.declarations) {
        const Model& m = *find_model(unit, d.model);
        EXPECT_EQ(m.weapons[d.weapon_index].name, d.model == unit.models[0].id ? "Astartes chainsword" : "Close combat weapon");
        const auto engaged = enemy_units_engaged_with_model(gs, unit, m, m.position);
        if (std::find(engaged.begin(), engaged.end(), target) != engaged.end()) EXPECT_EQ(d.targets.front().unit, target);
    }
    EXPECT_TRUE(validate_action(gs, Side::A, fight).ok());
}

TEST(M60_ClientText, SustainedHitsAreCountedInTheLogAndTheDiceCard) {
    HitRolls r;
    r.rolls = {HitRoll{6, 6, HitOutcome::CriticalHit}, HitRoll{3, 3, HitOutcome::Hit}};
    r.sustained_hits = 1;
    const std::string text = format_event(event::HitRollsMade{0, r});
    EXPECT_NE(text.find("-> 3 hits (1 from [SUSTAINED HITS])"), std::string::npos) << text;
    const GameState gs = wh40k::test::load_v1_scenario();
    const std::vector<LogEntry> log{LogEntry{"Step 1", {GameEvent{event::HitRollsMade{0, r}}}}};
    const auto cards = dice_cards(gs, log, 0);
    ASSERT_EQ(cards.size(), 1u);
    EXPECT_EQ(cards[0].result, "3 hits (1 critical), 1 of them from Sustained Hits");
}

TEST(M60_ClientText, TheHazardRollsAfterAUnitsAttacksHaveTheirOwnCard) {
    GameSession session(havocs_fixture());
    ASSERT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::RemainStationary, std::nullopt}).ok());
    const GameState& gs = session.state();
    const std::size_t sc = gs.units[0].models[0].weapons[0].profiles[1].has<ability::Hazardous>() ? 1 : 0;
    const action::Shoot shoot{gs.units[0].id, ShootingType::Normal,
                              {ShotDeclaration{gs.units[0].models[0].id, 0, sc, gs.units[1].id}}};
    ASSERT_TRUE(session.submit(shoot).ok());
    for (int guard = 0; guard < 4 && session.state().pending &&
                        std::holds_alternative<decision::OrderCasualties>(session.state().pending->kind);
         ++guard) {
        const auto& d = std::get<decision::OrderCasualties>(session.state().pending->kind);
        ASSERT_TRUE(session.submit(CasualtyPlan(session.state(), d.target).action()).ok());  // D-058's order
    }
    const auto cards = dice_cards(session.state(), session.log(), 0);
    const auto hazard = std::find_if(cards.begin(), cards.end(), [](const DiceCard& c) { return c.title == "Hazard rolls"; });
    ASSERT_NE(hazard, cards.end());
    EXPECT_EQ(hazard->rule, "24.15");
    EXPECT_NE(hazard->purpose.find("[HAZARDOUS]"), std::string::npos) << hazard->purpose;
    bool logged = false;
    for (const LogEntry& e : session.log()) {
        for (const GameEvent& ev : e.events) {
            if (std::holds_alternative<event::HazardRollsMade>(ev)) {
                logged = format_event(ev).rfind("[06.03] Hazard rolls: ", 0) == 0;
            }
        }
    }
    EXPECT_TRUE(logged);
    EXPECT_TRUE(session.undo());  // the last decision of the volley goes back like any other
}
