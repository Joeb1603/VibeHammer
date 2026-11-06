#include "dice_popup.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "board_view.hpp"
#include "help_widgets.hpp"

namespace wh40k::client {

namespace {

constexpr double kTumbleSeconds = 0.45;
constexpr int kDicePerRow = 10;

ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// Pip positions on a 3x3 grid (column, row) for each face.
const std::array<std::vector<std::pair<int, int>>, 7> kPips{{
    {},
    {{1, 1}},
    {{0, 0}, {2, 2}},
    {{0, 0}, {1, 1}, {2, 2}},
    {{0, 0}, {2, 0}, {0, 2}, {2, 2}},
    {{0, 0}, {2, 0}, {1, 1}, {0, 2}, {2, 2}},
    {{0, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {2, 2}},
}};

enum class Look { FaceDown, Tumbling, Shown };

// One die at `p` (top-left), `size` pixels square.
void draw_die(ImDrawList* dl, ImVec2 p, float size, const DieFace& die, Look look, int tumble_face) {
    const float round = size * 0.18f;
    const ImVec2 q(p.x + size, p.y + size);
    ImU32 fill = rgba(235, 235, 235);
    ImU32 pip = rgba(30, 30, 30);
    ImU32 edge = rgba(20, 20, 20);
    const float edge_w = 1.5f;
    int face = die.value;
    if (look == Look::FaceDown) {
        fill = rgba(70, 74, 84);
        edge = rgba(130, 135, 150);
    } else if (look == Look::Tumbling) {
        face = tumble_face;
    } else if (!die.neutral) {
        fill = die.pass ? rgba(60, 160, 85) : rgba(95, 95, 100);
        pip = die.pass ? rgba(255, 255, 255) : rgba(200, 200, 200);
    }
    // Criticals aren't highlighted yet (PLAN.md Later: a gold border; DieFace::critical carries it).
    dl->AddRectFilled(p, q, fill, round);
    dl->AddRect(p, q, edge, round, 0, edge_w);
    if (look == Look::FaceDown) {
        const char* text = "?";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), rgba(200, 200, 210), text);
        return;
    }
    if (face <= 0 || face > 6) {
        const char* text = "A";  // an automatic hit: no roll was made
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), pip, text);
        return;
    }
    const float r = size * 0.085f;
    for (const auto& [cx, cy] : kPips[static_cast<std::size_t>(face)]) {
        dl->AddCircleFilled(ImVec2(p.x + size * (0.25f + 0.25f * static_cast<float>(cx)),
                                   p.y + size * (0.25f + 0.25f * static_cast<float>(cy))),
                            r, pip);
    }
}

bool big_button(const char* label, bool green) {
    if (green) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.48f, 0.26f, 1.0f));
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
    const bool pressed = ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 1.3f));
    ImGui::PopFont();
    if (green) ImGui::PopStyleColor(3);
    return pressed;
}

bool enter_or_space() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
           ImGui::IsKeyPressed(ImGuiKey_Space, false);
}

}  // namespace

void DicePopup::push(std::vector<DiceCard> cards) {
    if (queue_.empty()) shown_ = 0;
    for (DiceCard& c : cards) queue_.push_back(std::move(c));
}

void DicePopup::clear() {
    queue_.clear();
    shown_ = 0;
    revealed_ = false;
    rolling_since_.reset();
    card_since_.reset();
    revealed_at_.reset();
}

void DicePopup::advance() {
    if (queue_.empty()) return;
    if (!revealed_) {
        revealed_ = true;
        rolling_since_.reset();
        return;
    }
    next();
}

void DicePopup::next() {
    queue_.pop_front();
    ++shown_;
    revealed_ = false;
    rolling_since_.reset();
    card_since_.reset();
    revealed_at_.reset();
    if (queue_.empty()) shown_ = 0;
}

void DicePopup::draw(Rectangle area, float ui_scale, std::optional<DicePace> pace) {
    if (queue_.empty()) return;
    const DiceCard& card = queue_.front();
    const double now = ImGui::GetTime();
    if (!card_since_) card_since_ = now;
    if (rolling_since_ && now - *rolling_since_ >= kTumbleSeconds) {
        rolling_since_.reset();
        revealed_ = true;
    }
    if (!needs_roll(card)) revealed_ = true;  // D-053: nothing to roll, so no Roll click
    bool auto_advance = false;
    if (pace) {
        // D-054: the bot rolls after a short pause (at once at Instant speed, with no tumble) and continues
        // after a dwell; a click anywhere skips the wait.
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (!revealed_ && !rolling_since_) {
            if (pace->roll_pause_s <= 0.0) {
                revealed_ = true;
            } else if (click || now - *card_since_ >= pace->roll_pause_s) {
                rolling_since_ = now;
            }
        } else if (revealed_) {
            if (!revealed_at_) revealed_at_ = now;
            if (click || now - *revealed_at_ >= pace->dwell_s) auto_advance = true;
        }
    }

    const Color side = side_color(card.roller);
    const ImVec4 side_colour(side.r / 255.0f, side.g / 255.0f, side.b / 255.0f, 1.0f);
    const float die = 40.0f * ui_scale;
    const float gap = 8.0f * ui_scale;
    const int count = static_cast<int>(card.dice.size());
    const int per_row = std::max(1, std::min(count, kDicePerRow));
    const float dice_w = static_cast<float>(per_row) * die + static_cast<float>(per_row - 1) * gap;
    const float width = std::max(dice_w, 380.0f * ui_scale) + ImGui::GetStyle().WindowPadding.x * 2.0f;

    ImGui::SetNextWindowPos(ImVec2(area.x + area.width * 0.5f, area.y + area.height * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, area.height));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleColor(ImGuiCol_Border, side_colour);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.12f, 0.15f, 0.97f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f * ui_scale);
    ImGui::Begin("##dice", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Who rolls, what for, and what it needs.
    ImGui::PushStyleColor(ImGuiCol_Button, side_colour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, side_colour);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, side_colour);
    ImGui::SmallButton(card.roller == core::Side::A ? "Side A rolls" : "Side B rolls");
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    const int total = shown_ + static_cast<int>(queue_.size());
    ImGui::TextDisabled("Roll %d of %d  (%s)", shown_ + 1, total, card.rule.c_str());
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted(card.title.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - ImGui::GetStyle().WindowPadding.x * 2.0f);
    if (!card.purpose.empty()) ImGui::TextUnformatted(card.purpose.c_str());
    if (!card.need.empty()) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", card.need.c_str());
    draw_effect_lines(card.notes);  // D-037: what the weapon's abilities did
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    // The dice, centred, in rows of up to ten.
    const Look look = revealed_ ? Look::Shown : (rolling_since_ ? Look::Tumbling : Look::FaceDown);
    const int rows = (count + kDicePerRow - 1) / kDicePerRow;
    const float rows_h = static_cast<float>(rows) * die + static_cast<float>(std::max(0, rows - 1)) * gap;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float inner = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int tick = static_cast<int>(now * 18.0);
    for (int i = 0; i < count; ++i) {
        const int row = i / kDicePerRow;
        const int col = i % kDicePerRow;
        const int in_row = std::min(kDicePerRow, count - row * kDicePerRow);
        const float row_w = static_cast<float>(in_row) * die + static_cast<float>(in_row - 1) * gap;
        const ImVec2 p(origin.x + (inner - row_w) * 0.5f + static_cast<float>(col) * (die + gap),
                       origin.y + static_cast<float>(row) * (die + gap));
        draw_die(dl, p, die, card.dice[static_cast<std::size_t>(i)], look, (tick + i * 5) % 6 + 1);
    }
    ImGui::Dummy(ImVec2(inner, rows_h));
    ImGui::Spacing();

    bool advance = false;
    if (revealed_) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted(card.result.c_str());
        ImGui::PopFont();
        const bool last = queue_.size() == 1;
        const char* label = last ? (needs_roll(card) ? "Done" : "OK") : "Continue";
        if (big_button(label, true) || enter_or_space()) advance = true;
    } else {
        ImGui::BeginDisabled(rolling_since_.has_value());
        if ((big_button("Roll", true) || enter_or_space()) && !rolling_since_) rolling_since_ = now;
        ImGui::EndDisabled();
    }
    ImGui::End();
    if (advance || auto_advance) next();
}

}  // namespace wh40k::client
