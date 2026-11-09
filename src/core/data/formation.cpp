#include "core/data/formation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include "core/data/load_error.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

using Json = nlohmann::json;

namespace {

// The scenario loader's own helpers (scenario_loader.cpp), so the messages are unchanged.
[[noreturn]] void fail(const std::string& what) {
    throw DataError("scenario: " + what);
}

const Json& require(const Json& j, const char* key, std::string_view where) {
    if (!j.is_object() || !j.contains(key)) {
        fail(std::string(where) + ": missing field '" + key + "'");
    }
    return j.at(key);
}

float require_inches(const Json& j, const char* key, const std::string& where) {
    const Json& e = require(j, key, where);
    if (!e.is_number()) {
        fail(where + ": field '" + key + "' must be a number of inches");
    }
    return e.get<float>();
}

}  // namespace

Formation parse_formation(const Json& j, const std::string& where) {
    Formation f;
    const Json& r = require(j, "ranks", where);
    if (!r.is_number_integer() || r.get<int>() < 1) {
        fail(where + ": formation.ranks must be a positive integer");
    }
    f.ranks = r.get<int>();
    f.gap_mm = inches_to_mm(require_inches(j, "gap_in", where));
    f.centre_x_mm = inches_to_mm(require_inches(j, "centre_x_in", where));
    // D-064: optional; absent means 0 (the D-008 layout, unchanged). Pushing the unit out of the zone
    // is caught by the D-009 wholly-within check in check_set_up.
    if (j.contains("depth_in")) {
        const float depth_in = require_inches(j, "depth_in", where);
        if (!(depth_in >= 0.0f)) {
            fail(where + ": D-064: formation.depth_in must be 0 or more inches");
        }
        f.depth_mm = inches_to_mm(depth_in);
    }
    // D-074 part 2: optional; absent means 0 (an oval's length along y, south to north). Any finite number of degrees,
    // given to every model of the unit (modulo 180 for an oval; a round base refuses anything but 0, place_formation).
    if (j.contains("facing_deg")) {
        const Json& e = j.at("facing_deg");
        if (!e.is_number()) {
            fail(where + ": D-074: formation.facing_deg must be a number of degrees");
        }
        f.raw_facing_deg = e.get<float>();
        if (!std::isfinite(f.raw_facing_deg)) {
            fail(where + ": D-074: formation.facing_deg must be a finite number of degrees");
        }
    }
    const Json& anchor = require(j, "anchor", where);
    // (Compared as a string, not with json's operator==(const char*): the same test, without GCC 13's -O3 false
    // -Warray-bounds positive in this translation unit.)
    if (!anchor.is_string() || anchor.get<std::string>() != "zone_front") {
        fail(where + ": unsupported formation anchor " + anchor.dump());
    }
    return f;
}

void place_formation(std::vector<Model>& models, const Formation& f, const Battlefield& field,
                     const DeploymentZone& zone, const std::string& where) {
    float front_line = 0.0f;
    float inward = 0.0f;  // direction from the front line towards the owning side's edge
    switch (zone.edge) {
        case BattlefieldEdge::South:
            front_line = zone.depth_mm;
            inward = -1.0f;
            break;
        case BattlefieldEdge::North:
            front_line = field.depth_mm - zone.depth_mm;
            inward = 1.0f;
            break;
        case BattlefieldEdge::East:
        case BattlefieldEdge::West:
            fail(where + ": D-008 formation supports north/south deployment zones only");
    }

    // D-074 part 2: a round base has no facing; an oval takes the formation's facing modulo 180. An oval must be at
    // least as long as it is wide (D-070; the unit loader checks it too, but a datasheet can come from elsewhere).
    for (Model& m : models) {
        if (m.base.shape == BaseShape::Oval && !(m.base.diameter_mm > 0.0f && m.base.length_mm >= m.base.diameter_mm)) {
            fail(where + ": D-070: model type '" + m.type_name +
                 "' has an oval base whose length_mm is smaller than its diameter_mm (width)");
        }
        if (m.base.shape == BaseShape::Round) {
            if (f.raw_facing_deg != 0.0f) {
                fail(where + ": D-074: model type '" + m.type_name +
                     "' has a round base, which has no facing (formation.facing_deg must be 0)");
            }
            m.facing_deg = 0.0f;
        } else {
            m.facing_deg = footprint_facing_deg(f.raw_facing_deg);
        }
    }
    const std::size_t n = models.size();
    const std::size_t ranks = static_cast<std::size_t>(f.ranks);
    const std::size_t per_rank = (n + ranks - 1) / ranks;
    std::vector<BaseExtents> ext;
    ext.reserve(n);
    float max_depth = 0.0f;  // the deepest base along y (for round bases: the largest diameter)
    for (const Model& m : models) {
        ext.push_back(base_extents(m.base, m.facing_deg));
        max_depth = std::max(max_depth, 2.0f * ext.back().half_y_mm);
    }

    for (std::size_t rank = 0; rank * per_rank < n; ++rank) {
        const std::size_t first = rank * per_rank;
        const std::size_t last = std::min(n, first + per_rank);
        float width = f.gap_mm * static_cast<float>(last - first - 1);
        for (std::size_t i = first; i < last; ++i) {
            width += 2.0f * ext[i].half_x_mm;
        }
        // D-064: depth_mm is added to the D-008 offset; at 0 the sum is that offset exactly, so files
        // without depth_in place their models bit-for-bit as before.
        const float rank_centre_y =
            front_line + inward * (f.depth_mm + (max_depth * 0.5f +
                                                 static_cast<float>(rank) * (max_depth + f.gap_mm)));
        float x = f.centre_x_mm - width * 0.5f;
        for (std::size_t i = first; i < last; ++i) {
            const float r = ext[i].half_x_mm;
            models[i].position = Position{x + r, rank_centre_y, 0.0f};
            x += 2.0f * r + f.gap_mm;
        }
    }
}

}  // namespace wh40k::core
