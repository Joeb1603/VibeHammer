#pragma once

// Internal helpers shared by the serialization .cpp files.

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

}  // namespace wh40k::core::json_detail
