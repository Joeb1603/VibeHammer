#pragma once

// M45 (D-054): the bot's reasons in the client's combat log. Presentation only and client-side only: the
// notes live next to the session's log (keyed by its entry index) and are never part of GameState, saves or
// replays. raylib-free so the shortening and the grouping are tested (ai tests M45_BotLog).
//
// A reason (BotChoice::reason) is accurate but long, especially Hard's ("... (Hard: win 100%, draw 0%, loss 0%,
// value +1.22; lookahead ...)"). The log shows a short form, the action and the main reason plus, for Hard, the
// chosen option's win / draw / loss odds; the full reason goes in the line's tooltip.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bot.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

// Hard's odds for the option it chose, in whole percent (as the reason prints them).
struct WinDrawLoss {
    int win = 0;
    int draw = 0;
    int loss = 0;
    friend bool operator==(const WinDrawLoss&, const WinDrawLoss&) = default;
};

struct ReasonSummary {
    std::string text;                 // the action and its main reason
    std::optional<WinDrawLoss> odds;  // Hard's odds, when the reason has them
};

// The short form of a bot reason:
// - Hard's "(Hard: ...)" block is dropped, and its first "win W%, draw D%, loss L%" kept as `odds` (the first
//   odds in a Hard block are always the chosen option's); a rule number before it stays ("(03.03)");
// - "(Normal's plan)" and a trailing score block ("(+0.82; Remain Stationary +0.57)") are dropped;
// - inside any other parentheses only the first clause is kept (up to "; ");
// - after " - " (where the move ends) only the first clause is kept (up to ", ").
[[nodiscard]] ReasonSummary summarize_reason(std::string_view reason);

// The summary as one string: its text, then " | win W% / draw D% / loss L%" when there are odds.
[[nodiscard]] std::string short_reason(std::string_view reason);

// The log line: "Bot (Tyranids): " + short_reason(reason).
[[nodiscard]] std::string bot_log_line(std::string_view army, std::string_view reason);

// Splits `text` into lines of at most `width` characters at spaces (a longer word gets a line of its own).
[[nodiscard]] std::vector<std::string> wrap_words(std::string_view text, std::size_t width);

struct BotLogNote {
    std::size_t log_index = 0;  // the session log entry of the note's first action
    core::Side side = core::Side::A;
    std::string line;    // bot_log_line(...)
    std::string reason;  // the full reason, for the tooltip
    // The log entries of the plan's actions grouped under this note (a move's start, placements and commit),
    // the first being log_index.
    std::vector<std::size_t> entries;
};

// The bot's notes for one game. A plan's later steps (model placements, the commit or reset of a move, with the
// same reason as the side's last note) are grouped under the note of its first step, so a move logs one line.
class BotLog {
public:
    // The bot's `choice` for `side` (army name `army`) was submitted and became session log entry `log_index`.
    void record(std::size_t log_index, core::Side side, std::string_view army, const BotChoice& choice);
    // Drop the notes of log entries that no longer exist (an undo left `log_size` entries).
    void truncate(std::size_t log_size);
    void clear() noexcept { notes_.clear(); }

    // The note shown at log entry `log_index`, if any.
    [[nodiscard]] const BotLogNote* at(std::size_t log_index) const noexcept;
    [[nodiscard]] const std::vector<BotLogNote>& notes() const noexcept { return notes_; }

private:
    std::vector<BotLogNote> notes_;  // in log order
};

}  // namespace wh40k::ai
