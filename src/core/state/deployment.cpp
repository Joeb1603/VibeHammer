#include "core/state/deployment.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/data/formation.hpp"
#include "core/data/zone_layout.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/units.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

std::string unit_text(UnitId id) { return "unit " + std::to_string(id.v); }
std::string model_text(ModelId id) { return "model " + std::to_string(id.v); }
std::string side_text(Side s) { return s == Side::A ? "Side A" : "Side B"; }

constexpr const char* kWhere = "D-081 set-up";
// Bounding boxes are grown by this much, so float rounding can never skip a pair that really touches.
constexpr float kBoxPadMm = 0.1f;
// The scan's grid step (D-081): 1/4" for the depth behind the zone's front line and for the formation's centre.
constexpr float kScanStepMm = 0.25f * MM_PER_INCH;

// An axis-aligned box on the battlefield (mm).
struct Box {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
};

Box box_of(const BasePose& p, float grow) {
    const BaseExtents e = base_extents(p.base, p.facing_deg);
    return Box{p.position.x - e.half_x_mm - grow, p.position.y - e.half_y_mm - grow, p.position.x + e.half_x_mm + grow,
               p.position.y + e.half_y_mm + grow};
}

bool boxes_meet(const Box& a, const Box& b) {
    return a.min_x <= b.max_x && b.min_x <= a.max_x && a.min_y <= b.max_y && b.min_y <= a.max_y;
}

// A box moved by (dx, dy), grown by the pad, so it still holds the base after the same move rounded in float.
Box moved_box(const Box& b, float dx, float dy) {
    return Box{b.min_x + dx - kBoxPadMm, b.min_y + dy - kBoxPadMm, b.max_x + dx + kBoxPadMm, b.max_y + dy + kBoxPadMm};
}

void grow_to(Box& into, const Box& b) {
    into.min_x = std::min(into.min_x, b.min_x);
    into.min_y = std::min(into.min_y, b.min_y);
    into.max_x = std::max(into.max_x, b.max_x);
    into.max_y = std::max(into.max_y, b.max_y);
}

// A unit on the battlefield as the set-up checks see it: its models' poses, each model's box (an enemy's grown by the
// engagement range, 03.04, so a box that doesn't meet it can neither overlap nor engage that model) and their union.
struct OnBattlefield {
    UnitId id;
    bool enemy = false;
    std::vector<BasePose> poses;
    std::vector<ModelId> models;
    std::vector<Box> boxes;
    Box reach;
};

// The battlefield seen by `side` setting a unit up: built once per query and shared by every check of that query.
struct SetUpView {
    Side side = Side::A;
    Rect field;
    Rect zone;
    std::vector<OnBattlefield> units;  // the units in `units` with models, in state order
};

SetUpView make_view(const GameState& state, Side side) {
    SetUpView v;
    v.side = side;
    v.field = Rect{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm};
    v.zone = zone_rect(state.battlefield, state.side(side).deployment_zone);
    for (const Unit& u : state.units) {
        if (u.models.empty()) continue;
        OnBattlefield t;
        t.id = u.id;
        t.enemy = u.side != side;
        const float grow = kBoxPadMm + (t.enemy ? kEngagementRangeMm + EPS_MM : 0.0f);
        for (const Model& m : u.models) {
            t.poses.push_back(pose_of(m));
            t.models.push_back(m.id);
            t.boxes.push_back(box_of(t.poses.back(), grow));
            if (t.boxes.size() == 1) {
                t.reach = t.boxes.back();
            } else {
                grow_to(t.reach, t.boxes.back());
            }
        }
        v.units.push_back(std::move(t));
    }
    return v;
}

// A placed unit's bounding boxes: each model's (no growth) and their union.
struct PlacedBoxes {
    std::vector<Box> own;
    Box block;
};

PlacedBoxes boxes_of(const std::vector<PosedModel>& placed) {
    PlacedBoxes b;
    b.own.reserve(placed.size());
    for (const PosedModel& m : placed) {
        b.own.push_back(box_of(m.pose(), 0.0f));
        if (b.own.size() == 1) {
            b.block = b.own.back();
        } else {
            grow_to(b.block, b.own.back());
        }
    }
    return b;
}

// The placed unit (each model's pose, in the unit's model order; `boxes` its boxes_of) against the set-up rules of
// 03.02 / D-081. With `out`, every problem is appended in the documented order (the battlefield and the zone, models
// on models, coherency, engagement); without it, the first problem found decides (the models on the battlefield are
// tried first, the cheapest test to fail where units stand). True when there is none. One function for both, so a
// proposal found by the scan is exactly what validation accepts.
bool check_placed(const SetUpView& v, UnitId unit, const std::vector<PosedModel>& placed, const PlacedBoxes& boxes,
                  std::vector<std::string>* out) {
    std::vector<std::string> on_field;
    std::vector<std::string> on_models;
    std::vector<std::string> engaged;
    // 03.01 Ending a Move / D-064: not on a model already on the battlefield; 03.02 / 03.04: unengaged. Bounding boxes
    // cull the pairs that can't touch: each unit's, then each model on the battlefield against the whole placed unit,
    // then against each placed model.
    for (const OnBattlefield& t : v.units) {
        if (!boxes_meet(boxes.block, t.reach)) continue;
        bool engaged_found = false;
        for (std::size_t k = 0; k < t.poses.size(); ++k) {
            if (!boxes_meet(boxes.block, t.boxes[k])) continue;
            for (std::size_t i = 0; i < placed.size(); ++i) {
                if (!boxes_meet(boxes.own[i], t.boxes[k])) continue;
                const BasePose a = placed[i].pose();
                if (bases_overlap(a, t.poses[k])) {
                    if (out == nullptr) return false;
                    on_models.push_back("03.01: " + model_text(placed[i].id) + " would be set up on " +
                                        model_text(t.models[k]) + " of " + unit_text(t.id) + " (D-064)");
                }
                if (t.enemy && !engaged_found && within(base_distance(a, t.poses[k]), kEngagementRangeMm)) {
                    if (out == nullptr) return false;
                    engaged_found = true;
                    engaged.push_back("03.02: " + unit_text(unit) + " would be set up engaged with " + unit_text(t.id) +
                                      " (03.04: " + model_text(placed[i].id) + " within 2\" of " +
                                      model_text(t.models[k]) + ")");
                }
            }
        }
    }
    // 03.02: on the battlefield; D-009: wholly within its own deployment zone.
    for (const PosedModel& m : placed) {
        const BasePose p = m.pose();
        if (!wholly_within(v.field, p)) {
            if (out == nullptr) return false;
            on_field.push_back("03.02: " + model_text(m.id) + " is not wholly on the battlefield");
        } else if (!wholly_within(v.zone, p)) {
            if (out == nullptr) return false;
            on_field.push_back("D-009: " + model_text(m.id) + " is not wholly within " + side_text(v.side) +
                               "'s deployment zone");
        }
    }
    // 03.01 Ending a Move / D-064: no model of the unit on another of the unit.
    std::vector<std::string> within_unit;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        for (std::size_t j = i + 1; j < placed.size(); ++j) {
            if (!boxes_meet(boxes.own[i], boxes.own[j])) continue;
            if (bases_overlap(placed[i].pose(), placed[j].pose())) {
                if (out == nullptr) return false;
                within_unit.push_back("03.01: " + model_text(placed[i].id) + " would be set up on " +
                                      model_text(placed[j].id) + " of its own unit (D-064)");
            }
        }
    }
    // 03.03 (by 03.01 Ending a Move and 03.02): coherency.
    std::vector<std::string> coherency;
    if (placed.size() > 1) {
        const CoherencyResult c = check_coherency(std::span<const PosedModel>(placed));
        if (!c.ok()) {
            if (out == nullptr) return false;
            for (const ModelId id : c.isolated) {
                coherency.push_back("03.03: " + model_text(id) + " would be more than 2\" from every other model in " +
                                    unit_text(unit));
            }
            for (const ModelId id : c.too_far) {
                coherency.push_back("03.03: " + model_text(id) + " would be more than 9\" from another model in " +
                                    unit_text(unit));
            }
        }
    }
    if (out != nullptr) {
        for (auto* list : {&on_field, &within_unit, &on_models, &coherency, &engaged}) {
            out->insert(out->end(), list->begin(), list->end());
        }
    }
    return on_field.empty() && within_unit.empty() && on_models.empty() && coherency.empty() && engaged.empty();
}

bool check_placed(const SetUpView& v, UnitId unit, const std::vector<PosedModel>& placed,
                  std::vector<std::string>* out) {
    return check_placed(v, unit, placed, boxes_of(placed), out);
}

// Whether `side` may set up (or declare not set up) unit `id` at all.
std::optional<std::string> unit_problem(const GameState& state, Side side, UnitId id) {
    if (const Unit* u = find_undeployed(state, id)) {
        if (u->side != side) return "03.02: " + unit_text(id) + " is not a friendly unit";
        return std::nullopt;
    }
    if (const Unit* u = state.find_unit(id)) {
        if (u->side != side) return "03.02: " + unit_text(id) + " is not a friendly unit";
        if (is_destroyed(*u)) return "D-081: " + unit_text(id) + " could not be set up and counts as destroyed";
        return "03.02: " + unit_text(id) + " has already been set up";
    }
    return "03.02: " + unit_text(id) + " does not exist";
}

float stored_facing(const Base& base, float facing_deg) {
    return base.shape == BaseShape::Round ? 0.0f : footprint_facing_deg(facing_deg);  // D-074
}

// A light copy of the unit's models for placing formations: ids, type names and bases only (no weapons).
std::vector<Model> formation_models(const Unit& u) {
    std::vector<Model> out(u.models.size());
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        out[i].id = u.models[i].id;
        out[i].type_name = u.models[i].type_name;
        out[i].base = u.models[i].base;
    }
    return out;
}

std::vector<PosedModel> posed(const std::vector<Model>& models) {
    std::vector<PosedModel> out;
    out.reserve(models.size());
    for (const Model& m : models) out.push_back(posed_of(m));
    return out;
}

action::DeployUnit action_of(UnitId unit, const std::vector<PosedModel>& placed) {
    action::DeployUnit a{unit, {}};
    a.models.reserve(placed.size());
    for (const PosedModel& m : placed) a.models.push_back(ModelPlacement{m.id, m.position, m.facing_deg});
    return a;
}

// Everything a proposal for one of `side`'s units needs, worked out once: the view of the battlefield and the
// auto-layout of the side's units in id order (set up and not; units destroyed have no models and are left out).
struct Planner {
    const GameState* state = nullptr;
    Side side = Side::A;
    DeploymentZone zone;
    SetUpView view;
    std::vector<UnitId> army;     // the layout's units, in id order
    std::optional<ZoneLayout> layout;  // nullopt for an east or west zone (no formation can be placed there)
};

Planner make_planner(const GameState& state, Side side) {
    Planner p;
    p.state = &state;
    p.side = side;
    p.zone = state.side(side).deployment_zone;
    p.view = make_view(state, side);
    if (p.zone.edge != BattlefieldEdge::South && p.zone.edge != BattlefieldEdge::North) return p;
    std::vector<const Unit*> units;
    for (const Unit& u : state.units) {
        if (u.side == side && !u.models.empty()) units.push_back(&u);
    }
    for (const Unit& u : state.undeployed) {
        if (u.side == side && !u.models.empty()) units.push_back(&u);
    }
    std::stable_sort(units.begin(), units.end(), [](const Unit* a, const Unit* b) { return a->id < b->id; });
    std::vector<std::vector<Base>> bases;
    for (const Unit* u : units) {
        p.army.push_back(u->id);
        bases.emplace_back();
        for (const Model& m : u->models) bases.back().push_back(m.base);
    }
    p.layout = layout_zone(bases, state.battlefield, p.zone);
    return p;
}

// The scan (D-081): ranks from the fewest coherent ones that fit the zone upward; for each, the depth behind the zone's
// front line from 0 in 1/4" steps; for each depth, the formation's centre from the battlefield's middle outward in
// 1/4" steps, west before east. The first legal spot wins.
std::optional<action::DeployUnit> scan(const Planner& p, const Unit& u) {
    const Battlefield& field = p.state->battlefield;
    std::vector<Model> models = formation_models(u);
    std::vector<Base> bases;
    for (const Model& m : models) bases.push_back(m.base);
    const float inward = p.zone.edge == BattlefieldEdge::South ? -1.0f : 1.0f;  // towards the owning side's edge
    const float middle = field.width_mm * 0.5f;
    std::vector<PosedModel> candidate(models.size());
    const std::size_t n = models.size();
    std::size_t last_per_rank = 0;
    for (int r = 1; r <= static_cast<int>(n); ++r) {
        // Ranks that put as many models in each rank as the last count tried give the same formation (place_formation
        // fills ceil(n / ranks) a rank), so the same candidates: skipping them changes no result.
        const std::size_t per_rank = (n + static_cast<std::size_t>(r) - 1) / static_cast<std::size_t>(r);
        if (per_rank == last_per_rank) continue;
        last_per_rank = per_rank;
        const FormationBlock block = formation_block(bases, r, field, p.zone);
        if (!block.coherent || !block_fits_zone(block, field, p.zone)) continue;
        // The formation at centre 0 and depth 0; each candidate is it moved by (centre, depth), its boxes too.
        place_formation(models, slot_formation(LayoutSlot{r, 0.0f, 0.0f}), field, p.zone, kWhere);
        const std::vector<PosedModel> pattern = posed(models);
        const PlacedBoxes pattern_boxes = boxes_of(pattern);
        PlacedBoxes boxes = pattern_boxes;
        for (int k = 0;; ++k) {
            const float depth = static_cast<float>(k) * kScanStepMm;
            if (depth + block.depth_mm > p.zone.depth_mm + EPS_MM) break;
            bool west_open = true;
            bool east_open = true;
            for (int step = 0; west_open || east_open; ++step) {
                // 0, -1, +1, -2, +2, ...: the middle, then west before east.
                const int offset = step == 0 ? 0 : (step % 2 == 1 ? -(step + 1) / 2 : step / 2);
                if (step > 0 && ((offset < 0 && !west_open) || (offset > 0 && !east_open))) continue;
                const float centre = middle + static_cast<float>(offset) * kScanStepMm;
                if (centre + block.min_x_mm < -EPS_MM || centre + block.max_x_mm > field.width_mm + EPS_MM) {
                    // Off the battlefield: every centre further out on this side is too (the middle closes neither).
                    if (offset < 0) west_open = false;
                    if (offset > 0) east_open = false;
                    continue;
                }
                const float dx = centre;
                const float dy = inward * depth;
                for (std::size_t i = 0; i < pattern.size(); ++i) {
                    candidate[i] = pattern[i];
                    candidate[i].position.x = pattern[i].position.x + dx;
                    candidate[i].position.y = pattern[i].position.y + dy;
                    boxes.own[i] = moved_box(pattern_boxes.own[i], dx, dy);
                }
                boxes.block = moved_box(pattern_boxes.block, dx, dy);
                if (check_placed(p.view, u.id, candidate, boxes, nullptr)) return action_of(u.id, candidate);
            }
        }
    }
    return std::nullopt;
}

std::optional<action::DeployUnit> propose(const Planner& p, const Unit& u) {
    if (!p.layout || u.models.empty()) return std::nullopt;  // east or west zone: no formation can be placed
    // 1. The unit's auto-layout slot, placed exactly as the scenario loader places a formation (D-079).
    if (p.layout->fits()) {
        const auto at = std::find(p.army.begin(), p.army.end(), u.id);
        if (at != p.army.end()) {
            std::vector<Model> models = formation_models(u);
            place_formation(models, slot_formation(p.layout->slots[static_cast<std::size_t>(at - p.army.begin())]),
                            p.state->battlefield, p.zone, kWhere);
            const std::vector<PosedModel> placed = posed(models);
            if (check_placed(p.view, u.id, placed, nullptr)) return action_of(u.id, placed);
        }
    }
    // 2. The scan.
    return scan(p, u);
}

}  // namespace

const Unit* find_undeployed(const GameState& state, UnitId id) {
    for (const Unit& u : state.undeployed) {
        if (u.id == id) return &u;
    }
    return nullptr;
}

std::optional<Side> side_to_deploy(const GameState& state) {
    const Side first = state.first_turn;
    const Side second = opponent(first);
    const auto count = [](const std::vector<Unit>& units, Side side) {
        return std::count_if(units.begin(), units.end(), [side](const Unit& u) { return u.side == side; });
    };
    const auto left_first = count(state.undeployed, first);
    const auto left_second = count(state.undeployed, second);
    if (left_first == 0 && left_second == 0) return std::nullopt;
    if (left_second == 0) return first;  // the side not yet done sets up the rest
    if (left_first == 0) return second;
    // Both have units left: alternate, the side taking the second turn first.
    return count(state.units, second) <= count(state.units, first) ? second : first;
}

std::vector<std::string> set_up_problems(const GameState& state, Side side, const action::DeployUnit& a) {
    if (auto p = unit_problem(state, side, a.unit)) return {*p};
    const Unit& u = *find_undeployed(state, a.unit);
    std::vector<std::string> out;
    // 03.02: place its models: each exactly once.
    std::vector<int> placed_count(u.models.size(), 0);
    std::vector<std::size_t> index_of(a.models.size(), u.models.size());
    for (std::size_t p = 0; p < a.models.size(); ++p) {
        const ModelId id = a.models[p].model;
        std::size_t i = 0;
        while (i < u.models.size() && u.models[i].id != id) ++i;
        if (i == u.models.size()) {
            out.push_back("03.02: " + model_text(id) + " is not a model of " + unit_text(u.id));
            continue;
        }
        index_of[p] = i;
        if (++placed_count[i] == 2) out.push_back("03.02: " + model_text(id) + " is placed more than once");
    }
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        if (placed_count[i] == 0) {
            out.push_back("03.02: " + model_text(u.models[i].id) + " of " + unit_text(u.id) +
                          " is not placed (every model of the unit is set up)");
        }
    }
    for (std::size_t p = 0; p < a.models.size(); ++p) {
        if (index_of[p] == u.models.size()) continue;
        const ModelPlacement& m = a.models[p];
        const Position& at = m.position;
        if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) {
            out.push_back("03.02: " + model_text(m.model) + "'s position must be a finite point on the battlefield");
        } else if (at.z != 0.0f) {
            out.push_back("03.02: " + model_text(m.model) +
                          " must be set up on the battlefield's surface (z 0: there is no terrain)");
        }
        if (!std::isfinite(m.facing_deg)) {
            out.push_back("D-074: " + model_text(m.model) + "'s facing must be a finite number of degrees");
        } else if (u.models[index_of[p]].base.shape == BaseShape::Round && m.facing_deg != 0.0f) {
            out.push_back("D-074: " + model_text(m.model) +
                          " has a round base, which has no facing; its facing must be 0");
        }
    }
    if (!out.empty()) return out;  // the geometry needs every model once, at a real point
    std::vector<PosedModel> placed(u.models.size());
    for (std::size_t p = 0; p < a.models.size(); ++p) {
        const Model& m = u.models[index_of[p]];
        placed[index_of[p]] = PosedModel{m.id, a.models[p].position, m.base, stored_facing(m.base, a.models[p].facing_deg)};
    }
    (void)check_placed(make_view(state, side), u.id, placed, &out);
    return out;
}

std::vector<std::string> cannot_set_up_problems(const GameState& state, Side side, UnitId unit) {
    if (auto p = unit_problem(state, side, unit)) return {*p};
    if (propose_set_up(state, unit)) {
        return {"D-081: " + unit_text(unit) +
                " can be set up (the engine finds a legal spot), so it can't be counted as not set up"};
    }
    return {};
}

std::optional<action::DeployUnit> propose_set_up(const GameState& state, UnitId unit) {
    const Unit* u = find_undeployed(state, unit);
    if (u == nullptr) return std::nullopt;
    return propose(make_planner(state, u->side), *u);
}

std::vector<SetUpProposal> deployment_proposals(const GameState& state, Side side) {
    std::vector<SetUpProposal> out;
    const Planner p = make_planner(state, side);
    for (const Unit& u : state.undeployed) {
        if (u.side != side) continue;
        out.push_back(SetUpProposal{u.id, propose(p, u)});
    }
    return out;
}

}  // namespace wh40k::core
