// D-004 property tests over random play: every fully specified legal action validates; template fills
// get a deterministic, rule-cited result; the other side can never act; `pending` always equals the
// recomputed decision.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::fill_template;
using wh40k::test::is_rule_cited;
using wh40k::test::select;
using wh40k::test::shoot_step_state;
using wh40k::test::TestRng;

namespace {

struct PlayStats {
    int actions = 0;
    int template_fills = 0;
    int path_fills = 0;  // ProposeModelMove templates only (D-004)
    int rejected_fills = 0;
};

void check_rejection(const ValidationResult& r) {
    ASSERT_FALSE(r.ok());
    for (const auto& reason : r.reasons) {
        EXPECT_TRUE(is_rule_cited(reason)) << reason;
    }
}

// Called with each action random play is about to apply (M59: checks which weapons get used).
using ActionHook = std::function<void(const GameState&, const GameAction&)>;

// Random play until GameOver or `max_actions`, checking the D-004 properties at every decision. `events`, if given,
// gets every event of the actions applied (M60).
PlayStats play_and_check(GameState& gs, TestRng& rng, int max_actions, const ActionHook& before_apply = {},
                         std::vector<GameEvent>* events = nullptr) {
    PlayStats stats;
    while (gs.pending && stats.actions < max_actions) {
        const Side side = decider(gs);
        const Side other = opponent(side);
        EXPECT_TRUE(legal_actions(gs, other).empty());
        const auto legal = legal_actions(gs, side);
        EXPECT_FALSE(legal.empty()) << describe(*gs.pending);

        for (const GameAction& a : legal) {
            check_rejection(validate_action(gs, other, a));  // never the other side's turn to act
            if (!is_template(a)) {
                const auto r = validate_action(gs, side, a);
                EXPECT_TRUE(r.ok()) << action_name(a) << ": " << (r.ok() ? "" : r.reasons[0]);
                continue;
            }
            check_rejection(validate_action(gs, side, a));  // an unfilled template is never valid
            for (int k = 0; k < 4; ++k) {
                const bool corrupt = rng.below(2) == 0;
                const GameAction filled = fill_template(gs, a, rng, corrupt);
                const auto first = validate_action(gs, side, filled);
                const auto second = validate_action(gs, side, filled);
                EXPECT_EQ(first, second);  // deterministic
                ++stats.template_fills;
                if (std::holds_alternative<action::ProposeModelMove>(a)) ++stats.path_fills;
                if (corrupt) {
                    check_rejection(first);
                    ++stats.rejected_fills;
                } else if (wh40k::test::clean_fill_always_valid(a)) {
                    EXPECT_TRUE(first.ok()) << (first.ok() ? "" : first.reasons[0]);
                } else if (!first.ok()) {
                    check_rejection(first);  // e.g. a random path through an enemy model: still rule-cited
                }
            }
        }

        const GameAction pick = wh40k::test::random_action(gs, rng);
        if (before_apply) before_apply(gs, pick);
        const std::vector<GameEvent> happened = apply_action(gs, side, pick);
        if (events != nullptr) events->insert(events->end(), happened.begin(), happened.end());
        ++stats.actions;
        EXPECT_EQ(gs.pending, compute_decision(gs));
    }
    return stats;
}

}  // namespace

TEST(D004_LegalActions, RandomPlayFromTheScenarioStart) {
    int moves_proposed = 0;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        GameState gs = wh40k::test::load_v1_scenario();
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 200);
        EXPECT_TRUE(gs.flow.stage == Stage::GameOver || stats.actions == 200) << "seed " << seed;
        moves_proposed += stats.path_fills;
    }
    EXPECT_GT(moves_proposed, 0);  // path templates (D-004) were filled and checked
}

TEST(D004_LegalActions, RandomPlayFromTheScenarioStartMovesModels) {
    // Separate assertion so a regression is easy to read: random play does use the path templates.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    TestRng rng(99);
    const PlayStats stats = play_and_check(gs, rng, 200);
    EXPECT_GT(stats.path_fills, 0);  // the move path templates, not just any template
}

TEST(D004_LegalActions, RandomPlayThroughAttackFlows) {
    int fills = 0;
    int rejected = 0;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        GameState gs = shoot_step_state(Side::A, seed);
        const UnitId gants = gs.units[1].id;
        std::vector<WeaponSelection> sel;
        for (std::size_t i = 0; i < 5; ++i) {
            const std::size_t profile = i % 2;  // mix Focused Fire (0) and Saturation (1): two batches
            sel.push_back(select(gs, 0, i, 0, profile, gants));
        }
        begin_attacks(gs, gs.units[0].id, AttackContext{AttackType::Shooting, true}, sel);
        EXPECT_EQ(gs.pending, compute_decision(gs));
        TestRng rng(seed * 7919);
        const PlayStats stats = play_and_check(gs, rng, 200);
        EXPECT_FALSE(gs.pending.has_value()) << "seed " << seed;  // ran to GameOver
        fills += stats.template_fills;
        rejected += stats.rejected_fills;
    }
    EXPECT_GT(fills, 0);  // the casualty-order template was exercised
    EXPECT_GT(rejected, 0);
}

TEST(D004_LegalActions, RandomPlayShootsWithFilledTemplates) {
    // D-016: random play fills Shoot templates; every clean fill is valid and resolves.
    int shots = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameState gs = wh40k::test::load_v1_scenario();
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        for (int i = 0; i < 200 && gs.pending; ++i) {
            const GameAction a = wh40k::test::random_action(gs, rng);
            if (std::holds_alternative<action::Shoot>(a)) ++shots;
            apply_action(gs, decider(gs), a);
        }
    }
    EXPECT_GT(shots, 0);
}

TEST(D004_LegalActions, RandomPlayDeclaresChargesAndStartsChargeMoves) {
    // M9: random play reaches the charge decisions; charge paths come from the path template.
    int declared = 0;
    int moves = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameState gs = wh40k::test::load_v1_scenario();
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        for (int i = 0; i < 300 && gs.pending; ++i) {
            const GameAction a = wh40k::test::random_action(gs, rng);
            if (std::holds_alternative<action::DeclareCharge>(a)) ++declared;
            if (std::holds_alternative<action::StartChargeMove>(a)) ++moves;
            apply_action(gs, decider(gs), a);
            EXPECT_EQ(gs.pending, compute_decision(gs));
        }
    }
    EXPECT_GT(declared, 0);
    EXPECT_GT(moves, 0);
}

TEST(D004_LegalActions, RandomPlayThroughTheFightPhase) {
    // M10: from a successful charge, random play runs the Fight phase (pile-in and consolidation path
    // templates, Fight declaration templates, fight sequence, passes) and the rest of the game.
    int fights = 0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        GameState gs = wh40k::test::after_successful_charge();
        gs.dice.seed = seed;
        apply_action(gs, Side::A, action::EndChargeStep{});
        TestRng rng(seed * 104729);
        const PlayStats stats = play_and_check(gs, rng, 120);
        fights += stats.template_fills;
    }
    EXPECT_GT(fights, 0);
}

TEST(D004_LegalActions, RandomPlayFromTheStrikeScenario) {
    // M53 / D-064: the same D-004 properties with seven units on the 60"x44" field. A few seeds keep the
    // Debug run short; every game reaches GameOver (about 280-400 random actions).
    int moves_proposed = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        GameState gs = load_scenario(wh40k::test::repo_root() / "data/scenarios/strike_44x60.json",
                                     wh40k::test::repo_root());
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 1000);
        EXPECT_EQ(gs.flow.stage, Stage::GameOver) << "seed " << seed << " after " << stats.actions << " actions";
        EXPECT_EQ(Json(gs).get<GameState>(), gs) << "seed " << seed;  // state round-trips
        moves_proposed += stats.path_fills;
    }
    EXPECT_GT(moves_proposed, 0);
}

// ---- M59 / D-070: the roster fixture (tests/data/scenarios) ----

namespace {

// D-002 / D-026 / D-071: a profile the engine can resolve today: fixed A and D, no [DEVASTATING WOUNDS], and no
// ability the engine doesn't recognise (M60; [HAZARDOUS], [SUSTAINED HITS] and [EXTRA ATTACKS] are implemented, and
// [TWIN-LINKED] since M61, D-072).
bool resolvable(const WeaponProfile& p) {
    return p.attacks.is_fixed() && p.damage.is_fixed() && !unimplemented_ability_problem(p) &&
           !p.has<ability::Unknown>();
}

struct WeaponUse {
    std::set<std::string> used;  // profile names fired or fought with
    int shoots = 0;
    int fights = 0;
};

// Records every Shoot / Fight random play applies, and checks each selected profile is one the engine
// can resolve (so the D-026 exception backup in apply_action is never needed).
ActionHook record_weapons(WeaponUse& use) {
    return [&use](const GameState& gs, const GameAction& a) {
        auto profile_of = [&](ModelId id, std::size_t w, std::size_t p) -> const WeaponProfile& {
            return find_model(gs, id)->weapons.at(w).profiles.at(p);
        };
        if (const auto* sh = std::get_if<action::Shoot>(&a)) {
            ++use.shoots;
            for (const ShotDeclaration& d : sh->shots) {
                const WeaponProfile& p = profile_of(d.model, d.weapon_index, d.profile_index);
                EXPECT_TRUE(resolvable(p)) << p.name;
                use.used.insert(p.name);
            }
        } else if (const auto* fi = std::get_if<action::Fight>(&a)) {
            ++use.fights;
            for (const MeleeDeclaration& d : fi->declarations) {
                const WeaponProfile& p = profile_of(d.model, d.weapon_index, d.profile_index);
                EXPECT_TRUE(resolvable(p)) << p.name;
                use.used.insert(p.name);
            }
        }
    };
}

}  // namespace

TEST(D004_LegalActions, RandomPlayFromTheRostersFixture) {
    // The D-004 properties with the two rosters (13 units, D-070) on the 44"x60" board. Every game ends,
    // the state round-trips, and no weapon the engine can't resolve yet is ever fired or fought with:
    // random A or D (D-002) is refused by validation, never by the engine throwing into the D-026 rollback.
    // (M61: the Hive Tyrant's [TWIN-LINKED] bonesword is resolved since D-072, so it may be used.)
    WeaponUse use;
    for (std::uint64_t seed = 1; seed <= 2; ++seed) {
        GameState gs = wh40k::test::load_rosters_fixture();
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 4000, record_weapons(use));
        EXPECT_EQ(gs.flow.stage, Stage::GameOver) << "seed " << seed << " after " << stats.actions << " actions";
        EXPECT_EQ(Json(gs).get<GameState>(), gs) << "seed " << seed;  // state round-trips
    }
    // The armies 10" apart (the Tyranids moved 26" south), so random play shoots and charges, and 0.5"
    // apart (engaged from the start: everyone fights in the first Fight phase).
    for (std::uint64_t seed = 3; seed <= 4; ++seed) {
        GameState gs = wh40k::test::load_rosters_fixture_closer(seed == 3 ? 26.0f : 35.5f);
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 4000, record_weapons(use));
        EXPECT_EQ(gs.flow.stage, Stage::GameOver) << "seed " << seed << " after " << stats.actions << " actions";
        EXPECT_EQ(Json(gs).get<GameState>(), gs) << "seed " << seed;  // state round-trips
    }
    EXPECT_GT(use.shoots, 0);
    EXPECT_GT(use.fights, 0);
    for (const char* refused : {"Havoc lascannon", "Bio-plasmic scream"}) {  // D-002 (M62)
        EXPECT_EQ(use.used.count(refused), 0u) << refused;
    }
}

TEST(D004_LegalActions, RandomGamesFromTheRostersFixtureFinish) {
    // Cheaper than the full D-004 check: more seeds of plain random play to the end.
    WeaponUse use;
    const ActionHook hook = record_weapons(use);
    for (std::uint64_t seed = 5; seed <= 14; ++seed) {
        GameState gs = wh40k::test::load_rosters_fixture_closer(seed % 3 == 0 ? 26.0f : seed % 3 == 1 ? 20.0f : 35.5f);
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        int actions = 0;
        while (gs.pending && actions < 6000) {
            const GameAction a = wh40k::test::random_action(gs, rng);
            hook(gs, a);
            apply_action(gs, decider(gs), a);
            ++actions;
        }
        EXPECT_EQ(gs.flow.stage, Stage::GameOver) << "seed " << seed << " after " << actions << " actions";
    }
    EXPECT_GT(use.shoots, 0);
    EXPECT_GT(use.fights, 0);
}

namespace {

// M60: the roster fixture with stand-ins so random play meets every new ability: the Havocs' lascannons and the
// Legionaries' boltguns are swapped for their datasheets' heavy bolters ([SUSTAINED HITS 1]; the rosters equip none),
// and the Hive Tyrant's bonesword loses [TWIN-LINKED] (then refused), so the Tyrant has an [EXTRA ATTACKS] weapon and
// another one. The plasma guns and pistols keep their [HAZARDOUS] supercharge profiles. (M61 implements
// [TWIN-LINKED]; the stand-in bonesword is kept so these games stay as they were, and M61's own random play uses the
// real one.)
GameState rosters_with_new_abilities(float closer_in) {
    GameState gs = wh40k::test::load_rosters_fixture_closer(closer_in);
    const Datasheet havocs = load_datasheet(wh40k::test::repo_root() / "data/units/havocs.json");
    const Datasheet legionaries = load_datasheet(wh40k::test::repo_root() / "data/units/legionaries.json");
    const Weapon* havoc_bolter = havocs.find_weapon("Havoc heavy bolter");
    const Weapon* heavy_bolter = legionaries.find_weapon("Heavy bolter");
    EXPECT_TRUE(havoc_bolter != nullptr && heavy_bolter != nullptr);
    for (Unit& u : gs.units) {
        for (Model& m : u.models) {
            for (Weapon& w : m.weapons) {
                if (w.name == "Havoc lascannon" && havoc_bolter != nullptr) w = *havoc_bolter;
                if (w.name == "Boltgun" && heavy_bolter != nullptr) w = *heavy_bolter;
                if (w.name == "Monstrous bonesword and lash whip") w.profiles[0].abilities.clear();
            }
        }
    }
    return gs;
}

}  // namespace

TEST(D004_LegalActions, RandomPlayExercisesHazardousSustainedHitsAndExtraAttacks) {
    // M60 (D-071): the D-004 properties with the new abilities in play. Hazard rolls follow [HAZARDOUS] attacks
    // (24.15), critical hits of [SUSTAINED HITS] weapons score extra hits (24.36) and the Tyrant declares its
    // talons with another melee weapon (24.11).
    WeaponUse use;
    std::vector<GameEvent> events;
    int extra_attacks_fights = 0;
    const ActionHook hook = [&](const GameState& gs, const GameAction& a) {
        record_weapons(use)(gs, a);
        if (const auto* fi = std::get_if<action::Fight>(&a)) {
            for (const MeleeDeclaration& d : fi->declarations) {
                if (is_extra_attacks_weapon(find_model(gs, d.model)->weapons.at(d.weapon_index))) {
                    ++extra_attacks_fights;
                    EXPECT_GE(std::count_if(fi->declarations.begin(), fi->declarations.end(),
                                            [&](const MeleeDeclaration& x) { return x.model == d.model; }),
                              2);  // with the (now usable) bonesword
                }
            }
        }
    };
    for (std::uint64_t seed = 21; seed <= 24; ++seed) {
        GameState gs = rosters_with_new_abilities(seed % 2 == 0 ? 26.0f : 35.5f);
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 4000, hook, &events);
        EXPECT_EQ(gs.flow.stage, Stage::GameOver) << "seed " << seed << " after " << stats.actions << " actions";
        EXPECT_EQ(Json(gs).get<GameState>(), gs) << "seed " << seed;
    }
    int hazard_rolls = 0;
    int sustained = 0;
    for (const GameEvent& e : events) {
        if (std::holds_alternative<event::HazardRollsMade>(e)) ++hazard_rolls;
        if (const auto* h = std::get_if<event::HitRollsMade>(&e)) sustained += h->rolls.sustained_hits;
    }
    int criticals = 0;
    for (const GameEvent& e : events) {
        if (const auto* h = std::get_if<event::HitRollsMade>(&e)) criticals += h->rolls.critical_hits();
    }
    EXPECT_GT(hazard_rolls, 0);
    EXPECT_GT(sustained, 0);
    EXPECT_GT(extra_attacks_fights, 0);
    EXPECT_GT(use.used.count("Plasma gun - supercharge") + use.used.count("Plasma pistol - supercharge"), 0u);
    EXPECT_GT(use.used.count("Havoc heavy bolter") + use.used.count("Heavy bolter"), 0u);
}

namespace {

// M61: the roster fixture with the Hive Tyrant's real [TWIN-LINKED] bonesword, and the Tyrannofex's fleshborer hive
// ([HEAVY], [SUSTAINED HITS 1], [TWIN-LINKED]; the Tyrannofex arrives in M65) as a stand-in for the Screamer-killers'
// bio-plasmic scream (random A, refused until M62).
GameState rosters_with_twin_linked(float closer_in) {
    GameState gs = wh40k::test::load_rosters_fixture_closer(closer_in);
    const Datasheet tyrannofex = load_datasheet(wh40k::test::repo_root() / "data/units/tyrannofex.json");
    const Weapon* hive = tyrannofex.find_weapon("Fleshborer hive");
    EXPECT_TRUE(hive != nullptr);
    for (Unit& u : gs.units) {
        for (Model& m : u.models) {
            for (Weapon& w : m.weapons) {
                if (w.name == "Bio-plasmic scream" && hive != nullptr) w = *hive;
            }
        }
    }
    return gs;
}

}  // namespace

TEST(D004_LegalActions, RandomPlayExercisesTwinLinkedReRolls) {
    // M61 (D-072): the D-004 properties with [TWIN-LINKED] weapons in play. Random play answers the re-roll decision
    // with every failed roll, none, or any other subset; Sustained Hits' extra wound rolls are re-rolled too.
    WeaponUse use;
    std::vector<GameEvent> events;
    int all_failed = 0;
    int none = 0;
    int other = 0;
    const ActionHook hook = [&](const GameState& gs, const GameAction& a) {
        record_weapons(use)(gs, a);
        const auto* r = std::get_if<action::RerollWoundRolls>(&a);
        if (r == nullptr) return;
        EXPECT_TRUE(gs.flow.attack && gs.flow.attack->wound_rerolls);
        std::vector<std::size_t> picked = r->indices;
        std::sort(picked.begin(), picked.end());
        if (picked.empty()) {
            ++none;
        } else if (picked == failed_wound_rolls(gs.flow.attack->wound_rerolls->rolls)) {
            ++all_failed;
        } else {
            ++other;
        }
    };
    for (std::uint64_t seed = 31; seed <= 34; ++seed) {
        GameState gs = rosters_with_twin_linked(seed % 2 == 0 ? 26.0f : 35.5f);
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 4000, hook, &events);
        EXPECT_EQ(gs.flow.stage, Stage::GameOver) << "seed " << seed << " after " << stats.actions << " actions";
        EXPECT_EQ(Json(gs).get<GameState>(), gs) << "seed " << seed;
    }
    int rerolled = 0;
    int sustained_rerolled = 0;
    int sustained = 0;  // the sustained hits of the batch whose dice were rolled last
    for (const GameEvent& e : events) {
        if (const auto* h = std::get_if<event::HitRollsMade>(&e)) sustained = h->rolls.sustained_hits;
        if (const auto* w = std::get_if<event::WoundRollsRerolled>(&e)) {
            ++rerolled;
            if (sustained > 0 && !w->rerolls.empty()) ++sustained_rerolled;
        }
    }
    EXPECT_GT(all_failed, 0);
    EXPECT_GT(none, 0);
    EXPECT_GT(other, 0);
    EXPECT_EQ(rerolled, all_failed + none + other);  // one event per answer, "Keep" included
    EXPECT_GT(sustained_rerolled, 0);
    EXPECT_GT(use.used.count("Monstrous bonesword and lash whip"), 0u);
    EXPECT_GT(use.used.count("Fleshborer hive"), 0u);
}

TEST(D026_UnimplementedAbilities, RosterWeaponsTheEngineCantResolveAreRefusedWithARuleCitedReason) {
    // Side A's first Shooting phase on the roster fixture: the Havoc lascannons (D6+1 damage, D-002) are
    // in range of the east Termagants (48") but never offered, and declaring them is a rule-cited refusal.
    GameState gs = wh40k::test::load_rosters_fixture();
    start_game(gs);
    for (std::uint32_t id = 1; id <= 5; ++id) {
        apply_action(gs, Side::A, action::StartMove{UnitId{id}, MoveType::RemainStationary, std::nullopt});
    }
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    const Unit& havocs = *gs.find_unit(UnitId{5});
    const UnitId east_gants{8};
    for (const ShotDeclaration& d : shot_options(gs, havocs, ShootingType::Normal)) {
        EXPECT_NE(find_model(havocs, d.model)->weapons.at(d.weapon_index).name, "Havoc lascannon");
    }
    const Model& gunner = havocs.models.at(1);
    ASSERT_EQ(gunner.weapons.at(0).name, "Havoc lascannon");
    ASSERT_TRUE(target_within_range(gunner, gunner.weapons[0].profiles[0], *gs.find_unit(east_gants)));
    const action::Shoot lascannon{havocs.id, ShootingType::Normal, {ShotDeclaration{gunner.id, 0, 0, east_gants}}};
    const ValidationResult r = validate_action(gs, Side::A, lascannon);
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(std::any_of(r.reasons.begin(), r.reasons.end(),
                            [](const std::string& s) { return s.rfind("D-002: ", 0) == 0; }))
        << r.reasons.front();
    for (const auto& reason : r.reasons) EXPECT_TRUE(is_rule_cited(reason)) << reason;

    // Melee: the Hive Tyrant's bonesword was refused for [TWIN-LINKED] until M61 (D-026); it is resolved since D-072.
    // A stand-in with [DEVASTATING WOUNDS] (24.10, still refused) takes its place here. (Its scything talons are an
    // [EXTRA ATTACKS] weapon since M60, D-071: with the bonesword refused, the talons alone are the legal selection.)
    GameState close = load_scenario(wh40k::test::repo_root() / wh40k::test::kHavocsVsTyrantFixture,
                                    wh40k::test::repo_root());
    Unit& tyrant = close.units[1];
    ASSERT_TRUE(tyrant.models[0].weapons[0].profiles[0].has<ability::TwinLinked>());
    EXPECT_FALSE(unimplemented_ability_problem(tyrant.models[0].weapons[0].profiles[0]).has_value());  // M61
    tyrant.models[0].weapons[0].profiles[0].abilities.push_back(ability::DevastatingWounds{});
    const Model& target = close.units[0].models[2];
    tyrant.models[0].position = Position{target.position.x,
                                         target.position.y + base_radius(target.base) + base_radius(tyrant.models[0].base),
                                         0.0f};  // base contact
    const std::vector<MeleeDeclaration> bonesword{
        MeleeDeclaration{tyrant.models[0].id, 0, 0, {MeleeTarget{close.units[0].id, 6}}}};
    const auto problems = melee_problems(close, tyrant, bonesword);
    ASSERT_FALSE(problems.empty());
    EXPECT_EQ(problems.front().rfind("24.10: ", 0), 0u) << problems.front();
    for (const MeleeDeclaration& d : melee_options(close, tyrant)) EXPECT_NE(d.weapon_index, 0u);  // never offered
}
