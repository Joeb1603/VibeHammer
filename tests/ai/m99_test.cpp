// M99 (D-101): the bot and the weapon abilities of M99: [ANTI-X Y+] (24.03) and [DEVASTATING WOUNDS] (24.10) in its
// odds (checked against the engine's own batches), [MELTA X] (24.25) and [ANTI] in its hypothetical volleys, and whole
// games with armies that carry the weapons M99 made usable.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attacks.hpp"
#include "session.hpp"
#include "support/army_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::group;
using wh40k::test::in;
using wh40k::test::repo_root;

namespace {

WeaponProfile roster_profile(const char* unit, const char* weapon, std::size_t p = 0) {
    return load_datasheet(repo_root() / (std::string("data/units/") + unit + ".json")).find_weapon(weapon)->profiles.at(p);
}

// The exact outcome against 20,000 engine batches (as M40's grid checks it): the means within 2% of the target's
// wounds, each P(k models destroyed) within 0.015.
void expect_matches_samples(const Volley& volley, const Unit& target, const std::string& label) {
    OddsCache cache;
    const TargetSnapshot snap = snapshot_of(target);
    const AttackOutcome exact = expected_outcome(cache, volley, snap);
    const SampledOutcome s = sample_outcome(volley, target, 99, 20000);
    const double scale = snap.wounds_left();
    EXPECT_NEAR(s.damage, exact.damage, 0.02 * scale) << label;
    EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * snap.models()) << label;
    ASSERT_EQ(s.models_destroyed_pmf.size(), exact.models_destroyed_pmf.size());
    for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
        EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << label << " k=" << k;
    }
}

// An army with the weapons M99 made usable: a Chaos Lord with the Daemon hammer ([DEVASTATING WOUNDS]), Terminators
// with a Chainfist ([ANTI-VEHICLE 3+]), Combi-weapons ([ANTI-INFANTRY 4+], [DEVASTATING WOUNDS]) and a Reaper
// autocannon, Havocs and Legionaries with a Meltagun ([MELTA 2]).
ArmyList m99_csm() {
    ArmyList l{"Chaos Space Marines (M99)", "chaos_space_marines", {}};
    l.units.push_back({"data/units/chaos_lord.json", {group("Chaos Lord", 1, {"Daemon hammer", "Plasma pistol"})}});
    l.units.push_back({"data/units/chaos_terminator_squad.json",
                       {group("Terminator Champion", 1, {"Chainfist", "Combi-weapon"}),
                        group("Accursed weapon and combi-weapon", 2, {"Accursed weapon", "Combi-weapon"}),
                        group("Power fist and combi-weapon", 1, {"Power fist", "Combi-weapon"}),
                        group("Heavy weapon", 1, {"Reaper autocannon", "Accursed weapon"})}});
    l.units.push_back({"data/units/havocs.json",
                       {group("Havoc Champion", 1, {"Astartes chainsword", "Meltagun"}),
                        group("Havoc w/ heavy bolter", 4, {"Havoc heavy bolter", "Close combat weapon"})}});
    l.units.push_back({"data/units/legionaries.json",
                       {group("Aspiring Champion", 1, {"Close combat weapon", "Bolt pistol", "Boltgun"}),
                        group("Legionary w/ boltgun", 3, {"Boltgun", "Bolt pistol", "Close combat weapon"}),
                        group("Legionary w/ other weapon", 1, {"Bolt pistol", "Close combat weapon", "Meltagun"})}});
    return l;
}

// Tyranids with two Stranglewebs (D6 attacks, [TORRENT], [DEVASTATING WOUNDS]) among the Termagants.
ArmyList m99_tyranids() {
    ArmyList l{"Tyranids (M99)", "tyranids", {}};
    l.units.push_back({"data/units/hive_tyrant.json",
                       {group("Hive Tyrant", 1, {"Monstrous bonesword and lash whip", "Monstrous scything talons"})}});
    l.units.push_back({"data/units/termagants.json",
                       {group("Termagants", 18, {"Chitinous claws and teeth", "Fleshborer"}),
                        group("Termagant w/ Strangleweb", 2, {"Strangleweb", "Chitinous claws and teeth"})}});
    l.units.push_back({"data/units/genestealers.json", {group("Genestealer", 5, {"Genestealers claws and talons"})}});
    l.units.push_back({"data/units/tyrant_guard.json", {group("Tyrant Guard", 3, {"Scything talons and rending claws"})}});
    return l;
}

}  // namespace

TEST(M99_Odds, TheCriticalWoundChanceFollowsTheAntiThreshold) {
    OddsCache cache;
    AttackProfile p;
    p.skill = 3;
    p.strength = 2;  // against T 5: 6+
    p.damage = DiceExpr::fixed(1);
    const HitWoundOdds plain = cache.hit_wound(p, 5);
    EXPECT_NEAR(plain.wound, 1.0 / 6.0, 1e-12);
    EXPECT_NEAR(plain.critical_wound, 1.0 / 6.0, 1e-12);
    EXPECT_FALSE(plain.devastating);
    p.critical_wound_on = 4;  // 24.03: 4, 5 and 6 are critical wounds
    const HitWoundOdds anti = cache.hit_wound(p, 5);
    EXPECT_NEAR(anti.wound, 3.0 / 6.0, 1e-12);
    EXPECT_NEAR(anti.critical_wound, 3.0 / 6.0, 1e-12);
    // A different key: the plain profile's odds are still its own.
    EXPECT_NEAR(cache.hit_wound(AttackProfile{p.skill, false, 2, 0, DiceExpr::fixed(1), 0, 0, {}, false, 6}, 5).wound,
                1.0 / 6.0, 1e-12);
    p.attack_time_abilities = {ability::DevastatingWounds{}};
    EXPECT_TRUE(cache.hit_wound(p, 5).devastating);
}

TEST(M99_Odds, DevastatingWoundsAgreeWithTwentyThousandEngineBatches) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const Unit& marines = gs.units[0];  // T 5, W 2, Sv 3+
    const Unit& gants = gs.units[1];    // T 3, W 1, Sv 5+
    const AttackContext shooting{AttackType::Shooting, false};
    // The Terminators' Combi-weapon as the engine resolves it against INFANTRY: critical wounds on 4+, no save for them.
    const WeaponProfile combi = roster_profile("chaos_terminator_squad", "Combi-weapon");
    WeaponSelection sel;
    const AttackProfile at_marines = selection_attack_profile(combi, sel, shooting, marines);
    EXPECT_EQ(at_marines.critical_wound_on, 4);
    EXPECT_TRUE(devastating_wounds(at_marines));
    expect_matches_samples(Volley{VolleyBatch{at_marines, 12, {}}}, marines, "Combi-weapon vs Intercessors");
    expect_matches_samples(Volley{VolleyBatch{selection_attack_profile(combi, sel, shooting, gants), 12, {}}}, gants,
                           "Combi-weapon vs Termagants");
    // The Daemon hammer (D 3 here against W 2: the excess of each critical wound is lost) and a wounded front model.
    const WeaponProfile hammer = roster_profile("chaos_lord", "Daemon hammer");
    const AttackContext fighting{AttackType::Fighting, false};
    Unit wounded = marines;
    wounded.models[2].wounds_remaining = 1;
    expect_matches_samples(Volley{VolleyBatch{selection_attack_profile(hammer, sel, fighting, marines), 8, {}}}, marines,
                           "Daemon hammer vs Intercessors");
    expect_matches_samples(Volley{VolleyBatch{selection_attack_profile(hammer, sel, fighting, wounded), 8, {}}}, wounded,
                           "Daemon hammer vs Intercessors (wounded)");
    // With [SUSTAINED HITS 1] and [TWIN-LINKED] on the same profile, and a random D.
    AttackProfile mixed = at_marines;
    mixed.attack_time_abilities = {ability::TwinLinked{}, ability::DevastatingWounds{}, ability::SustainedHits{1}};
    mixed.damage = DiceExpr{1, 3, 0};
    expect_matches_samples(Volley{VolleyBatch{mixed, 8, {}}}, marines, "sustained, twin-linked, D3");
}

TEST(M99_Odds, TheHypotheticalVolleysReadMeltaAndAnti) {
    // Havocs whose champion has a Meltagun (12", D D6, [MELTA 2]) against the Hive Tyrant: D6+2 within 6", D6 beyond.
    const GameState battle = wh40k::test::custom_battle(m99_csm(), m99_tyranids());
    const Unit* havocs = nullptr;
    const Unit* terminators = nullptr;
    const Unit* tyrant = nullptr;
    for (const Unit& u : battle.units) {
        if (u.name.rfind("Havocs", 0) == 0) havocs = &u;
        if (u.name.rfind("Chaos Terminator", 0) == 0) terminators = &u;
        if (u.name.rfind("Hive Tyrant", 0) == 0) tyrant = &u;
    }
    ASSERT_TRUE(havocs != nullptr && terminators != nullptr && tyrant != nullptr);
    OddsCache cache;
    const auto melta_damage = [&](float gap_in) -> std::optional<DiceExpr> {
        ShotContext context;
        context.gap_mm = in(gap_in);
        for (const VolleyBatch& b : unit_ranged_volley(cache, *havocs, *tyrant, context)) {
            if (b.profile.damage.num_dice == 1 && b.profile.damage.sides == 6) return b.profile.damage;
        }
        return std::nullopt;
    };
    EXPECT_EQ(melta_damage(5.0f), (DiceExpr{1, 6, 2}));
    EXPECT_EQ(melta_damage(10.0f), (DiceExpr{1, 6, 0}));
    EXPECT_EQ(melta_damage(20.0f), std::nullopt);  // out of its 12"
    // The Chainfist ([ANTI-VEHICLE 3+]) against a VEHICLE: critical wounds on 3+; against the Hive Tyrant (a MONSTER), 6.
    Unit vehicle = *tyrant;
    vehicle.keywords = {"Vehicle"};
    const auto chainfist_threshold = [&](const Unit& target) {
        int lowest = 6;
        for (const VolleyBatch& b : unit_melee_volley(cache, *terminators, target)) {
            lowest = std::min(lowest, b.profile.critical_wound_on);
        }
        return lowest;
    };
    EXPECT_EQ(chainfist_threshold(vehicle), 3);
    EXPECT_EQ(chainfist_threshold(*tyrant), 6);
    // The Combi-weapons ([ANTI-INFANTRY 4+]) against the INFANTRY Termagants.
    const Unit* gants = nullptr;
    for (const Unit& u : battle.units) {
        if (u.name.rfind("Termagants", 0) == 0) gants = &u;
    }
    ASSERT_NE(gants, nullptr);
    ShotContext close;
    close.gap_mm = in(10.0f);
    bool anti = false;
    for (const VolleyBatch& b : unit_ranged_volley(cache, *terminators, *gants, close)) {
        anti = anti || (b.profile.critical_wound_on == 4 && devastating_wounds(b.profile));
    }
    EXPECT_TRUE(anti);
}

TEST(M99_Games, ArmiesWithTheNewWeaponsPlayClean) {
    // The two lists are legal, and the bot plays them to the end on both sides with no rejected action, no reverted move
    // and no fallback; the weapons are used ([DEVASTATING WOUNDS] inflicts its mortal wounds in these games).
    for (const ArmyList& l : {m99_csm(), m99_tyranids()}) {
        const Faction* f = find_faction(wh40k::test::factions(), l.faction);
        ASSERT_NE(f, nullptr);
        EXPECT_TRUE(army_problems(l, *f, wh40k::test::army_resolver()).empty()) << l.name;
    }
    const auto battles = [](std::uint64_t seed) {
        return wh40k::test::custom_battle(m99_csm(), m99_tyranids(), seed % 2 == 0 ? Side::B : Side::A);
    };
    int devastating = 0;
    for (std::uint64_t seed = 1; seed <= 2; ++seed) {
        const BotConfig bot{};
        wh40k::client::GameSession session = wh40k::client::GameSession::new_game(
            battles(seed), seed, {true, true}, {!bot.plays_stratagems(), !bot.plays_stratagems()});
        const GameRecord record = play_bot_session(session, bot, bot);
        EXPECT_TRUE(record.finished) << "seed " << seed;
        EXPECT_EQ(record.rejected, 0u) << "seed " << seed;
        EXPECT_EQ(record.reverted, 0u) << "seed " << seed;
        EXPECT_EQ(record.fallbacks, 0u) << "seed " << seed;
        for (const auto& step : session.log()) {
            for (const GameEvent& e : step.events) {
                if (std::holds_alternative<event::DevastatingWoundsInflicted>(e)) ++devastating;
            }
        }
    }
    EXPECT_GT(devastating, 0);
}
