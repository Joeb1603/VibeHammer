#pragma once

// M75 (D-082 part 1): the "Custom battle" screen: each side builds its army (ArmyBuilder, the raylib- and ImGui-free
// logic), then the battle starts hotseat, against the bot or bot vs bot. Display and input only (D-024): every rule
// comes from core through ArmyBuilder.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "army_builder.hpp"
#include "settings.hpp"

namespace wh40k::client {

// What the player chose on the screen this frame.
struct BuilderRequest {
    enum class Kind { None, Start, Back };
    Kind kind = Kind::None;
    std::optional<std::pair<core::Side, std::size_t>> datasheet;  // "Datasheet" clicked for that unit
};

class ArmyBuilderScreen {
public:
    // When the screen opens: re-reads the army folders and clears the last message.
    void open(ArmyBuilder& builder);
    // Draws the screen over the whole window. `settings` gives the human's side and the bot level (shared with the
    // start screen); `status` is shown at the bottom (e.g. why the battle could not be built).
    BuilderRequest draw(ArmyBuilder& builder, AppSettings& settings, const std::string& status);

    // A line for the bottom of the screen (until the next edit's message).
    void set_message(std::string message) { message_ = std::move(message); }
    // For screenshots (--open-slot): open that slot's combo (group, slot) of the selected unit in the next frame.
    void open_slot(std::size_t group, std::size_t slot) { open_slot_ = std::pair{group, slot}; }

private:
    void draw_side(ArmyBuilder& b, core::Side side, BuilderRequest& out);
    void draw_editor(ArmyBuilder& b, BuilderRequest& out);
    void draw_group(ArmyBuilder& b, core::Side side, std::size_t unit, std::size_t group, const GroupView& g,
                    const ModelTypeView& type, int type_groups);
    void draw_battle_options(ArmyBuilder& b, AppSettings& s, BuilderRequest& out);
    void note(const std::optional<std::string>& refusal);

    std::string message_;  // the last refused edit, or what the last Save / Load did
    std::array<std::array<char, 96>, 2> names_{};  // the army name fields while edited
    std::array<bool, 2> name_active_{false, false};
    std::optional<std::pair<std::size_t, std::size_t>> open_slot_;
    std::array<std::vector<BuilderProblem>, 2> problems_;  // this frame's (army_problems lays out the zone)
};

}  // namespace wh40k::client
