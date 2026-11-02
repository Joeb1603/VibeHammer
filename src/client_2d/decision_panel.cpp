#include "decision_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <variant>

#include "action_text.hpp"
#include "core/engine/movement.hpp"
#include "decision_view.hpp"

namespace wh40k::client {

namespace {

const ImVec4 kHighlight{1.0f, 0.85f, 0.3f, 1.0f};
const ImVec4 kWarning{1.0f, 0.75f, 0.3f, 1.0f};

std::string unit_name(const core::GameState& gs, core::UnitId id) {
    const core::Unit* u = gs.find_unit(id);
    return (u != nullptr ? u->name : std::string("unit")) + " (" + std::to_string(id.v) + ")";
}

std::string shot_text(const core::GameState& gs, const ShotOption& o) {
    return o.profile + " -> " + unit_name(gs, o.declaration.target);
}

std::string shot_choice_text(const core::GameState& gs, const ShotPlan& plan, const ShotChoice& c) {
    for (const ShotOption& o : plan.options()) {
        if (o.weapon == c.weapon && o.declaration.profile_index == c.profile_index && o.declaration.target == c.target) {
            return shot_text(gs, o);
        }
    }
    return c.weapon;
}

std::string declaration_text(const core::GameState& gs, const ShotPlan& plan, const core::ShotDeclaration& d) {
    for (const ShotOption& o : plan.options()) {
        if (o.declaration == d) return shot_text(gs, o);
    }
    return "?";
}

std::string melee_text(const core::GameState& gs, const core::MeleeDeclaration& d, const MeleePlan& plan) {
    std::string weapon = "?";
    for (const MeleeOption& o : plan.options()) {
        if (o.declaration.model == d.model && o.declaration.weapon_index == d.weapon_index &&
            o.declaration.profile_index == d.profile_index) {
            weapon = o.profile;
        }
    }
    std::string targets;
    for (const core::MeleeTarget& t : d.targets) {
        targets += (targets.empty() ? "" : ", ") + unit_name(gs, t.unit) + " x" + std::to_string(t.attacks);
    }
    return weapon + " -> " + targets;
}

}  // namespace

void DecisionPanel::sync(const GameSession& session) {
    if (session.history().size() == synced_step_) return;
    synced_step_ = session.history().size();
    shots_.clear();
    melee_.reset();
    casualties_.reset();
    for (const core::GameAction& t : make_decision_view(session.legal()).templates) {
        if (const auto* s = std::get_if<core::action::Shoot>(&t)) {
            shots_.emplace_back(session.state(), s->unit, s->type);
        } else if (const auto* f = std::get_if<core::action::Fight>(&t)) {
            melee_.emplace(session.state(), f->unit);
        } else if (const auto* c = std::get_if<core::action::OrderCasualties>(&t)) {
            casualties_.emplace(session.state(), c->target);
        }
    }
}

std::optional<core::GameAction> DecisionPanel::draw(const GameSession& session,
                                                    std::optional<core::ModelId> selected_model) {
    const core::GameState& gs = session.state();
    if (!session.pending()) {
        ImGui::TextWrapped("%s", gs.result ? gs.result->reason.c_str() : "Nothing to decide.");
        return std::nullopt;
    }
    sync(session);
    ImGui::TextColored(kHighlight, "%s", core::describe(*session.pending()).c_str());
    std::optional<core::GameAction> chosen;
    const DecisionView view = make_decision_view(session.legal());

    for (std::size_t i = 0; i < view.buttons.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button(action_label(gs, view.buttons[i]).c_str())) chosen = view.buttons[i];
        ImGui::PopID();
    }

    bool path_hint = false;
    for (const core::GameAction& t : view.templates) {
        switch (*editor_kind(t)) {
            case EditorKind::Path:
                path_hint = true;
                break;
            case EditorKind::Shot:
                if (auto a = draw_shot_editor(session, std::get<core::action::Shoot>(t), selected_model)) chosen = a;
                break;
            case EditorKind::Melee:
                if (auto a = draw_melee_editor(session, std::get<core::action::Fight>(t), selected_model)) chosen = a;
                break;
            case EditorKind::Casualties:
                if (auto a = draw_casualty_editor(session, std::get<core::action::OrderCasualties>(t))) chosen = a;
                break;
        }
    }
    if (path_hint) {
        ImGui::Separator();
        ImGui::TextWrapped("Drag a model to add a straight line to its path, or drag the diamond to move the "
                           "whole unit. Then commit or reset the move.");
        // D-024: core's read-only query says what committing would do now.
        const std::vector<std::string> problems = core::move_end_problems(gs);
        if (problems.empty()) {
            ImGui::TextDisabled("The move can end here.");
        } else {
            ImGui::TextColored(kWarning, "Committing now would undo the move:");
            for (const std::string& p : problems) ImGui::BulletText("%s", p.c_str());
        }
    }
    return chosen;
}

std::optional<core::GameAction> DecisionPanel::draw_shot_editor(const GameSession& session,
                                                                const core::action::Shoot& tmpl,
                                                                std::optional<core::ModelId> selected_model) {
    const core::GameState& gs = session.state();
    auto it = std::find_if(shots_.begin(), shots_.end(),
                           [&](const ShotPlan& p) { return p.unit() == tmpl.unit && p.type() == tmpl.type; });
    if (it == shots_.end()) return std::nullopt;
    ShotPlan& plan = *it;
    std::optional<core::GameAction> chosen;
    ImGui::PushID(static_cast<int>(tmpl.unit.v * 10 + static_cast<unsigned>(tmpl.type)));
    ImGui::Separator();
    if (ImGui::CollapsingHeader(action_label(gs, tmpl).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        // D-001: one profile and target for the whole unit by default.
        const std::string current = plan.unit_default() ? shot_choice_text(gs, plan, *plan.unit_default()) : "-";
        if (ImGui::BeginCombo("Whole unit", current.c_str())) {
            for (const ShotChoice& c : plan.choices()) {
                if (ImGui::Selectable(shot_choice_text(gs, plan, c).c_str(), plan.unit_default() == c)) {
                    plan.set_default(c);
                }
            }
            ImGui::EndCombo();
        }
        const core::Unit& u = *gs.find_unit(tmpl.unit);
        for (const core::Model& m : u.models) {
            ImGui::PushID(static_cast<int>(m.id.v));
            const bool selected = selected_model == m.id;
            const auto d = plan.declaration_for(m.id);
            const std::string now = d ? declaration_text(gs, plan, *d) : "doesn't fire";
            const std::string label = "Model " + std::to_string(m.id.v) + (selected ? " *" : "");
            if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
            if (ImGui::BeginCombo(label.c_str(), (now + (plan.is_overridden(m.id) ? "" : " (unit)")).c_str())) {
                if (ImGui::Selectable("Use the unit's choice", !plan.is_overridden(m.id))) plan.clear_override(m.id);
                if (ImGui::Selectable("Doesn't fire")) plan.override_model(m.id, std::nullopt);
                for (const ShotOption& o : plan.options_for(m.id)) {
                    if (ImGui::Selectable(shot_text(gs, o).c_str(), d && *d == o.declaration)) {
                        plan.override_model(m.id, o.declaration);
                    }
                }
                ImGui::EndCombo();
            }
            if (selected) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (ImGui::Button("Shoot")) chosen = plan.expand();
    }
    ImGui::PopID();
    return chosen;
}

std::optional<core::GameAction> DecisionPanel::draw_melee_editor(const GameSession& session,
                                                                 const core::action::Fight& tmpl,
                                                                 std::optional<core::ModelId> selected_model) {
    if (!melee_ || melee_->unit() != tmpl.unit) return std::nullopt;
    const core::GameState& gs = session.state();
    MeleePlan& plan = *melee_;
    std::optional<core::GameAction> chosen;
    ImGui::Separator();
    ImGui::TextUnformatted(("Melee weapons: " + unit_name(gs, tmpl.unit)).c_str());
    if (plan.unit_default()) {
        const MeleeChoice& def = *plan.unit_default();
        const std::string current = def.weapon + " -> " + unit_name(gs, def.target);
        if (ImGui::BeginCombo("Whole unit", current.c_str())) {
            for (const MeleeChoice& c : plan.choices()) {
                if (ImGui::Selectable((c.weapon + " -> " + unit_name(gs, c.target)).c_str(), def == c)) {
                    plan.set_default(c);
                }
            }
            ImGui::EndCombo();
        }
    }
    for (const core::Model& m : gs.find_unit(tmpl.unit)->models) {
        const auto d = plan.declaration_for(m.id);
        if (!d) continue;  // no enemy unit engaged with this model
        ImGui::PushID(static_cast<int>(m.id.v));
        const bool selected = selected_model == m.id;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
        const std::string label = "Model " + std::to_string(m.id.v) + (selected ? " *" : "");
        if (ImGui::BeginCombo(label.c_str(), melee_text(gs, *d, plan).c_str())) {
            if (ImGui::Selectable("Use the unit's choice", !plan.is_overridden(m.id))) plan.clear_override(m.id);
            for (const MeleeOption& o : plan.options_for(m.id)) {
                if (ImGui::Selectable(melee_text(gs, o.declaration, plan).c_str())) plan.override_model(m.id, o.declaration);
            }
            ImGui::EndCombo();
        }
        if (selected) ImGui::PopStyleColor();
        // 04.02 Splitting Melee Attacks: one count per engaged target of this weapon.
        std::vector<core::UnitId> targets;
        for (const MeleeOption& o : plan.options_for(m.id)) {
            if (o.declaration.weapon_index == d->weapon_index && o.declaration.profile_index == d->profile_index) {
                targets.push_back(o.declaration.targets.front().unit);
            }
        }
        if (targets.size() > 1 && ImGui::TreeNode("Split attacks")) {
            core::MeleeDeclaration split = *d;
            split.targets.clear();
            bool changed = false;
            for (const core::UnitId t : targets) {
                int n = 0;
                for (const core::MeleeTarget& mt : d->targets) {
                    if (mt.unit == t) n = mt.attacks;
                }
                changed |= ImGui::InputInt(unit_name(gs, t).c_str(), &n);
                if (n > 0) split.targets.push_back(core::MeleeTarget{t, n});
            }
            if (changed) plan.override_model(m.id, split);  // the engine checks the split when submitted
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Fight")) chosen = plan.expand();
    return chosen;
}

std::optional<core::GameAction> DecisionPanel::draw_casualty_editor(const GameSession& session,
                                                                    const core::action::OrderCasualties& tmpl) {
    if (!casualties_ || casualties_->target() != tmpl.target) return std::nullopt;
    const core::GameState& gs = session.state();
    std::optional<core::GameAction> chosen;
    ImGui::Separator();
    ImGui::TextWrapped("Casualty order for %s, most preferred first (D-013):", unit_name(gs, tmpl.target).c_str());
    const auto& order = casualties_->priority();
    for (std::size_t i = 0; i < order.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) casualties_->move_up(i);
        ImGui::SameLine();
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) casualties_->move_down(i);
        ImGui::SameLine();
        ImGui::Text("%zu. model %u", i + 1, order[i].v);
        ImGui::PopID();
    }
    if (ImGui::Button("Confirm casualty order")) chosen = casualties_->action();
    return chosen;
}

}  // namespace wh40k::client
