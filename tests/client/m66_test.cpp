// M66 (D-074 part 3, D-075): oval bases in the client logic. Drawing helpers (outlines, offset rings, hit-testing,
// the turn's swept area), turn input building paths with facings (turn then move, else move then turn), move_plan
// keeping facings, the shaded area of an oval at a facing against the engine, the auto moves with an oval as mover and
// as target, the casualty order and step skipping with an oval, the grab offset, the wound display, the turn text, and
// round-only outputs unchanged (hashes of the outputs the M65 code gave on round-only states).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

#include "action_text.hpp"
#include "auto_charge_move.hpp"
#include "auto_fight_move.hpp"
#include "base_shape.hpp"
#include "casualty_order.hpp"
#include "casualty_picks.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/rules/fight.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "drop_fix.hpp"
#include "event_text.hpp"
#include "fight_move_skip.hpp"
#include "launch_options.hpp"
#include "move_plan.hpp"
#include "overlay_plan.hpp"
#include "reach_area.hpp"
#include "ring_outline.hpp"
#include "support/charge_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"
#include "timeline.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
namespace core = wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
namespace tf = wh40k::test::tyrannofex;

namespace {

const Base kOval = tf::kOval;

template <typename Done>
void play_until(GameState& gs, Done done) {
    wh40k::test::play_neutral_until(gs, done);
}

bool move_units(const GameState& g) { return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind); }

// The hand-placed Tyrannofex fixture at its Normal move (Side B first), the Tyrannofex at `facing`.
GameState tyrannofex_move(float facing = 0.0f) {
    GameState gs = tf::fixture(Side::B);
    tf::the_tyrannofex(gs).facing_deg = facing;
    start_game(gs);
    play_until(gs, move_units);
    apply_action(gs, Side::B, action::StartMove{gs.units[tf::kTyrannofex].id, MoveType::Normal, std::nullopt});
    return gs;
}

// Whether the engine takes the drop (validate_action) and the move can then end there (move_end_problems).
bool engine_accepts(const GameState& gs, const GameAction& move) {
    const Side side = gs.pending->side;
    if (!validate_action(gs, side, move).ok()) return false;
    GameState work = gs;
    apply_action(work, side, move);
    return move_end_problems(work).empty();
}

double point_to_ellipse(const BasePose& pose, const Position& p) {
    return oval_detail::point_ellipse_signed_distance(oval_detail::to_vec(p), oval_detail::ellipse_of(pose));
}

// Every move of an auto move is accepted in turn, the move can then end, and no model turned.
void expect_legal(const GameState& gs, const std::vector<GameAction>& moves, const std::string& label) {
    GameState work = gs;
    const Side side = gs.pending->side;
    for (const GameAction& a : moves) {
        const ValidationResult r = validate_action(work, side, a);
        ASSERT_TRUE(r.ok()) << label << ": " << (r.reasons.empty() ? std::string() : r.reasons.front());
        const auto* p = std::get_if<action::ProposeModelMove>(&a);
        ASSERT_NE(p, nullptr) << label;
        EXPECT_TRUE(p->facings.empty()) << label;  // the auto moves never turn a model
        const float before = find_model(work, p->model)->facing_deg;
        apply_action(work, side, a);
        EXPECT_EQ(find_model(work, p->model)->facing_deg, before) << label;
    }
    const std::vector<std::string> problems = move_end_problems(work);
    EXPECT_TRUE(problems.empty()) << label << ": " << (problems.empty() ? std::string() : problems.front());
}

namespace m66_probe {

using namespace wh40k;

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        const auto* c = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= c[i];
            h *= 1099511628211ull;
        }
    }
    void f(float v) {
        std::uint32_t u = 0;
        std::memcpy(&u, &v, sizeof u);
        bytes(&u, sizeof u);
    }
    void i(std::int64_t v) { bytes(&v, sizeof v); }
    void s(const std::string& v) {
        i(static_cast<std::int64_t>(v.size()));
        bytes(v.data(), v.size());
    }
    void p(const core::Position& v) {
        f(v.x);
        f(v.y);
        f(v.z);
    }
    void action(const core::GameAction& a) {
        nlohmann::json j;
        core::action::to_json(j, a);
        s(j.dump());
    }
};

struct Case {
    std::string name;
    core::GameState gs;
};

inline core::GameState started_move(core::GameState gs, core::UnitId unit, core::MoveType type,
                                    std::optional<core::FallBackMode> mode = std::nullopt) {
    core::apply_action(gs, gs.pending->side, core::action::StartMove{unit, type, mode});
    return gs;
}

inline core::GameState at_first_move_units(core::GameState gs) {
    core::start_game(gs);
    wh40k::test::play_neutral_until(gs, [](const core::GameState& g) {
        return g.pending && std::holds_alternative<core::decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// The states with a move in progress.
inline std::vector<Case> move_cases() {
    std::vector<Case> out;
    {
        core::GameState gs = at_first_move_units(wh40k::test::load_v1_scenario());
        const core::Side s = gs.pending->side;
        core::UnitId mover{};
        for (const core::Unit& u : gs.units) {
            if (u.side == s) {
                mover = u.id;
                break;
            }
        }
        out.push_back(Case{"v1_normal", started_move(gs, mover, core::MoveType::Normal)});
        out.push_back(Case{"v1_advance", started_move(gs, mover, core::MoveType::Advance)});
    }
    {
        const std::filesystem::path f = wh40k::test::repo_root() / "data/scenarios/strike_44x60.json";
        core::GameState gs = at_first_move_units(core::load_scenario(f, wh40k::test::repo_root()));
        const core::Side s = gs.pending->side;
        for (const core::Unit& u : gs.units) {
            if (u.side == s) {
                out.push_back(Case{"strike_normal_" + std::to_string(u.id.v), started_move(gs, u.id, core::MoveType::Normal)});
                break;
            }
        }
    }
    {
        namespace mf = wh40k::test::monsters;
        core::GameState gs = mf::at_movement(false, core::Side::B);
        out.push_back(Case{"monsters_tyrant_normal", started_move(gs, gs.units[mf::kTyrant].id, core::MoveType::Normal)});
        core::GameState eng = mf::at_movement(true, core::Side::A);
        out.push_back(Case{"monsters_fall_back",
                           started_move(eng, eng.units[mf::kLegionaries].id, core::MoveType::FallBack,
                                        core::FallBackMode::OrderedRetreat)});
    }
    for (const auto c : {wh40k::test::Charger::Intercessors, wh40k::test::Charger::Termagants}) {
        if (auto gs = wh40k::test::charge_move_state(c, wh40k::test::ChargeLayout{4.0f, 1.0f, 0}, 8)) {
            out.push_back(Case{std::string("charge_") + (c == wh40k::test::Charger::Intercessors ? "marines" : "gants"),
                               *gs});
        }
    }
    {
        core::GameState gs = wh40k::test::after_successful_charge();
        core::apply_action(gs, core::Side::A, core::action::EndChargeStep{});
        core::apply_action(gs, core::Side::A, core::action::StartPileIn{gs.units[0].id, {gs.units[1].id}});
        out.push_back(Case{"pile_in", gs});
    }
    return out;
}

inline void hash_spans(Fnv& h, const std::vector<client::ReachSpan>& spans) {
    h.i(static_cast<std::int64_t>(spans.size()));
    for (const client::ReachSpan& s : spans) {
        h.f(s.y);
        h.f(s.x0);
        h.f(s.x1);
    }
}

inline std::uint64_t region_hash(const core::GameState& gs, core::ModelId id) {
    Fnv h;
    const std::optional<core::MoveReach> reach = core::move_reach(gs, id);
    if (!reach) return h.h;
    const client::ReachRegion region = client::model_region(*reach);
    hash_spans(h, client::reach_spans(region, 1.7f));
    hash_spans(h, client::reach_spans(region, 0.9f));
    hash_spans(h, client::engaged_spans(region, 1.7f));
    const client::ReachRegion trimmed = client::trimmed_region(region);
    const client::ReachRegion fix = client::fix_region(region);
    const float span = reach->remaining_mm + 40.0f;
    for (int a = -20; a <= 20; ++a) {
        for (int b = -20; b <= 20; ++b) {
            const core::Position p{reach->from.x + span * static_cast<float>(a) / 20.0f,
                                   reach->from.y + span * static_cast<float>(b) / 20.0f, 0.0f};
            h.i(client::in_region(region, p) ? 1 : 0);
            h.i(client::in_region(trimmed, p) ? 1 : 0);
            const auto problem = client::drop_problem(fix, p);
            h.i(problem ? static_cast<int>(*problem) : -1);
        }
    }
    for (int a = -6; a <= 6; ++a) {
        for (int b = -6; b <= 6; ++b) {
            const core::Position drop{reach->from.x + span * static_cast<float>(a) / 6.0f,
                                      reach->from.y + span * static_cast<float>(b) / 6.0f, 0.0f};
            const std::optional<client::FixedDrop> fixed = client::fix_drop(fix, reach->from, drop);
            if (!fixed) {
                h.i(-1);
                continue;
            }
            h.p(fixed->to);
            h.i(static_cast<int>(fixed->problem));
            h.i(static_cast<int>(fixed->how));
        }
    }
    return h.h;
}

inline std::uint64_t rings_hash(const core::GameState& gs) {
    Fnv h;
    std::optional<core::ModelId> selected;
    if (gs.flow.move) selected = gs.find_unit(gs.flow.move->unit)->models.front().id;
    const std::vector<client::Ring> rings = client::plan_overlays(gs, selected, client::OverlayToggles{});
    client::PositionOverrides overrides;
    if (selected) {
        const core::Model* m = core::find_model(gs, *selected);
        overrides.emplace_back(*selected, core::Position{m->position.x + 10.0f, m->position.y + 5.0f, 0.0f});
    }
    const std::vector<client::Circle> circles = client::ring_circles(gs, rings, overrides);
    for (const client::Circle& c : circles) {
        h.p(c.centre);
        h.f(c.radius_mm);
        h.i(static_cast<int>(c.kind));
    }
    for (const client::Arc& a : client::outline_arcs(circles)) {
        h.p(a.centre);
        h.f(a.radius_mm);
        h.f(a.start_rad);
        h.f(a.end_rad);
        h.i(static_cast<int>(a.kind));
    }
    return h.h;
}

inline std::uint64_t plans_hash(const core::GameState& gs) {
    Fnv h;
    if (!gs.flow.move) return h.h;
    const core::Unit& u = *gs.find_unit(gs.flow.move->unit);
    for (const core::Model& m : u.models) {
        const core::Position to{m.position.x + 10.0f, m.position.y - 7.0f, 0.0f};
        h.action(client::extend_path(gs, m.id, to));
        if (const auto r = client::ruler(gs, m.id, to)) {
            h.f(r->moved_mm);
            h.f(r->max_mm);
            h.i(r->over ? 1 : 0);
        }
        h.s(client::action_label(gs, client::extend_path(gs, m.id, to)));
    }
    for (const auto& a : client::translate_unit(gs, u.id, 12.0f, -7.0f)) h.action(a);
    // The log text of a proposal.
    core::GameState work = gs;
    const core::Model& first = u.models.front();
    const core::GameAction move = client::extend_path(gs, first.id, core::Position{first.position.x + 3.0f,
                                                                                    first.position.y + 4.0f, 0.0f});
    if (core::validate_action(work, work.pending->side, move).ok()) {
        for (const core::GameEvent& e : core::apply_action(work, work.pending->side, move)) h.s(client::format_event(e));
    }
    return h.h;
}

inline std::uint64_t casualty_hash(const core::GameState& gs) {
    Fnv h;
    for (const core::Unit& u : gs.units) {
        for (const core::ModelId id : client::default_casualty_order(gs, u.id)) h.i(id.v);
        if (const auto r = client::default_coherency_removal(gs, u.id)) h.i(r->v);
        h.i(client::coherency_losses(core::placed_models(u)));
    }
    return h.h;
}

inline std::uint64_t auto_charge_hash() {
    Fnv h;
    using wh40k::test::Charger;
    for (const Charger c : {Charger::Intercessors, Charger::Termagants}) {
        for (const float gap : {3.0f, 6.0f}) {
            for (const float offset : {0.0f, 2.5f}) {
                for (const int ranks : {0, 2}) {
                    for (const int roll : {5, 9}) {
                        const auto gs = wh40k::test::charge_move_state(c, wh40k::test::ChargeLayout{gap, offset, ranks}, roll);
                        if (!gs) {
                            h.i(-7);
                            continue;
                        }
                        const client::AutoChargeMove m = client::auto_charge_move(*gs);
                        for (const auto& a : m.moves) h.action(a);
                        for (const auto& p : m.problems) h.s(p);
                        h.i(m.score.engaged);
                        h.i(m.score.within_1);
                        h.i(m.score.contacts);
                        h.f(m.score.total_gap_mm);
                    }
                }
            }
        }
    }
    return h.h;
}

inline std::uint64_t auto_fight_hash() {
    Fnv h;
    const auto add = [&](const core::GameState& gs) {
        const client::AutoFightMove m = client::auto_fight_move(gs);
        for (const auto& a : m.moves) h.action(a);
        for (const auto& p : m.problems) h.s(p);
        const client::FightMoveShape s = client::fight_move_shape(gs, gs.flow.move ? gs.flow.move->unit : core::UnitId{},
                                                                  gs.flow.move ? gs.flow.move->targets
                                                                               : std::vector<core::UnitId>{});
        h.i(s.engaged);
        h.i(s.in_contact);
        h.f(s.total_gap);
        h.i(s.links);
    };
    core::GameState pile = wh40k::test::after_successful_charge();
    core::apply_action(pile, core::Side::A, core::action::EndChargeStep{});
    const core::GameState before_pile = pile;
    core::apply_action(pile, core::Side::A, core::action::StartPileIn{pile.units[0].id, {pile.units[1].id}});
    add(pile);
    // The Termagants' pile-in too, and pile-ins moved 1-3" back (more room to move).
    for (const float back : {1.0f, 3.0f}) {
        core::GameState gs = wh40k::test::after_successful_charge();
        for (core::Model& m : gs.units[1].models) m.position.y += wh40k::test::in(back);
        core::apply_action(gs, core::Side::A, core::action::EndChargeStep{});
        const core::GameAction start = core::action::StartPileIn{gs.units[0].id, {gs.units[1].id}};
        h.i(client::fight_move_worthwhile(gs, core::Side::A, start) ? 1 : 0);
        if (core::validate_action(gs, core::Side::A, start).ok()) {
            core::apply_action(gs, core::Side::A, start);
            add(gs);
        }
    }
    // Step skipping on the pile-in decision.
    if (const auto skip = client::pointless_fight_step_skip(before_pile, core::legal_actions(before_pile, core::Side::A))) {
        h.action(*skip);
    } else {
        h.i(-3);
    }
    // The consolidation.
    core::GameState cons = wh40k::test::consolidate_step_state();
    for (const core::GameAction& a : core::legal_actions(cons, cons.pending->side)) {
        h.i(client::fight_move_worthwhile(cons, cons.pending->side, a) ? 1 : 0);
    }
    return h.h;
}

inline std::vector<std::pair<std::string, std::uint64_t>> hashes() {
    std::vector<std::pair<std::string, std::uint64_t>> out;
    for (const Case& c : move_cases()) {
        const core::Unit& u = *c.gs.find_unit(c.gs.flow.move->unit);
        Fnv regions;
        for (std::size_t i = 0; i < u.models.size(); i += 2) regions.i(static_cast<std::int64_t>(region_hash(c.gs, u.models[i].id)));
        // The whole unit's drag area, grabbed at its first model.
        hash_spans(regions, client::reach_spans(client::unit_region(c.gs, u.id, u.models.front().position), 2.0f));
        out.emplace_back(c.name + ".regions", regions.h);
        out.emplace_back(c.name + ".rings", rings_hash(c.gs));
        out.emplace_back(c.name + ".plans", plans_hash(c.gs));
        out.emplace_back(c.name + ".casualties", casualty_hash(c.gs));
    }
    out.emplace_back("auto_charge", auto_charge_hash());
    out.emplace_back("auto_fight", auto_fight_hash());
    return out;
}

}  // namespace m66_probe


}  // namespace

// ---- base_shape: outlines, rings, hit-testing ----

TEST(M66_BaseShape, HitTestingFollowsTheFacing) {
    const Position c = at(22.0f, 34.0f);
    for (const float f : {0.0f, 30.0f, 45.0f, 90.0f, -60.0f}) {
        const BasePose pose{c, kOval, f};
        const BaseAxes ax = base_axes(f);
        const auto along = [&](double u, double v) {
            return Position{c.x + static_cast<float>(u * ax.ux + v * ax.vx), c.y + static_cast<float>(u * ax.uy + v * ax.vy),
                            0.0f};
        };
        EXPECT_TRUE(base_contains(pose, along(59.0, 0.0))) << f;   // the major axis is 120 mm
        EXPECT_FALSE(base_contains(pose, along(61.0, 0.0))) << f;
        EXPECT_TRUE(base_contains(pose, along(0.0, -45.0))) << f;  // the minor axis is 92 mm
        EXPECT_FALSE(base_contains(pose, along(0.0, 47.0))) << f;
        EXPECT_TRUE(base_contains(pose, c));
    }
    // A point 55 mm east of the centre is on the base turned to 90 (its length along x), not at 0 (46 mm half width).
    const Position east{c.x + 55.0f, c.y, 0.0f};
    EXPECT_TRUE(base_contains(BasePose{c, kOval, 90.0f}, east));
    EXPECT_FALSE(base_contains(BasePose{c, kOval, 0.0f}, east));
    // The major axis at 45 runs north-west to south-east (D-074: (-sin f, cos f)).
    EXPECT_TRUE(base_contains(BasePose{c, kOval, 45.0f}, Position{c.x - 40.0f, c.y + 40.0f, 0.0f}));
    EXPECT_FALSE(base_contains(BasePose{c, kOval, 45.0f}, Position{c.x + 40.0f, c.y + 40.0f, 0.0f}));
    // A round base: the pre-M66 test.
    const Base round{BaseShape::Round, 32.0f, 0.0f};
    for (int i = 0; i < 200; ++i) {
        const Position p{c.x + static_cast<float>(i % 20) - 10.0f + 0.37f * static_cast<float>(i % 7),
                         c.y + static_cast<float>(i / 10) - 10.0f, 0.0f};
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        EXPECT_EQ(base_contains(BasePose{c, round, 0.0f}, p), dx * dx + dy * dy <= 16.0f * 16.0f);
    }
}

TEST(M66_BaseShape, TheOutlineIsTheEllipseAndTheRingItsOffsetCurve) {
    const Position c = at(10.0f, 10.0f);
    for (const float f : {0.0f, 45.0f, 90.0f, 17.5f}) {
        const BasePose pose{c, kOval, f};
        for (const Position& p : base_outline(pose, 64)) EXPECT_NEAR(point_to_ellipse(pose, p), 0.0, 1e-3) << f;
        const float d = core::kEngagementRangeMm;
        const std::vector<Position> ring = offset_outline(pose, d, 96);
        ASSERT_EQ(ring.size(), 96u);
        for (const Position& p : ring) {
            EXPECT_NEAR(point_to_ellipse(pose, p), static_cast<double>(d), 2e-3) << f;
            EXPECT_TRUE(offset_contains(pose, d + 0.01f, p));
            EXPECT_FALSE(offset_contains(pose, d - 0.05f, p));
        }
        // Its ends are a + d and b + d out along the axes...
        EXPECT_NEAR(core::centre_distance(c, ring[0]), 60.0f + d, 1e-3f);
        EXPECT_NEAR(core::centre_distance(c, ring[24]), 46.0f + d, 1e-3f);
        // ...and it is not that ellipse scaled: halfway round, the scaled ellipse's point is nearer the base than d.
        const BaseAxes ax = base_axes(f);
        const double t = std::numbers::pi / 4.0;
        const double su = (60.0 + d) * std::cos(t);
        const double sv = (46.0 + d) * std::sin(t);
        const Position scaled{c.x + static_cast<float>(su * ax.ux - sv * ax.vx), c.y + static_cast<float>(su * ax.uy - sv * ax.vy),
                              0.0f};
        EXPECT_LT(point_to_ellipse(pose, scaled), static_cast<double>(d) - 0.15);
    }
    // A round base's outline and ring are circles.
    const BasePose round{c, Base{BaseShape::Round, 32.0f, 0.0f}, 0.0f};
    for (const Position& p : offset_outline(round, 10.0f, 48)) EXPECT_NEAR(core::centre_distance(c, p), 26.0f, 1e-3f);
}

TEST(M66_BaseShape, TheTurnsSweptAreaHoldsTheBaseAtEveryFacingOfTheTurn) {
    const Position c = at(20.0f, 20.0f);
    for (const auto& [from, to] : {std::pair{0.0f, 60.0f}, std::pair{30.0f, -45.0f}, std::pair{80.0f, 100.0f}}) {
        const std::vector<Position> swept = turn_swept_outline(kOval, c, from, to, 360);
        ASSERT_EQ(swept.size(), 360u);
        const auto radius_at = [&](double deg) {  // the swept outline's radius in direction deg (sampled every degree)
            const int k = static_cast<int>(std::lround(deg)) % 360;
            return static_cast<double>(core::centre_distance(c, swept[static_cast<std::size_t>((k + 360) % 360)]));
        };
        const double delta = rotation_delta_deg(from, to);
        for (int s = 0; s <= 12; ++s) {
            const float f = static_cast<float>(from + delta * s / 12.0);
            for (const Position& p : base_outline(BasePose{c, kOval, f}, 360)) {
                const double deg = oval_detail::atan2_deg(p.y - c.y, p.x - c.x);
                // Within the swept radius in that direction (allowing for the one-degree sampling of the outline).
                EXPECT_LE(core::centre_distance(c, p), radius_at(deg < 0 ? deg + 360.0 : deg) + 0.6) << from << " " << to;
            }
        }
        // Where the major axis points during the turn, the swept radius is half the length.
        const double mid = 90.0 + from + delta / 2.0;
        EXPECT_NEAR(radius_at(mid < 0 ? mid + 360.0 : std::fmod(mid, 360.0)), 60.0, 0.05);
    }
}

// ---- move_plan: facings, turn then move, move then turn ----

TEST(M66_MovePlan, ExtendingATurnedModelsPathKeepsItsTurn) {
    GameState gs = tyrannofex_move();
    const ModelId t = tf::the_tyrannofex(gs).id;
    const Position s = tf::the_tyrannofex(gs).position;
    const action::ProposeModelMove first{t, {Position{s.x, s.y - 40.0f, 0.0f}}, {45.0f}};
    ASSERT_TRUE(validate_action(gs, Side::B, first).ok());
    apply_action(gs, Side::B, first);
    EXPECT_EQ(model_path_facings(gs, t), (std::vector<float>{0.0f, 45.0f}));
    const action::ProposeModelMove next = extend_path(gs, t, Position{s.x - 30.0f, s.y - 60.0f, 0.0f});
    EXPECT_EQ(next.path.size(), 2u);
    EXPECT_EQ(next.facings, (std::vector<float>{45.0f, 45.0f}));  // the turn is kept, the new line at 45
    ASSERT_TRUE(validate_action(gs, Side::B, next).ok());
    apply_action(gs, Side::B, next);
    EXPECT_EQ(tf::the_tyrannofex(gs).facing_deg, 45.0f);
    EXPECT_EQ(model_path_facings(gs, t), (std::vector<float>{0.0f, 45.0f, 45.0f}));
    // A round model's path has no facings, as before.
    GameState other = tf::fixture(Side::A);
    start_game(other);
    play_until(other, move_units);
    apply_action(other, Side::A, action::StartMove{other.units[tf::kLegionaries].id, MoveType::Normal, std::nullopt});
    const Model& l = other.units[tf::kLegionaries].models[0];
    EXPECT_TRUE(extend_path(other, l.id, Position{l.position.x, l.position.y - 20.0f, 0.0f}).facings.empty());
    EXPECT_EQ(model_path_facings(other, l.id), (std::vector<float>{0.0f}));
}

TEST(M66_MovePlan, TurnThenMoveFirstThenMoveThenTurn) {
    // In the open: turn then move.
    GameState gs = tyrannofex_move();
    const ModelId t = tf::the_tyrannofex(gs).id;
    const Position s = tf::the_tyrannofex(gs).position;
    const Position to{s.x + 30.0f, s.y - 30.0f, 0.0f};
    const TurnedDrop open = turned_drop(gs, t, to, 45.0f);
    EXPECT_EQ(open.how, TurnedDrop::How::TurnThenMove);
    EXPECT_EQ(open.action.path, std::vector<Position>{to});
    EXPECT_EQ(open.action.facings, std::vector<float>{45.0f});
    EXPECT_TRUE(validate_action(gs, Side::B, open.action).ok());

    // 1 mm from the west edge at facing 0 (half width 46 mm): turning there to 90 (half length 60 mm along x) would
    // cross the edge (03.01), so the model moves east first, then turns at the end.
    GameState edge = tf::fixture(Side::B);
    tf::the_tyrannofex(edge).position = Position{47.0f, in(34.0f), 0.0f};
    start_game(edge);
    play_until(edge, move_units);
    apply_action(edge, Side::B, action::StartMove{edge.units[tf::kTyrannofex].id, MoveType::Normal, std::nullopt});
    const Position east{47.0f + 40.0f, in(34.0f), 0.0f};
    const action::ProposeModelMove turn_first = extend_path_turned(edge, t, east, 90.0f, false);
    const ValidationResult refused = validate_action(edge, Side::B, turn_first);
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.reasons.front().find("while it turns"), std::string::npos) << refused.reasons.front();
    const TurnedDrop late = turned_drop(edge, t, east, 90.0f);
    EXPECT_EQ(late.how, TurnedDrop::How::MoveThenTurn);
    EXPECT_EQ(late.action.path, (std::vector<Position>{east, east}));
    EXPECT_EQ(late.action.facings, (std::vector<float>{0.0f, 90.0f}));
    ASSERT_TRUE(validate_action(edge, Side::B, late.action).ok());
    apply_action(edge, Side::B, late.action);
    EXPECT_EQ(tf::the_tyrannofex(edge).facing_deg, 90.0f);
    EXPECT_EQ(tf::the_tyrannofex(edge).position, east);
    // Refused both ways: turn then move, so the engine's reasons show.
    const Position nowhere{47.0f + 1.0f, in(34.0f), 0.0f};
    EXPECT_EQ(turned_drop(edge, t, nowhere, 60.0f).how, TurnedDrop::How::TurnThenMove);

    // A turn in place: the path is the start, with the new facing; nothing is moved.
    GameState in_place = tyrannofex_move();
    const TurnedDrop turn = turned_drop(in_place, t, s, 30.0f);
    EXPECT_EQ(turn.how, TurnedDrop::How::TurnInPlace);
    EXPECT_EQ(turn.action.path, std::vector<Position>{s});
    EXPECT_EQ(turn.action.facings, std::vector<float>{30.0f});
    ASSERT_TRUE(validate_action(in_place, Side::B, turn.action).ok());
    apply_action(in_place, Side::B, turn.action);
    EXPECT_EQ(tf::the_tyrannofex(in_place).position, s);
    EXPECT_EQ(tf::the_tyrannofex(in_place).facing_deg, 30.0f);
    EXPECT_EQ(ruler(in_place, t)->moved_mm, 0.0f);
    EXPECT_FLOAT_EQ(ruler(in_place, t)->turned_deg, 30.0f);
    // The same footprint (180 degrees on) is no turn.
    EXPECT_EQ(turned_drop(tyrannofex_move(), t, to, 180.0f).how, TurnedDrop::How::NoTurn);
}

// ---- reach_area and drop_fix with shapes ----

TEST(M66_ReachArea, AnOvalsShadedAreaAtAFacingAgreesWithTheEngine) {
    GameState gs = tyrannofex_move();
    const ModelId t = tf::the_tyrannofex(gs).id;
    // D-074 part 3: in_region is exact; reach_spans finds the shapes' edges by bisection, within the documented band.
    constexpr float kBandMm = 1.0f;
    for (const float facing : {30.0f, 90.0f}) {
        const std::optional<MoveReach> reach = move_reach(gs, t, facing);
        ASSERT_TRUE(reach);
        const ReachRegion region = model_region(*reach);
        ASSERT_TRUE(region.has_shapes());
        const std::vector<ReachSpan> spans = reach_spans(region, 2.0f);
        ASSERT_FALSE(spans.empty());
        int inside = 0;
        int outside = 0;
        int mismatches = 0;
        std::size_t row = 0;
        for (std::size_t i = 0; i < spans.size(); i = row) {
            const float y = spans[i].y;
            std::vector<ReachSpan> on_row;
            for (row = i; row < spans.size() && spans[row].y == y; ++row) on_row.push_back(spans[row]);
            if ((i / 3) % 7 != 0) continue;  // a sample of the rows
            for (float x = reach->from.x - reach->remaining_mm - 10.0f; x <= reach->from.x + reach->remaining_mm + 10.0f;
                 x += 3.0f) {
                const Position p{x, y, 0.0f};
                const GameAction move = action::ProposeModelMove{t, {p}, {facing}};
                const bool accepted = engine_accepts(gs, move);
                if (in_region(region, p) != accepted) ++mismatches;
                bool shaded = false;
                bool near_edge = false;
                for (const ReachSpan& s : on_row) {
                    shaded = shaded || (x >= s.x0 && x <= s.x1);
                    near_edge = near_edge || std::fabs(x - s.x0) < kBandMm || std::fabs(x - s.x1) < kBandMm;
                }
                if (near_edge) continue;
                if (shaded) {
                    ++inside;
                    EXPECT_TRUE(accepted) << "shaded but refused: facing " << facing << " at " << x << ", " << y;
                } else {
                    ++outside;
                    EXPECT_FALSE(accepted) << "left out but accepted: facing " << facing << " at " << x << ", " << y;
                }
            }
        }
        EXPECT_EQ(mismatches, 0) << facing;
        EXPECT_GT(inside, 100) << facing;
        EXPECT_GT(outside, 100) << facing;
    }
}

TEST(M66_ReachArea, ARoundModelsAreaRoundAnOvalAgreesWithTheEngine) {
    // A Legionary's Normal move: the Tyrannofex's Engagement Range is an oval shape, and so is its base as a blocker.
    GameState gs = tf::fixture(Side::A);
    tf::the_tyrannofex(gs).facing_deg = 60.0f;
    start_game(gs);
    play_until(gs, move_units);
    apply_action(gs, Side::A, action::StartMove{gs.units[tf::kLegionaries].id, MoveType::Normal, std::nullopt});
    const Model& l = gs.units[tf::kLegionaries].models[5];
    const std::optional<MoveReach> reach = move_reach(gs, l.id);
    ASSERT_TRUE(reach);
    const ReachRegion region = model_region(*reach);
    ASSERT_TRUE(region.has_shapes());
    EXPECT_FALSE(region.outside_shapes.empty());
    EXPECT_FALSE(region.outside_shape_shadows.empty());
    const ReachRegion trimmed = trimmed_region(region);
    // One model of ten moved alone breaks the unit's coherency, which the area doesn't promise (D-040): the engine's
    // verdict here is validate_action and ending unengaged (09.05).
    const auto accepts = [&](const Position& p) {
        const GameAction move = action::ProposeModelMove{l.id, {p}};
        if (!validate_action(gs, Side::A, move).ok()) return false;
        GameState work = gs;
        apply_action(work, Side::A, move);
        const Model& moved = *find_model(work, l.id);
        return enemy_units_engaged_with_model(work, work.units[tf::kLegionaries], moved, moved.position).empty();
    };
    int mismatches = 0;
    int accepted_count = 0;
    for (int a = -30; a <= 30; ++a) {
        for (int b = -30; b <= 30; ++b) {
            const Position p{reach->from.x + 6.0f * static_cast<float>(a), reach->from.y + 6.0f * static_cast<float>(b), 0.0f};
            const bool accepted = accepts(p);
            accepted_count += accepted ? 1 : 0;
            if (in_region(region, p) != accepted) ++mismatches;
            EXPECT_EQ(in_region(trimmed, p), in_region(region, p));  // M54's shortcut, with shapes
        }
    }
    EXPECT_EQ(mismatches, 0);
    EXPECT_GT(accepted_count, 100);
}

TEST(M66_DropFix, AnOvalsRefusedDropGoesToASpotTheEngineTakes) {
    GameState gs = tyrannofex_move();
    const Model& t = tf::the_tyrannofex(gs);
    int fixed_count = 0;
    for (const float facing : {0.0f, 45.0f, 90.0f}) {
        const ReachRegion fix = fix_region(model_region(*move_reach(gs, t.id, facing)));
        // Drops onto the Legionaries' rank, beyond the move and off the edge of the Screamer-killer.
        for (int k = 0; k < 10; ++k) {
            const Position drop = gs.units[tf::kLegionaries].models[static_cast<std::size_t>(k)].position;
            const std::optional<FixedDrop> fixed = fix_drop(fix, t.position, drop);
            if (!fixed) continue;
            ++fixed_count;
            EXPECT_TRUE(in_region(fix, fixed->to)) << facing;
            const ValidationResult r =
                validate_action(gs, Side::B, action::ProposeModelMove{t.id, {fixed->to}, {facing}});
            EXPECT_TRUE(r.ok()) << facing << ": " << (r.reasons.empty() ? std::string() : r.reasons.front());
        }
    }
    EXPECT_GT(fixed_count, 10);
    // A round model's drop onto the Tyrannofex goes next to it.
    GameState a = tf::fixture(Side::A);
    tf::the_tyrannofex(a).position = at(22.0f, 32.0f);  // about 3" from the rank: not engaged
    tf::the_tyrannofex(a).facing_deg = 45.0f;
    start_game(a);
    play_until(a, move_units);
    apply_action(a, Side::A, action::StartMove{a.units[tf::kLegionaries].id, MoveType::Advance, std::nullopt});
    const Model& l = a.units[tf::kLegionaries].models[4];
    const ReachRegion fix = fix_region(model_region(*move_reach(a, l.id)));
    const Position on_it{tf::the_tyrannofex(a).position.x + 20.0f, tf::the_tyrannofex(a).position.y - 30.0f, 0.0f};
    const std::optional<FixedDrop> fixed = fix_drop(fix, l.position, on_it);
    ASSERT_TRUE(fixed);
    // Its line runs into the Tyrannofex first (D-044, 03.01): as far as it can go along the drag, short of the oval.
    EXPECT_EQ(fixed->problem, DropProblem::ThroughEnemyModel);
    EXPECT_EQ(fixed->how, FixedDrop::How::AlongDrag);
    EXPECT_TRUE(in_region(fix, fixed->to));
    EXPECT_LT(base_distance(BasePose{fixed->to, l.base, 0.0f}, pose_of(tf::the_tyrannofex(a))), 1.0f);
    EXPECT_TRUE(validate_action(a, Side::A, action::ProposeModelMove{l.id, {fixed->to}}).ok());
}

// ---- the auto moves (D-050, D-056) with an oval mover and an oval target ----

TEST(M66_AutoMoves, TheTyrannofexChargesLegallyAtEveryFacing) {
    int planned = 0;
    for (const float facing : {0.0f, 45.0f, 90.0f}) {
        for (const int roll : {6, 9, 12}) {
            GameState gs = tf::fixture(Side::B);
            tf::the_tyrannofex(gs).facing_deg = facing;
            tf::the_tyrannofex(gs).position = at(22.0f, tf::kRankY + 5.5f);
            start_game(gs);
            play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind); });
            const auto faces = wh40k::test::faces_for(roll);
            ScriptedDice dice{faces[0], faces[1]};
            apply_action(gs, Side::B, action::DeclareCharge{gs.units[tf::kTyrannofex].id}, dice);
            const GameAction start = action::StartChargeMove{gs.units[tf::kTyrannofex].id, {gs.units[tf::kLegionaries].id}};
            if (!validate_action(gs, Side::B, start).ok()) continue;
            apply_action(gs, Side::B, start);
            const AutoChargeMove m = auto_charge_move(gs);
            if (!m.ok()) continue;
            ++planned;
            expect_legal(gs, m.moves, "facing " + std::to_string(facing) + " roll " + std::to_string(roll));
            EXPECT_GE(m.score.engaged, 1);
        }
    }
    EXPECT_GE(planned, 6);
}

TEST(M66_AutoMoves, UnitsChargeTheTyrannofexLegallyAtEveryFacing) {
    int planned = 0;
    for (const float facing : {0.0f, 45.0f, 90.0f, -30.0f}) {
        for (const int roll : {6, 8, 12}) {
            GameState gs = tf::fixture(Side::A);
            tf::the_tyrannofex(gs).facing_deg = facing;
            start_game(gs);
            play_until(gs, [](const GameState& g) {
                return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::A;
            });
            const auto faces = wh40k::test::faces_for(roll);
            ScriptedDice dice{faces[0], faces[1]};
            apply_action(gs, Side::A, action::DeclareCharge{gs.units[tf::kLegionaries].id}, dice);
            const GameAction start = action::StartChargeMove{gs.units[tf::kLegionaries].id, {gs.units[tf::kTyrannofex].id}};
            if (!validate_action(gs, Side::A, start).ok()) continue;
            apply_action(gs, Side::A, start);
            const AutoChargeMove m = auto_charge_move(gs);
            if (!m.ok()) continue;
            ++planned;
            expect_legal(gs, m.moves, "facing " + std::to_string(facing) + " roll " + std::to_string(roll));
            EXPECT_GE(m.score.contacts, 1);  // D-075: contact found round the big base
        }
    }
    EXPECT_GE(planned, 8);
}

TEST(M66_AutoMoves, PileInsWithTheTyrannofexAsMoverAndAsTargetAreLegal) {
    int planned = 0;
    for (const float facing : {0.0f, 45.0f, 90.0f}) {
        // The Tyrannofex piles in: 1.5" from the rank after a charge of its own.
        for (const bool tyrannofex_moves : {true, false}) {
            GameState gs = tf::fixture(tyrannofex_moves ? Side::B : Side::A);
            Model& t = tf::the_tyrannofex(gs);
            t.facing_deg = facing;
            const BaseExtents ext = base_extents(kOval, facing);
            t.position = Position{in(22.0f), in(tf::kRankY) + 16.0f + ext.half_y_mm + in(1.5f), 0.0f};
            start_game(gs);
            play_until(gs, [](const GameState& g) {
                return g.pending && std::holds_alternative<decision::PileIn>(g.pending->kind);
            });
            if (!gs.pending || !std::holds_alternative<decision::PileIn>(gs.pending->kind)) continue;
            const UnitId mover = tyrannofex_moves ? gs.units[tf::kTyrannofex].id : gs.units[tf::kLegionaries].id;
            const UnitId target = tyrannofex_moves ? gs.units[tf::kLegionaries].id : gs.units[tf::kTyrannofex].id;
            const GameAction start = action::StartPileIn{mover, {target}};
            if (!validate_action(gs, gs.pending->side, start).ok()) continue;
            EXPECT_TRUE(fight_move_worthwhile(gs, gs.pending->side, start));
            apply_action(gs, gs.pending->side, start);
            const AutoFightMove m = auto_fight_move(gs);
            ASSERT_TRUE(m.ok()) << (m.problems.empty() ? std::string() : m.problems.front());
            ++planned;
            expect_legal(gs, m.moves, "facing " + std::to_string(facing) + (tyrannofex_moves ? " mover" : " target"));
        }
    }
    EXPECT_GE(planned, 4);
}

TEST(M66_AutoMoves, ContactSamplingIsFinerRoundABigBase) {
    // D-075: 72 directions round any base up to 32 mm (as before), more round a bigger one.
    EXPECT_EQ(contact_direction_count(Base{BaseShape::Round, 25.0f, 0.0f}), 72);
    EXPECT_EQ(contact_direction_count(Base{BaseShape::Round, 28.5f, 0.0f}), 72);
    EXPECT_EQ(contact_direction_count(Base{BaseShape::Round, 32.0f, 0.0f}), 72);
    EXPECT_EQ(contact_direction_count(Base{BaseShape::Round, 40.0f, 0.0f}), 144);
    EXPECT_EQ(contact_direction_count(Base{BaseShape::Round, 90.0f, 0.0f}), 216);
    const double perimeter = base_perimeter_mm(kOval);
    EXPECT_NEAR(perimeter, 334.46, 0.01);  // Ramanujan's second formula for a 92 x 120 mm ellipse
    EXPECT_EQ(contact_direction_count(kOval), 288);
    // The spacing along the base is no worse than 72 round a 32 mm base.
    for (const Base& b : {Base{BaseShape::Round, 40.0f, 0.0f}, Base{BaseShape::Round, 90.0f, 0.0f}, kOval}) {
        EXPECT_LE(base_perimeter_mm(b) / contact_direction_count(b), std::numbers::pi * 32.0 / 72.0 + 1e-9);
    }
    // The 72 directions are the old table; 288 include it (within float rounding).
    const std::vector<UnitVector> d72 = direction_table(72);
    const std::vector<UnitVector> d288 = direction_table(288);
    ASSERT_EQ(d72.size(), 72u);
    ASSERT_EQ(d288.size(), 288u);
    for (std::size_t k = 0; k < 72; ++k) {
        EXPECT_EQ(d72[k].x, kDirections72[k].x);
        EXPECT_EQ(d72[k].y, kDirections72[k].y);
        EXPECT_NEAR(d288[4 * k].x, kDirections72[k].x, 1e-6f);
        EXPECT_NEAR(d288[4 * k].y, kDirections72[k].y, 1e-6f);
    }
    // The contact ring round a round and an oval base is the oval's offset curve: every spot in base contact, either way
    // round; a round pair's is the old closed form.
    const Base round32{BaseShape::Round, 32.0f, 0.0f};
    for (const float f : {0.0f, 45.0f, 90.0f}) {
        const BasePose oval_target{at(20.0f, 20.0f), kOval, f};
        for (const auto& spot : contact_ring(oval_target, round32, 0.0f, 288, 0.005f)) {
            ASSERT_TRUE(spot);
            const float gap = base_distance(BasePose{*spot, round32, 0.0f}, oval_target);
            EXPECT_TRUE(in_base_contact(gap)) << gap;
            EXPECT_GE(gap, 0.004f);
        }
        const BasePose round_target{at(20.0f, 20.0f), round32, 0.0f};
        for (const auto& spot : contact_ring(round_target, kOval, f, 72, 0.005f)) {
            ASSERT_TRUE(spot);
            const float gap = base_distance(BasePose{*spot, kOval, f}, round_target);
            EXPECT_TRUE(in_base_contact(gap)) << gap;
            EXPECT_GE(gap, 0.004f);
        }
    }
    const std::vector<std::optional<Position>> round_ring =
        contact_ring(BasePose{at(20.0f, 20.0f), round32, 0.0f}, round32, 0.0f, 72, 0.005f);
    for (std::size_t k = 0; k < 72; ++k) {
        const float r = 16.0f + 16.0f + 0.005f;
        EXPECT_EQ(*round_ring[k], (Position{at(20.0f, 20.0f).x + r * kDirections72[k].x, at(20.0f, 20.0f).y + r * kDirections72[k].y, 0.0f}));
    }
    // A contact spot round the oval found by bisection (two ovals use it) is in base contact with it, at every facing.
    for (const float f : {0.0f, 45.0f, 90.0f}) {
        const BasePose target{at(20.0f, 20.0f), kOval, f};
        for (const UnitVector& d : d288) {
            const auto spot = ring_spot(target, Base{BaseShape::Round, 32.0f, 0.0f}, 0.0f, d, 0.005f);
            ASSERT_TRUE(spot);
            const float gap = base_distance(BasePose{*spot, Base{BaseShape::Round, 32.0f, 0.0f}, 0.0f}, target);
            EXPECT_TRUE(in_base_contact(gap)) << gap;
            EXPECT_GE(gap, 0.004f);
        }
    }
}

// ---- casualties and step skipping ----

TEST(M66_Casualties, TheDefaultOrderAndThePicksWorkWithAnOval) {
    // Every frame of the M65 replay (the Tyrannofex shoots and fights the Legionaries): the default order of each unit
    // is every model once, and the casualty picks start from it.
    const Replay replay = load_replay(std::filesystem::path(WH40K_REPLAY_DIR) / "m65_oval_tyrannofex.json");
    const Timeline timeline = Timeline::from_replay(replay, wh40k::test::repo_root(), "m65");
    ASSERT_TRUE(timeline.error().empty());
    int picks_seen = 0;
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        const GameState& gs = timeline.frame(i).state;
        for (const Unit& u : gs.units) {
            std::vector<ModelId> order = default_casualty_order(gs, u.id);
            std::vector<ModelId> ids;
            for (const Model& m : u.models) ids.push_back(m.id);
            std::sort(order.begin(), order.end(), [](ModelId a, ModelId b) { return a.v < b.v; });
            std::sort(ids.begin(), ids.end(), [](ModelId a, ModelId b) { return a.v < b.v; });
            EXPECT_EQ(order, ids);
        }
        if (gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
            const UnitId target = std::get<decision::OrderCasualties>(gs.pending->kind).target;
            CasualtyPicks picks(gs, target);
            EXPECT_EQ(picks.action().priority.size(), gs.find_unit(target)->models.size());
            ++picks_seen;
        }
    }
    EXPECT_GT(picks_seen, 0);
    // Coherency losses with an oval: two Tyrannofexes 2" apart end to end are in coherency, side by side 2.2" too far.
    std::vector<PosedModel> two{PosedModel{ModelId{1}, at(10.0f, 10.0f), kOval, 0.0f},
                                PosedModel{ModelId{2}, Position{in(10.0f), in(10.0f) + 120.0f + 50.0f, 0.0f}, kOval, 0.0f},
                                PosedModel{ModelId{3}, Position{in(10.0f) + 92.0f + 50.0f, in(10.0f), 0.0f}, kOval, 0.0f}};
    EXPECT_EQ(coherency_losses(two), 0);
    two[2].facing_deg = 90.0f;  // now 120 mm across: 3 mm short of 2" ... 28 mm more than 2" from the first
    EXPECT_EQ(coherency_losses(two), check_coherency(std::span<const PosedModel>(two)).ok() ? 0 : 1);
}

TEST(M66_FightMoveSkip, AnOvalInBaseContactIsNotWorthMoving) {
    // D-048: the Tyrannofex 0.5 mm from a Legionary counts as in base contact in a pile-in, so it is not worth moving;
    // 2" away it is.
    for (const float gap : {0.5f, in(2.0f)}) {
        GameState gs = tf::fixture(Side::B);
        Model& t = tf::the_tyrannofex(gs);
        t.facing_deg = 90.0f;  // 46 mm half extent along y
        t.position = Position{gs.units[tf::kLegionaries].models[5].position.x, in(tf::kRankY) + 16.0f + 46.0f + gap, 0.0f};
        start_game(gs);
        play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::PileIn>(g.pending->kind); });
        ASSERT_TRUE(gs.pending && std::holds_alternative<decision::PileIn>(gs.pending->kind));
        const GameAction start = action::StartPileIn{gs.units[tf::kTyrannofex].id, {gs.units[tf::kLegionaries].id}};
        if (!validate_action(gs, gs.pending->side, start).ok()) {
            ADD_FAILURE() << "no pile-in at gap " << gap;
            continue;
        }
        EXPECT_EQ(fight_move_worthwhile(gs, gs.pending->side, start), gap > 1.0f) << gap;
    }
}

// ---- rings ----

TEST(M66_Rings, AnOvalsRingsAreItsOffsetOutline) {
    GameState gs = tyrannofex_move(30.0f);
    const Model& t = tf::the_tyrannofex(gs);
    const std::vector<Ring> rings = plan_overlays(gs, t.id, OverlayToggles{});
    const Position preview{t.position.x + 20.0f, t.position.y - 10.0f, 0.0f};
    const std::vector<Circle> circles = ring_circles(gs, rings, {{t.id, preview}}, {{t.id, 75.0f}});
    bool coherency = false;
    bool movement = false;
    for (std::size_t k = 0; k < rings.size(); ++k) {
        if (rings[k].model != t.id) continue;
        const Circle& c = circles[k];
        if (rings[k].kind == RingKind::Coherency) {
            coherency = true;
            ASSERT_TRUE(c.oval);
            EXPECT_EQ(c.oval->facing_deg, 75.0f);  // turns with the drag
            EXPECT_EQ(c.centre, preview);
            EXPECT_EQ(c.radius_mm, circumscribed_radius(kOval) + kCoherencyNearMm);
            // Inside it exactly when within 2" of the base.
            for (int a = 0; a < 72; ++a) {
                const float r = 60.0f + 50.0f + static_cast<float>(a % 9) * 3.0f;
                const float ang = static_cast<float>(a) * 5.0f * std::numbers::pi_v<float> / 180.0f;
                const Position p{preview.x + r * std::cos(ang), preview.y + r * std::sin(ang), 0.0f};
                const double d = point_to_ellipse(BasePose{preview, kOval, 75.0f}, p);
                if (std::fabs(d - kCoherencyNearMm) > 0.01) {
                    EXPECT_EQ(ring_contains(c, p), d <= kCoherencyNearMm);
                }
            }
        }
        if (rings[k].kind == RingKind::MoveRange) {
            movement = true;
            EXPECT_FALSE(c.oval);  // a circle: a model may turn freely (03.01)
        }
    }
    EXPECT_TRUE(coherency);
    EXPECT_TRUE(movement);
    // Alone, a whole closed outline; with a circle of the same kind over half of it, only the rest, and the circle's
    // arcs leave out what is inside the oval ring.
    const Circle oval_ring{at(20.0f, 20.0f), 60.0f + 50.8f, RingKind::Engagement, OvalRing{kOval, 30.0f, 50.8f}};
    const std::vector<RingPath> alone = outline_paths({oval_ring});
    ASSERT_EQ(alone.size(), 1u);
    EXPECT_EQ(alone[0].points.front(), alone[0].points.back());
    EXPECT_TRUE(outline_arcs({oval_ring}).empty());
    const Circle disc{Position{at(20.0f, 20.0f).x + 90.0f, at(20.0f, 20.0f).y, 0.0f}, 80.0f, RingKind::Engagement};
    const std::vector<RingPath> parts = outline_paths({oval_ring, disc});
    ASSERT_FALSE(parts.empty());
    for (const RingPath& p : parts) {
        for (const Position& q : p.points) EXPECT_GE(core::centre_distance(q, disc.centre), disc.radius_mm - 1e-3f);
    }
    const std::vector<Arc> arcs = outline_arcs({oval_ring, disc});
    ASSERT_FALSE(arcs.empty());
    for (const Arc& a : arcs) {
        const float mid = 0.5f * (a.start_rad + a.end_rad);
        const Position q{a.centre.x + a.radius_mm * std::cos(mid), a.centre.y + a.radius_mm * std::sin(mid), 0.0f};
        EXPECT_FALSE(ring_contains(oval_ring, q));
    }
    // A different kind doesn't merge.
    EXPECT_EQ(outline_arcs({oval_ring, Circle{disc.centre, disc.radius_mm, RingKind::Coherency}}).size(), 1u);
}

// ---- text, the grab offset, the wound display, the --turn flag ----

TEST(M66_Text, TheLogActionAndMoveTextSayWhenAModelTurned) {
    const Position a{0.0f, 0.0f, 0.0f};
    const Position b{0.0f, in(3.0f), 0.0f};
    EXPECT_EQ(describe_path({a, b}, {}), "moved 3.00\"");
    EXPECT_EQ(describe_path({a, b}, {0.0f, 45.0f}), "turned 45° then moved 3.00\"");
    EXPECT_EQ(describe_path({a, b, b}, {0.0f, 0.0f, 45.0f}), "moved 3.00\" then turned 45°");
    EXPECT_EQ(describe_path({a, a}, {10.0f, -20.0f}), "turned 30°");
    EXPECT_EQ(describe_path({a, b, b, Position{in(1.0f), in(3.0f), 0.0f}}, {0.0f, 0.0f, 90.0f, 90.0f}),
              "turned 90° and moved 4.00\"");
    EXPECT_EQ(describe_path({a, b}, {0.0f, 45.0f}, true), "turns 45° then moves 3.00\"");
    // 0 -> 170 is the same footprint as -10: a 10-degree turn (D-074 part 2).
    EXPECT_EQ(describe_path({a, b}, {0.0f, 170.0f}), "turned 10° then moved 3.00\"");

    GameState gs = tyrannofex_move();
    const ModelId t = tf::the_tyrannofex(gs).id;
    const Position s = tf::the_tyrannofex(gs).position;
    const action::ProposeModelMove turned{t, {Position{s.x, s.y - in(3.0f), 0.0f}}, {45.0f}};
    EXPECT_EQ(action_label(gs, turned), "Move model " + std::to_string(t.v) + " (turns 45° then moves 3.00\")");
    EXPECT_EQ(action_label(gs, action::ProposeModelMove{t, {Position{s.x, s.y - in(3.0f), 0.0f}}}),
              "Move model " + std::to_string(t.v));
    std::vector<GameEvent> events = apply_action(gs, Side::B, turned);
    std::string text;
    for (const GameEvent& e : events) {
        if (std::holds_alternative<event::ModelMoveProposed>(e)) text = format_event(e);
    }
    EXPECT_NE(text.find("model " + std::to_string(t.v) + " turns 45° then moves 3.00\""), std::string::npos) << text;
    const std::optional<Ruler> r = ruler(gs, t, Position{s.x + 10.0f, s.y, 0.0f}, 90.0f);
    ASSERT_TRUE(r);
    EXPECT_FLOAT_EQ(r->turned_deg, 90.0f);  // 45 so far, then 45 more
}

TEST(M66_Drag, TheDraggedModelKeepsItsGrabOffset) {
    // D-075: grabbed 40 mm east and 10 mm north of its centre, the model's centre stays that far from the cursor.
    const Position model{500.0f, 600.0f, 0.0f};
    const Position grab{540.0f, 610.0f, 0.0f};
    EXPECT_EQ(drag_drop(model, grab, grab), model);  // no jump when the drag starts
    EXPECT_EQ(drag_drop(model, grab, Position{600.0f, 700.0f, 0.0f}), (Position{560.0f, 690.0f, 0.0f}));
    // Grabbed at its centre (the old behaviour), it follows the cursor exactly.
    EXPECT_EQ(drag_drop(model, model, Position{612.5f, 588.0f, 0.0f}), (Position{612.5f, 588.0f, 0.0f}));
}

TEST(M66_Wounds, PipsUpToWSixABarAndANumberPastIt) {
    EXPECT_EQ(wound_display(1, 1).kind, WoundDisplay::Kind::None);    // unhurt
    EXPECT_EQ(wound_display(16, 16).kind, WoundDisplay::Kind::None);  // unhurt Tyrannofex
    const WoundDisplay pips = wound_display(2, 3);
    EXPECT_EQ(pips.kind, WoundDisplay::Kind::Pips);
    EXPECT_EQ(pips.left, 2);
    EXPECT_EQ(pips.total, 3);
    EXPECT_EQ(wound_display(1, 6).kind, WoundDisplay::Kind::Pips);  // W 6: still pips
    const WoundDisplay bar = wound_display(11, 16);
    EXPECT_EQ(bar.kind, WoundDisplay::Kind::Bar);
    EXPECT_EQ(bar.text, "11/16");
    EXPECT_FLOAT_EQ(bar.fraction(), 11.0f / 16.0f);
    EXPECT_EQ(wound_display(7, 10).text, "7/10");  // the Hive Tyrant and the Screamer-killers
    EXPECT_EQ(wound_display(-2, 7).left, 0);
}

TEST(M66_LaunchOptions, TurnStartsADragForScreenshots) {
    const LaunchOptions o = parse_launch_args({"--turn", "73:30"});
    ASSERT_TRUE(o.turn);
    EXPECT_EQ(*o.turn, (CliTurn{73, 30.0f, 0.0f, 0.0f}));
    const LaunchOptions o2 = parse_launch_args({"--turn", "73:-45.5:-2,1.5"});
    ASSERT_TRUE(o2.turn);
    EXPECT_EQ(*o2.turn, (CliTurn{73, -45.5f, -2.0f, 1.5f}));
    EXPECT_FALSE(parse_launch_args({}).turn);
    for (const char* bad : {"73", "x:30", "73:abc", "73:30:1", "73:30:1,b", ":30"}) {
        EXPECT_THROW((void)parse_launch_args({"--turn", bad}), std::invalid_argument) << bad;
    }
}

// ---- round-only output unchanged ----

TEST(M66_RoundOnly, TheClientLogicGivesTheM65OutputsOnRoundOnlyStates) {
    // D-074 part 3: hashes of the reach areas (spans, engaged spans, in_region, trimmed regions, drop problems, fixed
    // drops), the ring circles and outlines, the move plans and their text, the default casualty order and coherency
    // losses, the auto charge and fight moves, and step skipping, on round-only states (V1, Strike, the round monster
    // fixture, charges and pile-ins). The expected values are what the M65 code (commit 5f8fd78) gave for the same
    // probe, run once by hand on Linux x86-64 (GCC, glibc). Some of these outputs go through std::cos, std::sin,
    // std::atan2 and std::hypot, whose last bit may differ in another C runtime, so elsewhere the test is skipped.
#if !(defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__))
    GTEST_SKIP() << "the expected hashes were taken with glibc's libm on Linux x86-64";
#endif
    const std::vector<std::pair<std::string, std::uint64_t>> expected{
    {"v1_normal.regions", 0xd441a82cc26eed87ull},
    {"v1_normal.rings", 0x3b8eea9f08c5c66cull},
    {"v1_normal.plans", 0x1ba82a588131af27ull},
    {"v1_normal.casualties", 0x988c7e0622dd8429ull},
    {"v1_advance.regions", 0xa75cd26fc53c395eull},
    {"v1_advance.rings", 0xe8c527b7f1a3af89ull},
    {"v1_advance.plans", 0x2421c18e7ee44876ull},
    {"v1_advance.casualties", 0x988c7e0622dd8429ull},
    {"strike_normal_1.regions", 0x10f4c9cf7201deedull},
    {"strike_normal_1.rings", 0x358a5d80dfc6c413ull},
    {"strike_normal_1.plans", 0x7e3ceda33521029full},
    {"strike_normal_1.casualties", 0x212f1a139c66b5c9ull},
    {"monsters_tyrant_normal.regions", 0x6ca4ca23a170b259ull},
    {"monsters_tyrant_normal.rings", 0x35d1e4b3fc2aede4ull},
    {"monsters_tyrant_normal.plans", 0x8688b58952b5ab0bull},
    {"monsters_tyrant_normal.casualties", 0x36e951fc1b9673cbull},
    {"monsters_fall_back.regions", 0x0704922183852f2eull},
    {"monsters_fall_back.rings", 0x44e9d28b4d4fad71ull},
    {"monsters_fall_back.plans", 0xf8d52507a01b4ca0ull},
    {"monsters_fall_back.casualties", 0x7cb96a493cb05d4bull},
    {"charge_marines.regions", 0xaf1cd114e793f65dull},
    {"charge_marines.rings", 0x05225ed45eb7de95ull},
    {"charge_marines.plans", 0x2421c18e7ee44876ull},
    {"charge_marines.casualties", 0xc5f473e3d5d21f09ull},
    {"charge_gants.regions", 0xdd59f5ecd0e83f48ull},
    {"charge_gants.rings", 0x12241f0ad988afabull},
    {"charge_gants.plans", 0x7daa041323834858ull},
    {"charge_gants.casualties", 0xd7b1fd031a02932dull},
    {"pile_in.regions", 0xe95634e874ee920aull},
    {"pile_in.rings", 0x9f2b1e7e78330515ull},
    {"pile_in.plans", 0x82cb5e3eb26a87b4ull},
    {"pile_in.casualties", 0x988c7e0622dd8429ull},
    {"auto_charge", 0x938d3fdef2dc4731ull},
    {"auto_fight", 0xe18a6cf6575878f7ull},
    };
    const auto got = m66_probe::hashes();
    ASSERT_EQ(got.size(), expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].first, expected[i].first);
        EXPECT_EQ(got[i].second, expected[i].second) << got[i].first;
    }
}
