#pragma once

#include <stdexcept>
#include <string>

namespace wh40k::core {

// Thrown for corrupt or inconsistent data files (CLAUDE.md: exceptions only for programmer
// errors and corrupt data). Rule violations during play never throw.
class DataError : public std::runtime_error {
public:
    explicit DataError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace wh40k::core
