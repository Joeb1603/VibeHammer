#include "launch_options.hpp"

#include <algorithm>
#include <cmath>
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
            } else if (key == "--turn") {
                // M66: <model>:<deg>[:<dx>,<dy>]
                CliTurn t;
                const std::size_t c1 = value.find(':');
                if (c1 == std::string::npos) throw std::invalid_argument("expected model:deg");
                const std::string model = value.substr(0, c1);
                if (model.empty() || model.find_first_not_of("0123456789") != std::string::npos) {
                    throw std::invalid_argument("not a model id");
                }
                t.model = static_cast<unsigned>(std::stoul(model));
                const std::size_t c2 = value.find(':', c1 + 1);
                std::size_t used = 0;
                const std::string deg = value.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
                t.deg = std::stof(deg, &used);
                if (used != deg.size() || !std::isfinite(t.deg)) throw std::invalid_argument("not a number of degrees");
                if (c2 != std::string::npos) {
                    const std::string offset = value.substr(c2 + 1);
                    const std::size_t comma = offset.find(',');
                    if (comma == std::string::npos) throw std::invalid_argument("expected dx,dy");
                    const std::string dx = offset.substr(0, comma);
                    const std::string dy = offset.substr(comma + 1);
                    t.dx_in = std::stof(dx, &used);
                    if (used != dx.size()) throw std::invalid_argument("not a number");
                    t.dy_in = std::stof(dy, &used);
                    if (used != dy.size()) throw std::invalid_argument("not a number");
                }
                o.turn = t;
            } else if (key == "--window") {
                // M70: <w>x<h> in pixels.
                const std::size_t x = value.find('x');
                if (x == std::string::npos) throw std::invalid_argument("expected <w>x<h>");
                const std::string w = value.substr(0, x);
                const std::string h = value.substr(x + 1);
                for (const std::string* part : {&w, &h}) {
                    if (part->empty() || part->size() > 5 || part->find_first_not_of("0123456789") != std::string::npos) {
                        throw std::invalid_argument("not a size");
                    }
                }
                const CliWindow size{std::stoi(w), std::stoi(h)};
                constexpr int kMin = 320;
                constexpr int kMax = 16384;
                if (size.width < kMin || size.height < kMin || size.width > kMax || size.height > kMax) {
                    throw std::invalid_argument("out of range");
                }
                o.window = size;
            } else if (key == "--builder") {
                if (value != "0" && value != "1") throw std::invalid_argument("not 0 or 1");
                o.builder = value == "1";
            } else if (key == "--open-slot") {
                const std::size_t colon = value.find(':');
                if (colon == std::string::npos) throw std::invalid_argument("expected group:slot");
                const std::string g = value.substr(0, colon);
                const std::string sl = value.substr(colon + 1);
                for (const std::string* part : {&g, &sl}) {
                    if (part->empty() || part->size() > 4 || part->find_first_not_of("0123456789") != std::string::npos) {
                        throw std::invalid_argument("not a group and slot number");
                    }
                }
                o.open_slot = std::pair{static_cast<std::size_t>(std::stoul(g)), static_cast<std::size_t>(std::stoul(sl))};
            } else if (key == "--army-a" || key == "--army-b") {
                if (value.empty()) throw std::invalid_argument("empty");
                (key == "--army-a" ? o.army_a : o.army_b) = value;
            } else if (key == "--first") {
                if (value == "A") o.first = core::Side::A;
                else if (value == "B") o.first = core::Side::B;
                else throw std::invalid_argument("not A or B");
            } else if (key == "--deploy") {
                if (value == "auto") o.deploy = core::DeploymentMode::Auto;
                else if (value == "hand") o.deploy = core::DeploymentMode::Hand;
                else throw std::invalid_argument("not auto or hand");
            } else if (key == "--pick-dice" || key == "--pick-models" || key == "--click-units") {
                std::vector<std::size_t> items;
                std::size_t start = 0;
                while (start <= value.size()) {
                    const std::size_t comma = std::min(value.find(',', start), value.size());
                    const std::string item = value.substr(start, comma - start);
                    if (item.empty() || item.find_first_not_of("0123456789") != std::string::npos) {
                        throw std::invalid_argument("not a list of numbers");
                    }
                    items.push_back(static_cast<std::size_t>(std::stoul(item)));
                    start = comma + 1;
                }
                if (key == "--pick-dice") {
                    o.pick_dice = std::move(items);
                } else if (key == "--pick-models") {
                    o.pick_models.clear();  // M62: model ids
                    for (const std::size_t i : items) o.pick_models.push_back(static_cast<unsigned>(i));
                } else {
                    o.click_units.clear();  // M63: unit ids
                    for (const std::size_t i : items) o.click_units.push_back(static_cast<unsigned>(i));
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
    if (o.builder || o.custom_battle()) {
        // M75: the builder and a custom battle make their own game; a scenario, replay or save would contradict it.
        const char* what = o.builder ? "--builder 1" : o.army_a ? "--army-a" : o.army_b ? "--army-b" : o.first ? "--first" : "--deploy";
        for (const auto& [given, flag] : {std::pair{o.scenario.has_value(), "--scenario"},
                                          std::pair{o.replay.has_value(), "--replay"},
                                          std::pair{o.load.has_value(), "--load"}, std::pair{o.builder && o.menu, "--menu 1"}}) {
            if (given) throw std::invalid_argument(std::string(what) + " can't be combined with " + flag);
        }
    }
    return o;
}

void apply_launch_armies(const LaunchOptions& options, ArmyBuilder& builder) {
    std::array<std::optional<core::ArmyList>, 2> lists;
    for (const core::Side side : {core::Side::A, core::Side::B}) {
        const std::optional<std::string>& file = side == core::Side::A ? options.army_a : options.army_b;
        if (!file) continue;
        const char* flag = side == core::Side::A ? "--army-a" : "--army-b";
        std::filesystem::path path = *file;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) && std::filesystem::exists(builder.data_root() / path, ec)) {
            path = builder.data_root() / path;
        }
        try {
            lists[core::side_index(side)] = core::load_army(path);
        } catch (const std::exception& e) {
            throw std::invalid_argument(std::string("invalid value for ") + flag + ": '" + *file + "' (" + e.what() + ")");
        }
        if (core::find_faction(builder.factions(), lists[core::side_index(side)]->faction) == nullptr) {
            throw std::invalid_argument(std::string("invalid value for ") + flag + ": '" + *file + "' (D-079: unknown faction '" +
                                        lists[core::side_index(side)]->faction + "')");
        }
    }
    if (lists[0] && lists[1] && lists[0]->faction == lists[1]->faction) {
        throw std::invalid_argument("--army-a and --army-b are both '" + lists[0]->faction +
                                    "' (D-079: a custom battle needs two different factions)");
    }
    // The factions first (set_faction swaps the sides when the other has it), then the lists.
    for (const core::Side side : {core::Side::A, core::Side::B}) {
        if (const auto& l = lists[core::side_index(side)]) (void)builder.set_faction(side, l->faction);
    }
    for (const core::Side side : {core::Side::A, core::Side::B}) {
        const std::optional<std::string>& file = side == core::Side::A ? options.army_a : options.army_b;
        if (!file) continue;
        if (const auto refused = builder.load(side, *file)) {
            throw std::invalid_argument(std::string("invalid value for ") + (side == core::Side::A ? "--army-a" : "--army-b") +
                                        ": '" + *file + "' (" + *refused + ")");
        }
    }
    if (options.first) builder.set_first_turn(*options.first);
    if (options.deploy) builder.set_deployment(*options.deploy);
    if (!options.builder) {
        if (const auto problem = builder.start_problem()) {
            throw std::invalid_argument("the custom battle can't start: " + *problem);
        }
    }
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
