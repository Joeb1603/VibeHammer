#include "decision_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <variant>

#include "action_text.hpp"
#include "auto_charge_move.hpp"
#include "auto_fight_move.hpp"
#include "board_view.hpp"
#include "unit_style.hpp"
#include "casualty_order.hpp"
#include "core/engine/movement.hpp"
#include "decision_view.hpp"

namespace wh40k::client {

namespace {

const ImVec4 kHighlight{1.0f, 0.85f, 0.3f, 1.0f};
const ImVec4 kWarning{1.0f, 0.75f, 0.3f, 1.0f};

std::string unit_name(const core::GameState& gs, core::UnitId id) {
    const core::Unit* u = gs.find_unit(id);  // D-065: the same numbered name as the board label
    return (u != nullptr ? unit_display_name(gs, id) : std::string("unit")) + " (" + std::to_string(id.v) + ")";
}

std::string shot_text(const core::GameState& gs, const ShotOption& o) {
    return o.profile + " -> " + unit_name(gs, o.declaration.target);
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

// The decision's headline, a little larger than the body text, after a tag in the colour of the side that
// must act (D-030): "Side A to act: Space Marines".
void headline(const core::GameState& gs, core::Side side, const std::string& text) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    const Color c = side_color(side);
    const ImVec4 colour(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, colour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colour);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colour);
    const std::string tag = std::string(side == core::Side::A ? "Side A" : "Side B") + " to act: " + gs.side(side).name;
    ImGui::Button(tag.c_str());
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.6f);
    ImGui::AlignTextToFramePadding();
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
    auto_move_message_.clear();
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
    // D-051: casualties are picked on the board; without the board only the default order is offered.
    if (casualties_) {
        if (!begin_bar("Use default order")) return std::nullopt;
        headline(gs, session.pending()->side, core::describe(*session.pending()));
        ImGui::TextWrapped("Pick the casualties on the board, or use the default order.");
        ImGui::TableNextColumn();
        if (primary_button("Use default order", false)) chosen = casualties_->action();
        ImGui::SetItemTooltip("%s", kDefaultCasualtyOrderText);  // D-058
        ImGui::EndTable();
        return chosen;
    }
    // D-030: a lone button with no editor is the bar's main action, drawn large on the right.
    const bool lone = view.buttons.size() == 1 && view.templates.empty();
    if (!begin_bar(lone ? action_label(gs, view.buttons.front()) : std::string())) return std::nullopt;

    headline(gs, session.pending()->side, core::describe(*session.pending()));
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
                break;  // handled above (D-051)
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
    headline(gs, session.pending()->side, prompt.empty() ? core::describe(*session.pending()) : prompt);
    ImGui::TextWrapped("%s", board.hint(gs).c_str());
    // D-031: weapons are only chosen after the target; "Custom..." on the target edits them per model here.
    ShotPlan* custom = board.custom_shot() ? board.shot_plan() : nullptr;
    if (custom != nullptr) draw_shot_grid(gs, *custom, selected_model);
    // D-032: the same for a fight's melee weapons.
    MeleePlan* custom_fight = board.custom_melee() ? board.melee_plan() : nullptr;
    if (custom_fight != nullptr) draw_melee_grid(gs, *custom_fight, selected_model);
    for (const core::GameAction& t : view_.templates) {
        if (editor_kind(t) == EditorKind::Path) {
            move_status(gs);
            break;
        }
    }
    if (!auto_move_message_.empty()) ImGui::TextColored(kWarning, "%s", auto_move_message_.c_str());  // D-050
    // D-051: how much damage is still to be placed, in a coloured box under the hint.
    if (const CasualtyPicks* picks = board.casualty_picks(); picks != nullptr && picks->preview()) {
        const int left = picks->preview()->damage_left;
        const std::string text = left > 0 ? "Damage left to allocate: " + std::to_string(left)
                                          : std::string("All damage allocated: confirm the casualties");
        const ImVec4 colour = left > 0 ? ImVec4(0.75f, 0.35f, 0.10f, 1.0f) : ImVec4(0.20f, 0.55f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, colour);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colour);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colour);
        ImGui::Button(text.c_str());  // a label, not a control
        ImGui::PopStyleColor(3);
        // The "So far: model n -d ..." text line was removed (D-051): the board's marks show the same.
    }

    ImGui::TableNextColumn();
    bool back = false;
    bool undo_pick = false;
    {
        ButtonRow row;
        if ((custom != nullptr || custom_fight != nullptr) && row.button("Back")) back = true;
        // D-041: Cancel picking a shooting target (drops the shooter, as Esc does).
        if (board.can_cancel() && row.button("Cancel")) {
            back = true;
            cancel_requested_ = true;
        }
        if (const auto confirm = board.confirm()) {
            // D-042: a clicked model's removal for coherency is confirmed here; D-051: the picked casualties.
            std::string label = action_label(gs, *confirm);
            if (std::holds_alternative<core::action::RemoveModelForCoherency>(*confirm)) label = "Confirm: " + label;
            if (std::holds_alternative<core::action::OrderCasualties>(*confirm)) label = "Confirm casualties";
            if (row.button(label)) chosen = *confirm;
        }
        // D-051: take back the last casualty pick.
        if (board.can_undo_pick() && row.button("Undo pick")) undo_pick = true;
        const std::vector<core::GameAction> steps = board.step_buttons();
        for (std::size_t i = 0; i < steps.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool default_order = std::holds_alternative<core::action::OrderCasualties>(steps[i]);
            // D-058 (M47): "Use default order", or "Default order for the rest" after picks (it keeps them).
            if (row.button(default_order ? board.default_order_label() : action_label(gs, steps[i]))) chosen = steps[i];
            if (default_order) ImGui::SetItemTooltip("%s", kDefaultCasualtyOrderText);
            ImGui::PopID();
        }
        // D-050: place the pile-in / consolidation automatically; D-056: the charge move too. The player
        // reviews it, then commits.
        const bool fight_move = auto_fight_move_available(gs);
        if ((fight_move || auto_charge_move_available(gs)) && row.button("Auto move")) {
            std::vector<core::GameAction> moves;
            std::vector<std::string> problems;
            if (fight_move) {
                AutoFightMove plan = auto_fight_move(gs);
                if (plan.ok()) moves = std::move(plan.moves);
                problems = std::move(plan.problems);
            } else {
                AutoChargeMove plan = auto_charge_move(gs);
                if (plan.ok()) moves = std::move(plan.moves);
                problems = std::move(plan.problems);
            }
            if (!moves.empty()) {
                end_actions = std::move(moves);
            } else {
                auto_move_message_ = "Auto move: no legal arrangement found (" +
                                     (problems.empty() ? std::string("?") : problems.front()) + ")";
            }
        }
        // D-039: undo the last drag (one model, or the whole unit for a unit drag) of this move.
        if (undo_step_available_ && row.button("Undo last step")) undo_step_requested_ = true;
    }
    // The main action when nothing is left to do on the board.
    if (end && primary_button(end->label, true)) end_actions = end->actions;
    if (custom != nullptr && primary_button("Shoot", true)) chosen = custom->expand();
    if (custom_fight != nullptr && primary_button("Fight", true)) chosen = custom_fight->expand();
    ImGui::EndTable();
    if (back) board.cancel();  // after drawing: the plans being edited belong to the board
    if (undo_pick) board.undo_pick(gs);
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
    // The non-board fallback: the same grid as the custom-shot editor, then Shoot.
    std::optional<core::GameAction> chosen;
    if (!title.empty()) ImGui::TextUnformatted(title.c_str());
    draw_shot_grid(gs, plan, selected_model);
    ImGui::PushID(static_cast<int>(plan.unit().v * 10 + static_cast<unsigned>(plan.type())));
    if (ImGui::Button("Shoot")) chosen = plan.expand();
    ImGui::PopID();
    return chosen;
}

void DecisionPanel::draw_shot_grid(const core::GameState& gs, ShotPlan& plan,
                                   std::optional<core::ModelId> selected_model) {
    // D-031: one combo per model, in as many columns as fit across the bar. Each offers "Doesn't fire" and
    // every weapon and target that model can use (D-001); the model selected on the board is highlighted.
    ImGui::PushID(static_cast<int>(plan.unit().v * 10 + static_cast<unsigned>(plan.type())));
    const float cell = ImGui::GetFontSize() * 22.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    if (ImGui::BeginTable("##shot_grid", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (const core::Model& m : gs.find_unit(plan.unit())->models) {
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(m.id.v));
            const bool selected = selected_model == m.id;
            const auto d = plan.declaration_for(m.id);
            const std::string now = d ? declaration_text(gs, plan, *d) : "doesn't fire";
            if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Model %u%s", m.id.v, selected ? " *" : "");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##choice", now.c_str())) {
                if (ImGui::Selectable("Doesn't fire", !d)) plan.override_model(m.id, std::nullopt);
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
        ImGui::EndTable();
    }
    ImGui::PopID();
}

std::optional<core::GameAction> DecisionPanel::draw_melee_editor(const GameSession& session,
                                                                 const core::action::Fight& tmpl,
                                                                 std::optional<core::ModelId> selected_model) {
    // The non-board fallback: the same grid as the custom fight editor, then Fight.
    if (!melee_ || melee_->unit() != tmpl.unit) return std::nullopt;
    std::optional<core::GameAction> chosen;
    draw_melee_grid(session.state(), *melee_, selected_model);
    if (ImGui::Button("Fight")) chosen = melee_->expand();
    return chosen;
}

void DecisionPanel::draw_melee_grid(const core::GameState& gs, MeleePlan& plan,
                                    std::optional<core::ModelId> selected_model) {
    // D-032: one combo per model that can fight (every one must, D-021), in as many columns as fit. A model
    // whose weapon reaches more than one engaged unit can split its attacks (04.02) in a popup.
    ImGui::PushID(static_cast<int>(plan.unit().v));
    const float cell = ImGui::GetFontSize() * 24.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    if (ImGui::BeginTable("##melee_grid", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (const core::Model& m : gs.find_unit(plan.unit())->models) {
            const auto d = plan.declaration_for(m.id);
            if (!d) continue;  // no enemy unit engaged with this model
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(m.id.v));
            const bool selected = selected_model == m.id;
            // 04.02 Splitting Melee Attacks: the engaged targets of this model's weapon.
            std::vector<core::UnitId> targets;
            for (const MeleeOption& o : plan.options_for(m.id)) {
                if (o.declaration.weapon_index == d->weapon_index && o.declaration.profile_index == d->profile_index) {
                    targets.push_back(o.declaration.targets.front().unit);
                }
            }
            const bool can_split = targets.size() > 1;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Model %u%s", m.id.v, selected ? " *" : "");
            ImGui::SameLine();
            const ImGuiStyle& style = ImGui::GetStyle();
            const float split_w = ImGui::CalcTextSize("Split...").x + style.FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(can_split ? -(split_w + style.ItemSpacing.x) : -FLT_MIN);
            if (ImGui::BeginCombo("##choice", melee_text(gs, *d, plan).c_str())) {
                for (const MeleeOption& o : plan.options_for(m.id)) {
                    if (ImGui::Selectable(melee_text(gs, o.declaration, plan).c_str(), *d == o.declaration)) {
                        plan.override_model(m.id, o.declaration);
                    }
                }
                ImGui::EndCombo();
            }
            if (selected) ImGui::PopStyleColor();
            if (can_split) {
                ImGui::SameLine();
                if (ImGui::Button("Split...")) ImGui::OpenPopup("##split");
                if (ImGui::BeginPopup("##split")) {
                    ImGui::TextDisabled("Model %u's attacks per target (04.02)", m.id.v);
                    core::MeleeDeclaration split = *d;
                    split.targets.clear();
                    bool changed = false;
                    for (const core::UnitId t : targets) {
                        int n = 0;
                        for (const core::MeleeTarget& mt : d->targets) {
                            if (mt.unit == t) n = mt.attacks;
                        }
                        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
                        changed |= ImGui::InputInt(unit_name(gs, t).c_str(), &n);
                        if (n > 0) split.targets.push_back(core::MeleeTarget{t, n});
                    }
                    if (changed) plan.override_model(m.id, split);  // the engine checks the split when submitted
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

}  // namespace wh40k::client
