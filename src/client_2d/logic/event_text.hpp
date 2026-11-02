#pragma once

#include <string>

#include "core/engine/events.hpp"

namespace wh40k::client {

// One line for the event log, e.g. "[05.01] Hit rolls (batch 0): 3 5 6! 1x -> 3 hits".
[[nodiscard]] std::string format_event(const core::GameEvent& event);

// The dice log: events that report dice rolls (hit, wound, save, advance, charge, hazard, battle-shock).
[[nodiscard]] bool is_roll_event(const core::GameEvent& event);

}  // namespace wh40k::client
