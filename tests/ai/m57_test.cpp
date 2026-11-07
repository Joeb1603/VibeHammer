// M57: Hard across several units (D-067). 01.04 (closest unit, D-010), 03.03, 05.04, 09.02, 10.02, 11.02, 11.04,
// 12.07, 12.08 and D-007 (a wipe-out wins, otherwise a draw after battle round 5). The army value (per-unit-pair duel
// chains combined into P(win) - P(loss) over every unit), which is the duel's own value with one unit per side; the
// lookahead's real target choice and every enemy unit's reply; a chain line long enough for a 60" board; Hard's
// army-valued casualty, charge and consolidation choices; and seeded Strike games that are clean, with a fresh Hard
// bot giving the played answer.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "analysis/charge_odds.hpp"
#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "hard/evaluator.hpp"
#include "hard/hard_bot.hpp"
#include "hard/lookahead.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/charge_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::at;
using wh40k::test::in;

namespace {

// Units in the strike scenario's order (D-064): Intercessor Squads 1-3, then Termagants 1-4.
constexpr std::size_t kSquad1 = 0;
constexpr std::size_t kSquad2 = 1;
constexpr std::size_t kSquad3 = 2;
constexpr std::size_t kGants1 = 3;
constexpr std::size_t kGants2 = 4;
constexpr std::size_t kGants3 = 5;
constexpr std::size_t kGants4 = 6;
// V1's units (one each).
constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

GameState strike_scenario() {
    const auto root = wh40k::test::repo_root();
    return load_scenario(root / "data/scenarios/strike_44x60.json", root);
}

// The strike scenario played with the first legal action each time until `done` holds.
template <typename Pred>
GameState strike_until(Pred done) {
    GameState gs = strike_scenario();
    start_game(gs);
    for (int guard = 0; guard < 400 && !done(gs); ++guard) {
        const Side side = gs.pending.value().side;
        apply_action(gs, side, legal_actions(gs, side).front());
    }
    EXPECT_TRUE(done(gs)) << "fixture not reached";
    return gs;
}

GameState side_a_move_units() {
    return strike_until([](const GameState& gs) {
        return gs.pending && gs.pending->side == Side::A &&
               std::holds_alternative<decision::MoveUnits>(gs.pending->kind);
    });
}

Position centroid(const Unit& u) {
    Position c;
    for (const Model& m : u.models) {
        c.x += m.position.x;
        c.y += m.position.y;
    }
    const float n = static_cast<float>(u.models.size());
    return Position{c.x / n, c.y / n, 0.0f};
}

// Moves the whole unit so that its centroid is at `to`.
void place(Unit& u, Position to) {
    const Position c = centroid(u);
    for (Model& m : u.models) {
        m.position.x += to.x - c.x;
        m.position.y += to.y - c.y;
    }
}

void clear_flags(GameState& gs) {
    for (Unit& u : gs.units) {
        u.turn_flags = {};
        u.phase_flags = {};
    }
    gs.pending = compute_decision(gs);
}

// A copy of units[index] (fresh unit and model ids) for `side` with its centroid at `to`.
UnitId add_copy(GameState& gs, std::size_t index, Side side, Position to) {
    const UnitId id = wh40k::test::add_unit_copy(gs, index);
    Unit& u = gs.units.back();
    u.side = side;
    place(u, to);
    return id;
}

double cosine(float ax, float ay, float bx, float by) {
    const double la = std::hypot(ax, ay);
    const double lb = std::hypot(bx, by);
    return la > 0.0 && lb > 0.0 ? (static_cast<double>(ax) * bx + static_cast<double>(ay) * by) / (la * lb) : 0.0;
}

}  // namespace

// ==== The army value ====

TEST(M57_Evaluator, TheArmyValueOfOneUnitEachIsTheDuel) {
    // D-067: with one unit per side the army's combination (D-007 over every unit, each attacker's attention, the
    // last unit's tempo and material) is the duel's own value, term by term. Checked on V1 positions along a game:
    // each side's evaluator, the duel's way and the army's way over the same table (force_army).
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    int checked = 0;
    for (int step = 0; step < 120 && gs.pending && !gs.result; ++step) {
        if (step % 6 == 0) {
            for (const Side side : {Side::A, Side::B}) {
                analysis::OddsCache cache;
                const hard::Evaluator duel(cache, gs, side, hard::ChainWeights{});
                const hard::Evaluator army(cache, gs, side, hard::ChainWeights{}, nullptr, true);
                ASSERT_FALSE(duel.army());
                ASSERT_TRUE(army.army());
                const hard::Evaluation d = duel.evaluate(gs);
                const hard::Evaluation a = army.evaluate(gs);
                EXPECT_NEAR(a.score, d.score, 1e-12) << "step " << step;
                EXPECT_NEAR(a.p_win, d.p_win, 1e-12) << "step " << step;
                EXPECT_NEAR(a.p_loss, d.p_loss, 1e-12) << "step " << step;
                EXPECT_NEAR(a.v, d.v, 1e-12) << "step " << step;
                ++checked;
            }
        }
        const Side side = gs.pending->side;
        const std::vector<GameAction> legal = legal_actions(gs, side);
        if (legal.empty() || is_template(legal.front())) {
            const BotChoice c = Bot(level(BotLevel::Easy)).decide(gs, side);
            apply_action(gs, side, c.action);
        } else {
            apply_action(gs, side, legal.front());
        }
    }
    EXPECT_GE(checked, 10);
    // A real game of several units takes the army path; one unit each, the duel.
    analysis::OddsCache cache;
    EXPECT_TRUE(hard::Evaluator(cache, side_a_move_units(), Side::A, hard::ChainWeights{}).army());
    EXPECT_FALSE(hard::Evaluator(cache, wh40k::test::load_v1_scenario(), Side::A, hard::ChainWeights{}).army());
}

TEST(M57_Evaluator, EachUnitsFateIsItsDuelWithItsMainEnemyTheClosestUnit) {
    // D-067: a unit's main enemy is the closest enemy unit (01.04, D-010), not the first in unit order; every unit
    // with models has a fate, and V is P(win) - P(loss) within [-1, 1] for both sides.
    GameState gs = side_a_move_units();
    // Termagants 1 (side B's first unit) far in the north-west corner; Termagants 4 close to Intercessor Squad 3.
    place(gs.units[kGants1], at(4, 40));
    place(gs.units[kGants4], at(45, 26));
    clear_flags(gs);
    const Unit& squad3 = gs.units[kSquad3];
    ASSERT_EQ(normal::closest_enemy(gs, squad3)->id, gs.units[kGants4].id);
    EXPECT_EQ(hard::Evaluator::main_enemy(gs, squad3)->id, gs.units[kGants4].id);
    analysis::OddsCache cache;
    const hard::Evaluator a(cache, gs, Side::A, hard::ChainWeights{});
    const hard::Evaluator b(cache, gs, Side::B, hard::ChainWeights{});
    const std::vector<hard::UnitFate> fates = a.fates(gs);
    EXPECT_EQ(fates.size(), 7u);
    for (const hard::UnitFate& f : fates) {
        const Unit* u = gs.find_unit(f.unit);
        ASSERT_NE(u, nullptr);
        EXPECT_EQ(f.enemy, hard::Evaluator::main_enemy(gs, *u)->id);
        EXPECT_GE(f.p_wiped, 0.0);
        EXPECT_LE(f.p_wiped, 1.0);
    }
    const hard::Evaluation ea = a.evaluate(gs);
    const hard::Evaluation eb = b.evaluate(gs);
    EXPECT_NEAR(ea.v, -eb.v, 1e-12);
    EXPECT_NEAR(ea.p_win, eb.p_loss, 1e-12);
    EXPECT_GE(ea.v, -1.0);
    EXPECT_LE(ea.v, 1.0);
    EXPECT_GT(ea.v, 0.0);  // three Intercessor Squads against four Termagant units: side A is ahead
}

TEST(M57_Evaluator, AUnitTheEnemyIsBusyElsewhereLastsLonger) {
    // D-067 (attention): an enemy unit deals with the units closest to it first, so a unit kept behind others is less
    // likely to be wiped out than the enemy's first target, or than itself as that target, and that is a draw chance
    // for its army (D-007). Battle round 3, side B's turn: Intercessor Squad 2 alone against Termagants 1 and 2 about
    // 12" from it and Termagants 4 further away. (Two turns of the squad's shooting take about 11 Termagants: not
    // enough for 20, so behind the first target there is nothing left for Termagants 4.)
    GameState gs = side_a_move_units();
    for (const std::size_t i : {kSquad1, kSquad3, kGants3}) gs.units[i].models.clear();
    place(gs.units[kSquad2], at(30, 18));
    place(gs.units[kGants1], at(24, 31));
    place(gs.units[kGants2], at(36, 31));
    place(gs.units[kGants4], at(52, 36));
    gs.flow.battle_round = 3;
    gs.flow.turn_in_round = 1;
    clear_flags(gs);
    analysis::OddsCache cache;
    const hard::Evaluator eval(cache, gs, Side::B, hard::ChainWeights{});
    ASSERT_TRUE(eval.army());
    const auto wiped = [&](const GameState& s, std::size_t index) {
        for (const hard::UnitFate& f : eval.fates(s)) {
            if (f.unit == s.units[index].id) return f.p_wiped;
        }
        ADD_FAILURE() << "no fate";
        return 0.0;
    };
    const Unit* first = normal::closest_enemy(gs, gs.units[kSquad2]);
    ASSERT_TRUE(first != nullptr && first->id != gs.units[kGants4].id);
    const double behind = wiped(gs, kGants4);
    EXPECT_LT(behind, wiped(gs, static_cast<std::size_t>(first - gs.units.data())));
    // With Termagants 1 and 2 further away than it (in the north-west corner), Termagants 4 is the squad's first
    // target, and more likely to be wiped out.
    GameState first_target = gs;
    place(first_target.units[kGants1], at(4, 41));
    place(first_target.units[kGants2], at(10, 41));
    ASSERT_EQ(normal::closest_enemy(first_target, first_target.units[kSquad2])->id, first_target.units[kGants4].id);
    EXPECT_LT(behind, wiped(first_target, kGants4));
    // Its army's draw chance comes from it.
    EXPECT_GT(eval.evaluate(gs).p_draw, 0.0);
}

// ==== The chain line ====

TEST(M57_Line, TheChainLineCoversA60InchBoard) {
    // D-067: kMaxLine is 38 buckets (76"), more than the 60"x44" board's 74.4" diagonal (M44: 24, 48"). A duel across
    // the board (one unit each, opposite corners, about 57" apart) gets a line longer than 48", and its value still
    // tells 50" from 56" (a clamped line would read both as 48").
    EXPECT_GE(static_cast<float>(hard::max_line_buckets()) * hard::Evaluator::bucket_mm(),
              std::hypot(in(60.0f), in(44.0f)));
    GameState gs = side_a_move_units();
    for (const std::size_t i : {kSquad2, kSquad3, kGants1, kGants2, kGants3}) gs.units[i].models.clear();
    place(gs.units[kSquad1], at(5, 2));
    place(gs.units[kGants4], at(55, 41));
    clear_flags(gs);
    const float gap = *unit_distance(gs.units[kSquad1], gs.units[kGants4]);
    ASSERT_GT(gap, in(55.0f));
    analysis::OddsCache cache;
    const hard::Evaluator duel(cache, gs, Side::A, hard::ChainWeights{});
    ASSERT_FALSE(duel.army());
    EXPECT_GT(duel.line_buckets(), 24);
    EXPECT_GE(static_cast<float>(duel.line_buckets()) * hard::Evaluator::bucket_mm(), gap);
    const std::array<int, 2> wounds{10, 10};
    const hard::Evaluation near = duel.at(0, wounds, in(50.0f), {in(1.0f), in(1.0f)});
    const hard::Evaluation far = duel.at(0, wounds, in(56.0f), {in(1.0f), in(1.0f)});
    EXPECT_NE(near.score, far.score);
    EXPECT_GT(near.score, far.score);  // the Intercessors want to close in (tempo)
    // The army's tables use the whole diagonal.
    const hard::Evaluator army(cache, side_a_move_units(), Side::A, hard::ChainWeights{});
    EXPECT_EQ(army.line_buckets(), 38);
}

// ==== The lookahead ====

TEST(M57_Lookahead, RolloutMovesAimAtTheRealTargetNotTheFirstEnemyUnit) {
    // D-067: in a rollout Intercessor Squad 3 moves relative to its target (the enemy unit in its reach with the most
    // models, a tie to the closest; D-010): Termagants 4, about 20" north of it, not Termagants 1 (side B's first
    // unit, M44's enemy_of), far in the north-west corner.
    GameState gs = side_a_move_units();
    for (const std::size_t i : {kSquad1, kSquad2, kGants2, kGants3}) gs.units[i].models.clear();
    place(gs.units[kSquad3], at(46, 18));
    place(gs.units[kGants1], at(4, 40));
    place(gs.units[kGants4], at(46, 40));
    clear_flags(gs);
    ASSERT_TRUE(std::holds_alternative<decision::MoveUnits>(gs.pending->kind));
    hard::HardContext hc;
    analysis::OddsCache cache;
    const hard::Evaluator eval(cache, gs, Side::A, hc.weights.chain, &hc.chains);
    ASSERT_TRUE(eval.army());
    hard::LookaheadWeights w = hc.weights.lookahead;
    w.rollouts = 2;
    std::vector<hard::RolloutTrace> traces;
    (void)hard::monte_carlo(eval, cache, gs, Side::A, hard::rollout_seed(gs), w, {}, &traces);
    ASSERT_EQ(traces.size(), 2u);
    const Unit& squad = gs.units[kSquad3];
    const Position from = centroid(squad);
    const Position t4 = centroid(gs.units[kGants4]);
    const Position t1 = centroid(gs.units[kGants1]);
    int moves = 0;
    for (const hard::RolloutTrace& t : traces) {
        for (const GameAction& a : t.actions) {
            const auto* p = std::get_if<action::ProposeModelMove>(&a);
            if (p == nullptr || p->path.empty()) continue;
            const Model* m = find_model(squad, p->model);
            if (m == nullptr) continue;
            const float dx = p->path.back().x - m->position.x;
            const float dy = p->path.back().y - m->position.y;
            if (std::hypot(dx, dy) < in(0.5f)) continue;
            EXPECT_GT(cosine(dx, dy, t4.x - from.x, t4.y - from.y), cosine(dx, dy, t1.x - from.x, t1.y - from.y));
            ++moves;
            break;  // the first model's move of the rollout
        }
    }
    EXPECT_GE(moves, 1);
}

TEST(M57_Lookahead, EveryEnemyUnitRepliesInTheRollouts) {
    // D-067: from side A's Shoot step of the strike scenario, each rollout plays the rest of A's turn and side B's
    // whole turn, in which every Termagant unit is given its move (09.02); the budget counts simulated actions.
    const GameState gs = strike_until([](const GameState& s) {
        return s.pending && s.pending->side == Side::A && std::holds_alternative<decision::Shoot>(s.pending->kind);
    });
    hard::HardContext hc;
    analysis::OddsCache cache;
    const hard::Evaluator eval(cache, gs, Side::A, hc.weights.chain, &hc.chains);
    hard::LookaheadWeights w = hc.weights.lookahead;
    w.rollouts = 2;
    std::vector<hard::RolloutTrace> traces;
    const hard::MonteCarloResult r =
        hard::monte_carlo(eval, cache, gs, Side::A, hard::rollout_seed(gs), w, {}, &traces);
    EXPECT_EQ(r.rollouts, 2u);
    EXPECT_LE(r.actions, w.budget_actions);
    ASSERT_EQ(traces.size(), 2u);
    for (const hard::RolloutTrace& t : traces) {
        std::set<std::uint32_t> moved;
        for (const GameAction& a : t.actions) {
            if (const auto* m = std::get_if<action::StartMove>(&a)) moved.insert(m->unit.v);
        }
        for (const std::size_t i : {kGants1, kGants2, kGants3, kGants4}) {
            const Unit* u = t.end.find_unit(gs.units[i].id);
            ASSERT_NE(u, nullptr);
            if (u->models.empty()) continue;  // destroyed in side A's turn of the rollout
            EXPECT_TRUE(moved.count(gs.units[i].id.v) == 1) << "Termagants " << i - kGants1 + 1;
        }
        // It stops at the start of side A's next turn (or the game's end).
        EXPECT_TRUE(t.end.result.has_value() || (t.end.flow.battle_round == 2 && turn_side(t.end) == Side::A));
    }
    // Deterministic: the same seed gives the same rollouts.
    std::vector<hard::RolloutTrace> again;
    (void)hard::monte_carlo(eval, cache, gs, Side::A, hard::rollout_seed(gs), w, {}, &again);
    ASSERT_EQ(again.size(), traces.size());
    for (std::size_t i = 0; i < traces.size(); ++i) EXPECT_EQ(again[i].actions, traces[i].actions);
}

// ==== Hard's army-valued choices ====

TEST(M57_Casualties, FrontAndRearAreMeasuredAgainstTheAttacker) {
    // D-067: V1's Shoot step with a second Intercessor Squad on the Termagants' east flank (now the closest enemy
    // unit). The first squad fires (two Termagants die). Hard's "front models first" order (as the Termagants) is
    // measured against the attacker, not the closest unit, and Hard answers from the army value: one of its
    // candidates, the best valued unless Normal's own order is within the margin.
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    const Position g = centroid(gs.units[kGants]);
    const UnitId flank = add_copy(gs, kMarines, Side::A, g);
    {
        // A column of five, 3" east of the Termagants' east edge.
        Unit& f = *gs.find_unit(flank);
        float east = 0.0f;
        for (const Model& m : gs.units[kGants].models) east = std::max(east, m.position.x + base_radius(m.base));
        for (std::size_t i = 0; i < f.models.size(); ++i) {
            const float r = base_radius(f.models[i].base);
            f.models[i].position = Position{east + in(3.0f) + r, g.y + (static_cast<float>(i) - 2.0f) * 2.0f * r, 0.0f};
        }
    }
    gs.pending = compute_decision(gs);
    const Unit& gants = gs.units[kGants];
    ASSERT_EQ(normal::closest_enemy(gs, gants)->id, flank);
    std::vector<ShotDeclaration> shots;
    for (std::size_t i = 0; i < 2; ++i) {
        shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, 0, 0, gants.id});
    }
    ScriptedDice dice({6, 6, 1, 1, 6, 6, 1, 1});  // two hits, two wounds, two failed saves
    (void)apply_action(gs, Side::A, action::Shoot{gs.units[kMarines].id, ShootingType::Normal, shots}, dice);
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    ASSERT_EQ(normal::attacking_enemy(gs, gs.units[kGants])->id, gs.units[kMarines].id);
    hard::HardContext hc;
    analysis::OddsCache cache;
    std::vector<hard::HardCasualtyCandidate> cands;
    const std::optional<PolicyResult> r = hard::hard_casualties(hc, cache, gs, Side::B, &cands);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(cands.size(), 4u);
    ASSERT_EQ(cands[1].text, "the front models first");
    const Unit& g2 = gs.units[kGants];
    const auto gap_to = [&](const Model& m, const Unit& e) { return *model_distance_to_unit(m, m.position, e); };
    const Model& first = *find_model(g2, cands[1].priority.front());
    for (const Model& m : g2.models) {
        EXPECT_LE(gap_to(first, gs.units[kMarines]), gap_to(m, gs.units[kMarines]) + 1e-3f);
    }
    // Against the flank squad (the closest unit) the order would have been another.
    const Unit& flank_unit = *gs.find_unit(flank);
    std::vector<std::pair<float, ModelId>> by_flank;
    for (const Model& m : g2.models) by_flank.emplace_back(gap_to(m, flank_unit), m.id);
    std::stable_sort(by_flank.begin(), by_flank.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.v < b.second.v;
    });
    std::vector<ModelId> flank_order;
    for (const auto& [gap, id] : by_flank) flank_order.push_back(id);
    EXPECT_NE(cands[1].priority, flank_order);
    // Hard's answer is one of its candidates, from the army value.
    const auto* chosen = std::get_if<action::OrderCasualties>(&r->action);
    ASSERT_NE(chosen, nullptr);
    double best = -1e9;
    std::optional<double> chosen_value;
    for (const hard::HardCasualtyCandidate& c : cands) {
        ASSERT_TRUE(c.value.has_value()) << c.text;
        best = std::max(best, *c.value);
        if (c.priority == chosen->priority) chosen_value = c.value;
    }
    ASSERT_TRUE(chosen_value.has_value());
    EXPECT_GE(*chosen_value, std::min(best, *cands[0].value) - 1e-12);
    EXPECT_GE(*chosen_value + hc.weights.margin + 1e-9, best);
    EXPECT_NE(r->reason.find("Hard:"), std::string::npos) << r->reason;
}

TEST(M57_Charge, TheCloseSquadDeclaresAndTheChargeIsWorthMoreThanHolding) {
    // D-067: V1's Charge step with the Intercessors 3" from the Termagants and a second pair of units added (an
    // Intercessor Squad 11" from a second Termagant unit, east). Hard values every declarable charge with the army
    // value (P from the 2D6 covering the gap to the closest eligible target, 11.02): the close squad declares, and
    // its charge is worth more than holding.
    using wh40k::test::Charger;
    using wh40k::test::ChargeLayout;
    GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{3.0f, 0.0f, 0});
    for (const std::size_t i : {kMarines, kGants}) {
        for (Model& m : gs.units[i].models) m.position.x -= in(6.0f);
    }
    const Position s = centroid(gs.units[kMarines]);
    const UnitId squad2 = add_copy(gs, kMarines, Side::A, Position{s.x + in(12.0f), s.y, 0.0f});
    const UnitId gants2 = add_copy(gs, kGants, Side::B, Position{s.x + in(12.0f), s.y + in(13.0f), 0.0f});
    clear_flags(gs);
    ASSERT_TRUE(std::holds_alternative<decision::Charge>(gs.pending->kind));
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    const GameAction close = action::DeclareCharge{gs.units[kMarines].id};
    ASSERT_TRUE(std::find(legal.begin(), legal.end(), close) != legal.end());
    ASSERT_NEAR(mm_to_inches(*unit_distance(gs.units[kMarines], gs.units[kGants])), 3.0f, 0.01f);
    ASSERT_GT(*unit_distance(*gs.find_unit(squad2), *gs.find_unit(gants2)), in(10.0f));
    analysis::OddsCache cache;
    const hard::Evaluator eval(cache, gs, Side::A, hard::ChainWeights{});
    ASSERT_TRUE(eval.army());
    const double p = analysis::p_charge_roll_covers(*unit_distance(gs.units[kMarines], gs.units[kGants]));
    const hard::Evaluator::ChargeValues cv = eval.charge_values(gs, gs.units[kMarines].id, gs.units[kGants].id, p);
    EXPECT_TRUE(cv.wanted);
    EXPECT_GT(cv.charge.score, cv.hold.score);
    EXPECT_GT(cv.fight.score, cv.hold.score);
    const BotChoice c = Bot(level(BotLevel::Hard)).decide(gs, Side::A);
    EXPECT_EQ(c.action, close) << c.reason;
    EXPECT_NE(c.reason.find("Hard:"), std::string::npos) << c.reason;
}

TEST(M57_Consolidation, ConsolidatesBackIntoTheFightByTheArmyValue) {
    // D-067: M41's Consolidate step (12.07, 12.08) with three Intercessors 2" back from the combat, and a second
    // Termagant unit in a corner (several units on side B). Hard values not moving and each consolidation with the
    // army value: consolidating brings them back into the fight, and the move commits without a revert.
    GameState gs = wh40k::test::consolidate_step_state();
    for (std::size_t i = 2; i < gs.units[kMarines].models.size(); ++i) {
        gs.units[kMarines].models[i].position.y -= in(2.0f);
    }
    ASSERT_TRUE(unit_coherency(gs.units[kMarines]).ok());
    (void)add_copy(gs, kGants, Side::B, at(4.0f, 21.0f));
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Consolidate>(gs.pending->kind));
    ASSERT_EQ(gs.pending->side, Side::A);
    Bot bot(level(BotLevel::Hard));
    const BotChoice c = bot.decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartConsolidation>(c.action)) << c.reason;
    EXPECT_NE(c.reason.find("Hard:"), std::string::npos) << c.reason;
    EXPECT_NE(c.reason.find("for not moving"), std::string::npos) << c.reason;
    for (const GameEvent& e : apply_action(gs, Side::A, c.action)) {
        EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e));
    }
    for (int guard = 0; guard < 20 && gs.pending && gs.pending->side == Side::A &&
                        std::holds_alternative<decision::MoveModels>(gs.pending->kind);
         ++guard) {
        const BotChoice m = bot.decide(gs, Side::A);
        ASSERT_FALSE(m.stopped) << m.reason;
        for (const GameEvent& e : apply_action(gs, Side::A, m.action)) {
            EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e));
        }
    }
    EXPECT_EQ(bot.fallbacks(), 0u);
}

// ==== Seeded Strike games ====

namespace {

// D-054 acceptance on the 44"x60" scenario with Hard on several units: a whole game finishes with no rejected action,
// no reverted move and no fallback. (Release arena: 200 seeds per seat, PLAN.md M57; the two tests below take two of
// the shortest games, over in battle round 2, to keep Debug ctest quick.)
void expect_clean_strike_game(BotLevel a, BotLevel b, std::uint64_t seed) {
    const GameRecord g = play_bot_game(strike_scenario(), seed, level(a), level(b));
    EXPECT_TRUE(g.finished) << "seed " << seed << (g.problems.empty() ? "" : ": " + g.problems.front());
    EXPECT_EQ(g.rejected, 0u) << "seed " << seed;
    EXPECT_EQ(g.reverted, 0u) << "seed " << seed;
    EXPECT_EQ(g.fallbacks, 0u) << "seed " << seed;
}

}  // namespace

TEST(M57_StrikeGames, HardAsTheSpaceMarinesPlaysACleanGame) {
    expect_clean_strike_game(BotLevel::Hard, BotLevel::Normal, 12);
}

TEST(M57_StrikeGames, HardAsTheTyranidsPlaysACleanGame) {
    expect_clean_strike_game(BotLevel::Normal, BotLevel::Hard, 12);
}

TEST(M57_StrikeGames, AFreshHardBotGivesThePlayedAnswerAndNeverPeeksAtDice) {
    // D-054: every Hard plan is a pure function of the state it starts from, so at every decision of a Strike game a
    // fresh Hard bot (no memo) answers as the playing bot does, and at every MoveUnits, Charge and casualty decision
    // so does one whose game dice alone differ. Hard against Hard (seed 3), from the start through side A's first turn
    // (side B's casualty decisions included) and side B's first unit's move, with the client's automatic steps; as
    // M44, a move's model moves are checked for its first two (a fresh bot replans each from the move's start). The
    // checking bots forget their memo and loop guard before every decision (Bot::forget) but keep their chain tables
    // (a cache that never changes a decision, M44), and the bots run the army path with a lighter lookahead (two
    // rollouts on two plans, the same code), to keep Debug ctest quick.
    BotConfig config = level(BotLevel::Hard);
    config.hard.lookahead.rollouts = 2;
    config.hard.lookahead.plans = 2;
    client::GameSession session = client::GameSession::new_game(strike_scenario(), 3);
    std::array<Bot, 2> bots{Bot(config), Bot(config)};
    Bot fresh_bot(config);
    Bot dice_bot(config);
    client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    autos.auto_allocate = false;
    autos.skip_empty_steps = true;
    int decisions = 0;
    int checked = 0;
    int dice_checked = 0;
    std::set<std::size_t> kinds;
    std::size_t model_moves = 0;
    int b_moves = 0;
    while (session.pending() && session.history().size() < 3000) {
        (void)client::run_auto_actions(session, autos);
        if (!session.pending()) break;
        const GameState gs = session.state();
        const bool b_move_units =
            turn_side(gs) == Side::B && std::holds_alternative<decision::MoveUnits>(gs.pending->kind);
        if (b_move_units && ++b_moves > 1) break;
        const Side side = gs.pending->side;
        const BotChoice played = bots[side_index(side)].decide(gs, side);
        const bool model_move = std::holds_alternative<decision::MoveModels>(gs.pending->kind);
        model_moves = model_move ? model_moves + 1 : 0;
        if (!model_move || model_moves <= 2) {
            fresh_bot.forget();
            const BotChoice fresh = fresh_bot.decide(gs, side);
            ASSERT_EQ(played.action, fresh.action) << "decision " << decisions << ": " << played.reason << " / "
                                                   << fresh.reason;
            kinds.insert(gs.pending->kind.index());
            ++checked;
        }
        if (std::holds_alternative<decision::MoveUnits>(gs.pending->kind) ||
            std::holds_alternative<decision::Charge>(gs.pending->kind) ||
            std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
            GameState other = gs;
            other.dice.seed ^= 0x9E3779B97F4A7C15ull;
            other.dice.position += 37;
            dice_bot.forget();
            const BotChoice dice = dice_bot.decide(other, side);
            ASSERT_EQ(played.action, dice.action) << "decision " << decisions << ": " << played.reason;
            ++dice_checked;
        }
        ASSERT_FALSE(played.stopped) << played.reason;
        ASSERT_TRUE(session.submit(played.action).ok()) << played.reason;
        ++decisions;
    }
    EXPECT_GT(decisions, 20);
    EXPECT_GT(checked, 12);
    EXPECT_GE(dice_checked, 4);  // the four MoveUnits at least
    EXPECT_GE(kinds.size(), 4u);
}
