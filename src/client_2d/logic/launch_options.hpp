#pragma once

// The client's command-line flags (moved from main.cpp in M61 so they can be tested). Parsing only: main.cpp applies
// them. A flag takes one value ("--steps 3"); unknown flags are ignored, as before. A bad value, or flags that can't
// go together, is an std::invalid_argument naming the flag (main exits with code 2).
// M61 (D-072): --load <save.json> starts the client on a saved game (the in-game Save / Load format, D-025 / D-054),
// for screenshots of decisions --steps can't reach. It can't be combined with --scenario, --replay or --seed.
// --pick-dice <i,j,...> picks those wound dice (0-based) in the first [TWIN-LINKED] re-roll pop-up, as clicks would
// (for screenshots, like --select-model); it submits nothing. M62 (D-069): --pick-models <id,id,...> picks those models
// (by id) in the first casualty decision picked on the board (D-051), as clicks would; it submits nothing either.
// M63 (D-073): --click-units <id,id,...> clicks those units (by id) on the board, in order, in the first decision the
// board handles, as clicks would (a shooter, then its target, opens the target's weapon menu next to it). It stops at
// a click that would submit an action, so it submits nothing.
// M70: --window <w>x<h> opens the window at that size in pixels (each 320 to 16384) instead of maximized, for
// screenshots of a small window.
// M76 (D-082 part 2): --deploy-unit <unit>:<dx>,<dy>[:<model>:<x>,<y>] starts placing that unit by hand in its side's
// first deployment decision, moved (dx, dy) inches from core's proposal, with one model optionally put at (x, y) inches
// (for screenshots; it submits nothing). --turn <model>:<deg>[:<dx>,<dy>] also works on a model being placed.
// M75 (D-082): --builder 1 opens the army builder (the "Custom battle" screen); --army-a <file> and --army-b <file> (army
// lists), --first A|B and --deploy auto|hand start a custom battle with them (a side without a list keeps its stock
// army), or with --builder 1 preload the builder. None of them goes with --scenario, --replay or --load, and --builder 1
// not with --menu 1.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "army_builder.hpp"
#include "core/data/army_list.hpp"
#include "saved_game.hpp"
#include "settings.hpp"

namespace wh40k::client {

// M66 (D-074 part 3): --turn <model>:<deg>[:<dx>,<dy>]: in the first decision that moves that model, start dragging it
// (grabbed at its centre), turned `deg` degrees from its facing, the cursor `dx`, `dy` inches from it (for screenshots
// of the turn preview and the reach area at a facing); it submits nothing.
struct CliTurn {
    unsigned model = 0;
    float deg = 0.0f;
    float dx_in = 0.0f;
    float dy_in = 0.0f;
    friend bool operator==(const CliTurn&, const CliTurn&) = default;
};

// M76 (D-082 part 2): --deploy-unit <unit>:<dx>,<dy>[:<model>:<x>,<y>]: in the first deployment decision of the side that
// unit belongs to (a human's, D-081), start placing that unit by hand at core's proposal, move the whole unit `dx`, `dy`
// inches, then (optionally) put one of its models at (`x`, `y`) inches on the battlefield, for screenshots of the
// placement editor; it submits nothing.
struct CliDeployUnit {
    unsigned unit = 0;
    float dx_in = 0.0f;
    float dy_in = 0.0f;
    std::optional<unsigned> model;
    float x_in = 0.0f;
    float y_in = 0.0f;
    friend bool operator==(const CliDeployUnit&, const CliDeployUnit&) = default;
};

// M70: --window <w>x<h>.
struct CliWindow {
    int width = 0;
    int height = 0;
    friend bool operator==(const CliWindow&, const CliWindow&) = default;
};

struct LaunchOptions {
    std::optional<std::string> replay;
    std::optional<std::size_t> frame;
    std::optional<float> zoom;
    std::optional<std::string> screenshot;
    int steps = 0;
    std::optional<unsigned> select_unit;
    std::optional<unsigned> select_model;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> scenario;
    int dice = 0;       // --dice: 1 keeps the dice pop-ups from --steps, 2 also shows the first card's faces
    bool menu = false;  // --menu 1: show the start screen even with other flags (M38)
    BotSides bots{false, false};        // --bot (M39)
    bool bots_given = false;            // --bot was given (it overrides a --load save's players)
    std::optional<BotSpeed> bot_speed;  // --bot-speed
    int bot_steps = 0;                  // --bot-steps
    std::optional<std::string> load;    // --load (M61)
    std::vector<std::size_t> pick_dice;  // --pick-dice (M61)
    std::vector<unsigned> pick_models;   // --pick-models (M62)
    std::vector<unsigned> click_units;   // --click-units (M63)
    std::optional<CliTurn> turn;         // --turn (M66)
    std::optional<CliWindow> window;     // --window (M70)
    std::optional<CliDeployUnit> deploy_unit;  // --deploy-unit (M76)
    bool builder = false;                // --builder 1 (M75)
    std::optional<std::string> army_a;   // --army-a (M75)
    std::optional<std::string> army_b;   // --army-b (M75)
    std::optional<core::Side> first;     // --first (M75)
    std::optional<core::DeploymentMode> deploy;  // --deploy (M75)
    // --open-slot <group>:<slot> (M75): in the builder, open that wargear combo of the selected unit (screenshots).
    std::optional<std::pair<std::size_t, std::size_t>> open_slot;

    // M75: a custom battle's flags were given (--army-a, --army-b, --first or --deploy).
    [[nodiscard]] bool custom_battle() const { return army_a || army_b || first || deploy; }
};

// argv[1..]: "--key value" pairs.
[[nodiscard]] LaunchOptions parse_launch_args(const std::vector<std::string>& args);

// M61: the --load file, resolved like --replay (as given, else under `data_root`), loaded with load_game_file.
// std::invalid_argument ("invalid value for --load: ...") when it is missing, unreadable or fails to load.
[[nodiscard]] LoadedGame load_launch_save(const std::string& file, const std::filesystem::path& data_root);

// M75: puts --army-a, --army-b, --first and --deploy into `builder` (a list file as given, else under the builder's
// data root). With both lists their factions must differ; with one, the other side keeps or takes the stock army of a
// different faction. Unless `options.builder`, the battle must be able to start (no list problem). Every failure is an
// std::invalid_argument naming the flag (main exits with code 2).
void apply_launch_armies(const LaunchOptions& options, ArmyBuilder& builder);

// The players a --load game starts with: the save's, with --bot overriding them when given.
[[nodiscard]] SavePlayers launch_players(const SavePlayers& saved, const LaunchOptions& options);

}  // namespace wh40k::client
