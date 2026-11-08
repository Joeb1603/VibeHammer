// M68 (D-077, amends D-066): Easy and Normal for the roster units. The odds over each model's own loadout (checked against
// engine batches), [EXTRA ATTACKS] in every melee estimate, the cost of [HAZARDOUS] (checked against the engine's hazard
// rolls and mortal wounds), 10.06's and 17.03's -1s in hypothetical volleys, engaged MONSTERS still shooting in the
// charge planner, a MONSTER's Normal move through enemy infantry (17.01), material weighted by points, the points
// tiebreak in the move score's objective, and clean games of every Easy / Normal pairing on the roster scenario.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/threat.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "normal/shooting.hpp"
#include "normal/value.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::repo_root;
namespace mf = wh40k::test::monsters;
namespace normal = wh40k::ai::normal;

namespace {

// The roster scenario's units, by index (D-076: numbered west to east, Side A first).
constexpr std::size_t kChainswords = 0;
constexpr std::size_t kLord = 1;
constexpr std::size_t kHavocs = 3;
constexpr std::size_t kBoltguns = 4;
constexpr std::size_t kTermagants = 5;
constexpr std::size_t kGenestealers = 6;
constexpr std::size_t kGuard = 8;
constexpr std::size_t kTyrant = 9;

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

// Plays a started game, every unit remaining stationary and every step ended, until `side` has a decision of `Kind`.
template <typename Kind>
void advance_to(GameState& gs, Side side) {
    for (int guard = 0; guard < 600 && gs.pending; ++guard) {
        if (gs.pending->side == side && std::holds_alternative<Kind>(gs.pending->kind)) return;
        const Side d = decider(gs);
        const DecisionKind& k = gs.pending->kind;
        const std::vector<GameAction> legal = legal_actions(gs, d);
        GameAction a = legal.front();
        if (std::holds_alternative<decision::MoveUnits>(k)) {
            for (const GameAction& l : legal) {
                const auto* m = std::get_if<action::StartMove>(&l);
                if (m != nullptr && m->move_type == MoveType::RemainStationary) {
                    a = l;
                    break;
                }
            }
        } else if (std::holds_alternative<decision::Shoot>(k)) {
            a = action::EndShootStep{};
        } else if (std::holds_alternative<decision::Charge>(k)) {
            a = action::EndChargeStep{};
        }
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*gs.find_unit(oc->target));
        }
        apply_action(gs, d, a);
    }
    FAIL() << "the decision was not reached";
}

// One rank of `u`'s models along x, touching, centred on (x_in, y_in) (any base size: round bases only).
void rank_at(Unit& u, float x_in, float y_in) { mf::place_rank(u, x_in, y_in); }

float radius_in(const Unit& u) { return mm_to_inches(base_radius(u.models.front().base)); }

Weapon datasheet_weapon(const char* unit_file, const char* name) {
    return *load_datasheet(repo_root() / "data/units" / unit_file).find_weapon(name);
}

// The volley's batches as the engine gathers them for these declarations (D-006: identical attacks are one batch).
Volley engine_volley(const GameState& gs, UnitId shooter, ShootingType type, const std::vector<ShotDeclaration>& shots) {
    Volley v;
    for (const AttackBatch& b : shooting_batches(gs, shooter, type, shots)) v.push_back(volley_batch(b));
    return v;
}

// Each model's own first ranged weapon at `target` (the declarations a mixed loadout makes), with profile `profile` of a
// weapon that has several (the plasma guns' standard 0 and supercharge 1), else its only one.
std::vector<ShotDeclaration> own_weapons_at(const GameState& gs, std::size_t shooter, UnitId target, ShootingType type,
                                            std::size_t profile) {
    std::vector<ShotDeclaration> out;
    const Unit& u = gs.units[shooter];
    for (const ShotDeclaration& o : shot_options(gs, u, type)) {
        if (o.target != target) continue;
        const Model& m = *find_model(u, o.model);
        const std::size_t want = m.weapons[o.weapon_index].profiles.size() > 1 ? profile : 0;
        if (o.profile_index != want) continue;
        if (std::any_of(out.begin(), out.end(), [&](const ShotDeclaration& d) { return d.model == o.model; })) continue;
        out.push_back(o);
    }
    return out;
}

// As M60-M62: the exact outcome within 2% of 20,000 engine resolutions (core's resolve_attack_batch with the bot's own
// dice), and each models-destroyed probability within 0.015. For a volley destroying under half a model on average (the
// Chaos Lord's one pistol shot) 2% of it is below the samples' own noise, so models destroyed is held to 0.01 of a model.
void expect_matches_samples(OddsCache& cache, const Volley& v, const Unit& target, std::uint64_t seed,
                            const std::string& what) {
    const AttackOutcome exact = expected_outcome(cache, v, snapshot_of(target));
    const SampledOutcome s = sample_outcome(v, target, seed, 20000);
    EXPECT_NEAR(s.damage, exact.damage, 0.02 * exact.damage) << what;
    EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * std::max(exact.models_destroyed, 0.5)) << what;
    for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
        EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << what << " k=" << k;
    }
}

}  // namespace

// ---- 1. Per-model loadouts ----

TEST(M68_Loadouts, AUnitsVolleyIsTheSumOfItsLoadoutGroupsAsTheEngineGathersIt) {
    // The Havocs (four lascannons, the champion's plasma gun), the boltgun Legionaries (nine boltguns, the champion's
    // bolt pistol, 24.07) and the Chaos Lord, each at the Tyrant Guard 10" away: the hypothetical volley (each model its
    // own weapons) is the engine's batches for those declarations, and its exact outcome matches 20,000 engine batches.
    GameState gs = wh40k::test::load_rosters_scenario();
    rank_at(gs.units[kGuard], 22.0f, 30.0f);
    rank_at(gs.units[kHavocs], 22.0f, 30.0f - 10.0f - radius_in(gs.units[kGuard]) - radius_in(gs.units[kHavocs]));
    rank_at(gs.units[kBoltguns], 22.0f, 30.0f + 10.0f + radius_in(gs.units[kGuard]) + radius_in(gs.units[kBoltguns]));
    const Model& east = gs.units[kGuard].models.back();  // the Guard's east end; the Lord 10" east of it
    gs.units[kLord].models.front().position =
        Position{east.position.x + base_radius(east.base) + in(10.0f) + base_radius(gs.units[kLord].models.front().base),
                 in(30.0f), 0.0f};
    start_game(gs);
    advance_to<decision::Shoot>(gs, Side::A);
    const Unit& guard = gs.units[kGuard];
    OddsCache cache;
    std::uint64_t seed = 6801;
    for (const std::size_t shooter : {kHavocs, kBoltguns, kLord}) {
        const Unit& u = gs.units[shooter];
        const float gap = *unit_distance(u, guard);
        ASSERT_LE(gap, in(10.5f)) << u.name;
        ShotContext ctx;
        ctx.heavy = true;
        ctx.gap_mm = gap;
        const Volley hypothetical = unit_ranged_volley(cache, u, guard, ctx);
        // The engine's batches for each model's own weapon, with either plasma profile (the hypothetical volley takes
        // the one worth more; unpriced here, ShotContext::hazard_rate 0).
        const Volley engine_standard =
            engine_volley(gs, u.id, ShootingType::Normal, own_weapons_at(gs, shooter, guard.id, ShootingType::Normal, 0));
        const Volley engine_super =
            engine_volley(gs, u.id, ShootingType::Normal, own_weapons_at(gs, shooter, guard.id, ShootingType::Normal, 1));
        ASSERT_FALSE(engine_standard.empty()) << u.name;
        EXPECT_TRUE(hypothetical == engine_standard || hypothetical == engine_super) << u.name;
        if (shooter == kHavocs) {
            ASSERT_EQ(hypothetical.size(), 2u);  // the champion's plasma gun, then the four lascannons (D D6+1)
            EXPECT_EQ(hypothetical[1].attack_dice, 4);
            EXPECT_FALSE(hypothetical[1].profile.damage.is_fixed());
        }
        if (shooter == kBoltguns) {
            // 24.07: the champion's bolt pistol and nine boltguns, identical attacks (S5 AP-1 D1): one batch (D-006).
            ASSERT_EQ(hypothetical.size(), 1u);
            EXPECT_EQ(hypothetical[0].attack_dice, 9 * 2 + 1);
        }
        expect_matches_samples(cache, engine_standard, guard, seed++, u.name);
        expect_matches_samples(cache, hypothetical, guard, seed++, u.name + " (hypothetical)");
    }
    // In melee the boltgun champion fights with his chainsword (A4 AP-1), the others with close combat weapons (A3).
    const Volley melee = unit_melee_volley(cache, gs.units[kBoltguns], guard);
    ASSERT_EQ(melee.size(), 2u);
    EXPECT_EQ(melee[0].attack_dice + melee[1].attack_dice, 4 + 9 * 3);
    expect_matches_samples(cache, melee, guard, seed++, "boltgun Legionaries in melee");
}

// ---- 2. [EXTRA ATTACKS] in every melee estimate ----

TEST(M68_ExtraAttacks, EveryMeleeEstimateCountsTheTyrantsTalons) {
    // 24.11 (D-071): the Hive Tyrant fights with its bonesword and its scything talons. Every Normal melee estimate goes
    // through unit_melee_volley or the engine's melee_options (plan_melee): the charge planner's and move score's
    // melee_share, the casualty worth (model_worth), the threat roles. Each counts the talons.
    const GameState gs = wh40k::test::load_rosters_scenario();
    GameState bare = gs;
    Model& tyrant = bare.units[kTyrant].models.front();
    std::erase_if(tyrant.weapons, [](const Weapon& w) { return is_extra_attacks_weapon(w); });
    ASSERT_EQ(tyrant.weapons.size(), gs.units[kTyrant].models.front().weapons.size() - 1);
    OddsCache cache;
    const Unit& target = gs.units[kChainswords];
    const Volley v = unit_melee_volley(cache, gs.units[kTyrant], target);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].attack_dice + v[1].attack_dice, 6 + 4);
    EXPECT_GT(normal::melee_share(cache, gs, gs.units[kTyrant], target),
              normal::melee_share(cache, bare, bare.units[kTyrant], target));
    EXPECT_DOUBLE_EQ(normal::melee_share(cache, gs, gs.units[kTyrant], target),
                     normal::unit_share(gs, target, expected_outcome(cache, v, snapshot_of(target)).model_equivalents));
    const auto worth = normal::model_worth(cache, gs.units[kTyrant], target);
    const auto bare_worth = normal::model_worth(cache, bare.units[kTyrant], target);
    EXPECT_GT(worth.at(tyrant.id), bare_worth.at(tyrant.id));
    EXPECT_GT(unit_role(cache, gs.units[kTyrant], target).melee, unit_role(cache, bare.units[kTyrant], target).melee);
}

// ---- 3. [HAZARDOUS] ----

TEST(M68_Hazard, TheCostMatchesTwentyThousandEngineHazardRolls) {
    // 06.03 and 06.02 through core: the Havocs (W3, one or two rolls), the Chaos Lord (a CHARACTER, W5) and the Hive
    // Tyrant (a MONSTER: 3 mortal wounds a failed roll), against make_hazard_rolls with seeded dice and the engine's
    // inflict_mortal_wounds (the first candidate each time).
    const GameState gs = wh40k::test::load_rosters_scenario();
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const auto left = [](const Unit& u) {
        double total = 0.0;
        for (const Model& m : u.models) total += static_cast<double>(m.wounds_remaining) / m.characteristics.wounds;
        return total;
    };
    struct Case {
        std::size_t unit;
        int rolls;
        double exact;  // by hand: P(1-2) = 1/3, the mortal wounds over W
    };
    for (const Case c : {Case{kHavocs, 1, 1.0 / 9.0}, Case{kHavocs, 2, 2.0 / 9.0}, Case{kLord, 1, 1.0 / 15.0},
                         Case{kTyrant, 1, 1.0 / 10.0}}) {
        const Unit& u = gs.units[c.unit];
        const double expected = hazard_model_equivalents(u, c.rolls);
        EXPECT_NEAR(expected, c.exact, 1e-12) << u.name;
        DiceState state{6800 + c.unit, 0};
        SeededDice dice(state);
        double sum = 0.0;
        constexpr int kSamples = 20000;
        for (int i = 0; i < kSamples; ++i) {
            Unit copy = u;
            const HazardRolls h = make_hazard_rolls(dice, copy, c.rolls);
            const double before = left(copy);
            (void)inflict_mortal_wounds(copy, h.mortal_wounds, first);
            sum += before - left(copy);
        }
        EXPECT_NEAR(sum / kSamples, expected, 0.02 * expected) << u.name << " " << c.rolls;
    }
    EXPECT_EQ(hazard_model_equivalents(gs.units[kHavocs], 0), 0.0);
}

namespace {

// The roster scenario at Side A's Shoot decision with the Havocs' lascannons put away (only the champion's plasma gun
// shoots) and the Tyrant Guard `gap_in` inches north of them (nothing between), every other enemy out of 24".
GameState plasma_at_guard(float gap_in) {
    GameState gs = wh40k::test::load_rosters_scenario();
    for (Model& m : gs.units[kHavocs].models) {
        if (m.type_name != "Havoc Champion") {
            std::erase_if(m.weapons, [](const Weapon& w) { return w.name == "Havoc lascannon"; });
        }
    }
    const Unit& havocs = gs.units[kHavocs];  // one rank, held back 3" in its zone
    float x = 0.0f;
    for (const Model& m : havocs.models) x += m.position.x;
    x /= static_cast<float>(havocs.models.size());
    rank_at(gs.units[kGuard], mm_to_inches(x),
            mm_to_inches(havocs.models.front().position.y) + gap_in + radius_in(havocs) + radius_in(gs.units[kGuard]));
    start_game(gs);
    advance_to<decision::Shoot>(gs, Side::A);
    return gs;
}

// The plasma gun's declaration with profile `profile` (0 standard, 1 supercharge) at the Tyrant Guard.
std::vector<ShotDeclaration> plasma(const GameState& gs, std::size_t profile) {
    for (const ShotDeclaration& o : shot_options(gs, gs.units[kHavocs], ShootingType::Normal)) {
        if (o.target == gs.units[kGuard].id && o.profile_index == profile) return {o};
    }
    return {};
}

// A declaration's value as Normal counts it, with or without the hazard.
double value_of(OddsCache& cache, const GameState& gs, const std::vector<ShotDeclaration>& shots, bool hazard) {
    const auto batches = shooting_batches(gs, gs.units[kHavocs].id, ShootingType::Normal, shots);
    double v = normal::share_value(gs, normal::evaluate_in_best_order(cache, gs, batches));
    if (hazard) {
        const int rolls = hazardous_weapons_selected(gs, make_weapon_selections(gs, gs.units[kHavocs], ShootingType::Normal, shots));
        v -= normal::unit_share(gs, gs.units[kHavocs], hazard_model_equivalents(gs.units[kHavocs], rolls));
    }
    return v;
}

}  // namespace

TEST(M68_Hazard, NormalSuperchargesOnlyWhenItIsWorthIt) {
    // 18" away one plasma shot on supercharge does more damage to the Tyrant Guard (S8 AP-3 D2 against T8 Sv3+ W4) but
    // not enough to pay for the hazard roll (1 in 3 to lose a wound off a 135-point W3 Havoc): standard. Within half
    // range [RAPID FIRE 1] doubles the gain: supercharge.
    OddsCache cache;
    for (const float gap : {18.0f, 8.0f}) {
        const GameState gs = plasma_at_guard(gap);
        const auto standard = plasma(gs, 0);
        const auto supercharge = plasma(gs, 1);
        ASSERT_EQ(standard.size(), 1u);
        ASSERT_EQ(supercharge.size(), 1u);
        EXPECT_GT(value_of(cache, gs, supercharge, false), value_of(cache, gs, standard, false)) << gap;
        const bool worth_it = value_of(cache, gs, supercharge, true) > value_of(cache, gs, standard, true);
        EXPECT_EQ(worth_it, gap < 12.0f) << gap;
        const std::optional<normal::ShootingChoice> c =
            normal::plan_shooting(cache, gs, gs.units[kHavocs].id, ShootingType::Normal);
        ASSERT_TRUE(c.has_value());
        ASSERT_EQ(c->shots.size(), 1u);
        EXPECT_EQ(c->shots.front().profile_index, worth_it ? 1u : 0u) << gap;
        EXPECT_NEAR(c->value, value_of(cache, gs, worth_it ? supercharge : standard, true), 1e-12);
        // Easy keeps the M60 default plan (D-071), never a [HAZARDOUS] profile.
        Bot easy(level(BotLevel::Easy));
        const BotChoice e = easy.decide(gs, Side::A);
        const auto* shoot = std::get_if<action::Shoot>(&e.action);
        ASSERT_NE(shoot, nullptr);
        for (const ShotDeclaration& d : shoot->shots) {
            const Model* m = find_model(gs, d.model);
            EXPECT_FALSE(m->weapons[d.weapon_index].profiles[d.profile_index].has<ability::Hazardous>());
        }
    }
}

// ---- 4. The -1s in hypothetical volleys ----

TEST(M68_Modifiers, HypotheticalVolleysApplyTenOhSixAndSeventeenOhThreeAsTheEngine) {
    // The monster fixture, engaged: the Hive Tyrant engaged with the chainsword Legionaries. The Havocs' lascannons at the
    // engaged Tyrant (17.03: -1), the Legionaries' bolt pistols at it in close-quarters shooting (the exception: 0),
    // the Havocs as a stand-in VEHICLE engaged with it (10.06 and 17.03: -2, capped to -1 by core's make_hit_rolls),
    // as the engine's selections make them; [BLAST] refused at the engaged MONSTER and an engaged infantry unit not a
    // target at all (04.02).
    const GameState gs = mf::at_side_a_shooting();
    const Unit& havocs = gs.units[mf::kHavocs];
    const Unit& tyrant = gs.units[mf::kTyrant];
    OddsCache cache;
    ShotContext at_engaged;
    at_engaged.heavy = true;
    at_engaged.gap_mm = *unit_distance(havocs, tyrant);
    at_engaged.target_engaged = true;
    const Volley hypothetical = unit_ranged_volley(cache, havocs, tyrant, at_engaged);
    std::vector<ShotDeclaration> lascannons;
    for (std::size_t i = 1; i < havocs.models.size(); ++i) lascannons.push_back(ShotDeclaration{havocs.models[i].id, 0, 0, tyrant.id});
    const Volley engine = engine_volley(gs, havocs.id, ShootingType::Normal, lascannons);
    ASSERT_EQ(engine.size(), 1u);
    EXPECT_EQ(engine[0].profile.hit_modifier, -1);
    const auto lascannon = std::find_if(hypothetical.begin(), hypothetical.end(),
                                        [&](const VolleyBatch& b) { return b.profile == engine[0].profile; });
    ASSERT_NE(lascannon, hypothetical.end());
    EXPECT_EQ(lascannon->attack_dice, 4);
    // Without the fact (the M67 context) the same shot is at 0.
    ShotContext unengaged = at_engaged;
    unengaged.target_engaged = false;
    for (const VolleyBatch& b : unit_ranged_volley(cache, havocs, tyrant, unengaged)) EXPECT_EQ(b.profile.hit_modifier, 0);

    // Close quarters: the engaged Legionaries' pistols at the Tyrant they are engaged with: no -1.
    const Unit& legionaries = gs.units[mf::kLegionaries];
    ShotContext cq;
    cq.type = ShootingType::CloseQuarters;
    const Volley pistols = unit_ranged_volley(cache, legionaries, tyrant, cq);
    ASSERT_FALSE(pistols.empty());
    for (const VolleyBatch& b : pistols) EXPECT_EQ(b.profile.hit_modifier, 0);

    // [BLAST] at the engaged MONSTER: a frag missile is refused, the krak shot isn't (the engine's shot_options agree).
    GameState frag = gs;
    frag.units[mf::kHavocs].models[1].weapons = {datasheet_weapon("havocs.json", "Havoc missile launcher")};
    const Volley missiles = unit_ranged_volley(cache, frag.units[mf::kHavocs], frag.units[mf::kTyrant], at_engaged);
    bool krak = false;
    for (const VolleyBatch& b : missiles) {
        EXPECT_TRUE(b.random_attacks.empty());  // the frag profile's A D6 is not there
        krak = krak || !b.profile.damage.is_fixed();
    }
    EXPECT_TRUE(krak);
    for (const ShotDeclaration& o : shot_options(frag, frag.units[mf::kHavocs], ShootingType::Normal)) {
        if (o.target != frag.units[mf::kTyrant].id || o.model != frag.units[mf::kHavocs].models[1].id) continue;
        EXPECT_FALSE(frag.units[mf::kHavocs].models[1].weapons[o.weapon_index].profiles[o.profile_index].has<ability::Blast>());
    }
    // 04.02: the engaged Legionaries can't be the target of a unit not engaged with them; the engine agrees.
    ShotContext at_infantry;
    at_infantry.gap_mm = *unit_distance(gs.units[mf::kScreamer], legionaries);
    at_infantry.target_engaged = true;
    EXPECT_TRUE(unit_ranged_volley(cache, gs.units[mf::kScreamer], legionaries, at_infantry).empty());

    // 10.06 + 17.03 = -2, capped to -1 (D-011) by make_hit_rolls in the odds: M63's stand-in VEHICLE Havocs engaged
    // with the engaged Tyrant.
    GameState vehicle = mf::fixture(true, Side::A);
    vehicle.units[mf::kHavocs].keywords.push_back("Vehicle");
    mf::place_rank(vehicle.units[mf::kHavocs], 22.0f + mm_to_inches(30.0f) + 1.0f + 5.0f * mm_to_inches(20.0f),
                   mf::kRankY + mf::tyrant_centre_gap(1.0f));
    start_game(vehicle);
    advance_to<decision::Shoot>(vehicle, Side::A);
    const Volley cq_engine = engine_volley(vehicle, vehicle.units[mf::kHavocs].id, ShootingType::CloseQuarters, lascannons);
    ASSERT_EQ(cq_engine.size(), 1u);
    EXPECT_EQ(cq_engine[0].profile.hit_modifier, -2);
    const Volley cq_hypothetical = unit_ranged_volley(cache, vehicle.units[mf::kHavocs], vehicle.units[mf::kTyrant], cq);
    const auto cq_lascannon = std::find_if(cq_hypothetical.begin(), cq_hypothetical.end(),
                                           [&](const VolleyBatch& b) { return b.profile == cq_engine[0].profile; });
    ASSERT_NE(cq_lascannon, cq_hypothetical.end());
    VolleyBatch capped = *cq_lascannon;
    capped.profile.hit_modifier = -1;
    const TargetSnapshot snap = snapshot_of(vehicle.units[mf::kTyrant]);
    EXPECT_DOUBLE_EQ(expected_outcome(cache, Volley{*cq_lascannon}, snap).damage, expected_outcome(cache, Volley{capped}, snap).damage);

    // A MONSTER in close-quarters shooting at the unit it is engaged with can't use [BLAST] (10.06): the
    // Screamer-killer's scream; at a unit it isn't engaged with (`elsewhere`) it can, at -1.
    GameState scream = mf::fixture(true, Side::B);
    mf::place_rank(scream.units[mf::kScreamer], 30.0f, mf::kRankY + 1.0f + mm_to_inches(16.0f + 45.0f));
    const Unit& sk = scream.units[mf::kScreamer];
    EXPECT_TRUE(unit_ranged_volley(cache, sk, scream.units[mf::kLegionaries], cq).empty());
    ShotContext elsewhere = cq;
    elsewhere.elsewhere = true;
    elsewhere.gap_mm = *unit_distance(sk, scream.units[mf::kHavocs]);
    const Volley at_havocs = unit_ranged_volley(cache, sk, scream.units[mf::kHavocs], elsewhere);
    ASSERT_EQ(at_havocs.size(), 1u);
    EXPECT_EQ(at_havocs[0].profile.hit_modifier, -1);  // 10.06 MONSTER/VEHICLE models
    EXPECT_FALSE(at_havocs[0].random_attacks.empty());  // the scream, A D6+3
}

// ---- 5. The charge planner and engaged monsters ----

TEST(M68_ChargePlanner, AnEngagedMonsterStillShootsOtherUnits) {
    // 10.06 as M63 implemented it: an engaged MONSTER/VEHICLE unit shoots in close-quarters shooting, its MONSTER models
    // at units it isn't engaged with too (at -1). The Havocs' closest enemy is the Hive Tyrant, engaged with the
    // Legionaries, with a heavy venom cannon: holding is worth less for them with the cannon than without it. An
    // engaged unit that isn't a MONSTER (the Tyrant without the keyword) can't shoot them: then the cannon changes
    // nothing (M67's assumption, right for it).
    GameState gs = mf::fixture(true, Side::A);
    gs.units[mf::kTyrant].models.front().weapons.push_back(datasheet_weapon("hive_tyrant.json", "Heavy venom cannon"));
    start_game(gs);
    advance_to<decision::Charge>(gs, Side::A);
    const UnitId havocs = gs.units[mf::kHavocs].id;
    ASSERT_EQ(normal::closest_enemy(gs, gs.units[mf::kHavocs])->id, gs.units[mf::kTyrant].id);
    ASSERT_TRUE(is_engaged(gs, gs.units[mf::kTyrant]));
    GameState no_cannon = gs;
    no_cannon.units[mf::kTyrant].models.front().weapons.pop_back();
    OddsCache cache;
    const double with = normal::hold_value(cache, gs, havocs);
    const double without = normal::hold_value(cache, no_cannon, havocs);
    EXPECT_LT(with, without - 1e-6);
    // The cannon at -1 (10.06): what the Havocs expect to lose to it, as a share of them.
    const std::optional<ShotContext> shot =
        normal::next_turn_shot(gs, gs.units[mf::kTyrant], gs.units[mf::kHavocs], *unit_distance(gs.units[mf::kHavocs], gs.units[mf::kTyrant]));
    ASSERT_TRUE(shot.has_value());
    EXPECT_EQ(shot->type, ShootingType::CloseQuarters);
    EXPECT_TRUE(shot->elsewhere);
    EXPECT_GT(normal::ranged_share(cache, gs, gs.units[mf::kTyrant], gs.units[mf::kHavocs], *shot), 0.0);
    // Not a MONSTER: no shooting at the Havocs while engaged (10.06), with or without the cannon.
    for (GameState* s : {&gs, &no_cannon}) {
        std::erase_if(s->units[mf::kTyrant].keywords, [](const std::string& k) { return k == "Monster"; });
    }
    EXPECT_FALSE(normal::next_turn_shot(gs, gs.units[mf::kTyrant], gs.units[mf::kHavocs], 0.0f).has_value());
    EXPECT_DOUBLE_EQ(normal::hold_value(cache, gs, havocs), normal::hold_value(cache, no_cannon, havocs));
}

// ---- 6. A MONSTER walks through infantry ----

TEST(M68_Movement, AMonstersNormalMoveThroughInfantryIsPlannedAndTheEngineAcceptsIt) {
    // 17.01 (D-073): the Hive Tyrant, 2.04" north of the Legionaries' rank, may move 8" straight south through it. The
    // planner's candidates now include moves whose straight line crosses the rank (enemy models that aren't MONSTERS),
    // ending beyond it out of Engagement Range; each is accepted by the engine. The Tyrant Guard (INFANTRY) gets none.
    const GameState gs = mf::at_movement(false, Side::B);
    const Unit& tyrant = gs.units[mf::kTyrant];
    const Unit& rank = gs.units[mf::kLegionaries];
    Simulator sim(gs, 68);
    ASSERT_TRUE(sim.step(Side::B, action::StartMove{tyrant.id, MoveType::Normal, std::nullopt}).ok());
    const GameState origin = sim.state();
    const Position start = tyrant.models.front().position;
    const float r = base_radius(tyrant.models.front().base);
    const auto crosses = [&](const Position& end) {
        for (const Model& m : rank.models) {
            if (wh40k::client::point_segment_distance(m.position, start, end) < r + base_radius(m.base)) return true;
        }
        return false;
    };
    int through = 0;
    for (const normal::MoveCandidate& c : normal::move_candidates(origin)) {
        ASSERT_EQ(c.ends.size(), 1u);
        if (!crosses(c.ends.front())) continue;
        ++through;
        EXPECT_LT(c.ends.front().y, in(mf::kRankY)) << c.label;  // it can't end on them: beyond the rank
        Simulator move(origin, 68);
        for (const action::ProposeModelMove& p : c.proposals) ASSERT_TRUE(move.step(Side::B, p).ok()) << c.label;
        EXPECT_TRUE(move_end_problems(move.state()).empty()) << c.label;
        std::vector<GameEvent> events;
        ASSERT_TRUE(move.step(Side::B, action::CommitUnitMove{}, &events).ok()) << c.label;
        for (const GameEvent& e : events) EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e)) << c.label;
    }
    EXPECT_GT(through, 0);
    // The Tyrant Guard (not a MONSTER) walks round: no candidate of its crosses an enemy model.
    Simulator guard_sim(gs, 68);
    ASSERT_TRUE(guard_sim.step(Side::B, action::StartMove{gs.units[mf::kGuard].id, MoveType::Normal, std::nullopt}).ok());
    const std::vector<PosedModel> enemies = posed_enemy_models(guard_sim.state(), Side::B);
    for (const normal::MoveCandidate& c : normal::move_candidates(guard_sim.state())) {
        for (std::size_t i = 0; i < c.ends.size(); ++i) {
            const Model& m = gs.units[mf::kGuard].models[i];
            for (const PosedModel& e : enemies) {
                EXPECT_GE(wh40k::client::point_segment_distance(e.position, m.position, c.ends[i]),
                          base_radius(m.base) + base_radius(e.base) - 0.01f);
            }
        }
    }
}

TEST(M68_Movement, ADesperateEscapeIsMadeOnlyWhenTheLikelyLossesLeaveAMoveThatStands) {
    // Normal vs Normal, dice seed 193 (Release arena, 200 games): in battle round 5 the battle-shocked Chaos Terminators
    // (Desperate Escape their only fall-back mode, 09.07) are engaged with the Hive Tyrant standing among them against the
    // south edge, a Screamer-killer 2.7" north. Planned with no failed hazard roll (D-054) a fall-back move stands; after
    // the likely losses none does, and M68's first build had to reset the move (a UnitMoveReverted). Now Normal, like
    // Hard (D-067's escape_stands), keeps them in the fight (D-077).
    const GameState scenario = wh40k::test::load_rosters_scenario();
    wh40k::client::GameSession session = wh40k::client::GameSession::new_game(scenario, 193);
    std::array<Bot, 2> bots{Bot(level(BotLevel::Normal)), Bot(level(BotLevel::Normal))};
    wh40k::client::AutoSettings autos;  // as the arena plays a bot game
    autos.skip_empty_steps = true;
    autos.auto_allocate = false;
    autos.bot_controlled = {true, true};
    const UnitId terminators{3};
    const GameAction escape = action::StartMove{terminators, MoveType::FallBack, FallBackMode::DesperateEscape};
    bool reached = false;
    for (int i = 0; i < 4000 && session.pending(); ++i) {
        (void)wh40k::client::run_auto_actions(session, autos);
        if (!session.pending()) break;
        const GameState& gs = session.state();
        const Side side = gs.pending->side;
        const std::vector<GameAction> legal = legal_actions(gs, side);
        if (gs.flow.battle_round == 5 && std::find(legal.begin(), legal.end(), escape) != legal.end()) {
            reached = true;
            // Without failed rolls a fall-back move stands ...
            Simulator sixes(gs, 0);
            ScriptedDice six(std::vector<int>(gs.find_unit(terminators)->models.size(), 6));
            ASSERT_TRUE(sixes.step(side, escape, six).ok());
            OddsCache cache;
            EXPECT_FALSE(normal::standing_moves(cache, sixes.state(), normal::MoveWeights{}, 1, normal::kMaxSimulatedMoves).empty());
            // ... and Normal doesn't make the escape.
            Bot bot(level(BotLevel::Normal));
            const BotChoice c = bot.decide(gs, side);
            EXPECT_NE(c.action, escape) << c.reason;
            EXPECT_TRUE(validate_action(gs, side, c.action).ok());
            break;
        }
        const BotChoice c = bots[side_index(side)].decide(gs, side);
        ASSERT_TRUE(session.submit(c.action).ok());
    }
    EXPECT_TRUE(reached);
}

// ---- 7. Material weighted by points ----

TEST(M68_Points, SharesAreWeightedByPointsOnlyForSidesOfSeveralUnitTypes) {
    // D-077: a unit's weight is its points over the mean points of every unit (1915 / 14 on the rosters), when every unit
    // has points and its side has more than one unit type; otherwise 1 (V1, the bot scenarios and Strike have no points).
    const GameState rosters = wh40k::test::load_rosters_scenario();
    double total = 0.0;
    for (const Unit& u : rosters.units) {
        EXPECT_GT(u.points, 0);
        total += u.points;
    }
    EXPECT_EQ(total, 1915.0);
    for (const Unit& u : rosters.units) {
        EXPECT_DOUBLE_EQ(normal::share_weight(rosters, u), u.points * 14.0 / 1915.0) << u.name;
    }
    const Unit& gaunts = rosters.units[kTermagants];
    EXPECT_DOUBLE_EQ(normal::unit_share(rosters, gaunts, 1.0), 0.1 * 55.0 * 14.0 / 1915.0);
    EXPECT_DOUBLE_EQ(normal::alive_share(rosters, gaunts), 55.0 * 14.0 / 1915.0);
    EXPECT_DOUBLE_EQ(normal::hazard_rate(rosters, rosters.units[kHavocs], gaunts), (135.0 / 5.0) / (55.0 / 10.0));
    for (const char* file : {"data/scenarios/v1_skirmish.json", "data/scenarios/strike_44x60.json",
                             "data/scenarios/bot_20_termagants.json"}) {
        const GameState gs = load_scenario(repo_root() / file, repo_root());
        for (const Unit& u : gs.units) {
            EXPECT_EQ(normal::share_weight(gs, u), 1.0) << file;
            EXPECT_EQ(normal::unit_share(gs, u, 1.0), 1.0 / std::max<int>(u.starting_strength, static_cast<int>(u.models.size())));
        }
    }
    // A side of one unit type keeps weight 1 even with points: the rosters with the Tyranids reduced to their Termagants.
    GameState one_type = rosters;
    std::erase_if(one_type.units, [](const Unit& u) { return u.side == Side::B && u.name != "Termagants"; });
    for (const Unit& u : one_type.units) {
        if (u.side == Side::B) EXPECT_EQ(normal::share_weight(one_type, u), 1.0);
        if (u.side == Side::A) EXPECT_GT(std::fabs(normal::share_weight(one_type, u) - 1.0), 0.05) << u.name;
    }
    // Strike with points but one unit type a side: every weight 1.
    GameState strike = load_scenario(repo_root() / "data/scenarios/strike_44x60.json", repo_root());
    for (Unit& u : strike.units) u.points = u.side == Side::A ? 80 : 60;
    for (const Unit& u : strike.units) EXPECT_EQ(normal::share_weight(strike, u), 1.0);
    // A unit without points in a game where others have them: every weight 1.
    GameState partial = rosters;
    partial.units[kTermagants].points = 0;
    for (const Unit& u : partial.units) EXPECT_EQ(normal::share_weight(partial, u), 1.0);
}

// ---- 8. The objective knows the tiebreak ----

namespace {

// The Chaos Lord alone on Side A against the Hive Tyrant and a Termagant unit, at Side A's Move Units decision: the Lord
// loses the exchange with the Tyrant (lean < 0), so D-007 gives it a survival term.
GameState lord_alone() {
    GameState gs = wh40k::test::load_rosters_scenario();
    const UnitId gaunts = gs.units[kTermagants].id;  // the west Termagants
    std::erase_if(gs.units, [&](const Unit& u) {
        return u.side == Side::A ? u.name != "Chaos Lord" : (u.name != "Hive Tyrant" && u.id != gaunts);
    });
    // [0] the Lord, [1] the Termagants (far in the north-east corner), [2] the Hive Tyrant.
    gs.units[0].models.front().position = wh40k::test::at(22.0f, 20.0f);
    rank_at(gs.units[1], 34.0f, 58.0f);
    start_game(gs);
    advance_to<decision::MoveUnits>(gs, Side::A);
    return gs;
}

}  // namespace

TEST(M68_Tiebreak, ThePointsRuleReplacesTheDrawInTheMoveScore) {
    // D-076's tiebreak has no draw to wait for: no time preference, no D-007 survival term for a unit that isn't its
    // side's last, marginal exposure for every side. A side's last unit keeps a survival term (a wipe-out still loses at
    // once), worth what surviving buys on the engine's exact tally: twice D-007's when ahead, D-007's when level, none
    // when behind.
    const GameState rosters = wh40k::test::load_rosters_scenario();
    GameState rosters_d007 = rosters;
    rosters_d007.result_rule = ResultRule::D007Draw;
    OddsCache cache;
    for (const Unit& u : rosters.units) {
        if (u.side != Side::A) continue;
        const normal::MoveScoreContext points(cache, rosters, u.id);
        const normal::MoveScoreContext d007(cache, rosters_d007, u.id);
        EXPECT_TRUE(points.points_rule());
        EXPECT_FALSE(d007.points_rule());
        EXPECT_EQ(points.our_discount(), 1.0);
        EXPECT_EQ(points.their_discount(), 1.0);
        EXPECT_EQ(points.survival_weight(), 0.0);  // not the last unit
        EXPECT_FALSE(points.plays_for_draw());
        EXPECT_EQ(d007.lean(), points.lean());
        if (d007.lean() < 0.0) EXPECT_TRUE(d007.plays_for_draw());
    }
    GameState level_tally = lord_alone();
    const UnitId lord = level_tally.units[0].id;
    GameState d007 = level_tally;
    d007.result_rule = ResultRule::D007Draw;
    const normal::MoveScoreContext draw(cache, d007, lord);
    ASSERT_LT(draw.lean(), 0.0);
    ASSERT_GT(draw.survival_weight(), 0.0);
    EXPECT_LT(draw.our_discount(), 1.0);
    EXPECT_DOUBLE_EQ(normal::MoveScoreContext(cache, level_tally, lord).survival_weight(), draw.survival_weight());
    GameState ahead = level_tally;  // the Termagants destroyed: Side A ahead, 55 to 0
    ahead.units[1].models.clear();
    ASSERT_GT(points_destroyed(ahead).destroyed[0], points_destroyed(ahead).destroyed[1]);
    EXPECT_DOUBLE_EQ(normal::MoveScoreContext(cache, ahead, lord).survival_weight(), 2.0 * draw.survival_weight());
    GameState behind = level_tally;  // the Lord down to 1 wound: Side B ahead, 76 to 0
    behind.units[0].models.front().wounds_remaining = 1;
    ASSERT_LT(points_destroyed(behind).destroyed[0], points_destroyed(behind).destroyed[1]);
    EXPECT_EQ(normal::MoveScoreContext(cache, behind, lord).survival_weight(), 0.0);
}

// ---- 10. Legality on the new scenario ----

namespace {

void expect_clean(BotLevel a, BotLevel b, std::uint64_t seed) {
    const GameState scenario = wh40k::test::load_rosters_scenario();
    const GameRecord g = play_bot_game(scenario, seed, level(a), level(b), 5000);
    EXPECT_TRUE(g.finished) << "seed " << seed << (g.problems.empty() ? "" : ": " + g.problems.front());
    EXPECT_EQ(g.rejected, 0u) << "seed " << seed;
    EXPECT_EQ(g.reverted, 0u) << "seed " << seed;
    EXPECT_EQ(g.fallbacks, 0u) << "seed " << seed;
    EXPECT_TRUE(g.on_points || g.winner.has_value()) << "seed " << seed;  // D-076: decided either way
}

}  // namespace

TEST(M68_Games, EveryEasyAndNormalPairingPlaysTheRostersClean) {
    // Two games of each pairing (about 22 s in Debug); the arena plays 200 of each (PLAN.md, M68).
    for (const std::uint64_t seed : {1u, 5u}) {
        expect_clean(BotLevel::Easy, BotLevel::Easy, seed);
        expect_clean(BotLevel::Normal, BotLevel::Normal, seed + 1);
        expect_clean(BotLevel::Normal, BotLevel::Easy, seed + 2);
        expect_clean(BotLevel::Easy, BotLevel::Normal, seed + 3);
    }
}

// The bigger run (ten games of each pairing; about a minute in Release), run by hand:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M68_Games.DISABLED_*
TEST(M68_Games, DISABLED_TenGamesOfEveryPairingPlayTheRostersClean) {
    for (std::uint64_t seed = 101; seed <= 110; ++seed) {
        expect_clean(BotLevel::Easy, BotLevel::Easy, seed);
        expect_clean(BotLevel::Normal, BotLevel::Normal, seed);
        expect_clean(BotLevel::Normal, BotLevel::Easy, seed);
        expect_clean(BotLevel::Easy, BotLevel::Normal, seed);
    }
}

// ---- The bot-game replay ----

namespace {

// Dice seed 5 (Release arena, Normal vs Normal, seeds 1-8 traced on the roster scenario): a whole game decided on points
// destroyed after battle round 5 (D-076), with the Havoc champion's plasma gun on supercharge once (worth it, D-077) and
// on its standard profile otherwise, the lascannons at the Tyranid MONSTERS, charges and fights by both armies.
constexpr std::uint64_t kRostersSeed = 5;
constexpr const char* kRostersFile = "m68_rosters_normal_bot_game.json";

std::filesystem::path saved_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kRostersFile; }

struct BotGame {
    Replay replay;
    GameRecord record;
};

// Played through GameSession (every action validated, the client's automatic skips included, nothing auto-allocated
// for a bot), its history a scenario-form replay, as M45 and M58 do. Replaying it needs only core.
BotGame play_rosters_bot_game(std::uint64_t seed = kRostersSeed) {
    BotGame g;
    g.replay.description =
        "M68: a whole game played by the bot on both sides, level Normal vs Normal, from the 44\"x60\" portrait roster "
        "scenario (Chaos Space Marines against Tyranids) with dice seed " +
        std::to_string(seed) +
        " (D-077: points-weighted material, the points tiebreak in the objective, [HAZARDOUS] priced). Replaying it needs "
        "only the engine.";
    g.replay.initial.scenario = wh40k::test::kRostersScenario;
    g.replay.initial.seed = seed;
    wh40k::client::GameSession session(replay_initial_state(g.replay, repo_root()));
    g.record = play_bot_session(session, level(BotLevel::Normal), level(BotLevel::Normal), 5000);
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

}  // namespace

TEST(M68_Replay, TheRostersBotGameReplaysToItsResultWithCoreOnly) {
    const Replay saved = load_replay(saved_path());
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, wh40k::test::kRostersScenario);
    EXPECT_EQ(saved.initial.seed, kRostersSeed);
    const ReplayResult first = run_replay(saved, repo_root());
    ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons.front();
    EXPECT_EQ(replay_expectation(first.final_state), saved.expect);
    const ReplayResult second = run_replay(saved, repo_root());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.final_state, second.final_state);  // deterministic
    // Decided on points destroyed after the last battle round (D-076).
    ASSERT_TRUE(first.final_state.result.has_value());
    EXPECT_TRUE(first.final_state.result->points.has_value());
    EXPECT_EQ(first.final_state.flow.battle_round, 5);
    // The flow: a supercharged plasma shot, and both armies fighting in melee.
    const auto has = [&](auto pred) { return std::any_of(saved.steps.begin(), saved.steps.end(), pred); };
    GameState gs = replay_initial_state(saved, repo_root());
    EXPECT_TRUE(has([&](const ReplayStep& s) {
        const auto* shoot = std::get_if<action::Shoot>(&s.action);
        if (shoot == nullptr) return false;
        for (const ShotDeclaration& d : shoot->shots) {
            const Model* m = find_model(gs, d.model);
            if (m != nullptr && m->weapons[d.weapon_index].profiles[d.profile_index].has<ability::Hazardous>()) return true;
        }
        return false;
    }));
    for (const Side side : {Side::A, Side::B}) {
        EXPECT_TRUE(has([&](const ReplayStep& s) { return s.side == side && std::holds_alternative<action::Fight>(s.action); }));
    }
}

TEST(M68_Replay, TheSavedFileMatchesTheGame) {
    // Normal vs Normal with the replay's seed plays exactly the saved game now (D-054: the bots are deterministic), with
    // nothing rejected, reverted or answered by the fallback: every decision of both sides (a few seconds in Debug).
    const Replay saved = load_replay(saved_path());
    const BotGame g = play_rosters_bot_game();
    EXPECT_TRUE(g.record.finished);
    EXPECT_EQ(g.record.rejected, 0u);
    EXPECT_EQ(g.record.reverted, 0u);
    EXPECT_EQ(g.record.fallbacks, 0u);
    ASSERT_EQ(saved.steps.size(), g.replay.steps.size());
    for (std::size_t i = 0; i < saved.steps.size(); ++i) {
        ASSERT_EQ(Json(saved.steps[i]).dump(), Json(g.replay.steps[i]).dump()) << "step " << i;
    }
    EXPECT_EQ(Json(saved).dump(2), Json(g.replay).dump(2));
}

// Writes tests/replays/m68_rosters_normal_bot_game.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M68_Replay.DISABLED_RegenerateRostersNormalBotGameReplay
TEST(M68_Replay, DISABLED_RegenerateRostersNormalBotGameReplay) {
    const BotGame g = play_rosters_bot_game();
    ASSERT_TRUE(g.record.finished);  // a result, not the step cap
    ASSERT_EQ(g.record.rejected, 0u);
    ASSERT_EQ(g.record.reverted, 0u);
    ASSERT_EQ(g.record.fallbacks, 0u);
    const ReplayResult r = run_replay(g.replay, repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    ASSERT_TRUE(r.final_state.result.has_value());
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, saved_path());
}

