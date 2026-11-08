#include "reroll_popup.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

#include "board_view.hpp"
#include "dice_popup.hpp"

namespace wh40k::client {

namespace {

constexpr int kDicePerRow = 10;

ImVec4 side_colour(core::Side side) {
    const Color c = side_color(side);
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
}

bool wide_button(const std::string& label, bool green, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    if (green) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.48f, 0.26f, 1.0f));
    }
    const bool pressed = ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 1.2f));
    if (green) ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    return pressed;
}

}  // namespace

std::optional<core::GameAction> draw_reroll_popup(Rectangle area, float ui_scale, const core::GameState& state,
                                                  RerollPicks& picks) {
    if (!picks.active() || picks.dice().empty()) return std::nullopt;
    const core::Unit* attacker = state.find_unit(picks.attacker());
    const core::Side side = attacker != nullptr ? attacker->side : core::Side::A;
    const ImVec4 colour = side_colour(side);
    const float die = 40.0f * ui_scale;
    const float gap = 10.0f * ui_scale;
    const int count = static_cast<int>(picks.dice().size());
    const int per_row = std::max(1, std::min(count, kDicePerRow));
    const float dice_w = static_cast<float>(per_row) * die + static_cast<float>(per_row - 1) * gap;
    const float width = std::max(dice_w, 420.0f * ui_scale) + ImGui::GetStyle().WindowPadding.x * 2.0f;

    ImGui::SetNextWindowPos(ImVec2(area.x + area.width * 0.5f, area.y + area.height * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, area.height));
    ImGui::PushStyleColor(ImGuiCol_Border, colour);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.12f, 0.15f, 0.97f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f * ui_scale);
    ImGui::Begin("##reroll", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::PushStyleColor(ImGuiCol_Button, colour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colour);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colour);
    ImGui::SmallButton(side == core::Side::A ? "Side A chooses" : "Side B chooses");
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::TextDisabled("[TWIN-LINKED] (24.38)");
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted("Re-roll wound rolls?");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - ImGui::GetStyle().WindowPadding.x * 2.0f);
    if (!picks.purpose().empty()) ImGui::TextUnformatted(picks.purpose().c_str());
    const std::string need = "Need " + std::to_string(picks.required()) + "+ to wound. " +
                             std::to_string(picks.failed_count()) + " of " + std::to_string(count) +
                             " failed (outlined in red). Click dice to pick them: each is re-rolled once, before "
                             "modifiers, and a success can be re-rolled too.";
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", need.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    // The dice, centred, in rows of up to ten; each one a button that toggles it.
    const int rows = (count + kDicePerRow - 1) / kDicePerRow;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float inner = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ring = 3.0f * ui_scale;
    for (int i = 0; i < count; ++i) {
        const RerollDie& d = picks.dice()[static_cast<std::size_t>(i)];
        const int row = i / kDicePerRow;
        const int col = i % kDicePerRow;
        const int in_row = std::min(kDicePerRow, count - row * kDicePerRow);
        const float row_w = static_cast<float>(in_row) * die + static_cast<float>(in_row - 1) * gap;
        const ImVec2 p(origin.x + (inner - row_w) * 0.5f + static_cast<float>(col) * (die + gap),
                       origin.y + static_cast<float>(row) * (die + gap));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##die", ImVec2(die, die))) picks.toggle(static_cast<std::size_t>(i));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        draw_die_face(dl, p, die, DieFace{d.value, !d.failed, d.critical, false}, DieLook::Shown);
        const ImVec2 q(p.x + die, p.y + die);
        if (d.failed) dl->AddRect(p, q, IM_COL32(220, 60, 50, 255), die * 0.18f, 0, ring);  // failed: red outline
        if (d.selected) {
            // Picked to be re-rolled: a yellow ring around the die and a mark in its corner.
            const ImVec2 a(p.x - ring * 1.5f, p.y - ring * 1.5f);
            const ImVec2 b(q.x + ring * 1.5f, q.y + ring * 1.5f);
            dl->AddRect(a, b, IM_COL32(255, 215, 70, 255), die * 0.25f, 0, ring);
            dl->AddCircleFilled(ImVec2(q.x, p.y), die * 0.18f, IM_COL32(255, 215, 70, 255));
            const char* mark = "R";
            const ImVec2 ts = ImGui::CalcTextSize(mark);
            dl->AddText(ImVec2(q.x - ts.x * 0.5f, p.y - ts.y * 0.5f), IM_COL32(30, 30, 30, 255), mark);
        } else if (hovered) {
            dl->AddRect(p, q, IM_COL32(255, 215, 70, 160), die * 0.18f, 0, 1.5f);
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(inner, static_cast<float>(rows) * die + static_cast<float>(std::max(0, rows - 1)) * gap));
    ImGui::Spacing();

    std::optional<core::GameAction> chosen;
    const std::size_t picked = picks.selected().size();
    if (wide_button("Re-roll selected (" + std::to_string(picked) + ")", true, picked > 0)) chosen = picks.action();
    if (wide_button("Re-roll all failed (" + std::to_string(picks.failed_count()) + ")", false)) {
        chosen = picks.all_failed();
    }
    if (wide_button("Keep", false)) chosen = RerollPicks::keep();
    ImGui::End();
    return chosen;
}

}  // namespace wh40k::client
