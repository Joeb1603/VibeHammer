#include "core/engine/casualties.hpp"

#include <algorithm>
#include <variant>

#include "core/model/keywords.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/random_characteristics.hpp"

namespace wh40k::core {

namespace {

// D-051: answers each damaging step in turn (05.04 / 06.02 call it for every step with ask_when_forced):
// a model already picked if one is legal, else the next pick; it stops at the first step that needs a pick
// not made yet. After a stop (or an illegal pick) it just takes the first option: those steps are dropped.
class PickReplay {
public:
    explicit PickReplay(std::span<const ModelId> picks) : picks_(picks) {}

    ModelId operator()(std::span<const ModelId> options) {
        if (stop_step_ || invalid_) return options.front();
        const std::size_t step = calls_++;
        for (std::size_t i = 0; i < used_; ++i) {
            if (contains(options, picks_[i])) return picks_[i];  // keeps taking its forced damage
        }
        if (used_ < picks_.size()) {
            const ModelId pick = picks_[used_];
            if (!contains(options, pick)) {
                invalid_ = true;
                return options.front();
            }
            ++used_;
            return pick;
        }
        stop_step_ = step;
        options_.assign(options.begin(), options.end());
        return options.front();
    }

    [[nodiscard]] bool valid() const noexcept { return !invalid_ && (stop_step_ || used_ == picks_.size()); }
    [[nodiscard]] std::optional<std::size_t> stop_step() const noexcept { return stop_step_; }
    [[nodiscard]] const std::vector<ModelId>& options() const noexcept { return options_; }

private:
    static bool contains(std::span<const ModelId> options, ModelId id) {
        return std::find(options.begin(), options.end(), id) != options.end();
    }

    std::span<const ModelId> picks_;
    std::size_t used_ = 0;   // picks answered so far
    std::size_t calls_ = 0;  // damaging steps so far
    std::optional<std::size_t> stop_step_;
    std::vector<ModelId> options_;
    bool invalid_ = false;
};

std::optional<CasualtyPreview> preview_attack(const GameState& state, std::span<const ModelId> picks) {
    const AttackFlow& f = *state.flow.attack;
    if (!f.current || !f.current->order || !f.current->save_rolls) return std::nullopt;
    const BatchInProgress& b = *f.current;
    const AttackBatch& batch = f.batches[b.batch];
    const Unit* target = state.find_unit(batch.target);
    if (target == nullptr) return std::nullopt;
    Unit copy = *target;
    PickReplay replay(picks);
    const ModelChooser chooser = [&replay](const Unit&, std::span<const ModelId> options) { return replay(options); };
    // D-069: a random D takes its maximum and rolls nothing (damage_stand_in).
    const InflictDamageResult r = inflict_damage(copy, order_groups(b.groups, *b.order), *b.save_rolls, batch.profile,
                                                 chooser, true, damage_stand_in);
    if (!replay.valid()) return std::nullopt;

    CasualtyPreview out;
    if (!batch.profile.damage.is_fixed()) out.random_damage = batch.profile.damage;
    std::size_t damaging = 0;
    bool stopped = false;
    for (const SaveStep& s : r.steps) {
        if (!s.damage) continue;
        if (!stopped && replay.stop_step() && damaging == *replay.stop_step()) {
            stopped = true;
            out.options = replay.options();
            out.next_damage = s.damage->damage;
        }
        ++damaging;
        if (stopped) {
            out.damage_left += s.damage->damage;  // the rest, as if each took the first option
        } else {
            out.results.push_back(*s.damage);
        }
    }
    if (!stopped) out.attacks_lost = r.attacks_lost;
    if (out.random_damage && !out.results.empty()) out.provisional_from = 1;  // after the first random roll
    return out;
}

// 06.02 for `count` pending mortal wounds on `unit_id`: Desperate Escape's (09.07) or [HAZARDOUS]'s (24.15).
std::optional<CasualtyPreview> preview_mortal_wounds(const GameState& state, UnitId unit_id, int count,
                                                     std::span<const ModelId> picks) {
    const Unit* unit = state.find_unit(unit_id);
    if (unit == nullptr || count <= 0) return std::nullopt;
    Unit copy = *unit;
    PickReplay replay(picks);
    const ModelChooser chooser = [&replay](const Unit&, std::span<const ModelId> options) { return replay(options); };
    const MortalWoundsResult r = inflict_mortal_wounds(copy, count, chooser, true);
    if (!replay.valid()) return std::nullopt;

    CasualtyPreview out;
    for (std::size_t i = 0; i < r.steps.size(); ++i) {
        if (replay.stop_step() && i == *replay.stop_step()) {
            out.options = replay.options();
            out.next_damage = 1;  // 06.02: each mortal wound is 1 damage
            out.damage_left = static_cast<int>(r.steps.size() - i);
            return out;
        }
        out.results.push_back(r.steps[i]);
    }
    out.attacks_lost = r.not_inflicted;
    return out;
}

}  // namespace

std::optional<CasualtyPreview> preview_casualties(const GameState& state, std::span<const ModelId> picks) {
    if (!state.pending || !std::holds_alternative<decision::OrderCasualties>(state.pending->kind)) {
        return std::nullopt;
    }
    // As OrderCasualties' apply (attack_flow.cpp) tells them apart.
    if (state.flow.attack && state.flow.attack->pending_mortal_wounds > 0) {
        return preview_mortal_wounds(state, state.flow.attack->attacker, state.flow.attack->pending_mortal_wounds,
                                     picks);
    }
    if (state.flow.attack) return preview_attack(state, picks);
    if (state.flow.move) {
        return preview_mortal_wounds(state, state.flow.move->unit, state.flow.move->pending_mortal_wounds, picks);
    }
    return std::nullopt;
}

}  // namespace wh40k::core
