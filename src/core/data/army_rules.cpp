#include "core/data/army_rules.hpp"

#include <map>

namespace wh40k::core {

namespace {

using Multiset = std::map<std::string, int>;

std::string list_names(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& n : names) {
        out += (out.empty() ? "" : ", ") + n;
    }
    return out.empty() ? "none" : out;
}

// Takes `weapons` out of `pool` if it holds all of them.
bool take(Multiset& pool, const std::vector<std::string>& weapons) {
    Multiset need;
    for (const std::string& w : weapons) {
        ++need[w];
    }
    for (const auto& [w, n] : need) {
        const auto it = pool.find(w);
        if (it == pool.end() || it->second < n) {
            return false;
        }
    }
    for (const auto& [w, n] : need) {
        if ((pool[w] -= n) == 0) {
            pool.erase(w);
        }
    }
    return true;
}

void give_back(Multiset& pool, const std::vector<std::string>& weapons) {
    for (const std::string& w : weapons) {
        ++pool[w];
    }
}

// Depth-first over the slots in order and each slot's options in order: the first decomposition wins.
bool fill(const ModelWargear& wargear, std::size_t slot, Multiset& pool, std::vector<std::size_t>& chosen) {
    if (slot == wargear.slots.size()) {
        return pool.empty();
    }
    const auto& options = wargear.slots[slot].options;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (!take(pool, options[i].weapons)) {
            continue;
        }
        chosen.push_back(i);
        if (fill(wargear, slot + 1, pool, chosen)) {
            return true;
        }
        chosen.pop_back();
        give_back(pool, options[i].weapons);
    }
    return false;
}

}  // namespace

SlotFit loadout_fits_slots(const ModelWargear& wargear, const std::vector<std::string>& weapons) {
    SlotFit fit;
    Multiset pool;
    for (const std::string& w : weapons) {
        ++pool[w];
    }
    for (const std::string& f : wargear.fixed) {
        if (!take(pool, {f})) {
            fit.problem = "02.07 (D-079): '" + wargear.model_type + "' must carry its fixed weapon '" + f + "'";
            return fit;
        }
    }
    if (!fill(wargear, 0, pool, fit.options)) {
        std::vector<std::string> slot_names;
        for (const WargearSlot& s : wargear.slots) {
            slot_names.push_back(s.name);
        }
        fit.options.clear();
        fit.problem = "02.07 (D-079): '" + wargear.model_type + "' with " + list_names(weapons) +
                      " is not its fixed weapons (" + list_names(wargear.fixed) + ") plus one option of each slot (" +
                      list_names(slot_names) + ")";
    }
    return fit;
}

std::vector<std::string> loadout_weapons(const ModelWargear& wargear, const std::vector<std::size_t>& options) {
    std::vector<std::string> weapons = wargear.fixed;
    for (std::size_t s = 0; s < options.size() && s < wargear.slots.size(); ++s) {
        const WargearOption& o = wargear.slots[s].options.at(options[s]);
        weapons.insert(weapons.end(), o.weapons.begin(), o.weapons.end());
    }
    return weapons;
}

int fit_option_points(const ModelWargear& wargear, const SlotFit& fit) {
    if (!fit.fits()) {
        return 0;
    }
    int points = 0;
    for (std::size_t s = 0; s < fit.options.size() && s < wargear.slots.size(); ++s) {
        points += wargear.slots[s].options[fit.options[s]].points;
    }
    return points;
}

int bracket_points(const ListBuilding& lb, int models) {
    int points = lb.points.front().points;
    for (const PointsBracket& b : lb.points) {
        if (models >= b.min_models) {
            points = b.points;
        }
    }
    return points;
}

int limit_max(const WargearLimit& limit, int models) {
    if (limit.below_models && models < limit.below_models->models) {
        return limit.below_models->max;
    }
    int max = limit.max;
    if (limit.per_models) {
        max += limit.per_models->add * (models / limit.per_models->models);
    }
    return max;
}

}  // namespace wh40k::core
