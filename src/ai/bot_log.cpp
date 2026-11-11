#include "bot_log.hpp"

#include <algorithm>
#include <cctype>
#include <variant>

namespace wh40k::ai {

namespace {

constexpr std::size_t npos = std::string::npos;

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

// The ')' that closes the '(' at `open`, or npos.
std::size_t matching_close(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        if (s[i] == ')' && --depth == 0) return i;
    }
    return npos;
}

// The '(' of the innermost parentheses holding position `pos` (exclusive), or npos.
std::size_t enclosing_open(const std::string& s, std::size_t pos) {
    int depth = 0;
    for (std::size_t i = pos; i-- > 0;) {
        if (s[i] == ')') ++depth;
        if (s[i] == '(') {
            if (depth == 0) return i;
            --depth;
        }
    }
    return npos;
}

// Drops a trailing score block, "(+0.82; ...)" or "(-1.20 ...)".
std::string drop_trailing_score(std::string s) {
    s = trim(s);
    if (s.empty() || s.back() != ')') return s;
    const std::size_t open = enclosing_open(s, s.size() - 1);
    if (open == npos || open + 2 >= s.size()) return s;
    const char sign = s[open + 1];
    if ((sign == '+' || sign == '-') && std::isdigit(static_cast<unsigned char>(s[open + 2])) != 0) {
        return trim(s.substr(0, open));
    }
    return s;
}

// Inside parentheses, keeps only the first clause ("(42% charge odds; a charge move ...)" -> "(42% charge odds)").
std::string first_clauses_in_parentheses(std::string s) {
    for (std::size_t at = s.find("; "); at != npos; at = s.find("; ", at)) {
        const std::size_t open = enclosing_open(s, at);
        const std::size_t close = open == npos ? npos : matching_close(s, open);
        if (close == npos) {
            at += 2;
            continue;
        }
        s.erase(at, close - at);
    }
    return s;
}

// After " - " (where a move ends), keeps only the first clause.
std::string first_clause_after_dash(std::string s) {
    const std::size_t dash = s.find(" - ");
    if (dash == npos) return s;
    const std::size_t comma = s.find(", ", dash);
    if (comma == npos || enclosing_open(s, comma) != npos) return s;
    return s.substr(0, comma);
}

bool continues_a_plan(const core::GameAction& a) {
    return std::holds_alternative<core::action::ProposeModelMove>(a) ||
           std::holds_alternative<core::action::CommitUnitMove>(a) ||
           std::holds_alternative<core::action::ResetUnitMove>(a);
}

}  // namespace

ReasonSummary summarize_reason(std::string_view reason) {
    ReasonSummary out;
    std::string s = drop_trailing_score(std::string(reason));
    s = first_clauses_in_parentheses(std::move(s));
    s = first_clause_after_dash(std::move(s));
    out.text = trim(std::move(s));
    return out;
}

std::string short_reason(std::string_view reason) {
    return summarize_reason(reason).text;
}

std::string bot_log_line(std::string_view army, std::string_view reason) {
    return "Bot (" + std::string(army) + "): " + short_reason(reason);
}

std::vector<std::string> wrap_words(std::string_view text, std::size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t space = text.find(' ', at);
        const std::string_view word = text.substr(at, space == npos ? npos : space - at);
        at = space == npos ? text.size() : space + 1;
        if (word.empty()) continue;
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(std::move(line));
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty() || lines.empty()) lines.push_back(std::move(line));
    return lines;
}

void BotLog::record(std::size_t log_index, core::Side side, std::string_view army, const BotChoice& choice) {
    if (!notes_.empty()) {
        BotLogNote& last = notes_.back();
        if (last.side == side && last.reason == choice.reason && continues_a_plan(choice.action) &&
            last.entries.back() < log_index) {
            last.entries.push_back(log_index);
            return;
        }
    }
    notes_.push_back(BotLogNote{log_index, side, bot_log_line(army, choice.reason), choice.reason, {log_index}});
}

void BotLog::truncate(std::size_t log_size) {
    std::erase_if(notes_, [&](const BotLogNote& n) { return n.log_index >= log_size; });
    for (BotLogNote& n : notes_) {
        std::erase_if(n.entries, [&](std::size_t e) { return e >= log_size; });
    }
}

const BotLogNote* BotLog::at(std::size_t log_index) const noexcept {
    const auto it = std::lower_bound(notes_.begin(), notes_.end(), log_index,
                                     [](const BotLogNote& n, std::size_t i) { return n.log_index < i; });
    return it != notes_.end() && it->log_index == log_index ? &*it : nullptr;
}

}  // namespace wh40k::ai
