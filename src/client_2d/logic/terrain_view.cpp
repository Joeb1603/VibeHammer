#include "terrain_view.hpp"

#include <algorithm>
#include <string_view>

#include "core/geometry/oval.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/shooting.hpp"
#include "core/rules/terrain.hpp"
#include "core/state/queries.hpp"

namespace wh40k::client {

namespace {

std::string count_of(int n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// "2 dense features, 1 light feature": the area's features by category, densest first.
std::string feature_counts(const core::TerrainArea& area) {
    int counts[3] = {0, 0, 0};
    for (const core::TerrainFeature& f : area.features) ++counts[static_cast<int>(f.category)];
    const char* names[3] = {"exposed", "light", "dense"};
    std::string out;
    for (int c = 2; c >= 0; --c) {
        if (counts[c] == 0) continue;
        if (!out.empty()) out += ", ";
        out += count_of(counts[c], std::string(names[c]) + " feature", std::string(names[c]) + " features");
    }
    return out;
}

std::string area_title(const core::TerrainArea& area) {
    std::string out = area.name;
    if (area.objective && *area.objective != area.name) out += " (objective " + *area.objective + ")";
    if (area.objective && *area.objective == area.name) out += " (objective)";
    return out;
}

std::string feature_line(const core::TerrainFeature& f) {
    std::string out = "  " + f.name + ": " + std::string(core::terrain_category_name(f.category));
    switch (f.category) {
        case core::TerrainCategory::Exposed:
            out += ", blocks no move and no line of sight";
            break;
        case core::TerrainCategory::Light:
            out += ", no model ends a move on it";
            break;
        case core::TerrainCategory::Dense:
            if (f.height_mm) out += ", " + core::format_inches(*f.height_mm) + " tall";
            out += "; blocks lines of sight (13.11); only INFANTRY/BEASTS/SWARM/MOBILE move through it";
            if (f.height_mm && !core::more_than(*f.height_mm, core::kDenseMoveHeightMm)) out += " (any model: 2\" or less)";
            break;
    }
    return out;
}

// Reasons about the weapon alone (not about this target): the target can't explain them.
bool about_the_weapon(const std::string& reason) {
    for (const std::string_view prefix : {"04.01", "10.05", "10.06: '", "24.07", "D-016", "D-026", "D-069", "D-071"}) {
        if (reason.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

}  // namespace

std::vector<TerrainMark> terrain_marks(const core::GameState& state) {
    std::vector<TerrainMark> out;
    for (const core::TerrainArea& a : state.terrain) {
        out.push_back(TerrainMark{true, a.shape, core::TerrainCategory::Exposed, core::is_obscuring(a), a.name});
    }
    for (const core::TerrainArea& a : state.terrain) {
        for (const core::TerrainFeature& f : a.features) out.push_back(TerrainMark{false, f.shape, f.category, false, ""});
    }
    return out;
}

std::vector<std::string> terrain_tooltip(const core::GameState& state, const core::Position& p) {
    std::vector<std::string> out;
    const core::oval_detail::Vec v{p.x, p.y};
    for (const core::TerrainArea& a : state.terrain) {
        if (core::point_shape_signed_distance(v, a.shape) > 0.0) continue;
        std::string head = area_title(a) + ": terrain area";
        if (core::is_obscuring(a)) head += ", obscuring (13.10)";
        out.push_back(head);
        out.push_back("  INFANTRY/BEASTS/SWARM within it have the benefit of cover (13.08)");
        if (core::has_dense(a)) {
            out.push_back("  and are hidden unless they shot this turn or last: visible only within 15\" (13.09)");
        }
        for (const core::TerrainFeature& f : a.features) out.push_back(feature_line(f));
    }
    return out;
}

std::vector<std::string> terrain_summary(const core::GameState& state) {
    std::vector<std::string> out;
    for (const core::TerrainArea& a : state.terrain) {
        std::string line = area_title(a) + ": " + feature_counts(a);
        if (core::is_obscuring(a)) line += "; obscuring";
        if (core::has_dense(a)) line += "; hides";
        out.push_back(line);
    }
    return out;
}

std::optional<std::string> target_refusal_text(const core::GameState& state, core::UnitId shooter,
                                               core::ShootingType type, core::UnitId target) {
    const core::Unit* u = state.find_unit(shooter);
    if (u == nullptr || state.find_unit(target) == nullptr) return std::nullopt;
    std::optional<std::string> first;
    for (const core::Model& m : u->models) {
        for (std::size_t w = 0; w < m.weapons.size(); ++w) {
            for (std::size_t p = 0; p < m.weapons[w].profiles.size(); ++p) {
                const core::ShotDeclaration d{m.id, w, p, target};
                const std::vector<std::string> problems =
                    core::shot_problems(state, *u, type, std::span<const core::ShotDeclaration>(&d, 1));
                if (problems.empty()) return std::nullopt;  // the target can be selected
                for (const std::string& r : problems) {
                    if (!first && !about_the_weapon(r)) first = r;
                }
            }
        }
    }
    return first;
}

}  // namespace wh40k::client
