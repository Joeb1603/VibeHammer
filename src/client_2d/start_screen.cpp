#include "start_screen.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>

#include "casualty_order.hpp"

namespace wh40k::client {
namespace {

namespace fs = std::filesystem;

const ImVec4 kTitleColour(1.0f, 0.85f, 0.4f, 1.0f);
const ImVec4 kErrorColour(1.0f, 0.5f, 0.45f, 1.0f);

std::string file_label(const std::string& path) { return fs::path(path).filename().string(); }

// A full-width, taller-than-usual button.
bool big_button(const char* label) {
    return ImGui::Button(label, ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.5f));
}

template <typename E, std::size_t N>
void enum_combo(const char* label, E& value, const E (&values)[N], const char* (*name)(E)) {
    if (!ImGui::BeginCombo(label, name(value))) return;
    for (const E v : values) {
        if (ImGui::Selectable(name(v), v == value)) value = v;
    }
    ImGui::EndCombo();
}

}  // namespace

void StartScreen::refresh(const fs::path& data_root) {
    const std::string selected = replay_index_ < replays_.size() ? replays_[replay_index_] : std::string();
    scenarios_ = list_scenarios(data_root);
    replays_ = list_replays(data_root);
    // Keep the chosen replay; the first time, the full game.
    const std::string want = selected.empty() ? "tests/replays/m11_full_game.json" : selected;
    const auto it = std::find(replays_.begin(), replays_.end(), want);
    replay_index_ = it == replays_.end() ? 0 : static_cast<std::size_t>(it - replays_.begin());
    info_for_.clear();
}

StartRequest StartScreen::draw(AppSettings& s, const StartContext& ctx) {
    if (info_for_ != s.scenario) {
        info_ = scenario_info(resolve_data_path(ctx.data_root, s.scenario));
        info_for_ = s.scenario;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y * 0.96f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 1.6f, em * 1.2f));
    ImGui::Begin("##start", nullptr, kFlags);
    ImGui::PopStyleVar();

    // The title, centred.
    const float width = em * 50.0f;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 3.0f);
    const char* title = "VibeHammer";
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(kTitleColour, "%s", title);
    ImGui::PopFont();
    // M86: the sides only when the scenario's name doesn't already say them; a line wider than the column wraps.
    const std::string sides = info_.sides[0] + " vs " + info_.sides[1];
    const std::string subtitle = info_.name.find(sides) != std::string::npos ? info_.name : info_.name + "   |   " + sides;
    const float subtitle_width = ImGui::CalcTextSize(subtitle.c_str()).x;
    if (subtitle_width <= width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - subtitle_width) * 0.5f);
        ImGui::TextUnformatted(subtitle.c_str());
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextWrapped("%s", subtitle.c_str());
        ImGui::PopTextWrapPos();
    }
    const std::string tagline = "Warhammer 40,000 11th edition rules engine, version " + ctx.version;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - ImGui::CalcTextSize(tagline.c_str()).x) * 0.5f);
    ImGui::TextDisabled("%s", tagline.c_str());
    ImGui::Spacing();

    StartRequest out;
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##columns", 2, kTable, ImVec2(width, 0.0f))) {
        ImGui::TableSetupColumn("play", ImGuiTableColumnFlags_WidthFixed, em * 20.0f);
        ImGui::TableSetupColumn("options", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        draw_play(s, ctx, out);
        ImGui::TableNextColumn();
        ImGui::Indent(em * 0.6f);
        draw_options(s, ctx);
        ImGui::Unindent(em * 0.6f);
        ImGui::EndTable();
    }
    if (!ctx.message.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(kErrorColour, "%s", ctx.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return out;
}

void StartScreen::draw_play(AppSettings& s, const StartContext& ctx, StartRequest& out) {
    ImGui::SeparatorText("Play");
    if (big_button("Play hotseat")) out = StartRequest{StartRequest::Kind::Play, GameMode::Hotseat, {}};
    ImGui::SetItemTooltip("Two players at one computer, each acting when the panel names their side.");
    ImGui::Spacing();
    if (big_button("Play vs bot")) out = StartRequest{StartRequest::Kind::Play, GameMode::VsBot, {}};
    ImGui::SetItemTooltip("You play one side, the bot plays the other.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("You play:");
    for (const core::Side side : {core::Side::A, core::Side::B}) {
        ImGui::SameLine();
        const std::string label = info_.sides[side == core::Side::A ? 0 : 1];
        if (ImGui::RadioButton(label.c_str(), s.human_side == side)) s.human_side = side;
    }
    ImGui::Spacing();
    if (big_button("Watch bot vs bot")) out = StartRequest{StartRequest::Kind::Play, GameMode::BotVsBot, {}};
    ImGui::SetItemTooltip("The bot plays both sides.");
    ImGui::Spacing();
    // M75 (D-082): the army builder.
    if (big_button("Custom battle...")) out.kind = StartRequest::Kind::CustomBattle;
    ImGui::SetItemTooltip("Build both armies (Chaos Space Marines and Tyranids, up to 1000 points each), then play "
                          "them hotseat, against the bot or bot vs bot.");

    ImGui::SeparatorText("Resume or watch");
    ImGui::BeginDisabled(!ctx.save_exists);
    if (big_button("Continue saved game")) out.kind = StartRequest::Kind::ContinueSaved;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", ctx.save_exists ? ("Load " + ctx.save_label).c_str()
                                                : ("No saved game yet (" + ctx.save_label + ")").c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(replays_.empty());
    if (big_button("Watch a replay") && replay_index_ < replays_.size()) {
        out = StartRequest{StartRequest::Kind::WatchReplay, GameMode::Hotseat, replays_[replay_index_]};
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string current = replay_index_ < replays_.size() ? file_label(replays_[replay_index_]) : "no replays";
    if (ImGui::BeginCombo("##replay", current.c_str())) {
        for (std::size_t i = 0; i < replays_.size(); ++i) {
            if (ImGui::Selectable(file_label(replays_[i]).c_str(), i == replay_index_)) replay_index_ = i;
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Spacing();
    if (big_button("Quit")) out.kind = StartRequest::Kind::Quit;
}

void StartScreen::draw_options(AppSettings& s, const StartContext& /*ctx*/) {
    const float em = ImGui::GetFontSize();
    const float field = em * 13.0f;
    ImGui::SeparatorText("Game");
    ImGui::SetNextItemWidth(field);
    if (ImGui::BeginCombo("Scenario", file_label(s.scenario).c_str())) {
        for (const std::string& path : scenarios_) {
            if (ImGui::Selectable(file_label(path).c_str(), path == s.scenario)) s.scenario = path;
        }
        ImGui::EndCombo();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Seed:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Random", !s.fixed_seed)) s.fixed_seed = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Fixed", s.fixed_seed)) s.fixed_seed = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.fixed_seed);
    ImGui::SetNextItemWidth(em * 8.0f);
    ImGui::InputScalar("##seed", ImGuiDataType_U64, &s.seed);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Random: each new game gets a random seed. Fixed: every new game uses this seed.");

    ImGui::SeparatorText("Bot");
    // D-085 (M85): one bot, so no level to choose.
    constexpr BotSpeed kSpeeds[] = {BotSpeed::Slow, BotSpeed::Normal, BotSpeed::Fast, BotSpeed::Instant};
    ImGui::SetNextItemWidth(field);
    enum_combo("Speed", s.bot_speed, kSpeeds, &bot_speed_name);
    ImGui::SetItemTooltip("How long the bot pauses between its actions, so you can follow them.");

    ImGui::SeparatorText("Playing");
    ImGui::Checkbox("Show dice pop-ups", &s.show_dice);
    ImGui::Checkbox("Skip steps with nothing to do", &s.skip_empty_steps);
    ImGui::SetItemTooltip("When ending the step or passing is the only thing a side can do, do it for them.");
    ImGui::Checkbox("Fix illegal moves", &s.fix_moves);
    ImGui::SetItemTooltip("A model dropped where it can't go goes to the nearest spot it can, shown while dragging.");
    ImGui::Checkbox("Allocate damage automatically", &s.auto_allocate);
    ImGui::SetItemTooltip("The defending side's allocation order and casualty order take their defaults. The default "
                          "casualty order: %s", kDefaultCasualtyOrderText);
    ImGui::Checkbox("Warn before ending shooting with shots left", &s.warn_end_shooting);
    // D-038: the scale changes only when the slider is let go, so the slider doesn't resize under the mouse.
    if (!scale_active_) scale_percent_ = static_cast<int>(std::lround(s.ui_scale * 100.0f));
    ImGui::SetNextItemWidth(field);
    ImGui::SliderInt("UI scale", &scale_percent_, static_cast<int>(kMinUiScale * 100.0f),
                     static_cast<int>(kMaxUiScale * 100.0f), "%d%%");
    scale_active_ = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) s.ui_scale = static_cast<float>(scale_percent_) / 100.0f;
}

}  // namespace wh40k::client
