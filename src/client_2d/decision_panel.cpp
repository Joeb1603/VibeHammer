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

// Buttons laid out left to right, wrapping onto a new line when the next one would not fit (D-030).
class ButtonRow {
public:
    ButtonRow() : max_x_(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}
    bool button(const std::string& label) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float w = ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
        if (!first_ && ImGui::GetItemRectMax().x + style.ItemSpacing.x + w <= max_x_) ImGui::SameLine();
        first_ = false;
        return ImGui::Button(label.c_str());
    }

private:
    float max_x_;
    bool first_ = true;
};

// The decision's headline, a little larger than the body text.
void headline(const std::string& text) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    ImGui::TextColored(kHighlight, "%s", text.c_str());
    ImGui::PopFont();
}

// The bar's main action: full column width and taller than the other buttons. Green for ending a phase.
bool primary_button(const std::string& label, bool green) {
    if (green) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.48f, 0.26f, 1.0f));
    }
    const bool pressed =
        ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 1.4f));
    if (green) ImGui::PopStyleColor(3);
    return pressed;
}

// D-030: the bar's two columns, the decision on the left and its buttons on the right. The right column is
// wide enough for `widest` (the primary button's label), within a third to a half of the bar.
bool begin_bar(const std::string& widest) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float label = ImGui::CalcTextSize(widest.c_str()).x + ImGui::GetStyle().FramePadding.x * 6.0f;
    const float right = std::clamp(label, avail * 0.3f, avail * 0.5f);
    if (!ImGui::BeginTable("##bar", 2, ImGuiTableFlags_BordersInnerV)) return false;
    ImGui::TableSetupColumn("decision", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed, right);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    return true;
}

// During a move: whether committing now would keep it, on one line (the full list in a tooltip).
void move_status(const core::GameState& gs) {
    // D-024: core's read-only query says what committing would do now.
    const std::vector<std::string> problems = core::move_end_problems(gs);
    if (problems.empty()) {
        ImGui::TextDisabled("The move can end here.");
        return;
    }
    std::string line = "Committing now would undo the move: " + problems.front();
    if (problems.size() > 1) line += " (+" + std::to_string(problems.size() - 1) + " more)";
    ImGui::TextColored(kWarning, "%s", line.c_str());
    if (ImGui::IsItemHovered() && problems.size() > 1) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        for (const std::string& p : problems) ImGui::BulletText("%s", p.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

}  // namespace

void DecisionPanel::sync(const GameSession& session) {
    if (synced_step_ == session.history().size()) return;
    synced_step_ = session.history().size();
    view_ = make_decision_view(session.legal());
    shots_.clear();
    melee_.reset();
    casualties_.reset();
    for (const core::GameAction& t : view_.templates) {
        if (const auto* s = std::get_if<core::action::Shoot>(&t)) {
            shots_.emplace_back(session.state(), s->unit, s->type);
        } else if (const auto* f = std::get_if<core::action::Fight>(&t)) {
            melee_.emplace(session.state(), f->unit);
        } else if (const auto* c = std::get_if<core::action::OrderCasualties>(&t)) {
            casualties_.emplace(session.state(), c->target);
        }
    }
}

std::vector<core::GameAction> DecisionPanel::draw(const GameSession& session,
                                                  std::optional<core::ModelId> selected_model,
                                                  BoardInteraction* board) {
    const core::GameState& gs = session.state();
    if (!session.pending()) {
        ImGui::TextWrapped("%s", gs.result ? gs.result->reason.c_str() : "Nothing to decide.");
        return {};
    }
    sync(session);
    if (board != nullptr && board->active()) return draw_board_mode(session, *board, selected_model);
    if (auto a = draw_sidebar_mode(session, selected_model)) return {*a};
    return {};
}

std::optional<core::GameAction> DecisionPanel::draw_sidebar_mode(const GameSession& session,
                                                                 std::optional<core::ModelId> selected_model) {
    const core::GameState& gs = session.state();
    std::optional<core::GameAction> chosen;
    const DecisionView& view = view_;
    // D-030: a lone button with no editor is the bar's main action, drawn large on the right.
    const bool lone = view.buttons.size() == 1 && view.templates.empty();
    if (!begin_bar(lone ? action_label(gs, view.buttons.front()) : std::string())) return std::nullopt;

    headline(core::describe(*session.pending()));
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
        ImGui::TextWrapped("Drag a model to add a straight line to its path, or drag the diamond to move the "
                           "whole unit. Then commit or reset the move.");
        move_status(gs);
    }

    ImGui::TableNextColumn();
    if (lone) {
        if (primary_button(action_label(gs, view.buttons.front()), false)) chosen = view.buttons.front();
    } else {
        ButtonRow row;
        for (std::size_t i = 0; i < view.buttons.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (row.button(action_label(gs, view.buttons[i]))) chosen = view.buttons[i];
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return chosen;
}

std::vector<core::GameAction> DecisionPanel::draw_board_mode(const GameSession& session, BoardInteraction& board,
                                                             std::optional<core::ModelId> selected_model) {
    const core::GameState& gs = session.state();
    std::optional<core::GameAction> chosen;
    std::vector<core::GameAction> end_actions;
    const auto end = board.end_phase();
    if (!begin_bar(end ? end->label : std::string())) return {};

    // D-029: while picking a target the headline says so; otherwise it is the decision.
    const std::string prompt = board.prompt(gs);
    headline(prompt.empty() ? core::describe(*session.pending()) : prompt);
    ImGui::TextWrapped("%s", board.hint(gs).c_str());
    // The selected shooter's choices (D-001): the weapon picked on the target fires these.
    if (ShotPlan* plan = board.shot_plan()) {
        if (auto a = draw_shot_plan(gs, *plan, std::string(), selected_model)) chosen = a;
    }
    for (const core::GameAction& t : view_.templates) {
        if (editor_kind(t) == EditorKind::Path) {
            move_status(gs);
            break;
        }
    }

    ImGui::TableNextColumn();
    {
        ButtonRow row;
        if (const auto confirm = board.confirm()) {
            if (row.button(action_label(gs, *confirm))) chosen = *confirm;
        }
        const std::vector<core::GameAction> steps = board.step_buttons();
        for (std::size_t i = 0; i < steps.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (row.button(action_label(gs, steps[i]))) chosen = steps[i];
            ImGui::PopID();
        }
    }
    // The main action when nothing is left to do on the board.
    if (end && primary_button(end->label, true)) end_actions = end->actions;
    ImGui::EndTable();
    if (!end_actions.empty()) return end_actions;
    if (chosen) return {*chosen};
    return {};
}

std::optional<core::GameAction> DecisionPanel::draw_shot_editor(const GameSession& session,
                                                                const core::action::Shoot& tmpl,
                                                                std::optional<core::ModelId> selected_model) {
    const core::GameState& gs = session.state();
    auto it = std::find_if(shots_.begin(), shots_.end(),
                           [&](const ShotPlan& p) { return p.unit() == tmpl.unit && p.type() == tmpl.type; });
    if (it == shots_.end()) return std::nullopt;
    return draw_shot_plan(gs, *it, action_label(gs, tmpl), selected_model);
}

std::optional<core::GameAction> DecisionPanel::draw_shot_plan(const core::GameState& gs, ShotPlan& plan,
                                                              const std::string& title,
                                                              std::optional<core::ModelId> selected_model) {
    std::optional<core::GameAction> chosen;
    ImGui::PushID(static_cast<int>(plan.unit().v * 10 + static_cast<unsigned>(plan.type())));
    const float combo_w = ImGui::GetFontSize() * 18.0f;
    const core::Unit& u = *gs.find_unit(plan.unit());
    // One model's own choice (D-001), marked "(unit)" while it follows the unit's.
    auto model_combo = [&](const core::Model& m) {
        ImGui::PushID(static_cast<int>(m.id.v));
        const bool selected = selected_model == m.id;
        const auto d = plan.declaration_for(m.id);
        const std::string now = d ? declaration_text(gs, plan, *d) : "doesn't fire";
        const std::string label = "Model " + std::to_string(m.id.v) + (selected ? " *" : "");
        if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
        ImGui::SetNextItemWidth(combo_w);
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
    };

    if (!title.empty()) ImGui::TextUnformatted(title.c_str());
    // D-001: one profile and target for the whole unit by default.
    const std::string current = plan.unit_default() ? shot_choice_text(gs, plan, *plan.unit_default()) : "-";
    ImGui::SetNextItemWidth(combo_w);
    if (ImGui::BeginCombo("Whole unit", current.c_str())) {
        for (const ShotChoice& c : plan.choices()) {
            if (ImGui::Selectable(shot_choice_text(gs, plan, c).c_str(), plan.unit_default() == c)) {
                plan.set_default(c);
            }
        }
        ImGui::EndCombo();
    }
    // D-030: every model's choice is in a popup; the model selected on the board gets its own combo here.
    std::size_t changed = 0;
    for (const core::Model& m : u.models) changed += plan.is_overridden(m.id) ? 1 : 0;
    ImGui::SameLine();
    const std::string per_model =
        "Per-model weapons..." + (changed > 0 ? " (" + std::to_string(changed) + " changed)" : std::string());
    if (ImGui::Button(per_model.c_str())) ImGui::OpenPopup("##shot_models");
    ImGui::SameLine();
    if (ImGui::Button("Shoot")) chosen = plan.expand();
    for (const core::Model& m : u.models) {
        if (selected_model == m.id) {
            ImGui::SameLine();
            model_combo(m);
        }
    }
    if (ImGui::BeginPopup("##shot_models")) {
        ImGui::TextDisabled("Each model's weapon and target (D-001)");
        for (const core::Model& m : u.models) model_combo(m);
        ImGui::EndPopup();
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
    const float combo_w = ImGui::GetFontSize() * 18.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(("Melee weapons: " + unit_name(gs, tmpl.unit)).c_str());
    ImGui::SameLine();
    if (plan.unit_default()) {
        const MeleeChoice& def = *plan.unit_default();
        const std::string current = def.weapon + " -> " + unit_name(gs, def.target);
        ImGui::SetNextItemWidth(combo_w);
        if (ImGui::BeginCombo("Whole unit", current.c_str())) {
            for (const MeleeChoice& c : plan.choices()) {
                if (ImGui::Selectable((c.weapon + " -> " + unit_name(gs, c.target)).c_str(), def == c)) {
                    plan.set_default(c);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    // D-030: the per-model choices and attack splits are in a popup.
    std::size_t overridden = 0;
    for (const core::Model& m : gs.find_unit(tmpl.unit)->models) overridden += plan.is_overridden(m.id) ? 1 : 0;
    const std::string per_model = "Per-model / split attacks..." +
                                  (overridden > 0 ? " (" + std::to_string(overridden) + " changed)" : std::string());
    if (ImGui::Button(per_model.c_str())) ImGui::OpenPopup("##melee_models");
    ImGui::SameLine();
    if (ImGui::Button("Fight")) chosen = plan.expand();
    if (!ImGui::BeginPopup("##melee_models")) return chosen;
    ImGui::TextDisabled("Each model's melee weapon and targets (D-021)");
    for (const core::Model& m : gs.find_unit(tmpl.unit)->models) {
        const auto d = plan.declaration_for(m.id);
        if (!d) continue;  // no enemy unit engaged with this model
        ImGui::PushID(static_cast<int>(m.id.v));
        const bool selected = selected_model == m.id;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
        const std::string label = "Model " + std::to_string(m.id.v) + (selected ? " *" : "");
        ImGui::SetNextItemWidth(combo_w);
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
    ImGui::EndPopup();
    return chosen;
}

std::optional<core::GameAction> DecisionPanel::draw_casualty_editor(const GameSession& session,
                                                                    const core::action::OrderCasualties& tmpl) {
    if (!casualties_ || casualties_->target() != tmpl.target) return std::nullopt;
    const core::GameState& gs = session.state();
    std::optional<core::GameAction> chosen;
    const auto& order = casualties_->priority();
    // D-030: the order on one line; reordering is in a popup.
    std::string summary = "Casualty order for " + unit_name(gs, tmpl.target) + " (D-013): models ";
    constexpr std::size_t kShown = 8;
    for (std::size_t i = 0; i < order.size() && i < kShown; ++i) {
        summary += (i > 0 ? ", " : "") + std::to_string(order[i].v);
    }
    if (order.size() > kShown) summary += ", ...";
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(summary.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Edit order...")) ImGui::OpenPopup("##casualty_order");
    ImGui::SameLine();
    if (ImGui::Button("Confirm casualty order")) chosen = casualties_->action();
    if (ImGui::BeginPopup("##casualty_order")) {
        ImGui::TextDisabled("Most preferred first");
        for (std::size_t i = 0; i < order.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::ArrowButton("up", ImGuiDir_Up)) casualties_->move_up(i);
            ImGui::SameLine();
            if (ImGui::ArrowButton("down", ImGuiDir_Down)) casualties_->move_down(i);
            ImGui::SameLine();
            ImGui::Text("%zu. model %u", i + 1, order[i].v);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    return chosen;
}

}  // namespace wh40k::client
