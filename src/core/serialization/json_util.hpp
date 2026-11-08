#pragma once

// Internal helpers shared by the serialization .cpp files.

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/data/load_error.hpp"

namespace wh40k::core::json_detail {

using Json = nlohmann::json;

// Writes an optional as the value or null.
template <typename T>
void put_opt(Json& j, const char* key, const std::optional<T>& v) {
    if (v) {
        j[key] = *v;
    } else {
        j[key] = nullptr;
    }
}

// Reads a value-or-null field into an optional.
template <typename T>
void get_opt(const Json& j, const char* key, std::optional<T>& v) {
    const Json& e = j.at(key);
    if (e.is_null()) {
        v.reset();
    } else {
        v = e.get<T>();
    }
}

[[noreturn]] inline void bad_enum(std::string_view type, const Json& j) {
    throw DataError("state JSON: invalid " + std::string(type) + ": " + j.dump());
}

// D-074 part 2 (M65): a stored facing: a finite number of degrees, in the stored form (-90, 90] (an oval's facing
// modulo 180, footprint_facing_deg). `what` names it in the error.
inline float stored_facing(const Json& e, std::string_view what) {
    if (!e.is_number()) throw DataError("state JSON: D-074: " + std::string(what) + " must be a number: " + e.dump());
    const float f = e.get<float>();
    if (!std::isfinite(f) || !(f > -90.0f && f <= 90.0f)) {
        throw DataError("state JSON: D-074: " + std::string(what) + " " + e.dump() +
                        " is not in (-90, 90] (an oval's facing is stored modulo 180)");
    }
    return f;
}

}  // namespace wh40k::core::json_detail
