// M100 (D-102): the bot and the weapon abilities of M100: [LETHAL HITS] (24.23) in its odds (checked against the engine's
// own batches), [PSYCHIC] (24.29) and a spent [ONE SHOT] weapon (24.26) in its hypothetical volleys, and whole games
// with the Balefire tome.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
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

void expect_matches_samples(const Volley& volley, const Unit& target, const std::string& label) {
    OddsCache cache;
    const TargetSnapshot snap = snapshot_of(target);
    const AttackOutcome exact = expected_outcome(cache, volley, snap);
    const SampledOutcome s = sample_outcome(volley, target, 99, 20000);
    EXPECT_NEAR(s.damage, exact.damage, 0.02 * snap.wounds_left()) << label;
    EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * snap.models()) << label;
    ASSERT_EQ(s.models_destroyed_pmf.size(), exact.models_destroyed_pmf.size());
    for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
        EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << label << " k=" << k;
    }
}

AttackProfile bolt(std::vector<WeaponAbility> abilities) {
    AttackProfile p;
    p.skill = 3;
    p.strength = 4;
    p.armour_penetration = -1;
    p.damage = DiceExpr::fixed(1);
    p.attack_time_abilities = std::move(abilities);
    return p;
}

// Legionaries with the Balefire tome ([PSYCHIC]), and the rest of a legal list.
ArmyList tome_csm() {
    ArmyList l{"Chaos Space Marines (M100)", "chaos_space_marines", {}};
    l.units.push_back({"data/units/chaos_lord.json", {group("Chaos Lord", 1, {"Accursed weapon", "Plasma pistol"})}});
    l.units.push_back({"data/units/legionaries.json",
                       {group("Aspiring Champion", 1, {"Close combat weapon", "Bolt pistol", "Boltgun"}),
                        group("Legionary w/ boltgun", 3, {"Boltgun", "Bolt pistol", "Close combat weapon"}),
                        group("Legionary w/ balefire tome", 1, {"Balefire tome", "Bolt pistol", "Close combat weapon"})}});
    l.units.push_back({"data/units/havocs.json",
                       {group("Havoc Champion", 1, {"Astartes chainsword", "Flamer"}),
                        group("Havoc w/ heavy bolter", 4, {"Havoc heavy bolter", "Close combat weapon"})}});
    return l;
}

}  // namespace

TEST(M100_Odds, LethalHitsAgreeWithTwentyThousandEngineBatches) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const Unit& marines = gs.units[0];  // T 5, W 2, Sv 3+
    const Unit& gants = gs.units[1];    // T 3, W 1, Sv 5+
    OddsCache cache;
    const HitWoundOdds odds = cache.hit_wound(bolt({ability::LethalHits{}}), 5);
    EXPECT_TRUE(odds.lethal);
    EXPECT_FALSE(cache.hit_wound(bolt({}), 5).lethal);
    expect_matches_samples(Volley{VolleyBatch{bolt({ability::LethalHits{}}), 14, {}}}, marines, "lethal vs Intercessors");
    expect_matches_samples(Volley{VolleyBatch{bolt({ability::LethalHits{}}), 8, {}}}, gants, "lethal vs Termagants");
    expect_matches_samples(Volley{VolleyBatch{bolt({ability::LethalHits{}, ability::SustainedHits{1}}), 10, {}}}, marines,
                           "lethal and sustained vs Intercessors");
    expect_matches_samples(Volley{VolleyBatch{bolt({ability::LethalHits{}, ability::TwinLinked{}}), 12, {}}}, marines,
                           "lethal and twin-linked vs Intercessors");
    // More wounds than the same attacks without it.
    const TargetSnapshot snap = snapshot_of(marines);
    const VolleyBatch with[] = {VolleyBatch{bolt({ability::LethalHits{}}), 14, {}}};
    const VolleyBatch without[] = {VolleyBatch{bolt({}), 14, {}}};
    EXPECT_GT(expected_outcome(cache, with, snap).wounds, expected_outcome(cache, without, snap).wounds);
}

TEST(M100_Odds, ProfileSupportedKnowsASpentOneShotWeapon) {
    Weapon w = wh40k::test::load_v1_scenario().units[0].models[0].weapons[0];
    w.profiles[0].abilities.push_back(ability::OneShot{});
    EXPECT_TRUE(profile_supported(w, w.profiles[0]));
    w.one_shot_spent = true;
    EXPECT_FALSE(profile_supported(w, w.profiles[0]));
    EXPECT_TRUE(profile_supported(w.profiles[0]));  // the profile alone is one the engine resolves
    // A unit's hypothetical volley leaves a spent weapon out.
    GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    ShotContext context;
    context.gap_mm = in(10.0f);
    const auto dice_of = [&](const Unit& attacker) {
        int dice = 0;
        for (const VolleyBatch& b : unit_ranged_volley(cache, attacker, gs.units[1], context)) dice += b.attack_dice;
        return dice;
    };
    const int before = dice_of(gs.units[0]);
    for (Model& m : gs.units[0].models) {
        for (Weapon& weapon : m.weapons) {
            for (WeaponProfile& p : weapon.profiles) {
                if (p.kind == WeaponKind::Ranged) p.abilities.push_back(ability::OneShot{});
            }
        }
    }
    EXPECT_EQ(dice_of(gs.units[0]), before);
    for (Model& m : gs.units[0].models) {
        for (Weapon& weapon : m.weapons) weapon.one_shot_spent = true;
    }
    EXPECT_EQ(dice_of(gs.units[0]), 0);
}

TEST(M100_Odds, APsychicWeaponIgnoresCoverInTheHypotheticalVolley) {
    const WeaponProfile tome =
        load_datasheet(repo_root() / "data/units/legionaries.json").find_weapon("Balefire tome")->profiles.at(0);
    ASSERT_TRUE(tome.has<ability::Psychic>());
    GameState gs = wh40k::test::load_v1_scenario();
    Unit shooter = gs.units[0];
    shooter.models.resize(1);
    shooter.models[0].weapons = {Weapon{"Balefire tome", WeaponKind::Ranged, {tome}}};
    OddsCache cache;
    ShotContext context;
    context.gap_mm = in(10.0f);
    const Volley open = unit_ranged_volley(cache, shooter, gs.units[1], context);
    context.target_cover = true;
    const Volley covered = unit_ranged_volley(cache, shooter, gs.units[1], context);
    ASSERT_EQ(open.size(), 1u);
    EXPECT_EQ(covered, open);  // 24.29: the worsened BS is ignored
    // The same weapon without the ability does lose a point of BS in cover.
    shooter.models[0].weapons[0].profiles[0].abilities.clear();
    const Volley plain = unit_ranged_volley(cache, shooter, gs.units[1], context);
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_EQ(*plain[0].profile.skill, *open[0].profile.skill + 1);
}

TEST(M100_Games, AnArmyWithTheBalefireTomePlaysClean) {
    const ArmyList csm = tome_csm();
    const Faction* f = find_faction(wh40k::test::factions(), csm.faction);
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(army_problems(csm, *f, wh40k::test::army_resolver()).empty());
    for (std::uint64_t seed = 1; seed <= 2; ++seed) {
        const BotConfig bot{};
        wh40k::client::GameSession session = wh40k::client::GameSession::new_game(
            wh40k::test::custom_battle(csm, wh40k::test::stock_army(wh40k::test::tyranids()),
                                       seed % 2 == 0 ? Side::B : Side::A),
            seed, {true, true}, {!bot.plays_stratagems(), !bot.plays_stratagems()});
        const GameRecord record = play_bot_session(session, bot, bot);
        EXPECT_TRUE(record.finished) << "seed " << seed;
        EXPECT_EQ(record.rejected, 0u) << "seed " << seed;
        EXPECT_EQ(record.reverted, 0u) << "seed " << seed;
        EXPECT_EQ(record.fallbacks, 0u) << "seed " << seed;
    }
}

TEST(M100_Bot, FiresTheBalefireTomeWhenInRange) {
    // The Termagants 10" from the Legionaries at Side A's Shoot step: the bot's Shoot for the Legionaries includes the
    // model with the Balefire tome (18", [PSYCHIC]) firing it.
    GameState gs = wh40k::test::custom_battle(tome_csm(), wh40k::test::stock_army(wh40k::test::tyranids()));
    Unit* legion = nullptr;
    Unit* gants = nullptr;
    for (Unit& u : gs.units) {
        if (u.name.rfind("Legionaries", 0) == 0) legion = &u;
        if (u.name.rfind("Termagants", 0) == 0 && gants == nullptr) gants = &u;
    }
    ASSERT_TRUE(legion != nullptr && gants != nullptr);
    float lx = 0.0f;
    float gx = 0.0f;
    for (const Model& m : legion->models) lx += m.position.x / static_cast<float>(legion->models.size());
    for (const Model& m : gants->models) gx += m.position.x / static_cast<float>(gants->models.size());
    for (Model& m : gants->models) m.position.x += lx - gx;
    for (int i = 0; i < 4; ++i) {
        const float gap = *unit_distance(*legion, *gants);
        for (Model& m : gants->models) m.position.y -= gap - in(10.0f);
    }
    const UnitId legion_id = legion->id;
    start_game(gs);
    while (gs.pending && !std::holds_alternative<decision::Shoot>(gs.pending->kind)) {
        apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    ASSERT_TRUE(gs.pending && gs.pending->side == Side::A);
    Bot bot(BotConfig{});
    bool tome_fired = false;
    for (int guard = 0; guard < 200 && gs.pending && gs.flow.stage == Stage::Shoot; ++guard) {
        const Side side = gs.pending->side;
        GameAction action = legal_actions(gs, side).front();
        if (std::holds_alternative<decision::Shoot>(gs.pending->kind)) {
            const BotChoice choice = bot.decide(gs, side);
            ASSERT_FALSE(choice.stopped);
            action = choice.action;
            if (const auto* shoot = std::get_if<action::Shoot>(&action); shoot != nullptr && shoot->unit == legion_id) {
                for (const ShotDeclaration& d : shoot->shots) {
                    const Model* m = find_model(gs, d.model);
                    ASSERT_NE(m, nullptr);
                    tome_fired = tome_fired || m->weapons.at(d.weapon_index).name == "Balefire tome";
                }
            }
        } else if (auto* order = std::get_if<action::OrderCasualties>(&action); order != nullptr && order->priority.empty()) {
            for (const Model& m : gs.find_unit(order->target)->models) order->priority.push_back(m.id);
        }
        ASSERT_TRUE(validate_action(gs, side, action).ok());
        apply_action(gs, side, action);
    }
    EXPECT_TRUE(tome_fired);
}
