// M40: odds and threat analysis (D-054): per-die chances from core's roll functions, exact volley outcomes checked
// against sampled engine batches, charge and advance odds from the engine, threat ranges, roles and the matchup
// table per range band.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "analysis/charge_odds.hpp"
#include "analysis/odds.hpp"
#include "analysis/threat.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::in;

namespace {

const Unit& intercessors(const GameState& gs) { return wh40k::test::unit_of(gs, Side::A); }
const Unit& termagants(const GameState& gs) { return wh40k::test::unit_of(gs, Side::B); }

std::vector<Weapon> all_weapons() {
    std::vector<Weapon> out;
    for (const char* file : {"data/units/intercessor_squad.json", "data/units/termagants.json"}) {
        const Datasheet d = load_datasheet(wh40k::test::repo_root() / file);
        out.insert(out.end(), d.weapons.begin(), d.weapons.end());
    }
    return out;
}

const WeaponProfile& profile_named(const std::string& name) {
    static const std::vector<Weapon> weapons = all_weapons();
    for (const Weapon& w : weapons) {
        for (const WeaponProfile& p : w.profiles) {
            if (p.name == name) return p;
        }
    }
    throw std::logic_error("no profile " + name);
}

AttackProfile shooting_profile(const std::string& name, bool heavy) {
    AttackContext c;
    c.type = AttackType::Shooting;
    c.heavy_conditions_met = heavy;
    return make_attack_profile(profile_named(name), c);
}

// Plays a seeded Easy-vs-Easy game (as the M39 tests do) and returns the first state, at a bot's decision, that
// satisfies `pred`.
template <typename Pred>
std::optional<GameState> first_state(std::uint64_t seed, Pred pred) {
    client::GameSession s = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
    std::array<Bot, 2> bots{Bot(BotConfig{BotLevel::Easy}), Bot(BotConfig{BotLevel::Easy})};
    client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    while (s.pending() && s.history().size() < 5000) {
        (void)client::run_auto_actions(s, autos);
        if (!s.pending()) break;
        if (pred(s.state())) return s.state();
        const Side side = s.pending()->side;
        const BotChoice c = bots[side_index(side)].decide(s.state(), side);
        if (c.stopped || !s.submit(c.action).ok()) break;
    }
    return std::nullopt;
}

bool offers_declare_charge(const GameState& gs) {
    if (!std::holds_alternative<decision::Charge>(gs.pending->kind)) return false;
    const auto legal = legal_actions(gs, gs.pending->side);
    return std::any_of(legal.begin(), legal.end(),
                       [](const GameAction& a) { return std::holds_alternative<action::DeclareCharge>(a); });
}

// Translates `mover` along y (away from or towards `other`) until the units are `gap_mm` apart edge to edge.
void set_gap(GameState& gs, UnitId mover, UnitId other, float gap_mm) {
    Unit& m = *gs.find_unit(mover);
    const Unit& o = *gs.find_unit(other);
    const float sign = m.models.front().position.y > o.models.front().position.y ? 1.0f : -1.0f;
    for (int i = 0; i < 50; ++i) {
        const float d = *unit_distance(m, o);
        if (std::fabs(d - gap_mm) < 1e-4f) return;
        for (Model& model : m.models) model.position.y += sign * (gap_mm - d);
    }
}

}  // namespace

// ==== Per-die chances come out of the engine's own functions ====

TEST(M40_DieOdds, HitWoundAndSaveChancesComeFromTheEngineRollFunctions) {
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    // Bolt Rifle - Focused Fire (BS3+, S6, AP-1) against Termagants (T3, Sv5+).
    const HitWoundOdds& plain = cache.hit_wound(shooting_profile("Bolt Rifle - Focused Fire", false), 3);
    EXPECT_DOUBLE_EQ(plain.hit, 4.0 / 6.0);           // 3+ (05.01)
    EXPECT_DOUBLE_EQ(plain.critical_hit, 1.0 / 6.0);  // unmodified 6
    EXPECT_DOUBLE_EQ(plain.wound, 5.0 / 6.0);         // S6 vs T3: twice T -> 2+ (05.02)
    EXPECT_DOUBLE_EQ(plain.critical_wound, 1.0 / 6.0);
    const HitWoundOdds& heavy = cache.hit_wound(shooting_profile("Bolt Rifle - Focused Fire", true), 3);
    EXPECT_DOUBLE_EQ(heavy.hit, 5.0 / 6.0);  // [HEAVY] +1 (24.16): 2+
    // Fleshborer (BS4+, S5) against Intercessors (T5).
    const HitWoundOdds& flesh = cache.hit_wound(shooting_profile("Fleshborer", false), 5);
    EXPECT_DOUBLE_EQ(flesh.hit, 3.0 / 6.0);
    EXPECT_DOUBLE_EQ(flesh.wound, 3.0 / 6.0);  // S = T: 4+
    // Hand flamer: [TORRENT] always hits (24.37), no critical hits.
    const HitWoundOdds& flamer = cache.hit_wound(shooting_profile("Hand flamer", false), 3);
    EXPECT_DOUBLE_EQ(flamer.hit, 1.0);
    EXPECT_DOUBLE_EQ(flamer.critical_hit, 0.0);
    // Claws (S3) against T5: 5+; against T7 (S at most half T) 6+.
    AttackContext fight;
    fight.type = AttackType::Fighting;
    const AttackProfile claws = make_attack_profile(profile_named("Chitinous claws and teeth"), fight);
    EXPECT_DOUBLE_EQ(cache.hit_wound(claws, 5).wound, 2.0 / 6.0);
    EXPECT_DOUBLE_EQ(cache.hit_wound(claws, 7).wound, 1.0 / 6.0);
    // Saves (05.04): Sv5+ vs AP-1 fails on 1-5; Sv3+ vs AP0 fails on 1-2; an unmodified 1 always fails.
    const TargetSnapshot gaunts = snapshot_of(termagants(gs));
    const TargetSnapshot marines = snapshot_of(intercessors(gs));
    EXPECT_DOUBLE_EQ(cache.unsaved(gaunts.groups[0], -1), 5.0 / 6.0);
    EXPECT_DOUBLE_EQ(cache.unsaved(marines.groups[0], 0), 2.0 / 6.0);
    EXPECT_DOUBLE_EQ(cache.unsaved(marines.groups[0], -3), 5.0 / 6.0);
    // The same numbers as required_wound_roll's own table, face by face.
    for (int s = 1; s <= 12; ++s) {
        for (int t = 1; t <= 12; ++t) {
            AttackProfile p = claws;
            p.strength = s;
            const int need = required_wound_roll(s, t);
            const double expected = static_cast<double>(7 - std::max(need, 2)) / 6.0;
            EXPECT_DOUBLE_EQ(cache.hit_wound(p, t).wound, expected) << "S" << s << " T" << t;
        }
    }
}

TEST(M40_OddsCache, LookupsAreCachedAndDeterministicWhateverTheOrder) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const Unit& gaunts = termagants(gs);
    const Unit& marines = intercessors(gs);
    const ShotContext ctx{ShootingType::Normal, true, in(11)};
    OddsCache a;
    const AttackOutcome first = expected_outcome(a, unit_ranged_volley(a, marines, gaunts, ctx), snapshot_of(gaunts));
    const std::size_t computed = a.computed();
    EXPECT_GT(computed, 0u);
    const AttackOutcome again = expected_outcome(a, unit_ranged_volley(a, marines, gaunts, ctx), snapshot_of(gaunts));
    EXPECT_EQ(a.computed(), computed);  // nothing new was derived
    EXPECT_GT(a.lookups(), computed);
    // A cache warmed with other entries first gives bit-identical results.
    OddsCache b;
    (void)expected_outcome(b, unit_melee_volley(b, gaunts, marines), snapshot_of(marines));
    (void)expected_outcome(b, unit_ranged_volley(b, gaunts, marines, ShotContext{ShootingType::Normal, false, in(17)}),
                           snapshot_of(marines));
    const AttackOutcome other = expected_outcome(b, unit_ranged_volley(b, marines, gaunts, ctx), snapshot_of(gaunts));
    for (const AttackOutcome* o : {&again, &other}) {
        EXPECT_EQ(o->damage, first.damage);
        EXPECT_EQ(o->models_destroyed, first.models_destroyed);
        EXPECT_EQ(o->model_equivalents, first.model_equivalents);
        EXPECT_EQ(o->models_destroyed_pmf, first.models_destroyed_pmf);
    }
}

// ==== The exact outcome follows 05.04 ====

TEST(M40_ExpectedOutcome, WoundedModelsFirstExcessDamageLostAndThePmfSumsToOne) {
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    Unit marines = intercessors(gs);
    // A torrent profile (always hits, 24.37) that no save stops: each attack inflicts damage unless its wound
    // roll is an unmodified 1 (05.02), so N ~ Binomial(dice, 5/6) attacks inflict damage.
    AttackProfile sure = shooting_profile("Hand flamer", false);
    sure.strength = 20;
    sure.armour_penetration = -6;
    const double p = 5.0 / 6.0;
    // D1 x 3 against fresh W2 models: N wounds lost, floor(N / 2) models destroyed.
    sure.damage = DiceExpr::fixed(1);
    VolleyBatch b{sure, 3};
    AttackOutcome o = expected_outcome(cache, std::span<const VolleyBatch>(&b, 1), snapshot_of(marines));
    EXPECT_NEAR(o.damage, 3 * p, 1e-12);
    const double p_two_or_more = 3 * p * p * (1 - p) + p * p * p;
    EXPECT_NEAR(o.models_destroyed, p_two_or_more, 1e-12);
    EXPECT_NEAR(o.model_equivalents, 3 * p / 2, 1e-12);
    // D3 against W2: one wound of each D3 is lost with the model (excess damage, 05.04): N models, 2N wounds.
    sure.damage = DiceExpr::fixed(3);
    b = VolleyBatch{sure, 3};
    o = expected_outcome(cache, std::span<const VolleyBatch>(&b, 1), snapshot_of(marines));
    EXPECT_NEAR(o.damage, 6 * p, 1e-12);
    EXPECT_NEAR(o.models_destroyed, 3 * p, 1e-12);
    // A wounded model takes the damage first (05.04 Select Model): D1 x 1 destroys it with chance p, and it
    // counts as half a model because it had already lost half its wounds.
    marines.models[3].wounds_remaining = 1;
    sure.damage = DiceExpr::fixed(1);
    b = VolleyBatch{sure, 1};
    o = expected_outcome(cache, std::span<const VolleyBatch>(&b, 1), snapshot_of(marines));
    EXPECT_NEAR(o.models_destroyed, p, 1e-12);
    EXPECT_NEAR(o.model_equivalents, p / 2, 1e-12);
    // Attacks after the unit is destroyed are lost: 20 D3 attacks almost surely wipe the 10 wounds, no more.
    sure.damage = DiceExpr::fixed(3);
    b = VolleyBatch{sure, 20};
    o = expected_outcome(cache, std::span<const VolleyBatch>(&b, 1), snapshot_of(intercessors(gs)));
    EXPECT_LE(o.damage, 10.0);
    EXPECT_NEAR(o.damage, 10.0, 1e-8);
    EXPECT_NEAR(o.p_wipe(), 1.0, 1e-8);
    // A real profile's pmf sums to one.
    const AttackOutcome real = expected_outcome(
        cache, unit_ranged_volley(cache, intercessors(gs), termagants(gs), ShotContext{ShootingType::Normal, true, in(11)}),
        snapshot_of(termagants(gs)));
    double sum = 0.0;
    for (const double pk : real.models_destroyed_pmf) sum += pk;
    EXPECT_NEAR(sum, 1.0, 1e-12);
}

// ==== Acceptance: analytic expected damage within 2% of 20,000 sampled engine batches ====

namespace {

struct GridCase {
    std::string label;
    Volley volley;
    const Unit* target = nullptr;
};

// Every V1 weapon profile the engine accepts (D-026; M62: a random A or D is rolled, D-069), with and without [HEAVY]
// and within and outside [RAPID FIRE] half range, against each V1 target (and a wounded Intercessor squad). The attack
// dice come from selection_dice for one model (a random A at its mean), times enough models that the target loses about
// 60% of its wounds on average (so the means are not tiny and not capped).
std::vector<GridCase> grid(const std::vector<const Unit*>& targets, OddsCache& cache) {
    std::vector<GridCase> out;
    for (const Weapon& w : all_weapons()) {
        for (const WeaponProfile& p : w.profiles) {
            if (!profile_supported(p)) continue;
            const bool melee = p.kind == WeaponKind::Melee;
            const std::vector<bool> heavies = p.has<ability::Heavy>() ? std::vector<bool>{false, true}
                                                                      : std::vector<bool>{false};
            const std::vector<bool> halves = p.has<ability::RapidFire>() ? std::vector<bool>{false, true}
                                                                         : std::vector<bool>{false};
            for (const Unit* target : targets) {
                for (const bool heavy : heavies) {
                    for (const bool half : halves) {
                        AttackContext ctx;
                        ctx.type = melee ? AttackType::Fighting : AttackType::Shooting;
                        ctx.heavy_conditions_met = heavy;
                        WeaponSelection sel;
                        sel.target = target->id;
                        sel.target_within_half_range = half;
                        sel.target_models_at_selection = static_cast<int>(target->models.size());
                        const AttackProfile ap = make_attack_profile(p, ctx);
                        const SelectionDice dice = selection_dice(p, sel, ctx);  // D-069: a random A is not rolled here
                        const int fixed_per_model = dice.dice + dice.bonus;
                        const double per_model =
                            fixed_per_model + (dice.random ? pmf_mean(cache.value_pmf(*dice.random)) : 0.0);
                        const double damage =
                            ap.damage.is_fixed() ? ap.damage.fixed_value() : pmf_mean(cache.value_pmf(ap.damage));
                        const TargetSnapshot snap = snapshot_of(*target);
                        const HitWoundOdds& hw = cache.hit_wound(ap, snap.toughness);
                        const double q = hw.hit * hw.wound * cache.unsaved(snap.groups[0], ap.armour_penetration);
                        const double per_model_unsaved = per_model * q;
                        const int models = std::clamp(
                            static_cast<int>(std::lround(0.6 * snap.wounds_left() /
                                                         (per_model_unsaved * std::min(damage,
                                                                                       static_cast<double>(snap.slots[0].wounds_char))))),
                            1, 20);
                        GridCase c;
                        const bool wounded = std::any_of(target->models.begin(), target->models.end(), [](const Model& m) {
                            return m.wounds_remaining < m.characteristics.wounds;
                        });
                        c.label = p.name + (heavy ? " +heavy" : "") + (half ? " half-range" : "") + " vs " +
                                  target->name + (wounded ? " (wounded)" : "") + " x" + std::to_string(models);
                        VolleyBatch vb{ap, fixed_per_model * models, {}};
                        if (dice.random) vb.random_attacks.assign(static_cast<std::size_t>(models), *dice.random);
                        c.volley = Volley{vb};
                        c.target = target;
                        out.push_back(std::move(c));
                    }
                }
            }
        }
    }
    return out;
}

void check_against_samples(const std::vector<GridCase>& cases, OddsCache& cache, int samples) {
    for (const GridCase& c : cases) {
        const AttackOutcome exact = expected_outcome(cache, c.volley, snapshot_of(*c.target));
        const SampledOutcome s = sample_outcome(c.volley, *c.target, 4040, samples);
        EXPECT_NEAR(s.damage, exact.damage, 0.02 * exact.damage) << c.label;
        EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * exact.models_destroyed) << c.label;
        for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
            EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << c.label << " k=" << k;
        }
    }
}

}  // namespace

// The full grid: 18 accepted profiles (the Thunder Hammer and Strangleweb are rejected; the spinefist's [TWIN-LINKED] is
// resolved since M61, D-072, which adds its variant, and the Shardlauncher's random A since M62, D-069, which adds its
// two), 25 profile/context variants, 3 targets = 75 cases
// x 20,000 engine batches. About 6 minutes in Debug, so ctest runs
// the representative subset below and this runs on purpose (Release: a few seconds):
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M40_ExpectedDamage.DISABLED_*
// It passed in full in Debug and Release when M40 landed (recorded in D-054, M40 notes).
TEST(M40_ExpectedDamage, DISABLED_FullGridEveryV1ProfileContextAndTarget) {
    const GameState gs = wh40k::test::load_v1_scenario();
    Unit wounded = intercessors(gs);
    wounded.models[2].wounds_remaining = 1;
    OddsCache cache;
    const auto cases = grid({&intercessors(gs), &termagants(gs), &wounded}, cache);
    EXPECT_EQ(cases.size(), 3u * 25u);  // M61: + the [TWIN-LINKED] spinefist; M62: + the random-A Shardlauncher
    check_against_samples(cases, cache, 20000);
}

// The ctest subset of the grid, still 20,000 engine batches each: [HEAVY] with and without, in and out of
// [RAPID FIRE] half range, [BLAST], [TORRENT], D2 against W2, a wounded target, ranged and melee, both armies.
TEST(M40_ExpectedDamage, AnalyticIsWithinTwoPercentOfTwentyThousandEngineBatchesForRepresentativeProfiles) {
    const GameState gs = wh40k::test::load_v1_scenario();
    Unit wounded = intercessors(gs);
    wounded.models[2].wounds_remaining = 1;
    OddsCache cache;
    const auto all = grid({&intercessors(gs), &termagants(gs), &wounded}, cache);
    ASSERT_EQ(all.size(), 3u * 25u);  // M61: + the [TWIN-LINKED] spinefist (D-072); M62: + the Shardlauncher (D-069)
    const std::vector<std::string> picks{
        "Bolt Rifle - Focused Fire +heavy half-range vs Termagants",
        "Bolt Rifle - Focused Fire vs Intercessor Squad (wounded)",
        "Bolt Rifle - Saturation half-range vs Intercessor Squad x",
        "Fleshborer vs Intercessor Squad x",
        "Power fist vs Intercessor Squad (wounded)",
        "Grenade Launcher - Frag vs Termagants",
        "Hand flamer vs Termagants",
        "Spike rifle +heavy vs Intercessor Squad x",
        "Termagant spinefist vs Intercessor Squad x",  // M61: [TWIN-LINKED] (24.38), every failed wound roll re-rolled
        "Shardlauncher +heavy vs Termagants",          // M62: a random A (D3, D-069) with [BLAST] added after
    };
    std::vector<GridCase> subset;
    for (const GridCase& c : all) {
        for (const std::string& pick : picks) {
            if (c.label.rfind(pick, 0) == 0) subset.push_back(c);
        }
    }
    ASSERT_EQ(subset.size(), picks.size());
    check_against_samples(subset, cache, 20000);
}

TEST(M40_ExpectedDamage, RejectedProfilesAreExcluded) {
    EXPECT_FALSE(profile_supported(profile_named("Thunder Hammer")));        // [DEVASTATING WOUNDS], D-026
    EXPECT_TRUE(profile_supported(profile_named("Termagant spinefist")));    // [TWIN-LINKED]: resolved since M61 (D-072)
    EXPECT_TRUE(profile_supported(profile_named("Shardlauncher")));          // A D3: rolled since M62 (D-069)
    EXPECT_FALSE(profile_supported(profile_named("Strangleweb")));           // [DEVASTATING WOUNDS], D-026 (A D6 is rolled)
    EXPECT_TRUE(profile_supported(profile_named("Plasma pistol - supercharge")));  // [HAZARDOUS] is implemented
}

TEST(M40_ExpectedDamage, MixedVolleysMatchTheEngineToo) {
    // Two batches in sequence (D2 then D1) against W2 models, and a Blast weapon against ten models (+2 dice).
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    const Volley mixed{VolleyBatch{shooting_profile("Bolt Rifle - Focused Fire", true), 6},
                       VolleyBatch{shooting_profile("Bolt pistol", false), 5}};
    const AttackOutcome exact = expected_outcome(cache, mixed, snapshot_of(intercessors(gs)));
    const SampledOutcome s = sample_outcome(mixed, intercessors(gs), 99, 20000);
    EXPECT_NEAR(s.damage, exact.damage, 0.02 * exact.damage);
    EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * exact.models_destroyed);
    // [BLAST 1] against 10 models: A4 + 2 (24.05) through the hypothetical volley builder.
    Unit grenadier = intercessors(gs);
    grenadier.models.resize(1);
    grenadier.models[0].weapons = {Weapon{"Grenade Launcher", WeaponKind::Ranged, {profile_named("Grenade Launcher - Frag")}}};
    const Volley frag = unit_ranged_volley(cache, grenadier, termagants(gs), ShotContext{ShootingType::Normal, false, in(20)});
    ASSERT_EQ(frag.size(), 1u);
    EXPECT_EQ(frag[0].attack_dice, 6);
}

// ==== Acceptance: charge odds equal a brute force over all 36 dice pairs ====

TEST(M40_ChargeOdds, ExactOddsEqualABruteForceOverAllThirtySixPairs) {
    const std::optional<GameState> found = first_state(1, offers_declare_charge);
    ASSERT_TRUE(found.has_value());
    const Side side = found->pending->side;
    const UnitId charger = wh40k::test::unit_of(*found, side).id;
    const UnitId target = wh40k::test::unit_of(*found, opponent(side)).id;
    const std::vector<UnitId> targets{target};
    struct Row {
        float gap_in;
        double expected;  // the rulebook's numbers: P(2D6 >= gap), 0 beyond 12"
    };
    for (const Row row : {Row{3.0f, 35.0 / 36}, Row{6.0f, 26.0 / 36}, Row{9.0f, 10.0 / 36}, Row{11.5f, 1.0 / 36},
                          Row{13.0f, 0.0}}) {
        GameState gs = *found;
        set_gap(gs, target, charger, in(row.gap_in));
        int successes = 0;
        for (int a = 1; a <= 6; ++a) {
            for (int b = 1; b <= 6; ++b) {
                Simulator sim(gs, 0);
                ScriptedDice dice{a, b};
                if (!sim.step(side, action::DeclareCharge{charger}, dice).ok()) continue;
                for (const GameAction& act : legal_actions(sim.state(), side)) {
                    const auto* s = std::get_if<action::StartChargeMove>(&act);
                    if (s != nullptr && s->targets == targets) ++successes;
                }
            }
        }
        const double brute = successes / 36.0;
        const auto odds = charge_odds(gs, charger, targets);
        if (row.gap_in > kChargeRangeInches) {
            EXPECT_FALSE(odds.has_value()) << row.gap_in;  // 11.02: not within 12", no charge to declare
        } else {
            ASSERT_TRUE(odds.has_value()) << row.gap_in;
            EXPECT_NEAR(odds->p_success, brute, 1e-12) << row.gap_in;
        }
        EXPECT_NEAR(brute, row.expected, 1e-12) << row.gap_in;
        EXPECT_NEAR(p_charge_roll_covers(in(row.gap_in)), brute, 1e-12) << row.gap_in;  // the hypothetical bound
    }
}

TEST(M40_ChargeOdds, DiceTablesAndThePlacementSeam) {
    EXPECT_NEAR(two_d6_pmf()[7], 6.0 / 36, 1e-15);
    EXPECT_NEAR(two_d6_pmf()[2], 1.0 / 36, 1e-15);
    EXPECT_EQ(two_d6_pmf()[1], 0.0);
    EXPECT_NEAR(p_2d6_at_least(2), 1.0, 1e-12);
    EXPECT_NEAR(p_2d6_at_least(10), 6.0 / 36, 1e-12);
    EXPECT_EQ(p_2d6_at_least(13), 0.0);
    for (int f = 1; f <= 6; ++f) EXPECT_NEAR(d6_pmf()[static_cast<std::size_t>(f)], 1.0 / 6, 1e-15);
    // The M42 hook: a placement check that only accepts rolls of 8 or more cuts the odds to P(2D6 >= 8).
    const std::optional<GameState> found = first_state(1, offers_declare_charge);
    ASSERT_TRUE(found.has_value());
    const Side side = found->pending->side;
    GameState gs = *found;
    const UnitId charger = wh40k::test::unit_of(gs, side).id;
    const UnitId target = wh40k::test::unit_of(gs, opponent(side)).id;
    set_gap(gs, target, charger, in(4));
    const std::vector<UnitId> targets{target};
    const auto odds = charge_odds(gs, charger, targets, [&](const GameState& after, UnitId u, std::span<const UnitId>) {
        return *after.find_unit(u)->phase_flags.charge_roll >= 8;
    });
    ASSERT_TRUE(odds.has_value());
    EXPECT_NEAR(odds->p_success, p_2d6_at_least(8), 1e-12);
    EXPECT_FALSE(odds->success_by_total[7]);
    EXPECT_TRUE(odds->success_by_total[8]);
}

TEST(M40_AdvanceOdds, EachAdvanceRollGivesTheEnginesMaximumDistance) {
    const std::optional<GameState> found = first_state(2, [](const GameState& gs) {
        return std::holds_alternative<decision::MoveUnits>(gs.pending->kind);
    });
    ASSERT_TRUE(found.has_value());
    const Unit& u = wh40k::test::unit_of(*found, found->pending->side);
    const auto odds = advance_odds(*found, u.id);
    ASSERT_TRUE(odds.has_value());
    const ThreatRanges t = threat_ranges(u);
    for (std::size_t f = 1; f <= 6; ++f) {
        EXPECT_NEAR(odds->max_move_mm[f], in(6.0f + static_cast<float>(f)), 1e-3) << f;  // 09.06: M + the roll
        EXPECT_NEAR(t.advance_mm[f], odds->max_move_mm[f], 1e-3) << f;                    // the strategic table agrees
    }
    // Not at a move decision: nothing to say.
    const std::optional<GameState> charge = first_state(1, offers_declare_charge);
    ASSERT_TRUE(charge.has_value());
    EXPECT_FALSE(advance_odds(*charge, u.id).has_value());
}

TEST(M40_Distances, TheKthClosestModelIsTheGapAfterTheFrontModelsAreRemoved) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const Unit& marines = intercessors(gs);
    const Unit& gaunts = termagants(gs);
    std::vector<float> all;
    for (int k = 1; k <= 10; ++k) {
        const auto d = kth_closest_model_distance(marines, gaunts, k);
        ASSERT_TRUE(d.has_value());
        all.push_back(*d);
    }
    EXPECT_TRUE(std::is_sorted(all.begin(), all.end()));
    EXPECT_NEAR(all[0], *unit_distance(marines, gaunts), 1e-4);
    EXPECT_GT(all[9], all[0] + in(1));  // the back rank is further away
    EXPECT_FALSE(kth_closest_model_distance(marines, gaunts, 11).has_value());
    EXPECT_EQ(distance_after_casualties(marines, gaunts, 4.7), kth_closest_model_distance(marines, gaunts, 5));
    EXPECT_EQ(distance_after_casualties(marines, gaunts, 0.0), kth_closest_model_distance(marines, gaunts, 1));
    EXPECT_FALSE(distance_after_casualties(marines, gaunts, 10.0).has_value());
}

// ==== Threat ranges and the turn table ====

TEST(M40_Threat, RangesComeFromTheUnitsCharacteristicsAndUsableWeapons) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const ThreatRanges m = threat_ranges(intercessors(gs));
    EXPECT_NEAR(m.move_mm, in(6), 1e-3);
    EXPECT_NEAR(m.max_range_mm, in(24), 1e-3);
    EXPECT_NEAR(m.max_assault_range_mm, in(24), 1e-3);  // Saturation is [ASSAULT]
    EXPECT_NEAR(m.max_close_quarters_range_mm, in(12), 1e-3);
    const ThreatRanges t = threat_ranges(termagants(gs));
    EXPECT_NEAR(t.max_range_mm, in(18), 1e-3);
    EXPECT_EQ(t.max_close_quarters_range_mm, 0.0f);
    EXPECT_NEAR(t.shooting_reach_mm(), in(24), 1e-3);
    // M + charge: within M + 2" a charge always reaches; at M + 12" only a 12; beyond, 12" is the limit (11.02).
    EXPECT_NEAR(t.p_charge_after_move(in(8)), 1.0, 1e-12);
    EXPECT_NEAR(t.p_charge_after_move(in(15)), p_2d6_at_least(9), 1e-12);
    EXPECT_NEAR(t.p_charge_after_move(in(18)), 1.0 / 36, 1e-12);
    EXPECT_EQ(t.p_charge_after_move(in(18.5f)), 0.0);
    // Advance then Fleshborer: reaches 6" + f + 18", so 27" needs a 3+.
    EXPECT_NEAR(t.p_advance_shoot(in(27)), 4.0 / 6, 1e-12);
    EXPECT_EQ(t.p_advance_shoot(in(31)), 0.0);
}

TEST(M40_Threat, TheStrategicTurnTableAgreesWithTheEngine) {
    const std::optional<GameState> found = first_state(1, offers_declare_charge);
    ASSERT_TRUE(found.has_value());
    const Side side = found->pending->side;
    const UnitId id = wh40k::test::unit_of(*found, side).id;
    for (const MovePlan plan : {MovePlan::RemainStationary, MovePlan::NormalUpTo3, MovePlan::Normal, MovePlan::Advance,
                                MovePlan::FallBack}) {
        GameState gs = *found;
        Unit& u = *gs.find_unit(id);
        u.turn_flags = UnitTurnFlags{};
        u.turn_flags.advanced = plan == MovePlan::Advance;
        u.turn_flags.fell_back = plan == MovePlan::FallBack;
        u.turn_flags.moved_over_3in = plan == MovePlan::Normal || plan == MovePlan::Advance;
        u.phase_flags.selected_to_shoot = false;
        const TurnAllowance a = allowance_after(plan);
        const TurnOptions o = current_options(gs, u);
        EXPECT_EQ(a.charge, !o.charge_problem.has_value()) << static_cast<int>(plan);
        EXPECT_EQ(a.shoot, !o.shooting_types.empty()) << static_cast<int>(plan);
        if (a.shoot) {
            ASSERT_EQ(o.shooting_types.size(), 1u);
            EXPECT_EQ(o.shooting_types[0], a.type) << static_cast<int>(plan);
            EXPECT_EQ(o.heavy, a.heavy) << static_cast<int>(plan);
        }
    }
}

// ==== Acceptance: roles ====

TEST(M40_Role, TermagantsLeanRangedAndIntercessorsAreStrongAtBoth) {
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    const UnitRole gaunts = unit_role(cache, termagants(gs), intercessors(gs));
    const UnitRole marines = unit_role(cache, intercessors(gs), termagants(gs));
    EXPECT_EQ(gaunts.lean, Lean::Ranged);
    EXPECT_GT(gaunts.ranged, gaunts.melee);
    EXPECT_FALSE(gaunts.strong_melee);
    EXPECT_TRUE(marines.strong_ranged);
    EXPECT_TRUE(marines.strong_melee);
    EXPECT_GT(marines.ranged, 5.0 * gaunts.ranged);  // the matchup is lopsided (M39 arena: 200-0)
    std::printf("[ M40 role ] Termagants vs Intercessors: ranged %.3f melee %.3f share %.2f | "
                "Intercessors vs Termagants: ranged %.3f melee %.3f share %.2f\n",
                gaunts.ranged, gaunts.melee, gaunts.ranged_share, marines.ranged, marines.melee, marines.ranged_share);
}

// ==== The matchup table per range band ====

TEST(M40_Matchup, BandsOrderAsTheRulesSay) {
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    const Matchup mu = matchup(cache, intercessors(gs), termagants(gs));
    const DirectionTable& m = mu.a_to_b;
    const DirectionTable& t = mu.b_to_a;
    // [RAPID FIRE] within half range, [HEAVY] when stationary, nothing out of range.
    EXPECT_GT(m.at(RangeBand::HalfRange).turn.model_equivalents, m.at(RangeBand::InRange).turn.model_equivalents);
    EXPECT_GT(m.at(RangeBand::InRange).stationary.model_equivalents, 0.0);
    EXPECT_GE(m.at(RangeBand::InRange).stationary.model_equivalents, m.at(RangeBand::InRange).moved.model_equivalents);
    EXPECT_EQ(m.at(RangeBand::OutOfRange).turn.model_equivalents, 0.0);
    EXPECT_GT(m.at(RangeBand::InRange).advanced.model_equivalents, 0.0);  // Saturation is [ASSAULT]
    // Engaged: Intercessors fire Bolt pistols (10.06) and fight; Termagants only fight.
    EXPECT_GT(m.at(RangeBand::Engaged).stationary.model_equivalents, 0.0);
    EXPECT_EQ(t.at(RangeBand::Engaged).stationary.model_equivalents, 0.0);
    EXPECT_GT(t.at(RangeBand::Engaged).melee.model_equivalents, 0.0);
    EXPECT_GT(m.at(RangeBand::Engaged).turn.model_equivalents, m.at(RangeBand::Engaged).melee.model_equivalents);
    // The Termagants have no [RAPID FIRE]: the half-range band is worth the same as in range.
    EXPECT_DOUBLE_EQ(t.at(RangeBand::HalfRange).turn.model_equivalents, t.at(RangeBand::InRange).turn.model_equivalents);
    EXPECT_NEAR(t.at(RangeBand::InRange).gap_mm, in(18), 1e-3);
    for (const DirectionTable* d : {&m, &t}) {
        for (const BandValue& b : d->bands) {
            double sum = 0.0;
            for (const double p : b.turn.models_destroyed_pmf) sum += p;
            EXPECT_NEAR(sum, 1.0, 1e-9);
        }
    }
    std::printf("[ M40 matchup ] Intercessors -> Termagants (model equivalents): engaged %.3f (pistols %.3f, melee %.3f), "
                "half %.3f (moved %.3f), in range %.3f (moved %.3f, advanced %.3f)\n",
                m.at(RangeBand::Engaged).turn.model_equivalents, m.at(RangeBand::Engaged).stationary.model_equivalents,
                m.at(RangeBand::Engaged).melee.model_equivalents, m.at(RangeBand::HalfRange).stationary.model_equivalents,
                m.at(RangeBand::HalfRange).moved.model_equivalents, m.at(RangeBand::InRange).stationary.model_equivalents,
                m.at(RangeBand::InRange).moved.model_equivalents, m.at(RangeBand::InRange).advanced.model_equivalents);
    std::printf("[ M40 matchup ] Termagants -> Intercessors (model equivalents): engaged %.3f, in range %.3f "
                "(damage %.3f), advanced %.3f\n",
                t.at(RangeBand::Engaged).turn.model_equivalents, t.at(RangeBand::InRange).turn.model_equivalents,
                t.at(RangeBand::InRange).turn.damage, t.at(RangeBand::InRange).advanced.model_equivalents);
}

// ==== From a game state: the engine's batches, best shooting, and the position probe ====

TEST(M40_StateBased, EvaluatedShotsMatchTheHypotheticalVolleyAndValidate) {
    GameState gs = wh40k::test::load_v1_scenario_closer();  // every Intercessor within 12" of the Termagants
    const Unit& marines = intercessors(gs);
    const Unit& gaunts = termagants(gs);
    OddsCache cache;
    const auto plan = best_shooting(cache, gs, marines.id, gaunts.id);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->type, ShootingType::Normal);
    EXPECT_TRUE(shot_problems(gs, marines, plan->type, plan->shots).empty());
    EXPECT_EQ(plan->shots.size(), marines.models.size());
    // Stationary within half range: Focused Fire (2 dice at 2+ to hit) beats Saturation (3 dice at 3+).
    for (const ShotDeclaration& s : plan->shots) {
        EXPECT_EQ(find_model(marines, s.model)->weapons[s.weapon_index].profiles[s.profile_index].name,
                  "Bolt Rifle - Focused Fire");
    }
    // evaluate_shots of the same declarations gives the same outcome, and so does the hypothetical volley.
    const auto evaluated = evaluate_shots(cache, gs, marines.id, plan->type, plan->shots);
    ASSERT_EQ(evaluated.size(), 1u);
    EXPECT_DOUBLE_EQ(evaluated[0].outcome.model_equivalents, plan->outcome.model_equivalents);
    const AttackOutcome hypo = expected_outcome(
        cache, unit_ranged_volley(cache, marines, gaunts, ShotContext{ShootingType::Normal, true, *unit_distance(marines, gaunts)}),
        snapshot_of(gaunts));
    EXPECT_DOUBLE_EQ(hypo.model_equivalents, plan->outcome.model_equivalents);
    // The position probe: after a move of more than 3", Saturation is better (3 dice at 3+ vs 2 at 3+).
    PositionProbe probe(gs);
    UnitTurnFlags moved;
    moved.moved_over_3in = true;
    probe.set_moved(marines.id, moved);
    const auto after_move = best_shooting(cache, probe.state(), marines.id, gaunts.id);
    ASSERT_TRUE(after_move.has_value());
    const ShotDeclaration& s0 = after_move->shots.front();
    EXPECT_EQ(find_model(marines, s0.model)->weapons[s0.weapon_index].profiles[s0.profile_index].name,
              "Bolt Rifle - Saturation");
    EXPECT_LT(after_move->outcome.model_equivalents, plan->outcome.model_equivalents);
    // Placed 30" away nothing reaches.
    probe.reset();
    EXPECT_EQ(probe.state(), gs);
    std::vector<Position> far;
    for (const Model& m : marines.models) far.push_back(Position{m.position.x, m.position.y - in(30), 0.0f});
    probe.place(marines.id, far);
    EXPECT_FALSE(best_shooting(cache, probe.state(), marines.id, gaunts.id).has_value());
}

TEST(M40_StateBased, HalfRangeAgreesWithSelectWeaponTarget) {
    // within_half_range is the same test the engine records at Select Targets (24.30), at the edge too.
    const GameState base = wh40k::test::load_v1_scenario();
    const Unit& marines = intercessors(base);
    const WeaponProfile& rifle = marines.models[0].weapons[0].profiles[0];
    ASSERT_TRUE(rifle.has<ability::RapidFire>());
    int checked = 0;
    for (const float gap_in : {11.0f, 11.99f, 12.0f, 12.005f, 12.02f, 13.0f}) {
        GameState gs = base;
        set_gap(gs, termagants(gs).id, intercessors(gs).id, in(gap_in));
        for (const Model& m : intercessors(gs).models) {
            const WeaponSelection sel = select_weapon_target(gs, m.id, 0, 0, termagants(gs).id);
            const float d = *model_distance_to_unit(m, m.position, termagants(gs));
            EXPECT_EQ(within_half_range(rifle, d), sel.target_within_half_range) << gap_in;
            ++checked;
        }
    }
    EXPECT_EQ(checked, 30);
}

TEST(M40_StateBased, BestMeleeUsesTheEnginesOptionsWhenEngaged) {
    GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    // Not engaged: no melee option.
    EXPECT_FALSE(best_melee(cache, gs, intercessors(gs).id, termagants(gs).id).has_value());
    // Put the Termagants 0.5" away: the Intercessors within engagement range fight.
    set_gap(gs, termagants(gs).id, intercessors(gs).id, in(0.5f));
    const auto plan = best_melee(cache, gs, intercessors(gs).id, termagants(gs).id);
    ASSERT_TRUE(plan.has_value());
    EXPECT_TRUE(melee_problems(gs, intercessors(gs), plan->declarations).empty());
    EXPECT_GT(plan->outcome.model_equivalents, 0.0);
    const auto again = evaluate_melee(cache, gs, intercessors(gs).id, plan->declarations);
    ASSERT_EQ(again.size(), 1u);
    EXPECT_DOUBLE_EQ(again[0].outcome.model_equivalents, plan->outcome.model_equivalents);
}
