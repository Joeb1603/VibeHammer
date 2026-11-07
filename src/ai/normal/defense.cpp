#include "unit_style.hpp"
#include "normal/defense.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <variant>

#include "analysis/threat.hpp"
#include "core/engine/casualties.hpp"
#include "core/rules/fight.hpp"
#include "core/state/queries.hpp"
#include "normal/fight.hpp"
#include "normal/value.hpp"

namespace wh40k::ai::normal {

namespace {

// Score weights (lower score = removed first). Coherency is counted in models lost, so it outweighs the rest.
constexpr double kCoherencyWeight = 10.0;  // per model the End of Turn step would also remove (03.03)
constexpr double kWorthWeight = 2.0;       // per unit of worth relative to the unit's mean model
constexpr double kWoundedWeight = 0.05;    // a model with fewer wounds left goes first when there is a choice
constexpr double kUnthreatenedPosition = 0.3;  // position weight when no enemy can charge us next

using Worth = std::map<core::ModelId, double>;

std::vector<core::PlacedModel> without(const std::vector<core::PlacedModel>& models, const std::set<core::ModelId>& gone) {
    std::vector<core::PlacedModel> out;
    for (const core::PlacedModel& p : models) {
        if (gone.count(p.id) == 0) out.push_back(p);
    }
    return out;
}

// The unit's models' worth against the closest enemy (every model 1 if there is no enemy).
Worth worth_of(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& unit) {
    if (const core::Unit* e = closest_enemy(state, unit)) return model_worth(cache, unit, *e);
    Worth w;
    for (const core::Model& m : unit.models) w[m.id] = 1.0;
    return w;
}

double mean_worth(const Worth& w) {
    double sum = 0.0;
    for (const auto& [id, v] : w) sum += v;
    return w.empty() ? 1.0 : std::max(1e-9, sum / static_cast<double>(w.size()));
}

bool falling_back(const core::GameState& state, const core::Unit& unit) {
    return state.flow.move && state.flow.move->unit == unit.id && state.flow.move->type == core::MoveType::FallBack;
}

// The position term per model, in [0, 1]: 0 is the first to remove under the stance.
std::map<core::ModelId, double> position_rank(const core::GameState& state, const core::Unit& unit,
                                              CasualtyStance stance) {
    std::map<core::ModelId, double> out;
    const core::Unit* enemy = closest_enemy(state, unit);
    std::map<core::ModelId, float> gap;
    float lo = 0.0f;
    float hi = 0.0f;
    bool first = true;
    for (const core::Model& m : unit.models) {
        const float g = enemy != nullptr ? core::model_distance_to_unit(m, m.position, *enemy).value_or(0.0f) : 0.0f;
        gap[m.id] = g;
        lo = first ? g : std::min(lo, g);
        hi = first ? g : std::max(hi, g);
        first = false;
    }
    const float span = hi - lo;
    for (const core::Model& m : unit.models) {
        const double near = span > core::EPS_MM ? static_cast<double>((gap[m.id] - lo) / span) : 0.5;  // 0 closest
        const bool engaged = !core::enemy_units_engaged_with_model(state, unit, m, m.position).empty();
        switch (stance) {
            case CasualtyStance::AvoidContact: out[m.id] = near; break;
            case CasualtyStance::SeekContact: out[m.id] = 1.0 - near; break;
            case CasualtyStance::BreakCombat: out[m.id] = engaged ? 0.25 * near : 0.5 + 0.5 * near; break;
            case CasualtyStance::HoldCombat: out[m.id] = engaged ? 0.5 + 0.5 * (1.0 - near) : 0.5 * (1.0 - near); break;
        }
    }
    return out;
}

// The score of removing (or wounding) `id` from `unit`, given the models lost with it. Lower goes first.
double removal_score(const core::Unit& unit, core::ModelId id, int extra_losses, double position_weight,
                     const std::map<core::ModelId, double>& position, const Worth& worth, double mean) {
    const core::Model* m = core::find_model(unit, id);
    const int w = m != nullptr ? std::max(1, m->characteristics.wounds) : 1;
    const double wounds_frac = m != nullptr ? static_cast<double>(m->wounds_remaining) / w : 1.0;
    const auto pos = position.find(id);
    const auto val = worth.find(id);
    return kCoherencyWeight * extra_losses + position_weight * (pos != position.end() ? pos->second : 0.5) +
           kWorthWeight * ((val != worth.end() ? val->second : mean) / mean - 1.0) + kWoundedWeight * wounds_frac;
}

const char* stance_name(CasualtyStance s) {
    switch (s) {
        case CasualtyStance::AvoidContact: return "front models first";
        case CasualtyStance::SeekContact: return "rear models first";
        case CasualtyStance::BreakCombat: return "engaged models first";
        case CasualtyStance::HoldCombat: return "unengaged models first";
    }
    return "";
}

}  // namespace

StanceInfo casualty_stance(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& unit) {
    StanceInfo out;
    if (falling_back(state, unit)) {
        out.stance = CasualtyStance::BreakCombat;  // 09.07: the engaged models are the ones that must get away
        out.why = "falling back";
        return out;
    }
    if (core::is_engaged(state, unit)) {
        double ours = 0.0;
        double theirs = 0.0;
        for (const core::UnitId e : core::engaged_enemy_units(state, unit)) {
            const core::Unit* enemy = state.find_unit(e);
            if (enemy == nullptr) continue;
            ours += melee_share(cache, unit, *enemy);
            theirs += melee_share(cache, *enemy, unit);
        }
        if (will_fight(state, unit)) {
            // Its engaged models still fight back this phase (12.04): keep them, whoever is winning.
            out.stance = CasualtyStance::HoldCombat;
            out.why = "its engaged models still fight";
        } else {
            out.stance = theirs > ours ? CasualtyStance::BreakCombat : CasualtyStance::HoldCombat;
            out.why = theirs > ours ? "losing the combat" : "winning the combat";
        }
        return out;
    }
    const core::Unit* enemy = closest_enemy(state, unit);
    if (enemy == nullptr) {
        out.position_weight = 0.0;
        return out;
    }
    const float gap = core::unit_distance(unit, *enemy).value_or(0.0f);
    const double ours = melee_share(cache, unit, *enemy);
    const double theirs = melee_share(cache, *enemy, unit);
    const bool they_may_charge = analysis::threat_ranges(*enemy).p_charge_after_move(gap) > 0.0;
    out.stance = theirs > ours ? CasualtyStance::AvoidContact : CasualtyStance::SeekContact;
    out.position_weight = they_may_charge ? 1.0 : kUnthreatenedPosition;
    out.why = theirs > ours ? (they_may_charge ? client::unit_display_name(state, enemy->id) + " may charge" : "keeping away from " + client::unit_display_name(state, enemy->id))
                            : "keeping close to " + client::unit_display_name(state, enemy->id);
    return out;
}

std::optional<CasualtyOrder> casualty_order(analysis::OddsCache& cache, const core::GameState& state) {
    if (!state.pending) return std::nullopt;
    const auto* d = std::get_if<core::decision::OrderCasualties>(&state.pending->kind);
    if (d == nullptr) return std::nullopt;
    const core::Unit* unit = state.find_unit(d->target);
    if (unit == nullptr || unit->models.empty()) return std::nullopt;

    const StanceInfo stance = casualty_stance(cache, state, *unit);
    const std::map<core::ModelId, double> position = position_rank(state, *unit, stance.stance);
    const Worth worth = worth_of(cache, state, *unit);
    const double mean = mean_worth(worth);
    const std::vector<core::PlacedModel> placed = core::placed_models(*unit);

    const auto score_of = [&](core::ModelId id) {
        return removal_score(*unit, id, 0, stance.position_weight, position, worth, mean);
    };

    // 1. How many models this batch destroys: the same whatever the order for single-wound models, and nearly so
    //    for others (05.04: a wounded model takes the damage first). Counted on the engine's preview.
    std::size_t destroyed = 0;
    {
        std::vector<core::ModelId> picks;
        for (std::size_t guard = 0; guard <= unit->models.size() * 8; ++guard) {
            const std::optional<core::CasualtyPreview> pv = core::preview_casualties(state, picks);
            if (!pv) return std::nullopt;
            if (pv->done()) {
                for (const core::DamageResult& r : pv->results) destroyed += r.destroyed ? 1 : 0;
                break;
            }
            picks.push_back(pv->options.front());
        }
    }

    // 2. Which models to lose: wounded ones first (the engine forces them), then greedily the one leaving the unit
    //    in coherency with the lowest score; then swaps while one leaves fewer coherency losses (03.03).
    std::set<core::ModelId> lose;
    for (const core::Model& m : unit->models) {
        if (lose.size() < destroyed && m.wounds_remaining < m.characteristics.wounds) lose.insert(m.id);
    }
    const std::set<core::ModelId> forced = lose;
    const auto losses_of = [&](const std::set<core::ModelId>& set) { return coherency_losses(without(placed, set)); };
    while (lose.size() < destroyed) {
        std::optional<core::ModelId> best;
        int best_losses = 0;
        double best_score = 0.0;
        for (const core::Model& m : unit->models) {
            if (lose.count(m.id) > 0) continue;
            std::set<core::ModelId> trial = lose;
            trial.insert(m.id);
            const int l = losses_of(trial);
            const double sc = score_of(m.id);
            if (!best || l < best_losses || (l == best_losses && sc < best_score)) {
                best = m.id;
                best_losses = l;
                best_score = sc;
            }
        }
        if (!best) break;
        lose.insert(*best);
    }
    for (std::size_t iter = 0; iter < 2 * destroyed && losses_of(lose) > 0; ++iter) {
        const int now = losses_of(lose);
        std::optional<std::pair<core::ModelId, core::ModelId>> swap;  // (out of the set, into it)
        int swap_losses = now;
        double swap_score = 0.0;
        for (const core::ModelId out_id : lose) {
            if (forced.count(out_id) > 0) continue;
            for (const core::Model& m : unit->models) {
                if (lose.count(m.id) > 0) continue;
                std::set<core::ModelId> trial = lose;
                trial.erase(out_id);
                trial.insert(m.id);
                const int l = losses_of(trial);
                const double sc = score_of(m.id) - score_of(out_id);
                if (l < swap_losses || (swap && l == swap_losses && sc < swap_score)) {
                    swap = std::make_pair(out_id, m.id);
                    swap_losses = l;
                    swap_score = sc;
                }
            }
        }
        if (!swap) break;
        lose.erase(swap->first);
        lose.insert(swap->second);
    }

    // 3. The picks, step by step through the engine's preview: the chosen models (lowest score first) while one is
    //    a legal choice, else the legal choice with the lowest score.
    std::vector<core::ModelId> picks;
    int extra_total = 0;
    for (std::size_t guard = 0; guard <= unit->models.size() * 8; ++guard) {
        const std::optional<core::CasualtyPreview> pv = core::preview_casualties(state, picks);
        if (!pv) return std::nullopt;
        if (pv->done()) {
            std::set<core::ModelId> gone;
            for (const core::DamageResult& r : pv->results) {
                if (r.destroyed) gone.insert(r.model);
            }
            extra_total = coherency_losses(without(placed, gone));
            break;
        }
        std::optional<core::ModelId> best;
        bool best_chosen = false;
        double best_score = 0.0;
        for (const core::ModelId opt : pv->options) {
            const bool chosen = lose.count(opt) > 0;
            const double sc = score_of(opt);
            if (!best || (chosen && !best_chosen) || (chosen == best_chosen && sc < best_score)) {
                best = opt;
                best_chosen = chosen;
                best_score = sc;
            }
        }
        if (!best) return std::nullopt;
        picks.push_back(*best);
    }
    CasualtyOrder out;
    out.priority = picks;
    // The rest of the unit (no step picks them): in the same preference, for a stable answer.
    std::vector<core::ModelId> rest;
    for (const core::Model& m : unit->models) {
        if (std::find(picks.begin(), picks.end(), m.id) == picks.end()) rest.push_back(m.id);
    }
    std::stable_sort(rest.begin(), rest.end(), [&](core::ModelId a, core::ModelId b) {
        return removal_score(*unit, a, 0, stance.position_weight, position, worth, mean) <
               removal_score(*unit, b, 0, stance.position_weight, position, worth, mean);
    });
    out.priority.insert(out.priority.end(), rest.begin(), rest.end());
    out.reason = client::unit_display_name(state, unit->id) + ": casualties " + stance_name(stance.stance);
    if (!stance.why.empty()) out.reason += " (" + stance.why + ")";
    if (extra_total == 0) {
        out.reason += ", keeping coherency";
    } else {
        out.reason += ", coherency can't be kept";
    }
    return out;
}

std::optional<core::ModelId> coherency_removal(analysis::OddsCache& cache, const core::GameState& state,
                                               core::UnitId unit_id, std::string* reason) {
    const core::Unit* unit = state.find_unit(unit_id);
    if (unit == nullptr || unit->models.empty()) return std::nullopt;
    const StanceInfo stance = casualty_stance(cache, state, *unit);
    const std::map<core::ModelId, double> position = position_rank(state, *unit, stance.stance);
    const Worth worth = worth_of(cache, state, *unit);
    const double mean = mean_worth(worth);
    const std::vector<core::PlacedModel> placed = core::placed_models(*unit);
    std::optional<core::ModelId> best;
    double best_score = 0.0;
    int best_more = 0;
    for (const core::Model& m : unit->models) {
        const int more = coherency_losses(without(placed, {m.id}));
        const double s = removal_score(*unit, m.id, more, stance.position_weight, position, worth, mean);
        if (!best || s < best_score) {
            best = m.id;
            best_score = s;
            best_more = more;
        }
    }
    if (reason != nullptr && best) {
        *reason = client::unit_display_name(state, unit->id) + ": remove model " + std::to_string(best->v) + " to regain coherency (03.03)";
        if (best_more > 0) *reason += ", " + std::to_string(best_more) + " more to go";
    }
    return best;
}

std::optional<core::action::DeclareAllocationOrder> allocation_order(analysis::OddsCache& cache,
                                                                     const core::GameState& state,
                                                                     const std::vector<core::GameAction>& legal) {
    std::vector<const core::action::DeclareAllocationOrder*> orders;
    for (const core::GameAction& a : legal) {
        if (const auto* o = std::get_if<core::action::DeclareAllocationOrder>(&a)) orders.push_back(o);
    }
    if (orders.empty()) return std::nullopt;
    if (orders.size() == 1 || !state.flow.attack || !state.flow.attack->current) return *orders.front();
    const core::Unit* unit = state.find_unit(orders.front()->target);
    if (unit == nullptr) return *orders.front();
    const Worth worth = worth_of(cache, state, *unit);
    const std::vector<core::AllocationGroup>& groups = state.flow.attack->current->groups;
    // A group's value per wound: the damage goes to the cheapest first.
    std::vector<double> per_wound;
    for (const core::AllocationGroup& g : groups) {
        double value = 0.0;
        int wounds = 0;
        for (const core::ModelId id : g.models) {
            const core::Model* m = core::find_model(*unit, id);
            if (m == nullptr) continue;
            const auto it = worth.find(id);
            value += it != worth.end() ? it->second : 0.0;
            wounds += std::max(1, m->wounds_remaining);
        }
        per_wound.push_back(wounds > 0 ? value / wounds : 0.0);
    }
    const auto key = [&](const core::action::DeclareAllocationOrder& o) {
        std::vector<double> k;
        for (const std::size_t i : o.order) k.push_back(i < per_wound.size() ? per_wound[i] : 0.0);
        return k;
    };
    const core::action::DeclareAllocationOrder* best = orders.front();
    for (const auto* o : orders) {
        if (key(*o) < key(*best)) best = o;
    }
    return *best;
}

}  // namespace wh40k::ai::normal
