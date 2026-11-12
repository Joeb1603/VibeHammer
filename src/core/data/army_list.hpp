#pragma once

// D-079 part 2 (M72): army lists for custom battles. A list holds units of one faction's datasheets with their model
// counts and loadouts (02.06, 02.07); its points are computed from the BSData list data (D-079 part 1), never stored.
// army_problems applies the list rules agreed with the user (PLAN.md, "Custom battles"); custom_battle_scenario turns
// two lists into a rosters-style scenario (44"x60" portrait, Incursion, D-076's points result) for build_game_state,
// with formations from the auto-layout (zone_layout.hpp). Read-only and deterministic.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// ---- The battle (agreed with the user: Incursion only, set up as data/scenarios/rosters_44x60.json) ----
inline constexpr int kIncursionPointsLimit = 1000;
inline constexpr float kCustomBattleWidthIn = 44.0f;
inline constexpr float kCustomBattleDepthIn = 60.0f;
inline constexpr float kCustomBattleZoneDepthIn = 12.0f;
inline constexpr int kCustomBattleRounds = 5;

// D-079 / D-081 (M74): how a custom battle's units are set up. Auto: the auto-layout's formations (D-079 part 2), as a
// scenario places its units (D-003). Hand: set up by hand, the sides alternating, during the deployment step (D-081).
enum class DeploymentMode : std::uint8_t { Auto, Hand };

// ---- Lists ----

// Models of one type with one loadout (as a scenario's model group).
struct ArmyModelGroup {
    std::string type;
    int count = 0;
    std::vector<std::string> weapons;
    friend bool operator==(const ArmyModelGroup&, const ArmyModelGroup&) = default;
};

struct ArmyUnit {
    std::string unit_file;  // repo-relative, e.g. "data/units/legionaries.json"
    std::vector<ArmyModelGroup> models;
    friend bool operator==(const ArmyUnit&, const ArmyUnit&) = default;
};

// An army file: {"format":1,"kind":"vibehammer-army","name","faction","units":[{"unit_file","models":[...]}]}.
struct ArmyList {
    std::string name;
    std::string faction;  // a Faction::id
    std::vector<ArmyUnit> units;
    friend bool operator==(const ArmyList&, const ArmyList&) = default;
};

// One entry of data/factions.json.
struct Faction {
    std::string id;
    std::string name;
    std::string faction_keyword;          // 02.05, e.g. "Heretic Astartes"
    std::vector<std::string> unit_files;  // the datasheets an army of this faction may take
    std::string default_army;             // repo-relative army file: the stock Incursion roster
    friend bool operator==(const Faction&, const Faction&) = default;
};

void to_json(nlohmann::json& j, const ArmyModelGroup& v);
void from_json(const nlohmann::json& j, ArmyModelGroup& v);
void to_json(nlohmann::json& j, const ArmyUnit& v);
void from_json(const nlohmann::json& j, ArmyUnit& v);
void to_json(nlohmann::json& j, const ArmyList& v);
void from_json(const nlohmann::json& j, ArmyList& v);  // checks format 1 and kind "vibehammer-army"
void to_json(nlohmann::json& j, const Faction& v);
void from_json(const nlohmann::json& j, Faction& v);

// The army file's text: keys in the format's order, 2-space indent, a final newline.
[[nodiscard]] std::string army_file_text(const ArmyList& list);
// Throw DataError on a missing, malformed or wrong-kind file.
[[nodiscard]] ArmyList load_army(const std::filesystem::path& path);
void save_army(const ArmyList& list, const std::filesystem::path& path);

// <repo_root>/data/factions.json. Throws DataError on a malformed file.
[[nodiscard]] std::vector<Faction> load_factions(const std::filesystem::path& repo_root);
[[nodiscard]] const Faction* find_faction(const std::vector<Faction>& factions, std::string_view id);

// A resolver that loads <repo_root>/<unit_file> once and keeps it (shared by copies of the resolver).
[[nodiscard]] DatasheetResolver caching_datasheet_resolver(const std::filesystem::path& repo_root);

// ---- Points (D-079) ----

// The unit's size bracket, plus each model's option points, plus the copy surcharge when `earlier_copies` copies of
// its datasheet come earlier in the army. A group whose weapons don't fit the slots adds no option points. Throws
// DataError if the datasheet has no list data.
[[nodiscard]] int unit_points(const Datasheet& sheet, const ArmyUnit& unit, int earlier_copies);
// Every unit's points, earlier copies counted in list order. A unit whose datasheet can't be resolved or has no list
// data counts 0 (army_problems reports it).
[[nodiscard]] int army_points(const ArmyList& list, const DatasheetResolver& resolve);

// ---- Problems ----

struct ArmyProblem {
    std::optional<std::size_t> unit;  // the unit's index in the list; nullopt for the army as a whole
    std::string reason;               // rule- or decision-cited ("02.06: ...", "02.07 (D-079): ...", "D-079: ...")
    friend bool operator==(const ArmyProblem&, const ArmyProblem&) = default;
};

// D-079: why a weapon of the datasheet can't be picked: one of its profiles is refused by the engine
// (unimplemented_ability_problem: D-026, D-071, D-069), or the datasheet has no such weapon. nullopt: pickable.
[[nodiscard]] std::optional<std::string> weapon_refusal(const Datasheet& sheet, std::string_view weapon);
// D-079: the first refused weapon of an option, or nullopt when every one of its weapons is pickable.
[[nodiscard]] std::optional<std::string> option_refusal(const Datasheet& sheet, const WargearOption& option);

// The model types that have no playable loadout (every combination of slot options includes a refused weapon or
// breaks a per-model limit, or a fixed weapon is refused), in model_types order. Empty without list data.
[[nodiscard]] std::vector<std::string> unavailable_model_types(const Datasheet& sheet);

// D-079 (M75, for the army builder): the loadout a new model of `model_type` takes: the one default_unit gives it (per
// slot the BSData default if pickable, else the first pickable option; the first playable loadout if that breaks a
// per-model limit). nullopt when the type is unavailable, unknown, or the datasheet has no list data.
[[nodiscard]] std::optional<std::vector<std::string>> default_model_weapons(const Datasheet& sheet,
                                                                            std::string_view model_type);
// D-079 (M75): why `model_type` is one of unavailable_model_types (a fixed weapon refused, a slot whose every option
// is refused, or every combination breaking a per-model limit), rule-cited; nullopt when it has a playable loadout.
[[nodiscard]] std::optional<std::string> unavailable_reason(const Datasheet& sheet, std::string_view model_type);

// One unit's own problems: 02.06 counts, 02.07 slot decomposition, refused weapons, per-model and per-unit limits.
// (Faction, copies, points and the zone are the army's: army_problems.)
[[nodiscard]] std::vector<std::string> unit_problems(const Datasheet& sheet, const ArmyUnit& unit);

// D-079 / 02.06 / 02.07: everything that keeps `list` out of a custom battle, unit by unit, then for the army: its
// faction, at least one unit, at least one CHARACTER (agreed with the user), the copy limits at Incursion (2, 4 for
// Battleline), the points limit, and fitting its 44"x12" zone with the auto-layout. Empty: the list is legal.
[[nodiscard]] std::vector<ArmyProblem> army_problems(const ArmyList& list, const Faction& faction,
                                                     const DatasheetResolver& resolve,
                                                     int points_limit = kIncursionPointsLimit);

// D-079: a battle needs two different factions (agreed with the user: no mirror matches).
[[nodiscard]] std::vector<std::string> battle_problems(const ArmyList& a, const ArmyList& b);

// ---- New units and random armies ----

// D-079: a new unit of the datasheet: its minimum size; required model types first, then the first available types
// in model_types order; each model type's loadout takes, slot by slot, the BSData default if pickable, else the first
// pickable option (unavailable model types are skipped). nullopt if the datasheet has no list data or no legal unit
// can be made that way. The unit has no unit_problems.
[[nodiscard]] std::optional<ArmyUnit> default_unit(const Datasheet& sheet, const std::string& unit_file);

struct RandomArmyStats {
    int attempts = 0;        // whole armies drawn
    int rejected = 0;        // of those, refused by army_problems (counted by reason below)
    int did_not_fit = 0;     // refused for not fitting the zone
};

// D-079: a random legal army of the faction (deterministic in `seed`, SplitMix64): a random CHARACTER first, then
// random units (random size, model types and pickable loadouts within the limits) while the points and copy limits
// allow, in a shuffled order. Capped retries; if none is legal, the faction's first CHARACTER datasheet's
// default_unit alone. Always legal (army_problems is empty).
// The seed of a side's random army in a random-armies battle with `seed` (wh40k_bot_arena --random-armies 1: side A
// Chaos Space Marines, side B Tyranids), so the arena and the tests draw the same armies.
[[nodiscard]] constexpr std::uint64_t random_army_seed(Side side, std::uint64_t seed) noexcept {
    return side == Side::A ? seed : seed ^ 0x5DEECE66Dull;
}

[[nodiscard]] ArmyList random_army(const Faction& faction, const DatasheetResolver& resolve, std::uint64_t seed,
                                   int points_limit = kIncursionPointsLimit, RandomArmyStats* stats = nullptr);

// ---- The battle ----

// D-079: a custom battle as a rosters-style scenario for build_game_state: 44"x60" portrait, 12" zones south (A) and
// north (B), 5 battle rounds, `first_turn`, result "points_destroyed" (D-076), every unit with its points and an
// auto-layout formation. Side names are the army names, else the faction names. Throws DataError if the factions are
// the same or unknown, or either list has army_problems. D-081 (M74): with DeploymentMode::Hand the scenario has
// "deployment": "alternating" and no formations (the units are set up by hand); everything else is the same.
// D-084 (M86): with `mission` (a scenario file's "mission" block, load_mission_block) the battle plays the objective
// mission instead: "result": "mission" and that block. Without it (the default) the scenario is as before.
[[nodiscard]] nlohmann::json custom_battle_scenario(const ArmyList& a, const ArmyList& b, Side first_turn,
                                                    const std::vector<Faction>& factions,
                                                    const DatasheetResolver& resolve,
                                                    DeploymentMode mode = DeploymentMode::Auto,
                                                    const nlohmann::json* mission = nullptr);

}  // namespace wh40k::core
