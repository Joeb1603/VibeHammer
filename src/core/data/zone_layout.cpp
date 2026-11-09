#include "core/data/zone_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numeric>

#include "core/geometry/oval.hpp"
#include "core/geometry/units.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

constexpr const char* kWhere = "D-079 auto-layout";

float snap_round(float inches) { return std::round(inches / kLayoutGridIn) * kLayoutGridIn; }
// Up to the grid; a value a hair above a grid line (float noise) stays on it.
float snap_up(float inches) { return std::ceil(inches / kLayoutGridIn - 1e-3f) * kLayoutGridIn; }
float snap_down(float inches) { return std::floor(inches / kLayoutGridIn + 1e-3f) * kLayoutGridIn; }

std::vector<Model> models_of(const std::vector<Base>& bases) {
    std::vector<Model> models(bases.size());
    for (std::size_t i = 0; i < bases.size(); ++i) {
        models[i].id = ModelId{static_cast<std::uint32_t>(i + 1)};
        models[i].base = bases[i];
    }
    return models;
}

float front_line_mm(const Battlefield& field, const DeploymentZone& zone) {
    return zone.edge == BattlefieldEdge::South ? zone.depth_mm : field.depth_mm - zone.depth_mm;
}

using Block = FormationBlock;

// The loader's set-up checks within one side (scenario_loader.cpp: check_set_up, check_no_overlap): every unit
// coherent (03.03) and wholly within the zone (D-009), no model on another (03.02, D-064).
bool legal_set_up(const std::vector<std::vector<Base>>& units, const std::vector<LayoutSlot>& slots,
                  const Battlefield& field, const DeploymentZone& zone) {
    const Rect area = zone_rect(field, zone);
    std::vector<Model> all;
    for (std::size_t u = 0; u < units.size(); ++u) {
        Unit unit;
        unit.models = models_of(units[u]);
        place_formation(unit.models, slot_formation(slots[u]), field, zone, kWhere);
        if (!unit_coherency(unit).ok()) return false;
        for (const Model& m : unit.models) {
            if (!wholly_within(area, pose_of(m))) return false;
            all.push_back(m);
        }
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            if (bases_overlap(pose_of(all[i]), pose_of(all[j]))) return false;
        }
    }
    return true;
}

// Places the shelves (each a list of unit indices, west to east): centred, the first at the zone's front line, each
// further one 1" behind the deepest unit of the one before. nullopt if a shelf is too wide or too deep.
std::optional<std::vector<LayoutSlot>> place_shelves(const std::vector<std::vector<std::size_t>>& shelves,
                                                     const std::vector<Block>& blocks, const Battlefield& field,
                                                     const DeploymentZone& zone) {
    const float gap_mm = inches_to_mm(kLayoutGapIn);
    const float field_in = mm_to_inches(field.width_mm);
    std::vector<LayoutSlot> slots(blocks.size());
    float depth_in = 0.0f;
    for (const std::vector<std::size_t>& shelf : shelves) {
        float width_mm = 0.0f;
        float deepest_mm = 0.0f;
        for (std::size_t k = 0; k < shelf.size(); ++k) {
            width_mm += blocks[shelf[k]].width_mm() + (k == 0 ? 0.0f : gap_mm);
            deepest_mm = std::max(deepest_mm, blocks[shelf[k]].depth_mm);
        }
        if (width_mm > field.width_mm) return std::nullopt;
        if (inches_to_mm(depth_in) + deepest_mm > zone.depth_mm) return std::nullopt;
        float west_mm = (field.width_mm - width_mm) * 0.5f;
        for (const std::size_t u : shelf) {
            const Block& b = blocks[u];
            // The formation's centre that puts the block's west edge at west_mm, on the grid and inside the field.
            float centre_in = snap_round(mm_to_inches(west_mm - b.min_x_mm));
            const float lowest = mm_to_inches(-b.min_x_mm);
            const float highest = field_in - mm_to_inches(b.max_x_mm);
            if (centre_in < lowest) centre_in = snap_up(lowest);
            if (centre_in > highest) centre_in = snap_down(highest);
            slots[u] = LayoutSlot{b.ranks, centre_in, depth_in};
            west_mm += b.width_mm() + gap_mm;
        }
        depth_in = snap_up(depth_in + mm_to_inches(deepest_mm) + kLayoutGapIn);
    }
    return slots;
}

std::string inches_text(float mm) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(mm_to_inches(mm)));
    return buf;
}

}  // namespace

Formation slot_formation(const LayoutSlot& slot) {
    Formation f;
    f.ranks = slot.ranks;
    f.gap_mm = 0.0f;
    f.centre_x_mm = inches_to_mm(slot.centre_x_in);
    f.depth_mm = inches_to_mm(slot.depth_in);
    f.raw_facing_deg = 0.0f;
    return f;
}

FormationBlock formation_block(const std::vector<Base>& bases, int ranks, const Battlefield& field,
                               const DeploymentZone& zone) {
    FormationBlock out;
    out.ranks = ranks;
    std::vector<Model> models = models_of(bases);
    place_formation(models, slot_formation(LayoutSlot{ranks, 0.0f, 0.0f}), field, zone, kWhere);
    const float front = front_line_mm(field, zone);
    out.min_x_mm = models.front().position.x;
    out.max_x_mm = models.front().position.x;
    for (const Model& m : models) {
        const BaseExtents e = base_extents(m.base, m.facing_deg);
        out.min_x_mm = std::min(out.min_x_mm, m.position.x - e.half_x_mm);
        out.max_x_mm = std::max(out.max_x_mm, m.position.x + e.half_x_mm);
        const float behind = zone.edge == BattlefieldEdge::South ? front - (m.position.y - e.half_y_mm)
                                                                 : (m.position.y + e.half_y_mm) - front;
        out.depth_mm = std::max(out.depth_mm, behind);
    }
    Unit unit;
    unit.models = std::move(models);
    out.coherent = unit_coherency(unit).ok();
    return out;
}

bool block_fits_zone(const FormationBlock& b, const Battlefield& field, const DeploymentZone& zone) {
    return b.width_mm() <= field.width_mm && b.depth_mm <= zone.depth_mm;
}

std::optional<int> fewest_coherent_ranks(const std::vector<Base>& bases, const Battlefield& field,
                                         const DeploymentZone& zone) {
    if (bases.empty()) return std::nullopt;
    if (zone.edge != BattlefieldEdge::South && zone.edge != BattlefieldEdge::North) return std::nullopt;
    for (int r = 1; r <= static_cast<int>(bases.size()); ++r) {
        const FormationBlock m = formation_block(bases, r, field, zone);
        if (m.coherent && block_fits_zone(m, field, zone)) return r;
    }
    return std::nullopt;
}

ZoneLayout layout_zone(const std::vector<std::vector<Base>>& units, const Battlefield& field,
                       const DeploymentZone& zone) {
    ZoneLayout out;
    const std::string no_fit = "D-079: the army does not fit in its " + inches_text(field.width_mm) + "\"x" +
                               inches_text(zone.depth_mm) + "\" deployment zone";
    if (zone.edge != BattlefieldEdge::South && zone.edge != BattlefieldEdge::North) {
        out.problem = "D-079: the auto-layout places units in north and south deployment zones only";
        return out;
    }
    std::vector<Block> blocks;
    for (const std::vector<Base>& bases : units) {
        const std::optional<int> ranks = fewest_coherent_ranks(bases, field, zone);
        if (!ranks) {
            out.problem = no_fit;
            return out;
        }
        blocks.push_back(formation_block(bases, *ranks, field, zone));
    }
    const float gap_mm = inches_to_mm(kLayoutGapIn);

    // Shelves in list order.
    std::vector<std::vector<std::size_t>> in_order;
    float used_mm = 0.0f;
    for (std::size_t u = 0; u < blocks.size(); ++u) {
        const float w = blocks[u].width_mm();
        if (in_order.empty() || used_mm + gap_mm + w > field.width_mm) {
            in_order.push_back({u});
            used_mm = w;
        } else {
            in_order.back().push_back(u);
            used_mm += gap_mm + w;
        }
    }
    if (auto slots = place_shelves(in_order, blocks, field, zone); slots && legal_set_up(units, *slots, field, zone)) {
        out.slots = std::move(*slots);
        return out;
    }

    // Fallback: first-fit decreasing by width (ties in list order).
    std::vector<std::size_t> order(blocks.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return blocks[a].width_mm() > blocks[b].width_mm(); });
    std::vector<std::vector<std::size_t>> ffd;
    std::vector<float> widths;
    for (const std::size_t u : order) {
        const float w = blocks[u].width_mm();
        bool placed = false;
        for (std::size_t s = 0; s < ffd.size() && !placed; ++s) {
            if (widths[s] + gap_mm + w <= field.width_mm) {
                ffd[s].push_back(u);
                widths[s] += gap_mm + w;
                placed = true;
            }
        }
        if (!placed) {
            ffd.push_back({u});
            widths.push_back(w);
        }
    }
    if (auto slots = place_shelves(ffd, blocks, field, zone); slots && legal_set_up(units, *slots, field, zone)) {
        out.slots = std::move(*slots);
        return out;
    }
    out.problem = no_fit;
    return out;
}

}  // namespace wh40k::core
