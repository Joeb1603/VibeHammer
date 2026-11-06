#pragma once

// D-054 (M39): a save file with who played each side. The save (D-025) gets an optional block
//   "players": {"A": "human" | "bot", "B": "human" | "bot", "bot_level": "easy" | "normal" | "hard"}
// next to its "initial", "steps" and "state". GameState is unchanged (which sides are bots is client state).
// A save without the block (an old save, or a bare GameState) loads as hotseat; so does a block that can't be
// read.

#include <filesystem>

#include <nlohmann/json.hpp>

#include "session.hpp"
#include "settings.hpp"

namespace wh40k::client {

struct SavePlayers {
    BotSides bots{false, false};  // indexed by side_index
    BotLevel bot_level = BotLevel::Easy;
    friend bool operator==(const SavePlayers&, const SavePlayers&) = default;
};

[[nodiscard]] nlohmann::json players_to_json(const SavePlayers& players);
// The save's "players" block; hotseat if it is missing or isn't a valid block.
[[nodiscard]] SavePlayers players_from_save_json(const nlohmann::json& save);

// GameSession::to_save_json plus the "players" block.
[[nodiscard]] nlohmann::json save_json(const GameSession& session, const SavePlayers& players);

struct LoadedGame {
    GameSession session;
    SavePlayers players;
};
[[nodiscard]] LoadedGame load_game_json(const nlohmann::json& save);

// The file versions (core::DataError for an unreadable or corrupt file, as GameSession::load).
void save_game_file(const std::filesystem::path& path, const GameSession& session, const SavePlayers& players);
[[nodiscard]] LoadedGame load_game_file(const std::filesystem::path& path);

}  // namespace wh40k::client
