#pragma once

// M75 (D-082 part 1): the army builder behind the "Custom battle" screen. Each side builds an army list of its
// faction's datasheets (02.06, 02.07) for an Incursion battle (1000 points); the builder edits the two lists and
// shows what core's list rules say about them. Raylib- and ImGui-free; every rule comes from core's army list
// functions (D-024, D-079): points, problems, refusals, defaults and the battle itself. Client state only: nothing
// here is GameState until battle_state() builds the battle through core.

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/data/army_list.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"
#include "settings.hpp"

namespace wh40k::client {

// The battle a custom game is built from: both armies (indexed by side), who takes the first turn and how the units
// are set up. "New game" rebuilds the battle from it (M75).
struct CustomBattleSetup {
    std::array<core::ArmyList, 2> armies;
    core::Side first_turn = core::Side::A;
    core::DeploymentMode deployment = core::DeploymentMode::Auto;
    friend bool operator==(const CustomBattleSetup&, const CustomBattleSetup&) = default;
};

// D-079: the custom battle's initial state (not started): core's custom_battle_scenario through build_game_state.
// Throws core::DataError when the setup can't be played (the same factions, an unknown faction, a list problem).
// D-084 (M86): with `mission` (a scenario file's "mission" block) the battle plays the objective mission.
[[nodiscard]] core::GameState custom_battle_state(const CustomBattleSetup& setup,
                                                  const std::vector<core::Faction>& factions,
                                                  const core::DatasheetResolver& resolve,
                                                  const nlohmann::json* mission = nullptr);

// A problem of one side's list, attached to its unit where it has one (army_problems), else to the army.
struct BuilderProblem {
    std::optional<std::size_t> unit;
    std::string text;  // rule-cited, as core gives it
    friend bool operator==(const BuilderProblem&, const BuilderProblem&) = default;
};

// "02.06: unit size 4 outside 5-5" -> "Unit size 4 outside 5-5 (02.06)": the plain words first, the rule after (for the
// screen's problem lists). Text without a leading citation is returned unchanged.
[[nodiscard]] std::string plain_first(const std::string& reason);

// One option of a wargear slot as the unit editor shows it.
struct OptionView {
    std::string name;
    int points = 0;                        // the option's own points (D-079), per model
    std::optional<std::string> refusal;    // D-079: why it can't be picked (greyed out); nullopt: pickable
};

struct SlotView {
    std::string name;
    std::vector<OptionView> options;
    std::optional<std::size_t> chosen;  // nullopt when the group's weapons don't fit the slots (02.07)
};

// A loadout group: `count` models of one type with the same weapons.
struct GroupView {
    std::string type;
    int count = 0;
    std::vector<std::string> weapons;
    std::vector<SlotView> slots;  // empty for a model type without options
    bool fits = true;             // the weapons decompose into the type's slots (02.07)
};

// A model type of the unit's datasheet with its count in the unit and its range (02.06).
struct ModelTypeView {
    std::string name;
    int min = 0;
    int max = 0;
    int count = 0;
    std::optional<std::string> unavailable;  // D-079: no playable loadout, and why
    bool has_options = false;                // its models have wargear slots
};

// One datasheet in the "Add unit" list.
struct AddUnitView {
    std::string unit_file;
    std::string name;
    int points = 0;      // its default unit's points (the cheapest way to add it), 0 without list data
    int copies = 0;      // copies in the list now
    int limit = 0;       // D-079: copies allowed at Incursion (2, Battleline 4)
    std::optional<std::string> refusal;  // why it can't be added now (the limit reached, no playable unit)
};

// An army file the Load combo offers.
struct ArmyFile {
    std::string path;     // relative to the data root, generic form ("data/armies/tyranids_incursion.json")
    std::string name;     // the list's name
    std::string faction;  // its faction id
    bool stock = false;   // a shipped list under data/armies (never overwritten)
};

class ArmyBuilder {
public:
    // Loads <data_root>/data/factions.json and the factions' stock armies. Side A starts on the first faction's stock
    // army and side B on the second's (Chaos Space Marines and Tyranids). Throws core::DataError if they can't be
    // loaded or there are fewer than two factions.
    explicit ArmyBuilder(std::filesystem::path data_root);

    // ---- The settings (AppSettings::custom_battle) ----
    // An army missing from them, unreadable or of an unknown faction keeps the side's stock army.
    void apply(const CustomBattleSettings& settings);
    // The setup as settings: a side's army is left out (nullopt) while it equals that side's starting stock army.
    [[nodiscard]] CustomBattleSettings settings() const;

    // ---- Edits. Each returns nullopt when done, else why not (and changes nothing). ----
    // D-079 (no mirror matches): picking the other side's faction swaps the two sides' armies (the only way to change
    // factions while there are two); another faction gives the side that faction's stock army.
    std::optional<std::string> set_faction(core::Side side, const std::string& faction_id);
    std::optional<std::string> rename(core::Side side, const std::string& name);
    // A new unit of the datasheet with core's playable defaults (default_unit), appended and selected. Refused for a
    // datasheet of another faction, at the copy limit, or when no playable unit can be made.
    std::optional<std::string> add_unit(core::Side side, const std::string& unit_file);
    std::optional<std::string> remove_unit(core::Side side, std::size_t unit);
    std::optional<std::string> duplicate_unit(core::Side side, std::size_t unit);  // inserted after it, at the limit refused
    // 02.06: counts stay within each model type's min..max and the datasheet's maximum unit size. The unit may drop
    // below its minimum size while edited (so a model type can be swapped for another); that is a problem until fixed.
    std::optional<std::string> set_count(core::Side side, std::size_t unit, std::size_t group, int count);
    // One model of the model type with its default loadout (D-079): one more in a group that has that loadout, else a
    // new group (after the type's groups, or in the datasheet's model type order). Refused for an unavailable type, or
    // past the type's or the unit's maximum (02.06).
    std::optional<std::string> add_model_type(core::Side side, std::size_t unit, const std::string& type);
    // "+ another loadout": one model of the group moves into a new group of the same type and loadout right after it
    // (then its options can differ). Refused for a group of one model or a type without options.
    std::optional<std::string> split_group(core::Side side, std::size_t unit, std::size_t group);
    // The group's models join the first other group of the same model type (taking that group's loadout).
    std::optional<std::string> merge_group(core::Side side, std::size_t unit, std::size_t group);
    // The group's models leave the unit (refused if its model type would drop below its minimum, 02.06).
    std::optional<std::string> remove_group(core::Side side, std::size_t unit, std::size_t group);
    // D-079: the group's models take `option` in `slot`; a refused option can't be picked.
    std::optional<std::string> set_option(core::Side side, std::size_t unit, std::size_t group, std::size_t slot,
                                          std::size_t option);
    void reset_to_stock(core::Side side);
    // Loads an army file (as given, else under the data root). Refused (nothing changes) for an unreadable file, an
    // unknown faction or the other side's faction (D-079: no mirror matches).
    std::optional<std::string> load(core::Side side, const std::filesystem::path& path);
    // Writes the side's list. Refused for a path in <data root>/data/armies (the stock lists are never overwritten).
    std::optional<std::string> save(core::Side side, const std::filesystem::path& path);
    // <data root>/armies/<the list's name as a file name>.json, the folder created: the user's lists.
    std::optional<std::string> save_user(core::Side side);
    [[nodiscard]] std::filesystem::path user_army_path(core::Side side) const;

    void set_first_turn(core::Side side) { first_turn_ = side; }
    void set_deployment(core::DeploymentMode mode) { deployment_ = mode; }
    void set_play_mode(GameMode mode) { play_mode_ = mode; }
    void select(core::Side side, std::optional<std::size_t> unit);

    // ---- Views ----
    [[nodiscard]] const std::vector<core::Faction>& factions() const { return factions_; }
    [[nodiscard]] const core::DatasheetResolver& resolver() const { return resolve_; }
    [[nodiscard]] const std::filesystem::path& data_root() const { return data_root_; }
    [[nodiscard]] const core::ArmyList& list(core::Side side) const;
    [[nodiscard]] const core::Faction& faction(core::Side side) const;
    [[nodiscard]] core::Side first_turn() const { return first_turn_; }
    [[nodiscard]] core::DeploymentMode deployment() const { return deployment_; }
    [[nodiscard]] GameMode play_mode() const { return play_mode_; }
    [[nodiscard]] core::Side selected_side() const { return selected_side_; }
    [[nodiscard]] std::optional<std::size_t> selected_unit() const { return selected_unit_; }
    // Edited since the list was loaded, saved or reset.
    [[nodiscard]] bool edited(core::Side side) const { return edited_[core::side_index(side)]; }
    // Where the list was last loaded from or saved to (relative to the data root when under it); empty if neither.
    [[nodiscard]] const std::string& source(core::Side side) const { return source_[core::side_index(side)]; }

    // The datasheet of a unit of the list (nullopt if it can't be loaded).
    [[nodiscard]] std::optional<core::Datasheet> datasheet(core::Side side, std::size_t unit) const;
    // D-079: the army's points and one unit's (its copy surcharge counted from the units before it).
    [[nodiscard]] int points(core::Side side) const;
    [[nodiscard]] int unit_points(core::Side side, std::size_t unit) const;
    [[nodiscard]] int models(core::Side side, std::size_t unit) const;
    // D-079: army_problems, then battle_problems (both sides get those), in core's order.
    [[nodiscard]] std::vector<BuilderProblem> problems(core::Side side) const;
    [[nodiscard]] std::vector<std::string> unit_problems(core::Side side, std::size_t unit) const;
    [[nodiscard]] std::vector<GroupView> groups(core::Side side, std::size_t unit) const;
    [[nodiscard]] std::vector<ModelTypeView> model_types(core::Side side, std::size_t unit) const;
    [[nodiscard]] std::vector<AddUnitView> add_list(core::Side side) const;
    // The army files a side can load: stock lists then the user's, of the side's faction, sorted by path.
    [[nodiscard]] std::vector<ArmyFile> army_files(core::Side side) const;
    void refresh_files();  // re-reads the two folders (the screen calls it when it opens and after a save)

    // A one-unit battle holding just that unit (D-034's datasheet window needs a core::Unit); nullopt while the unit
    // has problems of its own or its datasheet can't be loaded.
    [[nodiscard]] std::optional<core::Unit> preview_unit(core::Side side, std::size_t unit) const;

    [[nodiscard]] bool can_start() const;
    // Why Start is disabled: the first problem, with its side ("Side B: ..."); nullopt when it can start.
    [[nodiscard]] std::optional<std::string> start_problem() const;
    [[nodiscard]] CustomBattleSetup setup() const;
    // The battle as a scenario (core's custom_battle_scenario of setup()), and its state (not started): what
    // custom_battle_state builds. Both throw core::DataError unless can_start().
    [[nodiscard]] nlohmann::json battle_scenario() const;
    [[nodiscard]] core::GameState battle_state() const;

    // D-084 (M86): the objective mission the battle plays: the "mission" block of a scenario file (the client gives it
    // data/scenarios/incursion_objectives_44x60.json's). Without one (the default) the battle is decided on points
    // destroyed (D-076), as before. load_mission throws core::DataError if the file has no mission.
    void load_mission(const std::filesystem::path& scenario_file);
    void set_mission(std::optional<nlohmann::json> mission) { mission_ = std::move(mission); }
    [[nodiscard]] const nlohmann::json* mission() const { return mission_ ? &*mission_ : nullptr; }

private:
    core::ArmyList& list_mut(core::Side side) { return armies_[core::side_index(side)]; }
    [[nodiscard]] const core::Faction* faction_of(const std::string& id) const;
    [[nodiscard]] core::ArmyList stock_list(const core::Faction& f) const;
    [[nodiscard]] std::optional<std::string> check_unit(core::Side side, std::size_t unit) const;
    [[nodiscard]] std::optional<std::string> check_group(core::Side side, std::size_t unit, std::size_t group) const;
    [[nodiscard]] int copies(core::Side side, const std::string& unit_file) const;
    void touched(core::Side side);

    std::filesystem::path data_root_;
    std::optional<nlohmann::json> mission_;  // D-084 (M86)
    std::vector<core::Faction> factions_;
    core::DatasheetResolver resolve_;
    std::array<core::ArmyList, 2> armies_;
    std::array<core::ArmyList, 2> start_stock_;  // each side's starting stock army (settings() leaves it out)
    std::array<bool, 2> edited_{false, false};
    std::array<std::string, 2> source_;
    core::Side first_turn_ = core::Side::A;
    core::DeploymentMode deployment_ = core::DeploymentMode::Auto;
    GameMode play_mode_ = GameMode::Hotseat;
    core::Side selected_side_ = core::Side::A;
    std::optional<std::size_t> selected_unit_;
    std::vector<ArmyFile> files_;
};

}  // namespace wh40k::client
