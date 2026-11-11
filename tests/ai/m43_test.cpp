// M43: the movement planner and whole-turn plan (D-054): 03.01-03.04, 09.02-09.07, 10.02-10.06, 11.02. Move types
// and destinations from the whole-turn score: Heavy when holding still is best, closing to Rapid Fire and charge
// range, keeping out of the enemy's likely charge range, Advance decided from its odds before the roll, Fall Back
// when staying engaged is worth less; every planned move legal (never ending engaged, never reverted) and the same
// from any state along it; clean arena games.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/threat.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::in;

namespace {

BotConfig normal_level() { return BotConfig{}; }

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

enum class Mover { Intercessors, Termagants };

// The V1 scenario played (first legal action each time) to side A's first MoveUnits decision; made once.
const GameState& first_move_step() {
    static const GameState base = [] {
        GameState gs = wh40k::test::load_v1_scenario();
        start_game(gs);
        while (!(gs.pending && gs.pending->side == Side::A &&
                 std::holds_alternative<decision::MoveUnits>(gs.pending->kind))) {
            const Side side = gs.pending.value().side;
            apply_action(gs, side, legal_actions(gs, side).front());
        }
        return gs;
    }();
    return base;
}

struct MoveLayout {
    float gap_in = 10.0f;       // edge to edge, the closest pair of models
    float advance_in = 0.0f;    // the mover first moved this far north of its deployment (negative: south)
    float offset_in = 0.0f;     // the enemy shifted sideways (east)
    int round = 1;              // battle round
    int enemy_models = 0;       // keep only this many of the enemy's models (0: all)
    int mover_models = 0;       // keep only this many of the mover's models (0: all)
    bool last_turn = false;     // the game's last turn (round 5, second turn: nothing after it, D-007)
    bool marine_enemy = false;  // the enemy is a second Intercessor Squad in a line (not the Termagants)
};

std::size_t mover_index(Mover m) { return m == Mover::Intercessors ? kMarines : kGants; }
std::size_t enemy_index(Mover m) { return m == Mover::Intercessors ? kGants : kMarines; }

// Side A's MoveUnits decision with `mover` (on side A, south, its front rank facing north) `gap_in` from the other
// unit (on side B, north of it). Turn and phase flags are cleared and the pending decision recomputed.
GameState move_fixture(Mover mover, const MoveLayout& layout) {
    GameState gs = first_move_step();
    Unit& us = gs.units[mover_index(mover)];
    Unit& them = gs.units[enemy_index(mover)];
    us.side = Side::A;
    them.side = Side::B;
    for (Unit* u : {&us, &them}) {
        u->turn_flags = {};
        u->phase_flags = {};
        u->battle_shocked = false;
    }
    if (us.models.front().position.y > them.models.front().position.y) {
        for (Unit* u : {&us, &them}) {
            for (Model& m : u->models) m.position.y = gs.battlefield.depth_mm - m.position.y;
        }
    }
    for (Model& m : us.models) m.position.y += in(layout.advance_in);
    if (layout.mover_models > 0 && static_cast<std::size_t>(layout.mover_models) < us.models.size()) {
        us.models.resize(static_cast<std::size_t>(layout.mover_models));
    }
    if (layout.marine_enemy) {
        // A second Intercessor Squad in a line north of the mover (the first unit's models, new ids).
        const UnitId id = them.id;
        them = us;
        them.id = id;
        them.side = Side::B;
        for (std::size_t i = 0; i < them.models.size(); ++i) {
            them.models[i].id = ModelId{1000 + static_cast<std::uint32_t>(i)};
            them.models[i].position.y += them.models[i].base.diameter_mm + in(layout.gap_in);
        }
    }
    if (layout.enemy_models > 0 && static_cast<std::size_t>(layout.enemy_models) < them.models.size()) {
        them.models.resize(static_cast<std::size_t>(layout.enemy_models));
    }
    for (Model& m : them.models) m.position.x += in(layout.offset_in);
    for (int i = 0; i < 60; ++i) {
        const float d = *unit_distance(us, them);
        if (std::fabs(d - in(layout.gap_in)) < 1e-4f) break;
        for (Model& m : them.models) m.position.y += in(layout.gap_in) - d;
    }
    const Rect field{0.0f, 0.0f, gs.battlefield.width_mm, gs.battlefield.depth_mm};
    for (const Unit* u : {&us, &them}) {
        for (const Model& m : u->models)
            EXPECT_TRUE(wholly_within(field, m.position, m.base)) << "fixture off the board";
    }
    gs.flow.battle_round = layout.round;
    if (layout.last_turn) {
        gs.flow.battle_round = gs.battle_rounds;
        gs.flow.turn_in_round = 1;
        gs.first_turn = Side::B;  // so the second turn is side A's
    }
    gs.pending = compute_decision(gs);
    return gs;
}

UnitId mover_id(const GameState& gs, Mover m) { return gs.units[mover_index(m)].id; }

struct PlayedMove {
    std::optional<action::StartMove> start;
    std::vector<std::string> reasons;
    int rejected = 0;
    int reverted = 0;
    int resets = 0;
    int fresh_mismatches = 0;
    GameState after;
};

// Answers side A's decisions with `bot` (applying each with the engine, the state's own dice) from MoveUnits until
// the unit's move has ended. The first `fresh_checks` decisions are also asked of a fresh bot (no memo).
PlayedMove play_move(GameState gs, Bot& bot, std::size_t fresh_checks = 2) {
    PlayedMove out;
    std::size_t decisions = 0;
    for (int guard = 0; guard < 80 && gs.pending && gs.pending->side == Side::A; ++guard) {
        const auto& kind = gs.pending->kind;
        const bool moving = std::holds_alternative<decision::MoveModels>(kind) ||
                            (gs.flow.move && std::holds_alternative<decision::OrderCasualties>(kind));
        if (out.start && !moving) break;
        if (!out.start && !std::holds_alternative<decision::MoveUnits>(kind)) break;
        const BotChoice c = bot.decide(gs, Side::A);
        out.reasons.push_back(c.reason);
        // D-054: a fresh bot (no memo) gives the same answer from any state along the plan.
        if (decisions++ < fresh_checks) {
            const BotChoice fresh = Bot(bot.config()).decide(gs, Side::A);
            EXPECT_EQ(fresh.action, c.action) << c.reason << " / " << fresh.reason;
            if (fresh.action != c.action) ++out.fresh_mismatches;
        }
        if (!validate_action(gs, Side::A, c.action).ok()) {
            ++out.rejected;
            break;
        }
        if (const auto* s = std::get_if<action::StartMove>(&c.action)) out.start = *s;
        if (std::holds_alternative<action::ResetUnitMove>(c.action)) ++out.resets;
        for (const GameEvent& e : apply_action(gs, Side::A, c.action)) {
            if (std::holds_alternative<event::UnitMoveReverted>(e)) ++out.reverted;
        }
        if (out.start && out.start->move_type == MoveType::RemainStationary) break;
    }
    out.after = std::move(gs);
    return out;
}

float gap_between(const GameState& gs, std::size_t a, std::size_t b) {
    return *unit_distance(gs.units[a], gs.units[b]);
}

int models_within(const GameState& gs, std::size_t unit, std::size_t enemy, float mm) {
    int n = 0;
    for (const Model& m : gs.units[unit].models) {
        if (within(*model_distance_to_unit(m, m.position, gs.units[enemy]), mm)) ++n;
    }
    return n;
}

void expect_clean(const PlayedMove& p, const std::string& what) {
    EXPECT_EQ(p.rejected, 0) << what;
    EXPECT_EQ(p.reverted, 0) << what;
    EXPECT_EQ(p.resets, 0) << what;
    EXPECT_EQ(p.fresh_mismatches, 0) << what;
    EXPECT_TRUE(p.start.has_value()) << what;
}

std::optional<normal::MoveTypeValues> values_at(const GameState& gs) {
    analysis::OddsCache cache;
    normal::MoveTypeValues values;
    if (!normal::decide_move_units(cache, gs, Side::A, legal_actions(gs, Side::A), decision_seed(gs), &values)) {
        return std::nullopt;
    }
    return values;
}

}  // namespace

// ==== Acceptance: holds still for [HEAVY] when that's best (24.16, 09.04) ====

TEST(M43_Heavy, InTheLastTurnFarFromAnotherIntercessorSquadItRemainsStationaryAndFiresHeavyFocusedFire) {
    // Nothing comes after this turn (D-007), no charge can reach, and moving more than 3" would lose [HEAVY]: the
    // Focused Fire profile is worth more stationary than anything a move gives.
    MoveLayout layout{19.0f, -4.6f};
    layout.marine_enemy = true;
    layout.last_turn = true;
    const GameState gs = move_fixture(Mover::Intercessors, layout);
    const auto values = values_at(gs);
    ASSERT_TRUE(values && values->remain_stationary && values->advance);
    EXPECT_GT(*values->remain_stationary, *values->advance);
    if (values->normal) EXPECT_GE(*values->remain_stationary, *values->normal);
    Bot bot(normal_level());
    const PlayedMove p = play_move(gs, bot);
    expect_clean(p, "last turn");
    ASSERT_TRUE(p.start);
    EXPECT_EQ(p.start->move_type, MoveType::RemainStationary) << p.reasons.front();
    const Unit& marines = p.after.units[kMarines];
    EXPECT_TRUE(shooting_context(p.after, marines).heavy_conditions_met);
    ASSERT_TRUE(p.after.pending && std::holds_alternative<decision::Shoot>(p.after.pending->kind));
    const BotChoice shot = bot.decide(p.after, Side::A);
    EXPECT_NE(shot.reason.find("Focused Fire"), std::string::npos) << shot.reason;
}

TEST(M43_Heavy, FarFromAnotherIntercessorSquadItMovesNoMoreThanThreeInchesAndKeepsHeavy) {
    MoveLayout layout{20.0f, -4.6f};
    layout.marine_enemy = true;
    const GameState gs = move_fixture(Mover::Intercessors, layout);
    Bot bot(normal_level());
    const PlayedMove p = play_move(gs, bot);
    expect_clean(p, "round 1");
    ASSERT_TRUE(p.start);
    EXPECT_TRUE(p.start->move_type == MoveType::RemainStationary || p.start->move_type == MoveType::Normal);
    const Unit& marines = p.after.units[kMarines];
    EXPECT_FALSE(marines.turn_flags.moved_over_3in) << p.reasons.front();
    EXPECT_FALSE(marines.turn_flags.advanced);
    EXPECT_TRUE(shooting_context(p.after, marines).heavy_conditions_met);
}

// ==== Acceptance: closes to Rapid Fire range and charge range when that's better (24.30, 11.02) ====

TEST(M43_RapidFire, IntercessorsSixteenInchesFromTermagantsCloseToHalfRangeAndChargeRange) {
    const GameState gs = move_fixture(Mover::Intercessors, MoveLayout{16.0f, -4.0f});
    const auto values = values_at(gs);
    ASSERT_TRUE(values && values->normal && values->remain_stationary);
    EXPECT_GT(*values->normal, *values->remain_stationary);
    Bot bot(normal_level());
    const PlayedMove p = play_move(gs, bot);
    expect_clean(p, "16in");
    ASSERT_TRUE(p.start);
    EXPECT_EQ(p.start->move_type, MoveType::Normal) << p.reasons.front();
    const float gap = gap_between(p.after, kMarines, kGants);
    EXPECT_TRUE(within(gap, in(kChargeRangeInches))) << gap / in(1.0f);  // 11.02: a charge can be declared
    EXPECT_GT(gap, kEngagementRangeMm);                                  // 09.05: ended unengaged
    // Every Intercessor within half the Bolt Rifle's range of the Termagants ([RAPID FIRE]).
    EXPECT_EQ(models_within(p.after, kMarines, kGants, in(12.0f)), 5);
    EXPECT_FALSE(p.after.units[kMarines].turn_flags.advanced);  // 09.06: an Advance would rule out the charge
}

// ==== Acceptance: stays out of the enemy's likely charge range when kiting is better ====

TEST(M43_Kiting, TermagantsTenInchesFromIntercessorsKeepOutOfRapidFireAndChargeRangeAndStillShoot) {
    const GameState gs = move_fixture(Mover::Termagants, MoveLayout{10.0f, 4.0f});
    const auto values = values_at(gs);
    ASSERT_TRUE(values && values->remain_stationary);
    EXPECT_GT(std::max(values->normal.value_or(-9.0), values->advance.value_or(-9.0)), *values->remain_stationary);
    Bot bot(normal_level());
    const PlayedMove p = play_move(gs, bot);
    expect_clean(p, "kiting");
    const float gap = gap_between(p.after, kGants, kMarines);
    EXPECT_GT(gap, in(14.0f)) << p.reasons.front();  // beyond the Bolt Rifles' half range, and further
    const analysis::ThreatRanges marines = analysis::threat_ranges(p.after.units[kMarines]);
    EXPECT_LT(marines.p_charge_after_move(gap), 0.25) << p.reasons.front();
    EXPECT_LT(marines.p_charge_after_move(gap), marines.p_charge_after_move(in(10.0f)));
    // Still within the Fleshborers' 18": the Termagants shoot this turn (Assault shooting after an Advance, 10.05).
    EXPECT_TRUE(within(gap, in(18.0f)));
    EXPECT_FALSE(eligible_shooting_types(p.after, p.after.units[kGants]).empty());
}

// ==== Acceptance: an Advance is decided from its odds before the roll (D-054), then planned with the roll ====

TEST(M43_Advance, DecidedFromTheOddsBeforeTheRollThenEachRollsMoveStandsAndKeepsTheAdvance) {
    const GameState gs = move_fixture(Mover::Termagants, MoveLayout{10.0f, 4.0f});
    const UnitId gants = mover_id(gs, Mover::Termagants);
    Bot bot(normal_level());
    const BotChoice c = bot.decide(gs, Side::A);
    const GameAction advance = action::StartMove{gants, MoveType::Advance, std::nullopt};
    ASSERT_EQ(c.action, advance) << c.reason;
    EXPECT_NE(c.reason.find("on average over the advance roll"), std::string::npos) << c.reason;
    EXPECT_TRUE(c.reason.find("Remain Stationary") != std::string::npos);  // the alternatives' values are given
    for (std::uint64_t k = 1; k <= 3; ++k) {
        GameState other = gs;
        other.dice.seed ^= 0x9E3779B97F4A7C15ull * k;
        other.dice.position += 7 * k;
        EXPECT_EQ(Bot(normal_level()).decide(other, Side::A).action, advance);
    }
    std::set<std::string> plans;
    for (int roll = 1; roll <= 6; ++roll) {
        GameState s = gs;
        ScriptedDice dice{roll};
        apply_action(s, Side::A, advance, dice);
        ASSERT_TRUE(s.pending && std::holds_alternative<decision::MoveModels>(s.pending->kind));
        const float max = *max_move_distance_mm(s, s.units[kGants].models.front().id);
        EXPECT_NEAR(max, in(6.0f + static_cast<float>(roll)), 1e-3f);
        Bot mover(normal_level());
        PlayedMove p;
        p.start = action::StartMove{gants, MoveType::Advance, std::nullopt};
        for (int guard = 0; guard < 40 && s.pending && std::holds_alternative<decision::MoveModels>(s.pending->kind);
             ++guard) {
            const BotChoice step = mover.decide(s, Side::A);
            if (guard == 0) EXPECT_EQ(Bot(normal_level()).decide(s, Side::A).action, step.action) << "roll " << roll;
            EXPECT_FALSE(std::holds_alternative<action::ResetUnitMove>(step.action)) << "roll " << roll;  // D-054
            ASSERT_TRUE(validate_action(s, Side::A, step.action).ok()) << "roll " << roll << ": " << step.reason;
            for (const GameEvent& e : apply_action(s, Side::A, step.action)) {
                EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e)) << "roll " << roll;
            }
            if (std::holds_alternative<action::CommitUnitMove>(step.action)) plans.insert(step.reason);
        }
        EXPECT_FALSE(s.flow.move.has_value()) << "roll " << roll;
        EXPECT_TRUE(s.units[kGants].turn_flags.advanced) << "roll " << roll;
        EXPECT_FALSE(is_engaged(s, s.units[kGants])) << "roll " << roll;
    }
    EXPECT_GE(plans.size(), 2u);  // the rolls get different planned moves
}

// ==== An engaged unit Falls Back when staying is worth less (09.07), and fights on when it is worth more ====

TEST(M43_FallBack, EngagedTermagantsFallBackAndEngagedIntercessorsStayInTheFight) {
    {
        const GameState gs = move_fixture(Mover::Termagants, MoveLayout{0.5f, 4.0f});
        ASSERT_TRUE(is_engaged(gs, gs.units[kGants]));
        const auto values = values_at(gs);
        ASSERT_TRUE(values && values->fall_back && values->remain_stationary);
        EXPECT_GT(*values->fall_back, *values->remain_stationary);
        Bot bot(normal_level());
        const PlayedMove p = play_move(gs, bot);
        expect_clean(p, "Termagants");
        ASSERT_TRUE(p.start);
        EXPECT_EQ(p.start->move_type, MoveType::FallBack) << p.reasons.front();
        EXPECT_EQ(p.start->fall_back_mode, FallBackMode::OrderedRetreat);
        EXPECT_FALSE(is_engaged(p.after, p.after.units[kGants]));
        EXPECT_TRUE(p.after.units[kGants].turn_flags.fell_back);
    }
    {
        const GameState gs = move_fixture(Mover::Intercessors, MoveLayout{0.5f, 4.0f});
        ASSERT_TRUE(is_engaged(gs, gs.units[kMarines]));
        const auto values = values_at(gs);
        ASSERT_TRUE(values && values->fall_back && values->remain_stationary);
        EXPECT_GT(*values->remain_stationary, *values->fall_back);
        Bot bot(normal_level());
        const PlayedMove p = play_move(gs, bot);
        expect_clean(p, "Intercessors");
        ASSERT_TRUE(p.start);
        EXPECT_EQ(p.start->move_type, MoveType::RemainStationary) << p.reasons.front();
    }
}

// ==== Acceptance: never ends a move in Engagement Range unless charging; no reverts ====

TEST(M43_Legal, AcrossAGridNoPlannedMoveEndsEngagedOrIsRevertedAndAFreshBotGivesTheSamePlan) {
    int cases = 0;
    std::set<MoveType> types;
    for (const Mover mover : {Mover::Intercessors, Mover::Termagants}) {
        for (const float gap : {3.0f, 7.0f, 12.0f, 16.0f}) {
            for (const float offset : {0.0f, 5.0f}) {
                const MoveLayout layout{gap, mover == Mover::Intercessors ? -2.0f : 0.0f, offset};
                const GameState gs = move_fixture(mover, layout);
                Bot bot(normal_level());
                const PlayedMove p = play_move(gs, bot);
                const std::string what = std::string(mover == Mover::Intercessors ? "Intercessors " : "Termagants ") +
                                         std::to_string(gap) + "in offset " + std::to_string(offset);
                expect_clean(p, what);
                if (p.start) types.insert(p.start->move_type);
                // 09.05 / 09.06 After Moving: unengaged (only a charge may end engaged).
                EXPECT_FALSE(is_engaged(p.after, p.after.units[mover_index(mover)])) << what;
                EXPECT_FALSE(p.after.flow.move.has_value()) << what;
                ++cases;
            }
        }
        // Starting engaged: Remain Stationary (still engaged) or a Fall Back that ends unengaged (09.07).
        const GameState engaged = move_fixture(mover, MoveLayout{0.5f, 4.0f});
        Bot bot(normal_level());
        const PlayedMove p = play_move(engaged, bot);
        expect_clean(p, "engaged");
        ASSERT_TRUE(p.start);
        if (p.start->move_type == MoveType::FallBack)
            EXPECT_FALSE(is_engaged(p.after, p.after.units[mover_index(mover)]));
        types.insert(p.start->move_type);
        ++cases;
    }
    EXPECT_EQ(cases, 18);
    EXPECT_GE(types.size(), 3u);  // Normal, Advance, Fall Back and/or Remain Stationary all came up
}

// ==== Dice independence and clean games ====

TEST(M43_NeverPeeksAtDice, MovementDecisionsAreTheSameWhateverTheDiceAndFromAnyStateOfThePlan) {
    std::size_t checked = 0;
    std::set<MoveType> types;
    for (const auto& [a, b] : {std::pair{normal_level(), normal_level()}}) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            client::GameSession s = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
            std::array<Bot, 2> bots{Bot(a), Bot(b)};
            client::AutoSettings autos;
            autos.bot_controlled = {true, true};
            while (s.pending() && s.history().size() < 5000) {
                (void)client::run_auto_actions(s, autos);
                if (!s.pending()) break;
                const Side side = s.pending()->side;
                const GameState gs = s.state();
                const BotChoice c = bots[side_index(side)].decide(gs, side);
                const auto& kind = gs.pending->kind;
                const bool movement =
                    std::holds_alternative<decision::MoveUnits>(kind) ||
                    (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move &&
                     (gs.flow.move->type == MoveType::Normal || gs.flow.move->type == MoveType::Advance ||
                      gs.flow.move->type == MoveType::FallBack));
                // Every MoveUnits decision, and the MoveModels steps of each move's start and every third one after
                // (a fresh bot replans the whole move each time).
                const std::size_t done = gs.flow.move ? gs.flow.move->paths.size() : 0;
                if (movement && done % 3 == 0) {
                    GameState other = gs;
                    other.dice.seed ^= 0x9E3779B97F4A7C15ull;
                    other.dice.position += 13;
                    EXPECT_EQ(Bot(normal_level()).decide(gs, side).action, c.action)
                        << "seed " << seed << ": " << c.reason;
                    EXPECT_EQ(Bot(normal_level()).decide(other, side).action, c.action) << "seed " << seed;
                    if (const auto* m = std::get_if<action::StartMove>(&c.action)) types.insert(m->move_type);
                    ++checked;
                }
                ASSERT_FALSE(c.stopped);
                ASSERT_TRUE(s.submit(c.action).ok()) << action_name(c.action) << ": " << c.reason;
            }
        }
    }
    EXPECT_GT(checked, 10u);
    EXPECT_GE(types.size(), 2u);
}

TEST(M43_Arena, NormalMovementPlaysCleanGamesThatTheSpaceMarinesWin) {
    // 12 seeds (the Release arena's 200 are recorded in PLAN.md, M43). D-085 (M85): the comparisons with level Easy
    // (games won sooner as Space Marines, lasting longer as Tyranids) went with Easy.
    const GameState scenario = wh40k::test::load_v1_scenario();
    const ArenaResult r = run_arena(scenario, 12, 1, normal_level(), normal_level());
    EXPECT_TRUE(r.clean()) << format_arena(r, scenario);
    EXPECT_EQ(r.fallbacks, 0u) << format_arena(r, scenario);
    EXPECT_GE(r.wins[0], 12);  // every game (the Release arena: all 200, M44)
}

// ==== Diagnostics (run on purpose): the ranked candidates of a position ====

TEST(M43_Diagnostics, DISABLED_PrintTheRankedCandidates) {
    for (const Mover mover : {Mover::Intercessors, Mover::Termagants}) {
        MoveLayout layout{12.0f, mover == Mover::Termagants ? 4.0f : 0.0f};
        layout.round = 3;
        layout.mover_models = mover == Mover::Termagants ? 4 : 0;
        const GameState gs = move_fixture(mover, layout);
        GameState s1 = gs;
        apply_action(s1, Side::A, action::StartMove{mover_id(gs, mover), MoveType::Normal, std::nullopt});
        analysis::OddsCache cache;
        const auto cands = normal::move_candidates(s1);
        normal::MoveScoreContext ctx(cache, s1, mover_id(gs, mover));
        std::vector<std::pair<double, std::size_t>> order;
        std::vector<normal::MoveScore> scores;
        for (std::size_t i = 0; i < cands.size(); ++i) {
            scores.push_back(
                normal::score_move_plan(ctx, normal::PlannedEnd{MoveType::Normal, cands[i].ends, cands[i].longest_mm}));
            order.emplace_back(-scores.back().total, i);
        }
        std::sort(order.begin(), order.end());
        std::printf("%s: %zu candidates (discounts %.2f / %.2f, survival weight %.3f)\n",
                    mover == Mover::Intercessors ? "Intercessors" : "Termagants", cands.size(), ctx.our_discount(),
                    ctx.their_discount(), ctx.survival_weight());
        for (std::size_t k = 0; k < std::min<std::size_t>(12, order.size()); ++k) {
            const auto& s = scores[order[k].second];
            std::printf(
                "  %-34s total %+.3f shoot %+.3f charge %+.3f (p %.2f) hold %+.3f edge %+.3f coh %+.3f "
                "survive %.2f gap %.1f\" %s\n",
                cands[order[k].second].label.c_str(), s.total, s.shoot_now, s.charge, s.p_charge, s.hold, s.edge,
                s.coherency, s.p_survive, s.gap_mm / 25.4f, s.enemy_reply.c_str());
        }
        const auto pm = normal::plan_unit_move(cache, s1);
        ASSERT_TRUE(pm.has_value());
        std::printf("  chosen: %s total %+.3f (charge %+.3f p %.2f)\n", pm->candidate.label.c_str(), pm->score.total,
                    pm->score.charge, pm->score.p_charge);
    }
}

TEST(M43_Diagnostics, DISABLED_PrintDecisions) {
    struct Case {
        const char* name;
        Mover mover;
        MoveLayout layout;
    };
    const auto marines = [](float gap, float advance, bool last) {
        MoveLayout l{gap, advance};
        l.marine_enemy = true;
        l.last_turn = last;
        return l;
    };
    const std::vector<Case> cases{
        {"marines vs marines 18in", Mover::Intercessors, marines(18.0f, -3.0f, false)},
        {"marines vs marines 20in", Mover::Intercessors, marines(20.0f, -4.6f, false)},
        {"marines vs marines 10in last", Mover::Intercessors, marines(10.0f, 0.0f, true)},
        {"marines vs marines 19in last", Mover::Intercessors, marines(19.0f, -4.6f, true)},
        {"marines vs marines 12in last", Mover::Intercessors, marines(12.0f, 0.0f, true)},
        {"marines vs gants 16in", Mover::Intercessors, MoveLayout{16.0f, -4.0f}},
        {"gants vs marines 10in mid", Mover::Termagants, MoveLayout{10.0f, 4.0f}},
        {"gants vs marines 20in", Mover::Termagants, MoveLayout{20.0f, -3.0f}},
        {"marines engaged with gants", Mover::Intercessors, MoveLayout{0.5f, 4.0f}},
        {"gants engaged with marines", Mover::Termagants, MoveLayout{0.5f, 4.0f}},
        {"gants engaged, round 5", Mover::Termagants, MoveLayout{0.5f, 4.0f, 0.0f, 5}},
    };
    for (const Case& c : cases) {
        const GameState gs = move_fixture(c.mover, c.layout);
        Bot bot(normal_level());
        normal::MoveTypeValues values;
        analysis::OddsCache cache;
        const auto legal = legal_actions(gs, Side::A);
        const auto r = normal::decide_move_units(cache, gs, Side::A, legal, decision_seed(gs), &values);
        ASSERT_TRUE(r.has_value());
        std::printf("%-28s %s\n", c.name, r->reason.c_str());
    }
}

TEST(M43_Diagnostics, DISABLED_TimeThePlanner) {
    for (const Mover mover : {Mover::Intercessors, Mover::Termagants}) {
        const GameState gs = move_fixture(mover, MoveLayout{12.0f, mover == Mover::Termagants ? 3.0f : 0.0f});
        GameState s1 = gs;
        apply_action(s1, Side::A, action::StartMove{mover_id(gs, mover), MoveType::Normal, std::nullopt});
        analysis::OddsCache cache;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<normal::MoveCandidate> cands;
        for (int i = 0; i < 20; ++i) cands = normal::move_candidates(s1);
        const auto t1 = std::chrono::steady_clock::now();
        normal::MoveScoreContext ctx(cache, s1, mover_id(gs, mover));
        for (int i = 0; i < 20; ++i) {
            for (const auto& c : cands)
                (void)normal::score_move_plan(ctx, normal::PlannedEnd{MoveType::Normal, c.ends, c.longest_mm});
        }
        const auto t2 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) (void)normal::plan_unit_move(cache, s1);
        const auto t3 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) (void)normal::charge_prospect(cache, gs, mover_id(gs, mover), 1, 0.0);
        const auto t4 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) (void)Bot(normal_level()).decide(gs, Side::A);
        const auto t5 = std::chrono::steady_clock::now();
        const auto ms = [](auto a, auto b, int n) {
            return std::chrono::duration<double, std::milli>(b - a).count() / n;
        };
        std::printf(
            "%s: %zu candidates; generate %.3f ms, cheap scores %.3f ms, plan_unit_move %.3f ms, "
            "charge_prospect %.3f ms, MoveUnits decision %.3f ms\n",
            mover == Mover::Intercessors ? "Intercessors" : "Termagants", cands.size(), ms(t0, t1, 20), ms(t1, t2, 20),
            ms(t2, t3, 20), ms(t3, t4, 5), ms(t4, t5, 5));
    }
}
