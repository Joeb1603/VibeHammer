#pragma once

#include <string_view>

namespace wh40k::core {

// M1 placeholder: proves the core library builds, links and is testable.
[[nodiscard]] std::string_view engine_version() noexcept;

}  // namespace wh40k::core
