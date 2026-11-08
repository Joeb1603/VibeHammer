#include "launch_options.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace wh40k::client {

LaunchOptions parse_launch_args(const std::vector<std::string>& args) {
    LaunchOptions o;
    for (std::size_t i = 0; i + 1 < args.size(); i += 2) {
        const std::string& key = args[i];
        const std::string& value = args[i + 1];
        try {
            if (key == "--replay") o.replay = value;
            else if (key == "--frame") o.frame = static_cast<std::size_t>(std::stoul(value));
            else if (key == "--zoom") o.zoom = std::stof(value);
            else if (key == "--screenshot") o.screenshot = value;
            else if (key == "--steps") o.steps = std::stoi(value);
            else if (key == "--select") o.select_unit = static_cast<unsigned>(std::stoul(value));
            else if (key == "--select-model") o.select_model = static_cast<unsigned>(std::stoul(value));
            else if (key == "--seed") o.seed = std::stoull(value);
            else if (key == "--scenario") o.scenario = value;
            else if (key == "--dice") o.dice = std::stoi(value);
            else if (key == "--menu") {
                if (value != "0" && value != "1") throw std::invalid_argument("not 0 or 1");
                o.menu = value == "1";
            } else if (key == "--bot") {
                if (value == "A") o.bots = {true, false};
                else if (value == "B") o.bots = {false, true};
                else if (value == "both") o.bots = {true, true};
                else if (value == "none") o.bots = {false, false};
                else throw std::invalid_argument("not A, B, both or none");
                o.bots_given = true;
            } else if (key == "--bot-level") {
                o.bot_level = bot_level_from_key(value);
                if (!o.bot_level) throw std::invalid_argument("not easy, normal or hard");
            } else if (key == "--bot-speed") {
                o.bot_speed = bot_speed_from_key(value);
                if (!o.bot_speed) throw std::invalid_argument("not slow, normal, fast or instant");
            } else if (key == "--bot-steps") {
                o.bot_steps = std::stoi(value);
                if (o.bot_steps < 0) throw std::invalid_argument("negative");
            } else if (key == "--load") {
                if (value.empty()) throw std::invalid_argument("empty");
                o.load = value;
            } else if (key == "--pick-dice") {
                o.pick_dice.clear();
                std::size_t start = 0;
                while (start <= value.size()) {
                    const std::size_t comma = std::min(value.find(',', start), value.size());
                    const std::string item = value.substr(start, comma - start);
                    if (item.empty() || item.find_first_not_of("0123456789") != std::string::npos) {
                        throw std::invalid_argument("not a list of dice indices");
                    }
                    o.pick_dice.push_back(static_cast<std::size_t>(std::stoul(item)));
                    start = comma + 1;
                }
            }
        } catch (const std::exception&) {
            throw std::invalid_argument("invalid value for " + key + ": '" + value + "'");
        }
    }
    if (o.load) {
        // M61: the save decides the game; a scenario, replay or seed of its own would contradict it.
        for (const auto& [given, flag] : {std::pair{o.scenario.has_value(), "--scenario"},
                                          std::pair{o.replay.has_value(), "--replay"},
                                          std::pair{o.seed.has_value(), "--seed"}}) {
            if (given) throw std::invalid_argument(std::string("--load can't be combined with ") + flag);
        }
    }
    return o;
}

LoadedGame load_launch_save(const std::string& file, const std::filesystem::path& data_root) {
    std::filesystem::path path = file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) && std::filesystem::exists(data_root / path, ec)) path = data_root / path;
    if (!std::filesystem::is_regular_file(path, ec)) {
        throw std::invalid_argument("invalid value for --load: '" + file + "' (no such file)");
    }
    try {
        return load_game_file(path);
    } catch (const std::exception& e) {
        throw std::invalid_argument("invalid value for --load: '" + file + "' (" + e.what() + ")");
    }
}

SavePlayers launch_players(const SavePlayers& saved, const LaunchOptions& options) {
    SavePlayers out = saved;
    if (options.bots_given) out.bots = options.bots;
    if (options.bot_level) out.bot_level = *options.bot_level;
    return out;
}

}  // namespace wh40k::client
