#pragma once

#include <filesystem>
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

    [[nodiscard]] const ModelTypeSheet* find_model_type(std::string_view name) const;
    [[nodiscard]] const Weapon* find_weapon(std::string_view name) const;
};

// Parses the unit file format. Accepts the data quirks as-is: skill as int / null / "N/A",
// A and D as int or dice string, unrecognised abilities as ability::Unknown.
// Throws DataError on structurally invalid data.
[[nodiscard]] Datasheet parse_datasheet(const nlohmann::json& j);
[[nodiscard]] Datasheet load_datasheet(const std::filesystem::path& path);

// Reads and parses a JSON file. Throws DataError if missing or malformed.
[[nodiscard]] nlohmann::json read_json_file(const std::filesystem::path& path);

}  // namespace wh40k::core
