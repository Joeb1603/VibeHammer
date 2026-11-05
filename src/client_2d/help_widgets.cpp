#include "help_widgets.hpp"

namespace wh40k::client {

ImVec4 effect_colour(EffectKind kind) {
    switch (kind) {
        case EffectKind::Active: return ImVec4(0.45f, 0.9f, 0.45f, 1.0f);
        case EffectKind::Inactive: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        case EffectKind::Info: return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
        case EffectKind::NotInGame: return ImVec4(1.0f, 0.6f, 0.3f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

void draw_effect_lines(const std::vector<EffectLine>& lines) {
    for (const EffectLine& l : lines) {
        ImGui::TextColored(effect_colour(l.kind), "%s %s", l.kind == EffectKind::Active ? "+" : "-", l.text.c_str());
    }
}

void draw_help(const std::vector<HelpEntry>& entries) {
    const float em = ImGui::GetFontSize();
    ImGui::PushTextWrapPos(em * 26.0f);
    bool first = true;
    for (const HelpEntry& h : entries) {
        if (!first) ImGui::Separator();
        first = false;
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", h.title.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", h.rule.c_str());
        ImGui::TextUnformatted(h.summary.c_str());
        if (h.in_this_game) {
            ImGui::TextColored(effect_colour(EffectKind::Active), "Used in this game");
        } else {
            ImGui::TextColored(effect_colour(EffectKind::NotInGame), "Not used in this game yet");
        }
    }
    ImGui::PopTextWrapPos();
}

void help_tooltip_for_last_item(const std::vector<HelpEntry>& entries) {
    if (entries.empty() || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    if (ImGui::BeginTooltip()) {
        draw_help(entries);
        ImGui::EndTooltip();
    }
}

void draw_help_tokens(const std::vector<HelpEntry>& tokens, ImVec4 colour) {
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& text = tokens[i].title;
        if (i > 0) {
            // Wrap: stay on this line only if the token fits.
            ImGui::SameLine();
            const float width = ImGui::CalcTextSize(text.c_str()).x;
            if (ImGui::GetCursorScreenPos().x + width > right) ImGui::NewLine();
        }
        ImGui::TextColored(colour, "%s", text.c_str());
        if (ImGui::IsItemHovered()) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(colour));
        }
        help_tooltip_for_last_item({tokens[i]});
    }
}

}  // namespace wh40k::client
