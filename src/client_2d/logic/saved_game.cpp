#include "saved_game.hpp"

#include <fstream>
#include <string>

#include "core/data/load_error.hpp"

namespace wh40k::client {

namespace {

constexpr const char* kSideKeys[2] = {"A", "B"};

}  // namespace

nlohmann::json players_to_json(const SavePlayers& players) {
    return nlohmann::json{{"A", players.bots[0] ? "bot" : "human"}, {"B", players.bots[1] ? "bot" : "human"}};
}

SavePlayers players_from_save_json(const nlohmann::json& save) {
    SavePlayers out;
    if (!save.is_object()) return out;
    const auto it = save.find("players");
    if (it == save.end() || !it->is_object()) return out;
    SavePlayers read;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto side = it->find(kSideKeys[i]);
        if (side == it->end() || !side->is_string()) return out;
        const std::string v = side->get<std::string>();
        if (v != "bot" && v != "human") return out;
        read.bots[i] = v == "bot";
    }
    return read;
}

nlohmann::json save_json(const GameSession& session, const SavePlayers& players) {
    nlohmann::json j = session.to_save_json();
    j["players"] = players_to_json(players);
    return j;
}

LoadedGame load_game_json(const nlohmann::json& save) {
    return LoadedGame{GameSession::from_save_json(save), players_from_save_json(save)};
}

void save_game_file(const std::filesystem::path& path, const GameSession& session, const SavePlayers& players) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw core::DataError("cannot write '" + path.string() + "'");
    out << save_json(session, players).dump(2) << '\n';
}

LoadedGame load_game_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw core::DataError("cannot read '" + path.string() + "'");
    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::exception& e) {
        throw core::DataError("'" + path.string() + "' is not valid JSON: " + e.what());
    }
    return load_game_json(j);
}

}  // namespace wh40k::client
