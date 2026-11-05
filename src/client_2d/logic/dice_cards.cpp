#include "dice_cards.hpp"

#include <algorithm>
#include <optional>
#include <type_traits>
#include <variant>

#include "core/engine/events.hpp"
#include "core/rules/attack_sequence.hpp"

namespace wh40k::client {

namespace {

namespace ev = core::event;

std::string unit_name(const core::GameState& state, core::UnitId id) {
    const core::Unit* u = state.find_unit(id);
    return u != nullptr ? u->name : "unit " + std::to_string(id.v);
}

core::Side side_of(const core::GameState& state, core::UnitId id, core::Side fallback) {
    const core::Unit* u = state.find_unit(id);
    return u != nullptr ? u->side : fallback;
}

core::Side other(core::Side s) { return s == core::Side::A ? core::Side::B : core::Side::A; }

std::string plural(int n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// What the attacks in the log so far are (04.03): who attacks, with which batches and weapons.
struct AttackContext {
    std::optional<core::UnitId> attacker;
    std::vector<core::AttackBatch> batches;
    std::vector<core::WeaponSelection> selections;  // the attacker's last ShootingStarted / MeleeAttacksDeclared
    bool melee = false;
    std::optional<std::size_t> batch;  // the batch being resolved
    std::vector<core::AllocationGroup> groups;  // the current target's groups, in allocation order
};

// The weapon profile a batch attacks with (its first selection), e.g. "Bolt Rifle - Focused Fire".
std::string weapon_name(const core::GameState& state, const AttackContext& c, const core::AttackBatch& batch) {
    if (batch.selections.empty() || batch.selections.front() >= c.selections.size()) return "";
    const core::WeaponSelection& s = c.selections[batch.selections.front()];
    for (const core::Unit& u : state.units) {
        for (const core::Model& m : u.models) {
            if (m.id != s.attacker || s.weapon_index >= m.weapons.size()) continue;
            const core::Weapon& w = m.weapons[s.weapon_index];
            return s.profile_index < w.profiles.size() ? w.profiles[s.profile_index].name : w.name;
        }
    }
    return "";  // the model has been destroyed since
}

const core::AttackBatch* current_batch(const AttackContext& c) {
    return c.batch && *c.batch < c.batches.size() ? &c.batches[*c.batch] : nullptr;
}

std::string attack_purpose(const core::GameState& state, const AttackContext& c) {
    const core::AttackBatch* batch = current_batch(c);
    if (!c.attacker || batch == nullptr) return "";
    std::string text = unit_name(state, *c.attacker) + (c.melee ? " fighting " : " shooting ") +
                       unit_name(state, batch->target);
    if (const std::string weapon = weapon_name(state, c, *batch); !weapon.empty()) text += " (" + weapon + ")";
    return text;
}

DiceCard hit_card(const core::GameState& state, const AttackContext& c, const core::HitRolls& rolls) {
    DiceCard card{"Hit rolls", attack_purpose(state, c), "", "", "05.01", core::Side::A, {}};
    if (c.attacker) card.roller = side_of(state, *c.attacker, core::Side::A);
    if (const core::AttackBatch* batch = current_batch(c)) {
        if (batch->profile.skill) {
            card.need = "Need " + std::to_string(*batch->profile.skill) + "+ to hit";
            if (batch->profile.hit_modifier != 0) {
                card.need += " (modifier " + std::string(batch->profile.hit_modifier > 0 ? "+" : "") +
                             std::to_string(batch->profile.hit_modifier) + ")";
            }
            card.need += "; an unmodified 6 is a critical hit";
        } else {
            card.need = "Hits automatically (Torrent)";
        }
    }
    int hits = 0;
    int criticals = 0;
    for (const core::HitRoll& r : rolls.rolls) {
        DieFace d;
        d.value = r.unmodified.value_or(0);
        d.critical = r.outcome == core::HitOutcome::CriticalHit;
        d.pass = r.outcome != core::HitOutcome::Fail;
        hits += d.pass ? 1 : 0;
        criticals += d.critical ? 1 : 0;
        card.dice.push_back(d);
    }
    card.result = plural(hits, "hit", "hits");
    if (criticals > 0) card.result += " (" + std::to_string(criticals) + " critical)";
    return card;
}

DiceCard wound_card(const core::GameState& state, const AttackContext& c, const core::WoundRolls& rolls) {
    DiceCard card{"Wound rolls", attack_purpose(state, c), "", "", "05.02", core::Side::A, {}};
    if (c.attacker) card.roller = side_of(state, *c.attacker, core::Side::A);
    card.need = "Need " + std::to_string(rolls.required) + "+ to wound; an unmodified 6 is a critical wound";
    int wounds = 0;
    int criticals = 0;
    for (const core::WoundRoll& r : rolls.rolls) {
        DieFace d;
        d.value = r.unmodified;
        d.critical = r.outcome == core::WoundOutcome::CriticalWound;
        d.pass = r.outcome != core::WoundOutcome::Fail;
        wounds += d.pass ? 1 : 0;
        criticals += d.critical ? 1 : 0;
        card.dice.push_back(d);
    }
    card.result = plural(wounds, "wound", "wounds");
    if (criticals > 0) card.result += " (" + std::to_string(criticals) + " critical)";
    if (rolls.rolls.empty()) card.result = "No hits to roll for";
    return card;
}

DiceCard save_card(const core::GameState& state, const AttackContext& c, core::UnitId target,
                   const std::vector<int>& rolls) {
    DiceCard card{"Save rolls", "", "", "", "05.03", core::Side::A, {}};
    card.roller = side_of(state, target, c.attacker ? other(side_of(state, *c.attacker, core::Side::A)) : core::Side::B);
    card.purpose = unit_name(state, target) + " saving";
    const core::AttackBatch* batch = current_batch(c);
    if (c.attacker) card.purpose += " against " + unit_name(state, *c.attacker);
    const int ap = batch != nullptr ? batch->profile.armour_penetration : 0;
    // D-024: core's 05.04 check, against the first group in allocation order (each V1 unit has one group).
    const core::AllocationGroup* group = c.groups.empty() ? nullptr : &c.groups.front();
    if (group != nullptr) {
        std::string need;
        if (group->save) {
            need = "Need " + std::to_string(std::max(2, *group->save - ap)) + "+ to save (Sv " +
                   std::to_string(*group->save) + "+, AP " + std::to_string(ap) + ")";
        }
        if (group->invuln_save) {
            need += (need.empty() ? "Need " : "; or ") + std::to_string(*group->invuln_save) + "+ invulnerable";
        }
        card.need = need.empty() ? "No save" : need + "; an unmodified 1 always fails";
    }
    int saved = 0;
    for (const int r : rolls) {
        DieFace d;
        d.value = r;
        d.pass = group != nullptr && core::check_save_roll(r, *group, ap).outcome == core::SaveOutcome::AttackFails;
        saved += d.pass ? 1 : 0;
        card.dice.push_back(d);
    }
    const int failed = static_cast<int>(rolls.size()) - saved;
    card.result = plural(saved, "save", "saves") + " made, " + std::to_string(failed) + " failed";
    return card;
}

std::vector<DieFace> two_dice(const core::TwoD6& roll, bool pass, bool neutral) {
    return {DieFace{roll.dice[0], pass, false, neutral}, DieFace{roll.dice[1], pass, false, neutral}};
}

}  // namespace

std::vector<DiceCard> dice_cards(const core::GameState& state, const std::vector<LogEntry>& log,
                                 std::size_t from) {
    std::vector<DiceCard> cards;
    AttackContext c;
    for (std::size_t i = 0; i < log.size(); ++i) {
        const bool show = i >= from;
        for (const core::GameEvent& event : log[i].events) {
            std::visit(
                [&](const auto& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, ev::ShootingStarted>) {
                        c.selections = e.selections;
                        c.melee = false;
                    } else if constexpr (std::is_same_v<T, ev::MeleeAttacksDeclared>) {
                        c.selections = e.selections;
                        c.melee = true;
                    } else if constexpr (std::is_same_v<T, ev::AttackBatchesGathered>) {
                        c.attacker = e.attacker;
                        c.batches = e.batches;
                        c.batch.reset();
                        c.groups.clear();
                    } else if constexpr (std::is_same_v<T, ev::AttackBatchSelected>) {
                        c.attacker = e.attacker;
                        c.batch = e.batch;
                        c.groups.clear();
                    } else if constexpr (std::is_same_v<T, ev::AllocationOrderDeclared>) {
                        c.groups = e.groups;
                    } else if constexpr (std::is_same_v<T, ev::HitRollsMade>) {
                        c.batch = e.batch;
                        if (show) cards.push_back(hit_card(state, c, e.rolls));
                    } else if constexpr (std::is_same_v<T, ev::WoundRollsMade>) {
                        c.batch = e.batch;
                        if (show) cards.push_back(wound_card(state, c, e.rolls));
                    } else if constexpr (std::is_same_v<T, ev::SaveRollsMade>) {
                        if (show) cards.push_back(save_card(state, c, e.target, e.rolls));
                    } else if constexpr (std::is_same_v<T, ev::AdvanceRolled>) {
                        if (!show) return;
                        DiceCard card{"Advance roll", unit_name(state, e.unit) + " advancing",
                                      "Add the roll to the unit's Move this phase", "", "09.06",
                                      side_of(state, e.unit, core::Side::A), {DieFace{e.roll, true, false, true}}};
                        card.result = "+" + std::to_string(e.roll) + "\" to Move" +
                                      (e.reused ? " (the roll from earlier this phase)" : "");
                        cards.push_back(card);
                    } else if constexpr (std::is_same_v<T, ev::ChargeRolled>) {
                        if (!show) return;
                        DiceCard card{"Charge roll", unit_name(state, e.unit) + " charging",
                                      "2D6: how far the charge can move (it must end in engagement range)", "",
                                      "11.02", side_of(state, e.unit, core::Side::A),
                                      two_dice(e.roll, true, true)};
                        card.result = std::to_string(e.roll.total()) + "\" charge";
                        if (e.eligible_targets.empty()) card.result += ": no unit can be reached";
                        cards.push_back(card);
                    } else if constexpr (std::is_same_v<T, ev::HazardRollsMade>) {
                        if (!show) return;
                        DiceCard card{"Hazard rolls", unit_name(state, e.unit), "Each 1-2 fails", "", "06.03",
                                      side_of(state, e.unit, core::Side::A), {}};
                        for (const int r : e.rolls.rolls) card.dice.push_back(DieFace{r, r > 2, false, false});
                        card.result = plural(e.rolls.failures, "failure", "failures") + ": " +
                                      plural(e.rolls.mortal_wounds, "mortal wound", "mortal wounds");
                        cards.push_back(card);
                    } else if constexpr (std::is_same_v<T, ev::BattleShockRolled>) {
                        if (!show) return;
                        const core::LeadershipRoll& l = e.roll.leadership;
                        DiceCard card{"Battle-shock roll", unit_name(state, e.unit),
                                      "2D6: need " + std::to_string(l.target) + "+ to pass", "", "01.07",
                                      side_of(state, e.unit, core::Side::A), two_dice(l.roll, l.success, false)};
                        card.result = std::to_string(l.roll.total()) + ": " +
                                      (l.success ? "passed" : (e.roll.becomes_battle_shocked ? "battle-shocked" : "failed"));
                        cards.push_back(card);
                    }
                },
                event);
        }
    }
    return cards;
}

}  // namespace wh40k::client
