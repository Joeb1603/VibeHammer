#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/geometry/primitives.hpp"
#include "core/model/characteristics.hpp"
#include "core/model/unit.hpp"
#include "core/model/weapon.hpp"

namespace wh40k::core {

// One model type on a datasheet (02.02 profile line + 02.06 composition entry + 02.07 options).
struct ModelTypeSheet {
    std::string name;
    int min = 0;
    int max = 0;
    Characteristics characteristics;
    Base base;
    std::vector<std::string> default_weapons;
    std::vector<std::string> weapon_options;
};

// D-070: a weapon profile that applies only in some circumstances ("Hunter: Monster/Vehicle"). Not
// implemented: the loader leaves it out of the weapon's profiles and lists it here.
struct ConditionalProfile {
    std::string weapon;
    std::string profile;
    std::string condition;
};

// ---- D-079: list-building data (02.06, 02.07; points, copies and limits from BSData) ----

// One choice in a wargear slot: the weapons it gives a model (several for a composite option) and its points.
struct WargearOption {
    std::string name;
    std::vector<std::string> weapons;
    int points = 0;
};

// A min 1 / max 1 wargear group of a model type (02.07): each model takes exactly one of its options.
struct WargearSlot {
    std::string name;
    std::vector<WargearOption> options;
    std::optional<std::string> default_option;

    [[nodiscard]] const WargearOption* find_option(std::string_view option_name) const;
};

// What a wargear limit counts: weapons carried (summed over the names), models that took an option of that
// name, or models of a model type.
enum class LimitCounts : std::uint8_t { Weapons, Option, ModelType };

struct BelowModels {  // with fewer than `models` models in the unit the maximum is `max`
    int models = 0;
    int max = 0;
};
struct PerModels {  // the maximum grows by `add` per `models` models in the unit (rounded down)
    int models = 0;
    int add = 0;
};

// A limit per model (a model type's `limits`) or per unit (`unit_limits`). See limit_max (army_rules.hpp).
struct WargearLimit {
    LimitCounts counts = LimitCounts::Weapons;
    std::vector<std::string> names;  // the weapons, or the one option / model type name
    int max = 0;
    std::optional<BelowModels> below_models;  // unit limits only
    std::optional<PerModels> per_models;      // unit limits only
};

// One model type's wargear: weapons every model carries, its slots and its limits per model.
struct ModelWargear {
    std::string model_type;
    std::vector<std::string> fixed;
    std::vector<WargearSlot> slots;
    std::vector<WargearLimit> limits;
};

struct PointsBracket {  // a unit of at least `min_models` models (up to the next bracket) costs `points`
    int min_models = 0;
    int points = 0;
};

struct CopySurcharge {  // a copy costs `points` more when `earlier_copies` copies come earlier in the army
    int earlier_copies = 0;
    int points = 0;
};

struct ListBuilding {
    std::vector<PointsBracket> points;      // min_models strictly increasing, from the composition's minimum
    std::optional<CopySurcharge> surcharge;
    int max_copies_default = 0;             // the Army Roster force
    int max_copies_incursion = 0;           // with the battle size "1. Incursion (1000 Point limit)"
    std::vector<ModelWargear> wargear;      // one per model type, in model_types order
    std::vector<WargearLimit> unit_limits;

    [[nodiscard]] const ModelWargear* find_wargear(std::string_view model_type) const;
};

// A unit datasheet (02), parsed from data/units/*.json (BSData via tools/fetch_bsdata.py).
// Loader-side only: GameState carries copies of what each model needs, not the datasheet.
struct Datasheet {
    std::string name;            // 02.01
    std::string source_commit;   // BSData commit the file was generated from
    std::vector<std::string> faction_keywords;  // 02.05
    std::vector<std::string> keywords;          // 02.05
    std::vector<AbilityText> abilities;         // 02.03 (text only in V1)
    int min_models = 0;                         // 02.06
    int max_models = 0;
    std::vector<ModelTypeSheet> model_types;
    std::vector<Weapon> weapons;                // 02.04
    std::vector<ConditionalProfile> conditional_profiles;  // D-070: left out of `weapons`
    std::optional<ListBuilding> list_building;   // D-079: absent in a file without "list_building"

    [[nodiscard]] const ModelTypeSheet* find_model_type(std::string_view name) const;
    [[nodiscard]] const Weapon* find_weapon(std::string_view name) const;
};

// Parses the unit file format. Accepts the data quirks as-is: skill as int / null / "N/A",
// A and D as int or dice string, unrecognised abilities as ability::Unknown.
// Throws DataError on structurally invalid data, and on inconsistent list-building data (D-079).
[[nodiscard]] Datasheet parse_datasheet(const nlohmann::json& j);
[[nodiscard]] Datasheet load_datasheet(const std::filesystem::path& path);

// Reads and parses a JSON file. Throws DataError if missing or malformed.
[[nodiscard]] nlohmann::json read_json_file(const std::filesystem::path& path);

}  // namespace wh40k::core
