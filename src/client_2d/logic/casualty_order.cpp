#include "casualty_order.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <variant>

#include "core/engine/casualties.hpp"
#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/state/queries.hpp"

namespace wh40k::client {

namespace {

using Alive = std::vector<unsigned char>;  // per model (unit order): 1 while it is still in the set

// 03.03 on a pairwise table. within_2(i, j) / within_9(i, j) are measured from model i to model j exactly as
// check_coherency measures them for model i (base_distance(i, j), `within` 2" and 9"), so every count below
// equals check_coherency's on the same models. D-074 part 3 (M66): each model with its facing (a round pair is the
// old base_distance, inline in core, so the same float).
class CoherencyTable {
public:
    explicit CoherencyTable(std::span<const core::PosedModel> models)
        : n_(models.size()), within_2_(n_ * n_), within_9_(n_ * n_) {
        for (std::size_t i = 0; i < n_; ++i) {
            for (std::size_t j = 0; j < n_; ++j) {
                if (i == j) continue;
                const float d = core::base_distance(models[i].pose(), models[j].pose());
                within_2_[i * n_ + j] = core::within(d, core::kCoherencyNearMm) ? 1 : 0;
                within_9_[i * n_ + j] = core::within(d, core::kCoherencyFarMm) ? 1 : 0;
            }
        }
    }

    [[nodiscard]] bool within_2(std::size_t i, std::size_t j) const noexcept { return within_2_[i * n_ + j] != 0; }
    [[nodiscard]] bool within_9(std::size_t i, std::size_t j) const noexcept { return within_9_[i * n_ + j] != 0; }

    // The models of `alive` within 2" of model i (i itself not counted).
    [[nodiscard]] int close_count(const Alive& alive, std::size_t i) const noexcept {
        int c = 0;
        for (std::size_t j = 0; j < n_; ++j) {
            if (j != i && alive[j] != 0 && within_2(i, j)) ++c;
        }
        return c;
    }

    // Normal's greedy (M41, D-054): while the models are out of coherency, remove the first one whose removal
    // leaves the fewest problems; the number removed.
    [[nodiscard]] int losses(Alive alive) const {
        int removed = 0;
        for (;;) {
            const Counts c = counts(alive);
            if (c.alive <= 1 || c.problems == 0) return removed;
            std::size_t best = 0;
            int best_problems = -1;
            for (std::size_t i = 0; i < n_; ++i) {
                if (alive[i] == 0) continue;
                const int p = problems_without(c, alive, i);
                if (best_problems < 0 || p < best_problems) {
                    best = i;
                    best_problems = p;
                }
            }
            alive[best] = 0;
            ++removed;
        }
    }

private:
    // check_coherency's view of the models of `alive`: problems = its isolated + too_far counts (0 for one
    // model or none).
    struct Counts {
        std::size_t alive = 0;
        std::vector<int> close;    // per model: the other models of the set within 2"
        std::vector<int> far_bad;  // per model: the other models of the set more than 9" away
        int problems = 0;
    };

    [[nodiscard]] Counts counts(const Alive& alive) const {
        Counts c;
        c.close.assign(n_, 0);
        c.far_bad.assign(n_, 0);
        for (std::size_t i = 0; i < n_; ++i) {
            if (alive[i] == 0) continue;
            ++c.alive;
            for (std::size_t j = 0; j < n_; ++j) {
                if (j == i || alive[j] == 0) continue;
                if (within_2(i, j)) ++c.close[i];
                if (!within_9(i, j)) ++c.far_bad[i];
            }
        }
        if (c.alive <= 1) return c;  // 03.03 applies to units of more than one model
        for (std::size_t i = 0; i < n_; ++i) {
            if (alive[i] == 0) continue;
            c.problems += (c.close[i] == 0 ? 1 : 0) + (c.far_bad[i] > 0 ? 1 : 0);
        }
        return c;
    }

    // The problems of the set without model m, from the set's counts.
    [[nodiscard]] int problems_without(const Counts& c, const Alive& alive, std::size_t m) const {
        if (c.alive <= 2) return 0;  // one model left
        int p = 0;
        for (std::size_t j = 0; j < n_; ++j) {
            if (j == m || alive[j] == 0) continue;
            const int close_left = c.close[j] - (within_2(j, m) ? 1 : 0);
            const int far_left = c.far_bad[j] - (within_9(j, m) ? 0 : 1);
            p += (close_left == 0 ? 1 : 0) + (far_left > 0 ? 1 : 0);
        }
        return p;
    }

    std::size_t n_;
    std::vector<unsigned char> within_2_;
    std::vector<unsigned char> within_9_;
};

// What the keys need about the unit, fixed at the decision (D-058).
struct Facts {
    std::vector<core::ModelId> ids;           // unit order
    CoherencyTable table;
    std::vector<std::optional<float>> gap;    // to the closest enemy model; nullopt for all: no enemy model
    std::vector<unsigned char> engaged;       // 03.04
    std::vector<std::size_t> odd_rank;        // 0 for an ordinary model; n - i for the odd one out

    Facts(const core::GameState& state, const core::Unit& unit, const std::vector<core::PosedModel>& placed)
        : table(placed) {
        const std::size_t n = placed.size();
        for (const core::PosedModel& p : placed) ids.push_back(p.id);
        const std::vector<core::PosedModel> enemies = core::posed_enemy_models(state, unit.side);  // D-074 part 3
        gap.assign(n, std::nullopt);
        for (std::size_t i = 0; i < n; ++i) {
            gap[i] = core::closest_distance(std::span<const core::PosedModel>(&placed[i], 1),
                                            std::span<const core::PosedModel>(enemies));
        }
        engaged.assign(n, 0);
        for (const core::ModelId id : core::engaged_models(std::span<const core::PosedModel>(placed),
                                                           std::span<const core::PosedModel>(enemies))) {
            for (std::size_t i = 0; i < n; ++i) {
                if (ids[i] == id) engaged[i] = 1;
            }
        }
        std::map<std::string, int> per_type;
        for (const core::Model& m : unit.models) ++per_type[m.type_name];
        odd_rank.assign(n, 0);
        if (per_type.size() > 1) {
            for (std::size_t i = 0; i < n; ++i) {
                if (per_type[unit.models[i].type_name] == 1) odd_rank[i] = n - i;
            }
        }
    }

    [[nodiscard]] std::optional<std::size_t> index_of(core::ModelId id) const {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) return i;
        }
        return std::nullopt;
    }
};

struct Key {
    std::size_t index = 0;
    int losses = 0;
    std::size_t odd_rank = 0;
    bool at_risk = false;
    std::optional<float> gap;
};

Key key_of(const Facts& f, const Alive& in_s, std::size_t m) {
    Key k;
    k.index = m;
    Alive rest = in_s;
    rest[m] = 0;
    k.losses = f.table.losses(rest);
    k.odd_rank = f.odd_rank[m];
    k.at_risk = f.engaged[m] == 0 && f.table.close_count(in_s, m) <= 1;
    k.gap = f.gap[m];
    return k;
}

// Whether a goes before b (D-058's keys, the first difference decides).
bool better(const Key& a, const Key& b) {
    if (a.losses != b.losses) return a.losses < b.losses;
    if (a.odd_rank != b.odd_rank) return a.odd_rank < b.odd_rank;
    if (a.at_risk != b.at_risk) return a.at_risk;
    if (a.gap && b.gap) {
        if (core::more_than(*a.gap, *b.gap)) return true;
        if (core::more_than(*b.gap, *a.gap)) return false;
    }
    return a.index > b.index;
}

// The best of `candidates` (indices, in unit order); nullopt if there are none.
std::optional<std::size_t> best_of(const Facts& f, const Alive& in_s, const std::vector<std::size_t>& candidates) {
    std::optional<Key> best;
    for (const std::size_t m : candidates) {
        const Key k = key_of(f, in_s, m);
        if (!best || better(k, *best)) best = k;
    }
    if (!best) return std::nullopt;
    return best->index;
}

std::vector<std::size_t> members(const Alive& in_s) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < in_s.size(); ++i) {
        if (in_s[i] != 0) out.push_back(i);
    }
    return out;
}

bool casualties_pending_for(const core::GameState& state, core::UnitId target) {
    if (!state.pending) return false;
    const auto* d = std::get_if<core::decision::OrderCasualties>(&state.pending->kind);
    return d != nullptr && d->target == target;
}

}  // namespace

int coherency_losses(std::vector<core::PosedModel> models) {
    const CoherencyTable table(models);
    return table.losses(Alive(models.size(), 1));
}

int coherency_losses(std::vector<core::PlacedModel> models) {
    std::vector<core::PosedModel> posed;
    posed.reserve(models.size());
    for (const core::PlacedModel& m : models) posed.push_back(core::PosedModel{m.id, m.position, m.base, 0.0f});
    return coherency_losses(std::move(posed));
}

std::vector<core::ModelId> default_casualty_order(const core::GameState& state, core::UnitId target,
                                                  std::span<const core::ModelId> picks) {
    std::vector<core::ModelId> order;
    const core::Unit* unit = state.find_unit(target);
    if (unit == nullptr) return order;
    const std::vector<core::PosedModel> placed = core::posed_models(*unit);  // D-074 part 3
    const Facts f(state, *unit, placed);
    Alive in_s(placed.size(), 1);
    for (const core::ModelId id : picks) {
        const std::optional<std::size_t> i = f.index_of(id);
        if (!i || in_s[*i] == 0) continue;
        order.push_back(id);
        in_s[*i] = 0;
    }
    // Phase A: the steps of the pending decision, through the engine's preview (05.04, 06.02).
    if (casualties_pending_for(state, target)) {
        for (std::size_t guard = 0; guard < placed.size(); ++guard) {
            const std::optional<core::CasualtyPreview> pv = core::preview_casualties(state, order);
            if (!pv || pv->done()) break;
            std::vector<std::size_t> offered;
            for (std::size_t i = 0; i < placed.size(); ++i) {
                if (in_s[i] != 0 && std::find(pv->options.begin(), pv->options.end(), f.ids[i]) != pv->options.end()) {
                    offered.push_back(i);
                }
            }
            const std::optional<std::size_t> pick = best_of(f, in_s, offered);
            if (!pick) break;
            order.push_back(f.ids[*pick]);
            in_s[*pick] = 0;
        }
    }
    // Phase B: the rest, the best each time.
    while (const std::optional<std::size_t> next = best_of(f, in_s, members(in_s))) {
        order.push_back(f.ids[*next]);
        in_s[*next] = 0;
    }
    return order;
}

std::optional<core::ModelId> default_coherency_removal(const core::GameState& state, core::UnitId unit_id) {
    const core::Unit* unit = state.find_unit(unit_id);
    if (unit == nullptr || unit->models.empty()) return std::nullopt;
    const std::vector<core::PosedModel> placed = core::posed_models(*unit);  // D-074 part 3
    const Facts f(state, *unit, placed);
    const Alive in_s(placed.size(), 1);
    const std::optional<std::size_t> best = best_of(f, in_s, members(in_s));
    if (!best) return std::nullopt;
    return f.ids[*best];
}

}  // namespace wh40k::client
