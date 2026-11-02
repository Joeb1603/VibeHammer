#pragma once

#include <string>
#include <utility>
#include <vector>

namespace wh40k::core {

// Result of validate_action. Every reason starts with the rule or decision it comes from,
// e.g. "09.02: unit 3 has already been selected to move this phase" or "D-007: the game is over".
struct ValidationResult {
    std::vector<std::string> reasons;

    [[nodiscard]] bool ok() const noexcept { return reasons.empty(); }
    [[nodiscard]] static ValidationResult success() { return {}; }
    [[nodiscard]] static ValidationResult failure(std::string reason) {
        ValidationResult r;
        r.reasons.push_back(std::move(reason));
        return r;
    }
    friend bool operator==(const ValidationResult&, const ValidationResult&) = default;
};

}  // namespace wh40k::core
