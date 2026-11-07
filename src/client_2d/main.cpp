// wh40k_client_2d (M12): hotseat play and replay viewing. The client never mutates game state itself: every
// game change is a GameAction submitted through GameSession, which validates it with the engine before
// applying it (D-024). Replays are stepped through Timeline (read-only).
//
// Usage: wh40k_client_2d [--replay <file>] [--frame <n>] [--zoom <px per mm>] [--screenshot <file.png>]
//                        [--steps <n>] [--select <unit id>] [--select-model <model id>] [--seed <n>]
//                        [--scenario <file>] [--dice <n>] [--menu <0|1>] [--bot A|B|both|none]
//                        [--bot-level easy|normal|hard] [--bot-speed slow|normal|fast|instant] [--bot-steps <n>]
//   M38 (D-055): with no flags the exe opens on the start screen and uses the settings saved next to it. Any flag
//   skips the start screen and runs with the default settings (nothing is read or saved), so scripted runs are
//   reproducible; --menu 1 shows the start screen anyway (for screenshots).
//   --steps n: in a new game, click the first decision button n times (for screenshots and smoke tests).
//   --seed n: the dice seed for the first new game (M13; the panel can change it for later games).
//   --scenario file: the scenario for new games (default data/scenarios/v1_skirmish.json).
//   --dice n: 1 keeps the dice pop-ups the --steps rolls made (otherwise dropped), 2 also shows the first
//             card's faces (M20, for screenshots).
//   M39 (D-054): --bot: the sides the bot plays in the first game (default none: hotseat); --bot-level and
//   --bot-speed set its level and pacing; --bot-steps n: after --steps, run up to n bot decisions at once
//   before the first frame (it stops at a human's decision; their dice pop-ups are dropped unless --dice).
//   M45: --bot-steps prints "bot-steps: <taken> of <n> bot actions taken" to stdout (package.ps1 checks it).

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <random>
#include <variant>
#include <string>
#include <vector>

#include "action_text.hpp"
#include "auto_actions.hpp"
#include "board_interaction.hpp"
#include "board_view.hpp"
#include "bot.hpp"
#include "bot_driver.hpp"
#include "bot_log.hpp"
#include "camera.hpp"
#include "casualty_order.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/units.hpp"
#include "core/state/queries.hpp"
#include "core/version.hpp"
#include "datasheet_view.hpp"
#include "decision_panel.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "dice_popup.hpp"
#include "drop_fix.hpp"
#include "event_text.hpp"
#include "help_widgets.hpp"
#include "measure.hpp"
#include "move_plan.hpp"
#include "overlays.hpp"
#include "phase_track.hpp"
#include "reach_area.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "settings.hpp"
#include "start_screen.hpp"
#include "timeline.hpp"
#include "undo_groups.hpp"
#include "watch_controls.hpp"

namespace wh40k::client {
namespace {

namespace fs = std::filesystem;

constexpr int kWindowWidth = 1400;
constexpr int kWindowHeight = 860;
// Sizes at UI scale 1 (D-028); multiplied by ui_scale().
constexpr float kPanelWidth = 440.0f;
constexpr float kHandlePx = 11.0f;
constexpr float kDragThresholdPx = 3.0f;
constexpr float kRulerText = 18.0f;
constexpr float kFontSize = 15.0f;
const Color kFixCue{255, 200, 90, 200};  // D-052: the cursor's outline and line to a fixed drop
// The folder holding data/: next to the exe when it has been copied out of the build (a zipped
// distribution), otherwise the source tree it was built from.
fs::path find_data_root() {
    const fs::path exe_dir = fs::path(GetApplicationDirectory());
    std::error_code ec;
    if (fs::exists(exe_dir / "data/scenarios", ec)) return exe_dir;
    return fs::path(WH40K_REPO_ROOT);
}
const fs::path kRepoRoot = find_data_root();
const fs::path kReplayDir = kRepoRoot / "tests/replays";
const fs::path kSavePath = kRepoRoot / "saves/game.json";              // gitignored
const fs::path kExportPath = kRepoRoot / "replays/exported_replay.json";  // gitignored
// M38 (D-055): the saved settings live next to the exe (under build/ in a source build, so not in the repo).
fs::path settings_path() { return fs::path(GetApplicationDirectory()) / "vibehammer_settings.json"; }

struct Options {
    std::optional<std::string> replay;
    std::optional<std::size_t> frame;
    std::optional<float> zoom;
    std::optional<std::string> screenshot;
    int steps = 0;
    std::optional<unsigned> select_unit;
    std::optional<unsigned> select_model;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> scenario;
    int dice = 0;  // --dice: 1 keeps the dice pop-ups from --steps, 2 also shows the first card's faces
    bool menu = false;  // --menu 1: show the start screen even with other flags (M38)
    BotSides bots{false, false};        // --bot (M39)
    std::optional<BotLevel> bot_level;  // --bot-level
    std::optional<BotSpeed> bot_speed;  // --bot-speed
    int bot_steps = 0;                  // --bot-steps
};

// Throws std::invalid_argument naming the flag when a value can't be parsed.
Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const std::string value = argv[i + 1];
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
        } else if (key == "--bot-level") {
            o.bot_level = bot_level_from_key(value);
            if (!o.bot_level) throw std::invalid_argument("not easy, normal or hard");
        } else if (key == "--bot-speed") {
            o.bot_speed = bot_speed_from_key(value);
            if (!o.bot_speed) throw std::invalid_argument("not slow, normal, fast or instant");
        } else if (key == "--bot-steps") {
            o.bot_steps = std::stoi(value);
            if (o.bot_steps < 0) throw std::invalid_argument("negative");
        }
        } catch (const std::exception&) {
            throw std::invalid_argument("invalid value for " + key + ": '" + value + "'");
        }
    }
    return o;
}

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

float distance_px(Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

enum class Mode { Menu, Play, Replay };  // Menu: the start screen (M38)

// A drag on the board: one model's next straight line, or the whole unit (the "Move unit" handle).
struct Drag {
    bool whole_unit = false;
    core::ModelId model;     // whole_unit: unused
    core::UnitId unit;
    core::Position grab;     // world position where the drag started
    core::Position current;  // world position of the cursor now
    Vector2 grab_screen{0, 0};
    // D-052 ("Fix illegal moves"): where the drag's straight line starts (the model, or the grab point for
    // the whole unit) and the region a refused drop is fixed into (none if the model can't be moved).
    core::Position from;
    std::optional<ReachRegion> fix;
    core::Position drop;             // where letting go drops now: the cursor, or the fixed spot
    std::optional<FixedDrop> fixed;  // set when `drop` is a fixed spot
    bool refused = false;            // the fix is on but found no spot for a refused drop
};

// D-035: the measuring ruler, from a left-press on nothing clickable.
struct Measure {
    core::Position start;  // world position of the press
    Vector2 grab_screen{0, 0};
};

class App {
public:
    // M38 (D-055): `settings` are the saved ones (persist: written back when they change) or, in a scripted run,
    // the defaults with the CLI's overrides (never written). The game starts on the start screen if `menu`.
    App(AppSettings settings, bool persist, std::optional<std::string> scenario_override, bool menu, BotSides bots)
        : settings_(std::move(settings)), saved_settings_(settings_), persist_(persist), bot_sides_(bots) {
        if (scenario_override) scenario_override_ = fs::path(*scenario_override);
        std::snprintf(path_, sizeof(path_), "%s", (kReplayDir / "m11_full_game.json").string().c_str());
        std::snprintf(save_path_, sizeof(save_path_), "%s", kSavePath.string().c_str());
        std::snprintf(export_path_, sizeof(export_path_), "%s", kExportPath.string().c_str());
        if (menu) {
            open_menu();
        } else {
            new_game();
        }
    }

    bool quit_requested() const { return quit_; }

    // M38: write the settings if they changed since the last write (only in a run that persists them).
    void save_settings_if_changed() {
        if (!persist_ || settings_ == saved_settings_) return;
        saved_settings_ = settings_;  // tried once per change, so a read-only folder doesn't retry every frame
        if (!save_settings(settings_path(), settings_)) {
            status_ = "Could not save the settings to " + settings_path().string();
        }
    }

    // D-038: a fresh seed, client side (core only ever sees the seed in GameState, so it stays
    // deterministic). Small enough to read and type back in.
    static std::uint64_t random_seed() {
        std::random_device device;
        return static_cast<std::uint64_t>(device() % 1000000000u);
    }

    void new_game() {
        try {
            // M13: the dice seed is chosen at game start (shown and editable in the panel). D-038: a new
            // random one each game unless "Fixed seed" is ticked.
            if (!settings_.fixed_seed) settings_.seed = random_seed();
            const fs::path scenario = scenario_path();
            game_bot_level_ = settings_.bot_level;  // M39: the level this game is played at (saved with it)
            start_session(GameSession::new_game(core::load_scenario(scenario, kRepoRoot), settings_.seed));
            status_ = "New game: " + scenario_info(scenario).name + ", seed " + std::to_string(settings_.seed) + ". " +
                      players_text();
            auto_actions_as_group();  // D-036
        } catch (const std::exception& e) {
            status_ = std::string("Could not load the scenario: ") + e.what();
        }
    }

    // The scenario for new games: --scenario, else the setting (relative to the data root).
    fs::path scenario_path() const {
        return scenario_override_ ? *scenario_override_ : resolve_data_path(kRepoRoot, settings_.scenario);
    }

    // M38 / M39: who plays which side, and the bot's level.
    std::string players_text() const {
        if (!bot_sides_[0] && !bot_sides_[1]) return "Hotseat: each side acts when the panel names it.";
        std::string level = bot_level_name(game_bot_level_);
        const ai::BotLevel played = ai::effective_level(game_bot_level_);
        if (played != game_bot_level_) {
            level += std::string(" (not built yet: it plays as ") + bot_level_name(played) + ")";
        }
        if (bot_sides_[0] && bot_sides_[1]) return "Bot vs bot, level " + level + ".";
        const core::Side bot = bot_sides_[0] ? core::Side::A : core::Side::B;
        return "The bot plays " + side_name(bot) + ", level " + level + ".";
    }

    // ---- M38: the start screen ----
    void open_menu() {
        mode_ = Mode::Menu;
        session_.reset();  // the current game is discarded (the Files section can save it first)
        timeline_ = Timeline{};
        ++generation_;
        panel_.reset();
        reasons_.clear();
        drag_.reset();
        measure_.reset();
        open_sheets_.clear();
        dice_.clear();
        before_dice_.reset();
        undo_groups_.clear();
        victory_pending_ = false;
        end_warning_.reset();
        menu_.clear();
        board_ = BoardInteraction();
        board_key_.reset();
        clear_selection();
        bot_sides_ = {false, false};
        driver_.invalidate();  // M39: any bot decision in progress is dropped
        bot_log_.clear();      // M45
        watch_.reset();
        status_.clear();  // the start screen shows only what goes wrong there
        start_screen_.refresh(kRepoRoot);
    }

    void draw_menu() {
        StartContext ctx;
        ctx.data_root = kRepoRoot;
        std::error_code ec;
        ctx.save_exists = fs::is_regular_file(save_path_, ec);
        ctx.save_label = save_path_;
        ctx.message = status_;
        ctx.version = std::string(core::engine_version());
        const StartRequest r = start_screen_.draw(settings_, ctx);
        switch (r.kind) {
            case StartRequest::Kind::None: break;
            case StartRequest::Kind::Play:
                scenario_override_.reset();  // the start screen's scenario from now on
                bot_sides_ = bot_sides_for(r.mode, settings_.human_side);
                new_game();
                break;
            case StartRequest::Kind::ContinueSaved:
                load_game();  // M39: with the save's players block (an old save loads as hotseat)
                break;
            case StartRequest::Kind::WatchReplay:
                std::snprintf(path_, sizeof(path_), "%s", resolve_data_path(kRepoRoot, r.replay).string().c_str());
                load_replay(path_);
                break;
            case StartRequest::Kind::Quit: quit_ = true; break;
        }
    }

    void start_session(GameSession session) {
        session_.emplace(std::move(session));
        mode_ = Mode::Play;
        panel_.reset();  // the editors belong to the old session
        ++generation_;
        reasons_.clear();
        drag_.reset();
        open_sheets_.clear();  // D-034
        fit_pending_ = true;
        scroll_log_ = true;
        // D-033: no pop-ups for rolls made before this session started (a load, or the game start).
        dice_.clear();
        before_dice_.reset();
        dice_seen_ = session_->log().size();
        undo_groups_.clear();  // D-039
        victory_pending_ = false;  // D-038: only a game that ends while it is played shows the pop-up
        end_warning_.reset();
        // M39 (D-054): the bot starts afresh on the new game, at this game's level.
        driver_.set_config(ai::BotConfig{game_bot_level_});
        driver_.invalidate();
        watch_.reset();  // M45
        bot_reason_.clear();
        bot_log_.clear();
    }

    // --dice: keep (1) or also reveal (2) the pop-ups the --steps rolls made; otherwise drop them.
    void cli_dice(int mode) {
        if (mode <= 0) dice_.clear();
        if (mode >= 2) dice_.reveal();
    }

    void save_game() {
        if (!session_) return;
        try {
            save_game_file(save_path_, *session_, SavePlayers{bot_sides_, game_bot_level_});  // M39: and its players
            status_ = std::string("Saved to ") + save_path_;
        } catch (const std::exception& e) {
            status_ = std::string("Could not save: ") + e.what();
        }
    }

    void load_game() {
        try {
            LoadedGame loaded = load_game_file(save_path_);
            bot_sides_ = loaded.players.bots;  // M39 (D-054): an old save has no players block: hotseat
            game_bot_level_ = loaded.players.bot_level;
            start_session(std::move(loaded.session));
            status_ = std::string("Loaded ") + save_path_ + " (seed " + std::to_string(session_->seed()) + "). " +
                      players_text();
        } catch (const std::exception& e) {
            status_ = std::string("Could not load the save: ") + e.what();
        }
    }

    void export_replay() {
        if (!session_) return;
        try {
            const fs::path path = export_path_;
            if (path.has_parent_path()) fs::create_directories(path.parent_path());
            core::save_replay(session_->to_replay("Exported from VibeHammer: " +
                                                  std::to_string(session_->history().size()) + " steps, seed " +
                                                  std::to_string(session_->seed()) + "."),
                              path);
            status_ = "Exported replay to " + path.string();
        } catch (const std::exception& e) {
            status_ = std::string("Could not export the replay: ") + e.what();
        }
    }

    bool load_replay(const fs::path& requested) {
        fs::path path = requested;
        if (!fs::exists(path) && fs::exists(kRepoRoot / path)) path = kRepoRoot / path;
        try {
            const core::Replay replay = core::load_replay(path);
            timeline_ = Timeline::from_replay(replay, kRepoRoot, "Replay: " + path.filename().string());
            ++generation_;
            frame_ = 0;
            mode_ = Mode::Replay;
            status_ = replay.description;
            fit_pending_ = true;
            scroll_log_ = true;
            return true;
        } catch (const std::exception& e) {
            status_ = std::string("Could not load replay: ") + e.what();
            return false;
        }
    }

    void go_to(std::size_t frame) {
        if (timeline_.size() == 0) return;
        frame_ = std::min(frame, timeline_.size() - 1);
        scroll_log_ = true;
    }

    // --steps: click the first decision button (never a template) `n` times. It stops at the bot's decision
    // (D-054: the human's buttons don't answer it).
    void auto_steps(int n) {
        for (int i = 0; i < n && mode_ == Mode::Play && session_ && session_->pending() && !bot_deciding(); ++i) {
            const DecisionView view = make_decision_view(session_->legal());
            if (view.buttons.empty()) break;
            submit(view.buttons.front());
        }
    }

    // --bot-steps (M39): up to `n` bot decisions now, at once, before the first frame. Stops at a human's
    // decision.
    void bot_steps(int n) {
        int taken = 0;
        for (int i = 0; i < n && mode_ == Mode::Play && session_ && bot_deciding(); ++i) {
            const ai::BotChoice c = driver_.decide_now(session_->state(), session_->pending()->side);
            if (!submit_bot(c)) break;
            ++taken;
        }
        // M45: package.ps1's second smoke test checks the bot really acted.
        if (n > 0) std::printf("bot-steps: %d of %d bot actions taken\n", taken, n);
    }

    void select(std::optional<unsigned> unit, std::optional<unsigned> model) {
        if (unit) selected_unit_ = core::UnitId{*unit};
        if (model) selected_model_ = core::ModelId{*model};
    }

    void set_zoom(float px_per_mm) { camera_.set_zoom(px_per_mm, board_centre()); }

    // D-028: the UI scale is the screen's scale times the user's "UI scale" slider.
    void set_auto_scale(float scale) { auto_scale_ = scale; }
    float ui_scale() const { return std::clamp(auto_scale_ * settings_.ui_scale, 0.75f, 3.0f); }
    float panel_width() const { return kPanelWidth * ui_scale(); }

    void apply_scale() {
        const float s = ui_scale();
        if (s == applied_scale_) return;
        if (applied_scale_ == 0.0f) base_style_ = ImGui::GetStyle();
        ImGuiStyle& style = ImGui::GetStyle();
        style = base_style_;
        style.ScaleAllSizes(s);
        style.FontSizeBase = kFontSize;
        style.FontScaleDpi = s;
        applied_scale_ = s;
        fit_pending_ = true;
    }

    void frame() {
        if (menu_requested_) {  // M38: asked for while the last frame was drawn
            menu_requested_ = false;
            open_menu();
        }
        apply_scale();
        if (mode_ == Mode::Menu) {
            ClearBackground(Color{18, 20, 24, 255});
            draw_menu();
            save_settings_if_changed();
            return;
        }
        if (!dice_.active() && before_dice_) {
            before_dice_.reset();  // D-033: the last pop-up closed: show the game as it is now
            scroll_log_ = true;
        }
        bot_tick();  // M39 (D-054)
        ClearBackground(Color{24, 26, 30, 255});
        if (!has_view()) {
            // No game and no replay (e.g. the scenario could not be loaded): show why, and the panel can
            // still load a save or a replay.
            const int size = static_cast<int>(kRulerText * ui_scale());
            DrawText(status_.c_str(), size, size, size, RAYWHITE);
            draw_ui();
            save_settings_if_changed();
            return;
        }
        if (fit_pending_ || IsWindowResized()) {
            camera_.fit(state().battlefield.width_mm, state().battlefield.depth_mm, board_area());
            fit_pending_ = false;
        }
        sync_board();
        // D-035: a ruler whose release happened over a window (and so was not seen) ends here.
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) measure_.reset();
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) handle_mouse();
        if (!io.WantCaptureKeyboard) handle_keys();

        // D-029: units that can't be clicked now are drawn slightly grey.
        std::optional<core::UnitId> emphasised;
        if (!io.WantCaptureMouse && board_.waiting_for_click()) {
            if (const auto h = model_at(state(), camera_, GetMousePosition()); h && board_.clickable(h->unit->id)) {
                emphasised = h->unit->id;
            }
        }
        draw_board(
            state(), camera_, [this](core::UnitId id) { return board_.greyed(id); }, emphasised, ui_scale(),
            [this](core::UnitId unit, core::ModelId model) { return board_.model_dimmed(unit, model); });
        ring_circles_ = ring_circles(state(), plan_overlays(state(), selected_model_, overlays_), drag_overrides());
        // Rings stop at the battlefield edge (no model can be beyond it).
        const Rectangle field = camera_.to_screen(core::Rect{0.0f, 0.0f, state().battlefield.width_mm,
                                                             state().battlefield.depth_mm});
        BeginScissorMode(static_cast<int>(field.x), static_cast<int>(field.y), static_cast<int>(field.width),
                         static_cast<int>(field.height));
        draw_reach();  // D-040: under the rings
        draw_overlays(camera_, outline_arcs(ring_circles_), ui_scale());
        EndScissorMode();
        draw_selection_and_moves();
        draw_measure();
        draw_ui();
        save_settings_if_changed();
    }

private:
    // Something to show: a live game in Play mode, a loaded replay in Replay mode.
    bool has_view() const {
        if (mode_ == Mode::Menu) return false;
        return mode_ == Mode::Play ? session_.has_value() : timeline_.size() > 0;
    }

    // What is drawn. Precondition: has_view(). D-033: while dice pop-ups show, the moment the dice were
    // rolled (the state before that action), so the board and bars don't run ahead of the roll.
    const core::GameState& state() const {
        if (mode_ != Mode::Play) return timeline_.frame(frame_).state;
        if (before_dice_ && waiting_for_dice()) return *before_dice_;
        return session_->state();
    }

    // D-033: dice pop-ups are open. Nothing from the next decision is drawn or accepted meanwhile: the board
    // is inactive (no target line, grey, outline, menu or clicks), there is no move handle, and the action
    // bar shows who is rolling. Everything is rebuilt for the real decision when the last card closes.
    bool waiting_for_dice() const { return mode_ == Mode::Play && dice_.active(); }

    // D-054 (M39): a bot owns the pending decision. Human input (board clicks, drags, the decision panel) is
    // ignored meanwhile; the action bar shows the bot thinking, with Pause and Step.
    bool bot_deciding() const {
        return mode_ == Mode::Play && session_ && session_->pending() &&
               bot_sides_[core::side_index(session_->pending()->side)];
    }
    // The board takes no input now (dice showing, or the bot's decision).
    bool board_waits() const { return waiting_for_dice() || bot_deciding(); }

    // D-054: the bot's action as its own Undo group (with what follows automatically). False if it stopped or
    // was refused; the driver then stops and the reasons are shown.
    bool submit_bot(const ai::BotChoice& c) {
        if (c.stopped) {
            driver_.stop(c.problems);
            status_ = "The bot stopped: " + (c.problems.empty() ? c.reason : c.problems.front());
            return false;
        }
        bot_reason_ = c.reason;
        undo_groups_.begin(session_->history().size(), InputOwner::Bot);
        const std::size_t log_index = session_->log().size();  // M45: the log entry this action becomes
        const core::Side side = session_->pending()->side;
        const std::string army = session_->state().side(side).name;
        const bool ok = submit_one(c.action);
        if (ok) bot_log_.record(log_index, side, army, c);
        if (ok) run_auto_actions_now();
        undo_groups_.end(session_->history().size());
        if (!ok) {
            driver_.stop(reasons_);
            status_ = "The bot's action was refused: " + (reasons_.empty() ? std::string("?") : reasons_.front());
        }
        return ok;
    }

    // D-054: hand the driver the time and what's in the way; submit the bot's action when it is due. At Instant
    // speed several actions may go in one frame (decided on this thread, so there is no wait for a worker).
    void bot_tick() {
        if (mode_ != Mode::Play || !session_) return;
        driver_.set_speed(settings_.bot_speed);
        const bool instant = settings_.bot_speed == BotSpeed::Instant;
        driver_.set_threading(instant ? ai::BotDriver::Threading::Synchronous : ai::BotDriver::Threading::Worker);
        // M45: Step while a dice pop-up shows reveals or closes that card; M48: only a bot's card (D-059).
        if (watch_.step_closes_popup(shown_card())) dice_.advance();
        for (int i = 0; i < (instant ? 20 : 1) && bot_deciding(); ++i) {
            ai::DriverInputs in;
            in.state = &session_->state();
            in.step = session_->history().size();
            in.bot_sides = bot_sides_;
            in.popup_open = dice_.active() || end_warning_.has_value();
            in.paused = watch_.paused();
            in.step_once = watch_.step_requested();
            const std::optional<ai::BotChoice> c = driver_.tick(GetTime(), in);
            if (!c) return;
            watch_.delivered();
            if (!submit_bot(*c)) return;
        }
    }

    // M48 (D-059): the dice card showing, if any, and whether a bot rolls it.
    ShownCard shown_card() const {
        const DiceCard* card = dice_.current();
        if (card == nullptr) return ShownCard::None;
        return bot_sides_[core::side_index(card->roller)] ? ShownCard::Bots : ShownCard::Humans;
    }

    Rectangle board_area() const {
        const float top = bar_height_ + action_height_;
        return Rectangle{0.0f, top, static_cast<float>(GetScreenWidth()) - panel_width(),
                         static_cast<float>(GetScreenHeight()) - top};
    }

    // D-028: while dragging, the dragged models' rings are drawn at their preview positions.
    PositionOverrides drag_overrides() const {
        PositionOverrides out;
        if (!drag_) return out;
        const core::Unit* u = state().find_unit(drag_->unit);
        if (u == nullptr) return out;
        const float dx = drag_->drop.x - drag_->grab.x;
        const float dy = drag_->drop.y - drag_->grab.y;
        for (const core::Model& m : u->models) {
            if (drag_->whole_unit) {
                out.emplace_back(m.id, core::Position{m.position.x + dx, m.position.y + dy, m.position.z});
            } else if (m.id == drag_->model) {
                out.emplace_back(m.id, drag_->drop);
            }
        }
        return out;
    }

    // The path a model has moved so far this move, then a line on to `to` (a drag preview).
    void draw_path_to(const core::GameState& gs, const core::Model& m, Vector2 to, Color colour) {
        std::vector<core::Position> path = model_path(gs, m.id);
        if (path.empty()) path.push_back(m.position);
        const float width = 2.0f * ui_scale();
        for (std::size_t i = 1; i < path.size(); ++i) {
            DrawLineEx(camera_.to_screen(path[i - 1]), camera_.to_screen(path[i]), width, colour);
        }
        DrawLineEx(camera_.to_screen(path.back()), to, width, colour);
    }

    Vector2 board_centre() const {
        const Rectangle a = board_area();
        return Vector2{a.x + a.width * 0.5f, a.y + a.height * 0.5f};
    }

    // D-029: the board's click state for the current decision, rebuilt whenever the decision changes.
    void sync_board() {
        if (board_waits()) {  // D-054: the bot's decision is not offered to the human either
            // D-033: the session has already moved on to the next decision; don't offer it yet.
            board_ = BoardInteraction();
            board_key_.reset();
            menu_.clear();
            open_menu_ = false;
            menu_anchor_.reset();
            return;
        }
        const std::size_t step = mode_ == Mode::Play ? session_->history().size() : 0;
        const BoardKey key{generation_, mode_, step};
        if (board_key_ == key) return;
        const bool first = !board_key_.has_value() && !board_built_;
        board_key_ = key;
        board_built_ = true;
        menu_.clear();
        menu_unit_.reset();
        // The session's own state, so the board always matches its legal actions (even while dice show).
        board_ = mode_ == Mode::Play && session_->pending() ? BoardInteraction(session_->state(), session_->legal())
                                                             : BoardInteraction();
        if (!first) tidy_selection();  // D-043: a new decision (Reset move, Commit, any submit, undo)
    }

    // What a board click or a menu choice led to: submit it, or offer its menu at the cursor.
    void apply(const ClickResult& r) {
        if (r.submit) submit(*r.submit);
        if (!r.menu.empty()) {
            menu_ = r.menu;
            open_menu_ = true;
            menu_anchor_ = camera_.to_world(GetMousePosition());
        }
    }

    // Every game change goes through here (and so through validate_action). True if it was applied.
    // D-036 / D-038: with skipping or automatic damage allocation on, the decisions that follow are answered
    // too.
    // D-041: forget the clicked unit and model (Cancel puts the board back as it was before the click).
    void clear_selection() {
        selected_unit_.reset();
        selected_model_.reset();
    }

    // D-039: one player input, with what follows automatically, is one Undo group.
    bool submit(const core::GameAction& action) {
        undo_groups_.begin(session_->history().size());
        const bool ok = submit_one(action);
        if (ok) run_auto_actions_now();
        undo_groups_.end(session_->history().size());
        return ok;
    }

    // D-039: several actions from one input (a whole-unit drag, an end-phase button that is several
    // actions) as one Undo group. Every rejection's reasons are kept; `stop_at_rejection` stops at the first.
    void submit_group(const std::vector<core::GameAction>& actions, bool stop_at_rejection) {
        undo_groups_.begin(session_->history().size());
        std::vector<std::string> all;
        bool any = false;
        for (const core::GameAction& a : actions) {
            const bool ok = submit_one(a);
            all.insert(all.end(), reasons_.begin(), reasons_.end());
            any = any || ok;
            if (!ok && stop_at_rejection) break;
        }
        if (any) run_auto_actions_now();
        reasons_ = std::move(all);
        undo_groups_.end(session_->history().size());
    }

    // The automatic actions on their own (a setting was just ticked, the game started): their own group.
    void auto_actions_as_group() {
        if (!session_) return;
        undo_groups_.begin(session_->history().size());
        run_auto_actions_now();
        undo_groups_.end(session_->history().size());
    }

    // M13 / D-039: undo the last group (a whole drag, a button, with what followed automatically).
    void undo_last() {
        if (!session_) return;
        // D-054: against the bot, Undo goes back to the human's last input (the bot's moves after it go too).
        if (bot_sides_[0] || bot_sides_[1]) {
            undo_groups_.undo_human(*session_);
        } else {
            undo_groups_.undo(*session_);
        }
        driver_.invalidate();
        bot_log_.truncate(session_->log().size());  // M45: the undone actions' notes go too
        panel_.reset();  // the rebuilt state may have the same step count as the editors' old one
        ++generation_;
        reasons_.clear();
        drag_.reset();
        scroll_log_ = true;
        dice_.clear();  // D-033: no pop-ups for the replayed rolls
        before_dice_.reset();
        dice_seen_ = session_->log().size();
        victory_pending_ = false;
    }

    // D-036 / D-038: answer the decisions the settings cover, recording them for Undo.
    void run_auto_actions_now() {
        if (mode_ != Mode::Play || !session_) return;
        const AutoSettings settings{settings_.skip_empty_steps, settings_.auto_allocate, bot_sides_};
        const std::vector<core::GameAction> done =
            run_auto_actions(*session_, settings, [this](const core::GameAction& a) { return submit_one(a); });
        std::string text;
        for (const core::GameAction& a : done) {
            if (!is_skip(a)) continue;  // allocation shows in the dice pop-ups and the log
            text += (text.empty() ? "Skipped (nothing worth doing): " : ", ") + action_label(session_->state(), a);
        }
        if (!text.empty()) status_ = std::move(text);
    }

    bool submit_one(const core::GameAction& action) {
        core::GameState before = session_->state();
        const std::size_t log_before = session_->log().size();
        const core::ValidationResult r = session_->submit(action);
        reasons_ = r.reasons;
        if (r.ok()) {
            scroll_log_ = true;
            if (session_->state().result && !before.result) victory_pending_ = true;  // D-038
            // D-033: a pop-up for each roll the action made. While they show, the screen stays at the
            // moment of the roll (an earlier roll's moment if pop-ups were already showing).
            std::vector<DiceCard> cards =
                settings_.show_dice ? dice_cards(session_->state(), session_->log(), dice_seen_) : std::vector<DiceCard>{};
            if (!cards.empty() && !dice_.active()) {
                before_dice_ = std::move(before);
                log_before_dice_ = log_before;
            }
            dice_.push(std::move(cards));
            dice_seen_ = session_->log().size();
        }
        return r.ok();
    }

    // D-040 / D-041: what is shaded: where the dragged model (or, when not dragging, the selected model of
    // the moving unit) can be dropped for its next straight line, or, while the whole unit is dragged, where
    // the mouse can be dropped.
    std::optional<ReachRegion> reach_region() const {
        if (!overlays_.move_range) return std::nullopt;
        const auto unit = moving_unit();
        if (!unit) return std::nullopt;
        if (drag_ && drag_->whole_unit) return unit_region(state(), *unit, drag_->grab);
        std::optional<core::ModelId> model;
        if (drag_) {
            model = drag_->model;
        } else if (const core::Unit* u = state().find_unit(*unit); u != nullptr && selected_model_) {
            for (const core::Model& m : u->models) {
                if (m.id == *selected_model_) model = m.id;
            }
        }
        if (!model) return std::nullopt;
        const std::optional<core::MoveReach> reach = core::move_reach(state(), *model);
        if (!reach) return std::nullopt;
        return model_region(*reach);
    }

    // D-040: a faint fill of that region, one screen row per span.
    void draw_reach() {
        const std::optional<ReachRegion> region = reach_region();
        if (!region) return;
        const float px = camera_.px_per_mm();
        if (!(px > 0.0f)) return;
        const float step_mm = 1.0f / px;  // one screen pixel
        Color fill = ring_color(RingKind::MoveRange);
        fill.a = 46;
        const auto draw_spans = [&](const std::vector<ReachSpan>& spans, Color colour) {
            for (const ReachSpan& s : spans) {
                const Vector2 a = camera_.to_screen(s.x0, s.y);
                const Vector2 b = camera_.to_screen(s.x1, s.y);
                DrawRectangleRec(Rectangle{std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), 1.0f}, colour);
            }
        };
        draw_spans(reach_spans(*region, step_mm), fill);
        // D-053: where it may go but can't end (within Engagement Range), faintly in the engagement colour.
        Color engaged = ring_color(RingKind::Engagement);
        engaged.a = 40;
        draw_spans(engaged_spans(*region, step_mm), engaged);
    }

    // The unit being moved model by model, if it is this player's MoveModels decision.
    std::optional<core::UnitId> moving_unit() const {
        if (mode_ != Mode::Play || !session_->pending() || board_waits()) return std::nullopt;  // D-033, D-054
        if (const auto* d = std::get_if<core::decision::MoveModels>(&session_->pending()->kind)) return d->unit;
        return std::nullopt;
    }

    std::optional<Vector2> unit_handle_screen() const {
        const auto unit = moving_unit();
        if (!unit) return std::nullopt;
        const core::Unit* u = state().find_unit(*unit);
        if (u == nullptr || u->models.empty()) return std::nullopt;
        float x = 0, y = 0;
        for (const core::Model& m : u->models) {
            x += m.position.x;
            y += m.position.y;
        }
        const float n = static_cast<float>(u->models.size());
        return camera_.to_screen(x / n, y / n);
    }

    void handle_mouse() {
        const Vector2 mouse = GetMousePosition();
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) camera_.zoom_by(std::pow(1.15f, wheel), mouse);  // zoom to cursor
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) camera_.pan(GetMouseDelta());

        const auto hovered = model_at(state(), camera_, mouse);
        if (hovered && !drag_ && !measuring()) draw_tooltip(*hovered);
        // D-034: a right-click (not a right-drag, which pans) on a model opens its unit's datasheet.
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            right_press_ = RightPress{mouse, hovered ? std::optional(hovered->unit->id) : std::nullopt};
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && right_press_) {
            const RightPress p = *right_press_;
            right_press_.reset();
            if (p.unit && distance_px(p.screen, mouse) < kDragThresholdPx * ui_scale()) open_sheet(*p.unit);
        }
        if (distance_px(mouse, rest_mouse_) > 2.0f * ui_scale()) {
            rest_mouse_ = mouse;
            rest_since_ = GetTime();
        }
        const bool rested = GetTime() - rest_since_ >= 0.6;
        if (!hovered && !drag_ && !measuring() && rested && !board_.picking_target() && menu_.empty()) {
            draw_ring_tooltip(camera_.to_world(mouse));
        }

        // D-035: the measuring ruler ends on release; a short press is an ordinary click on empty board.
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && measure_) {
            const Measure m = *measure_;
            measure_.reset();
            if (distance_px(m.grab_screen, mouse) < kDragThresholdPx * ui_scale() && !board_waits()) empty_click();
        }

        if (board_waits()) {  // D-033 / D-054: the board waits for the dice or the bot (zoom, pan, measuring still work)
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hovered) measure_ = Measure{camera_.to_world(mouse), mouse};
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) press(mouse, hovered);
        if (drag_) {
            drag_->current = camera_.to_world(mouse);
            update_drop(*drag_);
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && drag_) release(mouse);
    }

    // A click on empty board drops the board selection, but not a custom shot or fight being edited (D-031, D-032).
    void empty_click() {
        if (board_.custom_editor()) {
            selected_model_.reset();
            return;
        }
        board_.cancel();
        clear_selection();  // D-043
    }

    // D-043: the selection rings stay only on the unit the board is working with (its actor, the moving
    // unit, or the unit whose menu is open). Called after the events that can change that, not every
    // frame, so --select still shows a selection before any input.
    void tidy_selection() {
        if (mode_ != Mode::Play) return;
        if (!keep_selection(selected_unit_, board_.actor(), moving_unit(), menu_.empty() ? std::nullopt : menu_unit_)) {
            clear_selection();
        }
    }

    // D-035: true once a measuring press has moved past the drag threshold.
    bool measuring() const {
        return measure_ && distance_px(measure_->grab_screen, GetMousePosition()) >= kDragThresholdPx * ui_scale();
    }

    void draw_measure() {
        if (!measuring()) return;
        const float s = ui_scale();
        const Vector2 from = camera_.to_screen(measure_->start);
        const Vector2 to = GetMousePosition();
        const Color colour{120, 230, 255, 230};
        DrawLineEx(from, to, 2.5f * s, colour);
        DrawCircleV(from, 4.0f * s, colour);
        DrawCircleV(to, 4.0f * s, colour);
        const std::string text = measure_label(measure_->start, camera_.to_world(to));
        DrawText(text.c_str(), static_cast<int>(to.x + 14.0f * s), static_cast<int>(to.y - 22.0f * s),
                 static_cast<int>(kRulerText * s), colour);
    }

    void press(Vector2 mouse, const std::optional<HoveredModel>& hovered) {
        const auto moving = moving_unit();
        if (const auto handle = unit_handle_screen(); handle && distance_px(*handle, mouse) <= kHandlePx * ui_scale()) {
            drag_ = Drag{true, {}, *moving, camera_.to_world(mouse), camera_.to_world(mouse), mouse};
            drag_->from = drag_->grab;
            drag_->fix = fix_region(unit_region(state(), *moving, drag_->grab));  // D-052
            update_drop(*drag_);
            return;
        }
        if (!hovered) {
            // D-035: a press on nothing clickable starts the measuring ruler; on release it is a click
            // (empty_click) unless the cursor moved past the drag threshold.
            measure_ = Measure{camera_.to_world(mouse), mouse};
            return;
        }
        const core::UnitId unit = hovered->unit->id;
        if (selected_unit_ == unit) {
            selected_model_ = hovered->model->id;  // a model in the selected unit
        } else {
            selected_unit_ = unit;
            selected_model_.reset();
        }
        if (moving && *moving == unit) {
            selected_model_ = hovered->model->id;
            drag_ = Drag{false, hovered->model->id, unit, camera_.to_world(mouse), camera_.to_world(mouse), mouse};
            drag_->from = hovered->model->position;
            if (const auto reach = core::move_reach(state(), hovered->model->id)) {
                drag_->fix = fix_region(model_region(*reach));  // D-052
            }
            update_drop(*drag_);
            return;
        }
        if (board_.picking_model()) {
            apply(board_.click_model(state(), unit, hovered->model->id));  // D-042
            tidy_selection();
            return;
        }
        if (board_.active()) {
            const ClickResult r = board_.click(state(), unit);  // D-029
            if (!r.menu.empty()) menu_unit_ = unit;
            apply(r);
        }
        tidy_selection();  // D-043: a click that started nothing leaves no rings
    }

    void release(Vector2 mouse) {
        const Drag d = *drag_;
        drag_.reset();
        if (distance_px(d.grab_screen, mouse) < kDragThresholdPx * ui_scale()) return;  // a click, not a drag
        if (d.whole_unit) {
            // The "Move unit" handle: one ProposeModelMove per model, each validated by the engine, all one
            // Undo group (D-039). With a fixed drop (D-052), the fixed offset.
            std::vector<core::GameAction> moves;
            for (const auto& a : translate_unit(state(), d.unit, d.drop.x - d.grab.x, d.drop.y - d.grab.y)) {
                moves.push_back(a);
            }
            submit_group(moves, false);
            if (d.fixed && reasons_.empty()) status_ = fix_message(*d.fixed);
            return;
        }
        // D-052: the fixed spot, checked by the engine first. If it were refused (it shouldn't be), the drop is
        // made as it was, so the engine's reasons show as before.
        if (d.fixed) {
            const core::action::ProposeModelMove fixed = extend_path(state(), d.model, d.drop);
            if (session_->check(fixed).ok()) {
                if (submit(fixed)) status_ = fix_message(*d.fixed);
                return;
            }
        }
        submit(extend_path(state(), d.model, d.current));
    }

    // D-052: where letting go drops: the cursor, or with "Fix illegal moves" on and a drop the engine would
    // refuse, the nearest spot it can go (fix_drop). Drawn while dragging, so the player sees it first.
    void update_drop(Drag& d) const {
        d.drop = d.current;
        d.fixed.reset();
        d.refused = false;
        if (!settings_.fix_moves || !d.fix) return;
        d.fixed = fix_drop(*d.fix, d.from, d.current);
        if (d.fixed) {
            d.drop = d.fixed->to;
        } else {
            d.refused = drop_problem(*d.fix, d.current).has_value();
        }
    }

    void handle_keys() {
        if (mode_ == Mode::Play && !board_waits() && IsKeyPressed(KEY_ESCAPE)) {
            board_.cancel();
            if (!moving_unit()) clear_selection();  // D-041: as Cancel
        }
        if (mode_ == Mode::Play && session_ && (bot_sides_[0] || bot_sides_[1])) {  // M45: the watch keys
            BotSpeed& speed = settings_.bot_speed;
            if (IsKeyPressed(KEY_P)) watch_.apply(WatchCommand::TogglePause, speed);
            if (IsKeyPressed(KEY_N)) watch_.apply(WatchCommand::Step, speed);
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) watch_.apply(WatchCommand::Faster, speed);
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) watch_.apply(WatchCommand::Slower, speed);
        }
        if (mode_ != Mode::Replay) return;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) go_to(frame_ + 1);
        if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && frame_ > 0) go_to(frame_ - 1);
        if (IsKeyPressed(KEY_HOME)) go_to(0);
        if (IsKeyPressed(KEY_END)) go_to(timeline_.size() - 1);
    }

    // D-028: what the rings under the cursor mean.
    void draw_ring_tooltip(const core::Position& at) {
        std::vector<RingKind> kinds;
        for (const Circle& c : ring_circles_) {
            const bool inside = std::hypot(at.x - c.centre.x, at.y - c.centre.y) <= c.radius_mm;
            if (inside && std::find(kinds.begin(), kinds.end(), c.kind) == kinds.end()) kinds.push_back(c.kind);
        }
        if (kinds.empty()) return;
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        for (const RingKind k : kinds) {
            ImGui::TextColored(to_imvec(ring_color(k)), "%s", std::string(ring_name(k)).c_str());
            ImGui::TextUnformatted(std::string(ring_meaning(k)).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    void draw_tooltip(const HoveredModel& h) {
        const core::Model& m = *h.model;
        ImGui::BeginTooltip();
        ImGui::Text("%s (unit %u, %s)", h.unit->name.c_str(), h.unit->id.v, side_name(h.unit->side).c_str());
        ImGui::Text("Model %u: %s", m.id.v, m.type_name.c_str());
        ImGui::Text("Wounds %d/%d, base %.1f mm", m.wounds_remaining, m.characteristics.wounds, m.base.diameter_mm);
        ImGui::TextUnformatted(stat_line(m.characteristics).c_str());  // D-034
        ImGui::Text("Position (%.2f\", %.2f\")", core::mm_to_inches(m.position.x), core::mm_to_inches(m.position.y));
        if (h.unit->battle_shocked) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Battle-shocked");
        ImGui::TextDisabled("Right-click: datasheet");
        ImGui::EndTooltip();
    }

    // ---- D-034: datasheet windows ----
    void open_sheet(core::UnitId unit) {
        if (std::find(open_sheets_.begin(), open_sheets_.end(), unit) == open_sheets_.end()) {
            open_sheets_.push_back(unit);
        }
        ImGui::SetWindowFocus(sheet_title(unit).c_str());  // bring an already open sheet to the front
    }

    std::string sheet_title(core::UnitId id) const {
        const core::Unit* u = state().find_unit(id);
        return "Datasheet: " + (u ? u->name : std::string("unit")) + "##sheet" + std::to_string(id.v);
    }

    void draw_datasheets() {
        const Rectangle area = board_area();
        std::vector<core::UnitId> keep;
        for (std::size_t i = 0; i < open_sheets_.size(); ++i) {
            const core::UnitId id = open_sheets_[i];
            const core::Unit* u = state().find_unit(id);
            if (u == nullptr) continue;  // not in this game
            const float em = ImGui::GetFontSize();
            const float offset = em * 2.0f * static_cast<float>(id.v % 4);
            ImGui::SetNextWindowPos(ImVec2(area.x + em + offset, area.y + em + offset), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(em * 44.0f, em * 30.0f), ImGuiCond_FirstUseEver);
            const ImVec4 colour = to_imvec(side_color(u->side));
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(colour.x * 0.6f, colour.y * 0.6f, colour.z * 0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, colour);
            bool open = true;
            const bool visible = ImGui::Begin(sheet_title(id).c_str(), &open, ImGuiWindowFlags_NoCollapse);
            ImGui::PopStyleColor(2);
            if (visible) draw_datasheet(datasheet_view(*u));
            ImGui::End();
            if (open) keep.push_back(id);
        }
        open_sheets_ = std::move(keep);
    }

    static void draw_datasheet(const DatasheetView& v) {
        ImGui::Text("%s, %s", v.side.c_str(), v.models.c_str());
        if (v.battle_shocked) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Battle-shocked");
        }
        constexpr ImGuiTableFlags kTable = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##stats", 8, kTable)) {
            for (const char* h : {"Model", "M", "T", "Sv", "InSv", "W", "Ld", "OC"}) ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const DatasheetStatRow& r : v.stats) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s x%d", r.model_type.c_str(), r.count);
                for (const std::string* s : {&r.move, &r.toughness, &r.save, &r.invuln_save, &r.wounds, &r.leadership,
                                             &r.objective_control}) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s->c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Hover a weapon ability or keyword to see what it does.");
        weapon_table("Ranged weapons", "BS", v.ranged);
        weapon_table("Melee weapons", "WS", v.melee);
        ImGui::SeparatorText("Abilities (shown only: not applied in V1)");
        ImGui::PushTextWrapPos(0.0f);
        for (const DatasheetAbility& a : v.abilities) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", a.name.c_str());
            ImGui::TextUnformatted(a.text.c_str());
            ImGui::Spacing();
        }
        ImGui::SeparatorText("Keywords (hover for what they mean)");
        draw_help_tokens(v.keyword_help, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::TextDisabled("Faction:");
        ImGui::SameLine();
        draw_help_tokens(v.faction_keyword_help, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::PopTextWrapPos();
    }

    static void weapon_table(const char* title, const char* skill, const std::vector<DatasheetWeaponRow>& rows) {
        if (rows.empty()) return;
        ImGui::SeparatorText(title);
        constexpr ImGuiTableFlags kTable = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable(title, 9, kTable)) return;
        for (const char* h : {"Weapon", "Range", "A", skill, "S", "AP", "D", "Abilities", "Models"}) {
            ImGui::TableSetupColumn(h);
        }
        ImGui::TableHeadersRow();
        for (const DatasheetWeaponRow& r : rows) {
            ImGui::TableNextRow();
            for (const std::string* s : {&r.name, &r.range, &r.attacks, &r.skill, &r.strength, &r.armour_penetration,
                                         &r.damage}) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s->c_str());
            }
            // D-037: each ability is hoverable; abilities this game doesn't apply are orange.
            ImGui::TableNextColumn();
            for (std::size_t i = 0; i < r.ability_help.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                const HelpEntry& h = r.ability_help[i];
                ImGui::TextColored(h.in_this_game ? ImVec4(0.6f, 0.85f, 1.0f, 1.0f) : effect_colour(EffectKind::NotInGame),
                                   "%s", h.title.c_str());
                help_tooltip_for_last_item({h});
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", r.carried_by);
        }
        ImGui::EndTable();
    }

    void draw_ruler(Vector2 at, const std::optional<Ruler>& r) {
        if (!r) return;
        char text[64];
        std::snprintf(text, sizeof(text), "%.2f\" / %.2f\"", static_cast<double>(core::mm_to_inches(r->moved_mm)),
                      static_cast<double>(core::mm_to_inches(r->max_mm)));
        const float s = ui_scale();
        DrawText(text, static_cast<int>(at.x + 14.0f * s), static_cast<int>(at.y - 22.0f * s),
                 static_cast<int>(kRulerText * s), r->over ? RED : RAYWHITE);
    }

    void draw_selection_and_moves() {
        const core::GameState& gs = state();
        auto outline = [&](core::UnitId id, Color colour) {
            if (const core::Unit* u = gs.find_unit(id)) {
                for (const core::Model& m : u->models) {
                    const float r = camera_.to_px(m.base.diameter_mm * 0.5f) + 3.0f * ui_scale();
                    DrawRing(camera_.to_screen(m.position), r - ui_scale(), r + 1.5f * ui_scale(), 0.0f, 360.0f, 48,
                             colour);
                }
            }
        };
        if (board_.actor() && !moving_unit()) outline(*board_.actor(), YELLOW);
        // D-029: while picking a target, a line from the acting unit to the cursor.
        if (board_.picking_target() && board_.actor()) {
            if (const core::Unit* u = gs.find_unit(*board_.actor()); u != nullptr && !u->models.empty()) {
                float x = 0, y = 0;
                for (const core::Model& m : u->models) {
                    x += m.position.x;
                    y += m.position.y;
                }
                const float n = static_cast<float>(u->models.size());
                const bool menu_open = !menu_.empty() && menu_anchor_.has_value();
                const Vector2 mouse = menu_open ? camera_.to_screen(*menu_anchor_) : GetMousePosition();
                const float s = ui_scale();
                DrawLineEx(camera_.to_screen(x / n, y / n), mouse, 2.5f * s, Color{255, 220, 80, 200});
                if (!menu_open) {
                    DrawText("Pick a target", static_cast<int>(mouse.x + 16.0f * s),
                             static_cast<int>(mouse.y + 8.0f * s), static_cast<int>(kRulerText * s),
                             Color{255, 220, 80, 230});
                }
            }
        }
        for (const core::UnitId id : board_.chosen_targets()) outline(id, Color{255, 90, 90, 255});
        // D-042: the model chosen for removal (coherency).
        if (const auto chosen = board_.chosen_model()) {
            if (const core::Model* m = core::find_model(gs, *chosen)) {
                const float r = camera_.to_px(m->base.diameter_mm * 0.5f) + 3.0f * ui_scale();
                DrawRing(camera_.to_screen(m->position), r - ui_scale(), r + 2.5f * ui_scale(), 0.0f, 360.0f, 48,
                         Color{255, 90, 90, 255});
                DrawLineEx(camera_.to_screen(m->position.x - m->base.diameter_mm * 0.35f, m->position.y - m->base.diameter_mm * 0.35f),
                           camera_.to_screen(m->position.x + m->base.diameter_mm * 0.35f, m->position.y + m->base.diameter_mm * 0.35f),
                           3.0f * ui_scale(), Color{255, 90, 90, 230});
                DrawLineEx(camera_.to_screen(m->position.x - m->base.diameter_mm * 0.35f, m->position.y + m->base.diameter_mm * 0.35f),
                           camera_.to_screen(m->position.x + m->base.diameter_mm * 0.35f, m->position.y - m->base.diameter_mm * 0.35f),
                           3.0f * ui_scale(), Color{255, 90, 90, 230});
            }
        }
        // D-051: what the casualty picks so far do: a red ring and cross for a model that dies, an orange ring
        // for one that only loses wounds, with the wounds lost beside it.
        if (const CasualtyPicks* picks = board_.casualty_picks()) {
            const float s = ui_scale();
            for (const CasualtyMark& mark : picks->marks()) {
                const core::Model* m = core::find_model(gs, mark.model);
                if (m == nullptr) continue;
                const Color colour = mark.destroyed ? Color{255, 90, 90, 255} : Color{255, 170, 60, 255};
                const Vector2 c = camera_.to_screen(m->position);
                const float r = camera_.to_px(m->base.diameter_mm * 0.5f) + 3.0f * s;
                DrawRing(c, r - s, r + 2.5f * s, 0.0f, 360.0f, 48, colour);
                if (mark.destroyed) {
                    // A black outline under the red cross, so it shows on red (Termagant) bases too.
                    const float k = m->base.diameter_mm * 0.35f;
                    const Vector2 a0 = camera_.to_screen(m->position.x - k, m->position.y - k);
                    const Vector2 a1 = camera_.to_screen(m->position.x + k, m->position.y + k);
                    const Vector2 b0 = camera_.to_screen(m->position.x - k, m->position.y + k);
                    const Vector2 b1 = camera_.to_screen(m->position.x + k, m->position.y - k);
                    DrawLineEx(a0, a1, 6.0f * s, BLACK);
                    DrawLineEx(b0, b1, 6.0f * s, BLACK);
                    DrawLineEx(a0, a1, 3.0f * s, colour);
                    DrawLineEx(b0, b1, 3.0f * s, colour);
                }
                const std::string text = "-" + std::to_string(mark.wounds_lost);
                const int tx = static_cast<int>(c.x + r + 2.0f * s);
                const int ty = static_cast<int>(c.y - r);
                const int size = static_cast<int>(kRulerText * s);
                DrawText(text.c_str(), tx + 1, ty + 1, size, BLACK);  // shadow, for the same reason
                DrawText(text.c_str(), tx, ty, size, colour);
            }
        }
        // Selection.
        if (selected_unit_) {
            if (const core::Unit* u = gs.find_unit(*selected_unit_)) {
                for (const core::Model& m : u->models) {
                    const float r = camera_.to_px(m.base.diameter_mm * 0.5f) + 3.0f;
                    const bool chosen = selected_model_ == m.id;
                    DrawCircleLinesV(camera_.to_screen(m.position), r, chosen ? YELLOW : WHITE);
                    if (chosen) DrawCircleLinesV(camera_.to_screen(m.position), r + 2.0f, YELLOW);
                }
            }
        }
        // The move in progress: ghost markers at the start positions and the proposed paths.
        if (gs.flow.move) {
            const core::Unit* u = gs.find_unit(gs.flow.move->unit);
            for (const core::Model& m : u->models) {
                const std::vector<core::Position> path = model_path(gs, m.id);
                if (path.size() < 2) continue;
                const float r = camera_.to_px(m.base.diameter_mm * 0.5f);
                DrawCircleV(camera_.to_screen(path.front()), r, Color{255, 255, 255, 40});
                DrawCircleLinesV(camera_.to_screen(path.front()), r, Color{255, 255, 255, 140});
                for (std::size_t i = 1; i < path.size(); ++i) {
                    DrawLineEx(camera_.to_screen(path[i - 1]), camera_.to_screen(path[i]), 2.0f,
                               Color{255, 255, 255, 160});
                }
            }
        }
        if (const auto handle = unit_handle_screen()) {
            const Vector2 h = *handle;
            DrawPoly(h, 4, kHandlePx * ui_scale(), 45.0f, Color{255, 220, 80, 230});
            DrawPolyLines(h, 4, kHandlePx * ui_scale(), 45.0f, BLACK);
        }
        // The drag preview and the live ruler.
        if (drag_) {
            const Vector2 cursor = camera_.to_screen(drag_->current);
            const Vector2 dropped = camera_.to_screen(drag_->drop);
            // D-052: a red ghost when the fix found no spot for a refused drop.
            const Color ghost = drag_->refused ? Color{230, 60, 60, 110} : Color{255, 255, 255, 70};
            if (drag_->whole_unit) {
                const float dx = drag_->drop.x - drag_->grab.x;
                const float dy = drag_->drop.y - drag_->grab.y;
                const core::Unit* u = gs.find_unit(drag_->unit);
                std::optional<Ruler> worst;
                for (const core::Model& m : u->models) {
                    const core::Position to{m.position.x + dx, m.position.y + dy, 0.0f};
                    draw_path_to(gs, m, camera_.to_screen(to), Color{255, 255, 255, 160});
                    DrawCircleV(camera_.to_screen(to), camera_.to_px(m.base.diameter_mm * 0.5f), Color{255, 255, 255, 60});
                    const auto r = ruler(gs, m.id, to);
                    if (r && (!worst || r->moved_mm > worst->moved_mm)) worst = r;
                }
                draw_ruler(cursor, worst);
            } else {
                const core::Model* m = nullptr;
                for (const core::Model& x : gs.find_unit(drag_->unit)->models) {
                    if (x.id == drag_->model) m = &x;
                }
                if (m != nullptr) {
                    draw_path_to(gs, *m, dropped, YELLOW);
                    DrawCircleV(dropped, camera_.to_px(m->base.diameter_mm * 0.5f), ghost);
                    draw_ruler(cursor, ruler(gs, m->id, drag_->drop));
                    if (drag_->fixed) DrawCircleLinesV(cursor, camera_.to_px(m->base.diameter_mm * 0.5f), kFixCue);
                }
            }
            // D-052: the cursor and where the fixed drop will go.
            if (drag_->fixed) {
                DrawLineEx(cursor, dropped, 1.5f * ui_scale(), kFixCue);
                DrawCircleLinesV(cursor, 4.0f * ui_scale(), kFixCue);
            }
        }
    }

    // ---- fixed layout (D-028): the phase bar over the board, the sidebar on the right ----

    // D-037: one menu choice: its label, then a weapon's stats and what its abilities do to this volley
    // (green: applies, grey: doesn't), with the abilities' help on hover. True if it was clicked.
    static bool menu_entry(const MenuEntry& e) {
        if (e.detail.empty() && e.effects.empty()) {
            const bool clicked = ImGui::Selectable(e.label.c_str());
            help_tooltip_for_last_item(e.help);
            return clicked;
        }
        const float spacing = ImGui::GetTextLineHeightWithSpacing();
        const std::size_t lines = 1 + (e.detail.empty() ? 0 : 1) + e.effects.size();
        float width = ImGui::CalcTextSize(e.label.c_str()).x;
        width = std::max(width, ImGui::CalcTextSize(e.detail.c_str()).x);
        for (const EffectLine& l : e.effects) width = std::max(width, ImGui::CalcTextSize(("+ " + l.text).c_str()).x);
        const ImVec2 start = ImGui::GetCursorPos();
        const float height = spacing * static_cast<float>(lines) - ImGui::GetStyle().ItemSpacing.y;
        const bool clicked = ImGui::Selectable("##entry", false, ImGuiSelectableFlags_None, ImVec2(width, height));
        help_tooltip_for_last_item(e.help);
        const ImVec2 end = ImGui::GetCursorPos();
        ImGui::SetCursorPos(start);
        ImGui::TextUnformatted(e.label.c_str());
        if (!e.detail.empty()) ImGui::TextDisabled("%s", e.detail.c_str());
        draw_effect_lines(e.effects);
        ImGui::SetCursorPos(end);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return clicked;
    }

    // D-029: the menu a board click offers (move types, weapons, fight types).
    void draw_unit_menu() {
        if (open_menu_) {
            ImGui::OpenPopup("##unit_menu");
            open_menu_ = false;
        }
        if (!ImGui::BeginPopup("##unit_menu")) {
            if (!menu_.empty()) {
                menu_.clear();  // closed by a click off it, or Esc
                menu_anchor_.reset();
                menu_unit_.reset();
                tidy_selection();  // D-043
            }
            return;
        }
        std::optional<MenuEntry> picked;
        for (std::size_t i = 0; i < menu_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (menu_[i].custom || menu_[i].cancel) ImGui::Separator();
            if (menu_entry(menu_[i])) picked = menu_[i];
            ImGui::PopID();
        }
        ImGui::EndPopup();
        if (picked) {
            menu_.clear();
            menu_anchor_.reset();
            if (picked->cancel) clear_selection();  // D-041: as before the click
            menu_unit_.reset();
            apply(board_.choose(state(), *picked));
            tidy_selection();  // D-043
        }
    }

    void draw_ui() {
        const float w = static_cast<float>(GetScreenWidth());
        const float h = static_cast<float>(GetScreenHeight());
        if (has_view()) draw_unit_menu();
        constexpr ImGuiWindowFlags kFixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (has_view()) {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float bar = ImGui::GetFrameHeightWithSpacing() * 2.0f + style.WindowPadding.y * 2.0f;
            if (bar != bar_height_) {
                bar_height_ = bar;
                fit_pending_ = true;
            }
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(w - panel_width(), bar_height_));
            ImGui::Begin("##phasebar", nullptr, kFixed | ImGuiWindowFlags_NoScrollbar);
            draw_phase_bar();
            ImGui::End();

            // D-030: the action bar under the phase bar. A fixed height (long content goes in popups and
            // tooltips) so the board never refits while a decision is made.
            const float action = ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetFontSize() * 0.5f +
                                 style.WindowPadding.y * 2.0f;
            if (action != action_height_) {
                action_height_ = action;
                fit_pending_ = true;
            }
            ImGui::SetNextWindowPos(ImVec2(0.0f, bar_height_));
            const bool grows = mode_ == Mode::Play && board_.custom_editor();
            ImGuiWindowFlags action_flags = kFixed;
            if (grows) {
                // D-031, D-032: a custom shot or fight may need more rows; the bar then overlays the board's top
                // instead of resizing it, so the board doesn't refit.
                ImGui::SetNextWindowSizeConstraints(ImVec2(w - panel_width(), action_height_),
                                                    ImVec2(w - panel_width(), h * 0.45f));
                action_flags |= ImGuiWindowFlags_AlwaysAutoResize;
            } else {
                ImGui::SetNextWindowSize(ImVec2(w - panel_width(), action_height_));
            }
            // D-030: outlined and tinted in the colour of the side that must act now (01.03), so it is
            // obvious whose input the game is waiting for.
            ImVec4 bg(0.13f, 0.14f, 0.17f, 1.0f);
            // While dice show, the side rolling them (so the bar matches the pop-up, D-033).
            std::optional<core::Side> acting;
            if (mode_ == Mode::Play) {
                if (const DiceCard* card = dice_.current()) {
                    acting = card->roller;
                } else if (state().pending) {
                    acting = state().pending->side;
                }
            }
            const bool deciding = acting.has_value();
            if (deciding) {
                const ImVec4 c = to_imvec(side_color(*acting));
                constexpr float kTint = 0.22f;
                bg = ImVec4(bg.x + (c.x - bg.x) * kTint, bg.y + (c.y - bg.y) * kTint, bg.z + (c.z - bg.z) * kTint, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, c);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f * ui_scale());
            }
            ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
            ImGui::Begin("##actionbar", nullptr, action_flags);
            ImGui::PopStyleColor();
            if (deciding) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            if (mode_ == Mode::Play) {
                if (watching()) draw_watch_bar();  // M45: always there in bot vs bot
                if (const DiceCard* card = dice_.current()) {
                    draw_rolling(*card);  // D-033: the next decision waits for the dice pop-ups
                } else if (bot_deciding()) {
                    draw_bot_thinking();  // D-054
                } else {
                    draw_decision();
                }
            } else {
                draw_replay_controls();
            }
            action_drawn_height_ = ImGui::GetWindowHeight();
            ImGui::End();
            if (!reasons_.empty()) draw_reasons(w - panel_width());
        }
        ImGui::SetNextWindowPos(ImVec2(w - panel_width(), 0.0f));
        ImGui::SetNextWindowSize(ImVec2(panel_width(), h));
        ImGui::Begin("##sidebar", nullptr, kFixed);
        draw_sidebar();
        ImGui::End();
        if (has_view()) draw_datasheets();  // D-034
        // D-033: the dice pop-up, over everything else.
        if (has_view() && mode_ == Mode::Play) dice_.draw(board_area(), ui_scale(), bot_dice_pace());
        if (mode_ == Mode::Play) {
            draw_end_warning();  // D-038
            draw_victory();
        }
    }

    static ImVec4 to_imvec(Color c, float alpha = 1.0f) {
        return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha);
    }

    // A label on a coloured background that does nothing when clicked.
    static void chip(const char* label, ImVec4 colour) {
        ImGui::PushStyleColor(ImGuiCol_Button, colour);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colour);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colour);
        ImGui::Button(label);
        ImGui::PopStyleColor(3);
    }

    void draw_phase_bar() {
        const core::GameState& s = state();
        const PhaseTrack t = phase_track(s);
        const float gap = ImGui::GetFontSize() * 1.5f;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Battle round %d of %d", t.round, t.rounds);
        if (t.result) {
            ImGui::SameLine(0.0f, gap);
            ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Game over: %s", t.result->c_str());
            return;
        }
        if (t.turn) {
            ImGui::SameLine(0.0f, gap);
            const std::string turn = side_name(*t.turn) + "'s turn: " + s.side(*t.turn).name;
            chip(turn.c_str(), to_imvec(side_color(*t.turn)));
        }
        // The phase track: done phases dimmed, the current one highlighted in the turn side's colour.
        ImGui::SameLine(0.0f, gap);
        for (std::size_t i = 0; i < kTurnPhases.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
            }
            const std::string name(core::phase_name(kTurnPhases[i]));
            if (t.phase_index && i == *t.phase_index) {
                chip(name.c_str(), to_imvec(side_color(t.turn.value_or(core::Side::A))));
            } else if (t.phase_index && i < *t.phase_index) {
                ImGui::TextDisabled("%s", name.c_str());
            } else {
                ImGui::TextUnformatted(name.c_str());
            }
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Step:");
        ImGui::SameLine();
        ImGui::TextUnformatted(t.step.c_str());
        if (t.waiting_for) {
            ImGui::SameLine(0.0f, gap);
            const std::string waiting = "Waiting for " + side_name(*t.waiting_for);
            chip(waiting.c_str(), to_imvec(side_color(*t.waiting_for), 0.85f));
        }
        ImGui::SameLine(0.0f, gap);
        ImGui::TextDisabled("CP: Side A %d, Side B %d", s.command_points[0], s.command_points[1]);
    }

    void draw_sidebar() {
        draw_toolbar();
        if (!has_view()) {
            draw_files();
            return;
        }
        const float frame = ImGui::GetFrameHeightWithSpacing();
        // D-030: the decision (or the replay controls) is in the action bar over the board.
        if (ImGui::CollapsingHeader("Game", ImGuiTreeNodeFlags_DefaultOpen)) draw_game_info();
        ImGui::SeparatorText("Log");
        const float files_h = files_open_ ? frame * 6.0f : frame;
        const float log_h = std::max(frame * 4.0f, ImGui::GetContentRegionAvail().y - files_h);
        ImGui::BeginChild("##log", ImVec2(0.0f, log_h));
        draw_logs();
        ImGui::EndChild();
        draw_files();
    }

    void draw_toolbar() {
        if (ImGui::Button("Main menu")) menu_requested_ = true;  // M38: this game is discarded
        ImGui::SetItemTooltip("Back to the start screen. This game is discarded: save it first (Files) to keep it.");
        ImGui::SameLine();
        if (ImGui::Button("New game")) new_game();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::InputScalar("seed", ImGuiDataType_U64, &settings_.seed);
        ImGui::SameLine();
        ImGui::Checkbox("Fixed seed", &settings_.fixed_seed);  // D-038
        ImGui::SetItemTooltip("Off: each new game gets a random seed. On: new games use the seed in the box.");
        const bool playing = mode_ == Mode::Play && session_.has_value();
        if (playing) {
            ImGui::SameLine();
            const bool vs_bot = bot_sides_[0] || bot_sides_[1];
            ImGui::BeginDisabled(!session_->can_undo() || (vs_bot && !undo_groups_.has_human_group(*session_)));
            if (ImGui::Button("Undo")) undo_last();  // D-039: the last input, with what followed it (D-054)
            ImGui::SetItemTooltip("Undo your last action (a whole drag or button), and anything done "
                                  "automatically after it.");
            ImGui::EndDisabled();
            ImGui::TextDisabled("This game's seed: %llu", static_cast<unsigned long long>(session_->seed()));
        }
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    }

    void draw_game_info() {
        const core::GameState& s = state();
        for (const core::Unit& u : s.units) {
            ImGui::TextColored(to_imvec(side_color(u.side)), "%s", side_name(u.side).c_str());
            if (mode_ == Mode::Play && bot_sides_[static_cast<std::size_t>(u.side)]) {  // M38
                ImGui::SameLine();
                ImGui::TextDisabled("(bot)");
                ImGui::SetItemTooltip("The bot plays this side (D-054).");
            }
            ImGui::SameLine();
            ImGui::Text("%s (unit %u): %zu/%d models%s", u.name.c_str(), u.id.v, u.models.size(), u.starting_strength,
                        u.battle_shocked ? ", battle-shocked" : "");
        }
        if (mode_ == Mode::Play && (bot_sides_[0] || bot_sides_[1])) {  // M48 (D-059): the bot block, near the top
            ImGui::Spacing();
            bool paused = watch_.paused();
            if (ImGui::Checkbox("Pause the bots (P)", &paused)) watch_.set_paused(paused);  // client state, not saved
            ImGui::SetItemTooltip("The bots stop before their next action; their dice wait for a click. Unpaused, "
                                  "the bot's dice pop-ups roll and continue by themselves (a click on the pop-up or "
                                  "the board skips the wait). Step (in the bar above the board, key N) still does "
                                  "one action. Not kept between games.");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Rings, shown when relevant (hover for details):");
        overlay_toggle(RingKind::MoveRange, &overlays_.move_range);
        overlay_toggle(RingKind::Engagement, &overlays_.engagement);
        overlay_toggle(RingKind::Coherency, &overlays_.coherency);
        overlay_toggle(RingKind::WeaponRange, &overlays_.ranges);
        overlay_toggle(RingKind::ChargeDistance, &overlays_.charge);
        ImGui::Spacing();
        if (ImGui::Checkbox("Show dice pop-ups", &settings_.show_dice) && !settings_.show_dice) dice_.clear();  // D-033
        if (ImGui::Checkbox("Skip steps with nothing to do", &settings_.skip_empty_steps) && settings_.skip_empty_steps) auto_actions_as_group();
        ImGui::SetItemTooltip("When ending the step or passing is the only thing a side can do, do it for them.");
        ImGui::Checkbox("Fix illegal moves", &settings_.fix_moves);  // D-052
        ImGui::SetItemTooltip("A model dropped where it can't go (too far, off the battlefield, on a model, into an "
                              "enemy model, not closer) goes to the nearest spot it can, shown while dragging. "
                              "Ending within Engagement Range is left for Commit to check.");
        if (ImGui::Checkbox("Allocate damage automatically", &settings_.auto_allocate) && settings_.auto_allocate) auto_actions_as_group();
        // M47: on its own checkbox (it followed the bot block, so with a bot it was on the last bot checkbox).
        ImGui::SetItemTooltip("The defending side's allocation order and casualty order take their defaults (05.03, "
                              "05.04) instead of asking. The default casualty order: %s",
                              kDefaultCasualtyOrderText);
        ImGui::Checkbox("Warn before ending shooting with shots left", &settings_.warn_end_shooting);
        ImGui::SetItemTooltip("Ask before ending the Shooting phase while a unit still has a target it can shoot.");
        ImGui::Spacing();
        if (ImGui::Button("Fit board")) fit_pending_ = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        // D-038: the scale changes only when the slider is let go. Changing it while dragging resized the
        // slider under the mouse, so the value ran away.
        if (!scale_slider_active_) scale_percent_ = static_cast<int>(std::lround(settings_.ui_scale * 100.0f));
        ImGui::SliderInt("UI scale", &scale_percent_, static_cast<int>(kMinUiScale * 100.0f),
                         static_cast<int>(kMaxUiScale * 100.0f), "%d%%");
        scale_slider_active_ = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) settings_.ui_scale = static_cast<float>(scale_percent_) / 100.0f;
        ImGui::TextDisabled("click a unit to act, left-drag: move, right-click: datasheet, right-drag: pan, wheel: zoom, Esc: cancel");
    }

    static void overlay_toggle(RingKind kind, bool* value) {
        const std::string label(ring_name(kind));
        ImGui::PushID(label.c_str());
        // M50 (D-061): the row (swatch, checkbox, summary) is one group, so hovering any part of it shows the
        // full meaning, wrapped like the board's ring tooltip (SetItemTooltip doesn't wrap).
        ImGui::BeginGroup();
        const float size = ImGui::GetFrameHeight();
        ImGui::ColorButton("##swatch", to_imvec(ring_color(kind)),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(size, size));
        ImGui::SameLine();
        ImGui::Checkbox(label.c_str(), value);
        ImGui::Indent(size + ImGui::GetStyle().ItemSpacing.x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", std::string(ring_summary(kind)).c_str());
        ImGui::PopStyleColor();
        ImGui::Unindent(size + ImGui::GetStyle().ItemSpacing.x);
        ImGui::EndGroup();
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextColored(to_imvec(ring_color(kind)), "%s", label.c_str());
            ImGui::TextUnformatted(std::string(ring_meaning(kind)).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    void draw_files() {
        files_open_ = ImGui::CollapsingHeader("Files: save, load, replays");
        if (!files_open_) return;
        const bool playing = mode_ == Mode::Play && session_.has_value();
        ImGui::InputText("##save", save_path_, sizeof(save_path_));
        ImGui::SameLine();
        ImGui::BeginDisabled(!playing);
        if (ImGui::Button("Save")) save_game();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Load")) load_game();
        ImGui::InputText("##export", export_path_, sizeof(export_path_));
        ImGui::SameLine();
        ImGui::BeginDisabled(!playing);
        if (ImGui::Button("Export replay")) export_replay();
        ImGui::EndDisabled();
        ImGui::InputText("##path", path_, sizeof(path_));
        if (ImGui::Button("Load replay")) load_replay(path_);
        ImGui::SameLine();
        if (fs::is_directory(kReplayDir) && ImGui::BeginCombo("##replays", "tests/replays")) {
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(kReplayDir)) {
                if (entry.path().extension() == ".json") files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& f : files) {
                if (ImGui::Selectable(f.filename().string().c_str())) {
                    std::snprintf(path_, sizeof(path_), "%s", f.string().c_str());
                    load_replay(f);
                }
            }
            ImGui::EndCombo();
        }
    }

    // D-054: a bot's own dice roll and continue by themselves while the bots aren't paused (M51, D-062),
    // paced by the bot speed (Enter, or a click on the pop-up or the bare board, skips the wait; M48). A human's
    // dice wait for the human as before. M49 (D-060): a paced card shows no Roll / Continue button (the pop-up
    // draws them exactly when this returns nullopt, dice_buttons_shown).
    std::optional<DicePace> bot_dice_pace() const {
        const DiceCard* card = dice_.current();
        if (card == nullptr ||
            !watch_.card_paced(bot_sides_[core::side_index(card->roller)])) {
            return std::nullopt;
        }
        return DicePace{ai::bot_dice_roll_pause_seconds(settings_.bot_speed),
                        ai::bot_dice_dwell_seconds(settings_.bot_speed)};
    }

    // M45: Watch bot vs bot (both sides are bots).
    bool watching() const { return mode_ == Mode::Play && session_ && bot_sides_[0] && bot_sides_[1]; }

    // M48 (D-059): a tooltip for an item in the action bar. It waits for a longer rest than the default and
    // opens below the bar's bottom edge, so it never covers the bot's status or reason lines.
    static void bar_tooltip(const char* text) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal |
                                  ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_AllowWhenDisabled)) {
            return;
        }
        const float bar_bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, bar_bottom + ImGui::GetStyle().ItemSpacing.y));
        if (ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    // M45 (D-054): Step and the bot speed; in the watch bar and, against one bot, while it thinks or rolls.
    // M48 (D-059): Pause moved to the sidebar ("Pause the bots (P)"); Step comes first, so it never shifts.
    void draw_watch_buttons() {
        BotSpeed& speed = settings_.bot_speed;
        if (ImGui::Button("Step")) watch_.apply(WatchCommand::Step, speed);
        bar_tooltip("The bot's next action now, also while paused; with the bot's dice showing, its next roll "
                    "(key N). Pause the bots in the sidebar (key P).");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        int index = static_cast<int>(speed);
        if (ImGui::Combo("Bot speed", &index, "Slow\0Normal\0Fast\0Instant\0")) speed = static_cast<BotSpeed>(index);
        bar_tooltip("Changes the bot's pace at once (keys + and -). Slow is three times as long as Normal.");
    }

    // M45: the watch bar on top of the action bar in bot vs bot: what is playing, and the controls.
    // M48 (D-059): Step first, then the speed, then what is playing and its status.
    void draw_watch_bar() {
        draw_watch_buttons();
        ImGui::SameLine();
        const std::string tag = std::string("Watching bot vs bot (") + bot_level_name(game_bot_level_) + ")";
        chip(tag.c_str(), ImVec4(0.30f, 0.33f, 0.40f, 1.0f));
        ImGui::SameLine();
        const std::string status =
            session_->state().result ? std::string("Game over") : watch_status(watch_, settings_.bot_speed);
        if (watch_.paused()) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", status.c_str());
        } else {
            ImGui::TextDisabled("%s", status.c_str());
        }
        ImGui::Separator();
    }

    // M45: a bot reason, short (the action, the main reason, Hard's odds), with the full reason as its tooltip.
    static void bot_reason_text(const char* prefix, const std::string& reason) {
        const std::string text = std::string(prefix) + ai::short_reason(reason);
        ImGui::TextWrapped("%s", text.c_str());
        bar_tooltip(reason.c_str());  // M48: after a longer rest, below the bar
    }

    // D-054: the action bar while the bot decides: who, what it is about to do, Step and the speed (M48).
    void draw_bot_thinking() {
        const core::Side side = session_->pending()->side;
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        const std::string tag = "Bot (" + session_->state().side(side).name + ")";
        chip(tag.c_str(), to_imvec(side_color(side)));
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.6f);
        ImGui::AlignTextToFramePadding();
        if (driver_.stopped()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "has stopped: it found no legal action");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", watch_.paused() ? "is paused" : "is thinking...");
        }
        ImGui::PopFont();
        if (!watching()) draw_watch_buttons();  // in bot vs bot they are on the watch bar
        const std::optional<std::string> next = driver_.pending_reason();
        if (driver_.stopped()) {
            std::string text;
            for (const std::string& r : driver_.stop_reasons()) text += (text.empty() ? "" : "; ") + r;
            if (!text.empty()) ImGui::TextWrapped("%s", text.c_str());
        } else if (next) {
            bot_reason_text("Next: ", *next);  // M45: short, the full reason as a tooltip
        } else if (!bot_reason_.empty()) {
            bot_reason_text("Last: ", bot_reason_);
        }
    }

    // D-033: the action bar while a dice pop-up is open: who is rolling and what for, matching the pop-up.
    void draw_rolling(const DiceCard& card) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        const std::string tag = side_name(card.roller) + " rolls: " + state().side(card.roller).name;
        chip(tag.c_str(), to_imvec(side_color(card.roller)));
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.6f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s (%s)", card.title.c_str(), card.rule.c_str());
        ImGui::PopFont();
        // M48 (D-059): against one bot, its Step / speed row stays while its dice show (the same row as in
        // draw_bot_thinking), so it doesn't vanish and come back. In bot vs bot it is on the watch bar.
        if (!watching() && shown_card() == ShownCard::Bots) draw_watch_buttons();
        // M49 (D-060): a bot's card that rolls by itself has no Roll button to point at.
        const char* how = bot_dice_pace() ? "The bot rolls its own dice." : "Roll the dice in the pop-up.";
        const std::string hint = (card.purpose.empty() ? std::string() : card.purpose + ". ") + how;
        ImGui::TextWrapped("%s", hint.c_str());
    }

    void draw_decision() {
        // D-039: "Undo last step" in a move, while the last input moved this unit's models.
        panel_.set_undo_step_available(undo_groups_.last_group_is_move_step(*session_));
        const std::vector<core::GameAction> actions = panel_.draw(*session_, selected_model_, &board_);
        if (panel_.take_undo_step_request()) {
            undo_last();
            return;
        }
        if (panel_.take_cancel_request()) clear_selection();  // D-041
        // D-038: ending the Shooting phase while a unit can still shoot asks first.
        const bool ends_shooting = std::any_of(actions.begin(), actions.end(), [](const core::GameAction& a) {
            return std::holds_alternative<core::action::EndShootStep>(a);
        });
        if (ends_shooting && settings_.warn_end_shooting && !units_that_can_still_shoot(session_->legal()).empty()) {
            end_warning_ = actions;
            open_end_warning_ = true;
            return;
        }
        submit_all(actions);
    }

    // In order; stop at the first one rejected (the end-phase button can be several). One Undo group.
    void submit_all(const std::vector<core::GameAction>& actions) {
        if (actions.size() == 1) {
            submit(actions.front());
            return;
        }
        if (!actions.empty()) submit_group(actions, true);
    }

    // D-038: "<units> can still shoot. End the Shooting phase anyway?"
    void draw_end_warning() {
        if (open_end_warning_) {
            ImGui::OpenPopup("Units can still shoot");
            open_end_warning_ = false;
        }
        const ImVec2 centre(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Units can still shoot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        std::string names;
        if (session_) {
            for (const core::UnitId id : units_that_can_still_shoot(session_->legal())) {
                const core::Unit* u = session_->state().find_unit(id);
                names += (names.empty() ? "" : ", ") + (u != nullptr ? u->name : "unit " + std::to_string(id.v));
            }
        }
        ImGui::Text("%s can still shoot at a target.", names.empty() ? "A unit" : names.c_str());
        ImGui::TextUnformatted("End the Shooting phase anyway?");
        ImGui::Spacing();
        bool close = false;
        if (ImGui::Button("End phase")) {
            const std::vector<core::GameAction> actions = end_warning_.value_or(std::vector<core::GameAction>{});
            end_warning_.reset();
            close = true;
            if (session_) submit_all(actions);
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep shooting") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            end_warning_.reset();
            close = true;
        }
        ImGui::Checkbox("Warn me about this", &settings_.warn_end_shooting);
        if (close || !end_warning_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // D-038: the game ended while it was played: who won and why, once the last dice pop-up is closed.
    void draw_victory() {
        if (victory_pending_ && !dice_.active() && session_ && session_->state().result) {
            ImGui::OpenPopup("##victory");
            victory_pending_ = false;
        }
        const ImVec2 centre(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("##victory", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            return;
        }
        const std::optional<GameOverText> text = session_ ? game_over_text(session_->state()) : std::nullopt;
        if (text) {
            const ImVec4 colour = text->winner ? to_imvec(side_color(*text->winner)) : ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.2f);
            ImGui::TextColored(colour, "%s", text->title.c_str());
            ImGui::PopFont();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextDisabled("%s", text->reason.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        bool new_one = false;
        if (ImGui::Button("New game")) new_one = true;
        ImGui::SameLine();
        bool to_menu = false;
        if (ImGui::Button("Main menu")) to_menu = true;  // M38
        ImGui::SameLine();
        if (ImGui::Button("Close") || !text) ImGui::CloseCurrentPopup();
        if (new_one || to_menu) ImGui::CloseCurrentPopup();
        if (to_menu) menu_requested_ = true;
        ImGui::EndPopup();
        if (new_one) new_game();
    }

    // D-030: the last rejection, rule-cited, in a red strip over the top of the board (so the board doesn't
    // move when it appears).
    void draw_reasons(float width) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, bar_height_ + std::max(action_height_, action_drawn_height_)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, ImGui::GetIO().DisplaySize.y * 0.4f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.32f, 0.08f, 0.08f, 0.94f));
        ImGui::Begin("##reasons", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1), "Not allowed:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) reasons_.clear();
        ImGui::PushTextWrapPos(width - ImGui::GetStyle().WindowPadding.x);
        for (const std::string& r : reasons_) ImGui::BulletText("%s", r.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
    }

    void draw_replay_controls() {
        const std::size_t last = timeline_.size() - 1;
        if (ImGui::Button("|<")) go_to(0);
        ImGui::SameLine();
        if (ImGui::Button("<") && frame_ > 0) go_to(frame_ - 1);
        ImGui::SameLine();
        if (ImGui::Button(">")) go_to(frame_ + 1);
        ImGui::SameLine();
        if (ImGui::Button(">|")) go_to(last);
        ImGui::SameLine();
        ImGui::Text("%zu / %zu", frame_, last);
        ImGui::SameLine();
        int slider = static_cast<int>(frame_);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##frame", &slider, 0, static_cast<int>(last))) go_to(static_cast<std::size_t>(slider));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", timeline_.frame(frame_).label.c_str());
        ImGui::PopFont();
        std::string info = timeline_.source();
        if (state().pending) info += "  |  Waiting for: " + core::describe(*state().pending);
        ImGui::TextDisabled("%s", info.c_str());
        if (!timeline_.error().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", timeline_.error().c_str());
        }
    }

    // The log as display lines, rebuilt only when the log changes (not formatted every frame).
    struct LogLine {
        std::string text;
        bool label = false;  // a step header (drawn dimmed)
        bool roll = false;   // shown on the Dice tab
        const ai::BotLogNote* bot = nullptr;  // M45: a line of the bot's note (its side's colour, the reason's tooltip)
    };

    // M45: about how many characters fit on a log line (the bot's notes are wrapped to it).
    std::size_t log_line_chars() const {
        const float avg = ImGui::CalcTextSize("abcdefghijklmnopqrstuvwxyz").x / 26.0f;
        const float width =
            panel_width() - 3.0f * ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize;
        return static_cast<std::size_t>(std::max(20.0f, width / std::max(1.0f, avg)));
    }

    void refresh_log_cache() {
        // D-033: while dice show, the log stops before the roll's action, so it doesn't give the results away.
        std::size_t count = frame_ + 1;
        if (mode_ == Mode::Play) count = before_dice_ && dice_.active() ? log_before_dice_ : session_->log().size();
        const LogKey key{generation_, mode_, count, bot_log_.notes().size(), log_line_chars()};
        if (log_key_ == key) return;
        log_key_ = key;
        log_lines_.clear();
        auto add_entry = [&](const std::string& label, bool first, const std::vector<core::GameEvent>& events) {
            if (!first) log_lines_.push_back(LogLine{"-- " + label, true, false});
            for (const auto& e : events) log_lines_.push_back(LogLine{format_event(e), false, is_roll_event(e)});
        };
        if (mode_ == Mode::Play) {
            const std::size_t chars = log_line_chars();
            for (std::size_t i = 0; i < count; ++i) {
                log_lines_.push_back(LogLine{"-- " + session_->log()[i].label, true, false});
                // M45: the bot's note for this action (a move's placements and commit share its start's note).
                if (const ai::BotLogNote* note = bot_log_.at(i)) {
                    bool first = true;
                    for (std::string& part : ai::wrap_words(note->line, chars)) {
                        log_lines_.push_back(LogLine{(first ? "" : "    ") + part, false, false, note});
                        first = false;
                    }
                }
                for (const auto& e : session_->log()[i].events) {
                    log_lines_.push_back(LogLine{format_event(e), false, is_roll_event(e)});
                }
            }
        } else {
            for (std::size_t f = 0; f <= frame_; ++f) {
                add_entry(timeline_.frame(f).label, f == 0, timeline_.frame(f).events);
            }
        }
        dice_lines_.clear();
        for (std::size_t i = 0; i < log_lines_.size(); ++i) {
            if (log_lines_[i].roll) dice_lines_.push_back(i);
        }
    }

    void draw_log_lines(bool dice_only) {
        const int n = static_cast<int>(dice_only ? dice_lines_.size() : log_lines_.size());
        ImGuiListClipper clipper;  // only the visible lines are drawn
        clipper.Begin(n);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const LogLine& line = log_lines_[dice_only ? dice_lines_[static_cast<std::size_t>(i)]
                                                           : static_cast<std::size_t>(i)];
                if (line.label) {
                    ImGui::TextDisabled("%s", line.text.c_str());
                } else if (line.bot != nullptr) {
                    draw_bot_log_line(line);
                } else {
                    ImGui::TextUnformatted(line.text.c_str());
                }
            }
        }
    }

    // M45: a line of the bot's note, in its side's colour; the tooltip has the full reason.
    static void draw_bot_log_line(const LogLine& line) {
        const ai::BotLogNote& note = *line.bot;
        ImGui::TextColored(to_imvec(side_color(note.side)), "%s", line.text.c_str());
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(note.reason.c_str());
            if (note.entries.size() > 1) ImGui::TextDisabled("%zu actions of one plan.", note.entries.size());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void draw_logs() {
        refresh_log_cache();
        if (ImGui::BeginTabBar("logs")) {
            for (const bool dice : {false, true}) {
                if (ImGui::BeginTabItem(dice ? "Dice" : "Events")) {
                    ImGui::BeginChild(dice ? "dice" : "events");
                    draw_log_lines(dice);
                    if (scroll_log_) ImGui::SetScrollHereY(1.0f);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        scroll_log_ = false;
    }

    struct LogKey {
        std::uint64_t generation = 0;
        Mode mode = Mode::Play;
        std::size_t count = 0;
        std::size_t bot_notes = 0;   // M45
        std::size_t line_chars = 0;  // M45: the bot's notes are wrapped to the panel
        friend bool operator==(const LogKey&, const LogKey&) = default;
    };

    Mode mode_ = Mode::Play;
    std::uint64_t generation_ = 0;  // bumped whenever the session or replay is replaced or rebuilt
    std::optional<LogKey> log_key_;
    std::vector<LogLine> log_lines_;
    std::vector<std::size_t> dice_lines_;
    std::optional<GameSession> session_;
    Timeline timeline_;
    std::size_t frame_ = 0;
    BoardCamera camera_;
    OverlayToggles overlays_;
    DecisionPanel panel_;
    struct BoardKey {
        std::uint64_t generation = 0;
        Mode mode = Mode::Play;
        std::size_t step = 0;
        friend bool operator==(const BoardKey&, const BoardKey&) = default;
    };
    BoardInteraction board_;            // D-029: clicks on the board for the current decision
    std::optional<BoardKey> board_key_;  // the decision board_ was built for
    std::vector<MenuEntry> menu_;        // the open click menu's entries
    bool open_menu_ = false;
    std::optional<core::UnitId> menu_unit_;  // D-043: the unit whose click opened the menu
    bool board_built_ = false;               // D-043: the first board (keeps a --select selection)
    std::optional<core::Position> menu_anchor_;  // where the open menu's click was (the target line ends there)
    Vector2 rest_mouse_{0.0f, 0.0f};             // ring tooltips wait for the cursor to rest
    double rest_since_ = 0.0;
    std::optional<core::UnitId> selected_unit_;
    std::optional<core::ModelId> selected_model_;
    std::optional<Drag> drag_;
    std::optional<Measure> measure_;  // D-035
    struct RightPress {
        Vector2 screen;
        std::optional<core::UnitId> unit;  // the unit under the cursor when the right button went down
    };
    std::optional<RightPress> right_press_;  // D-034: a right-click opens a datasheet, a right-drag pans
    std::vector<core::UnitId> open_sheets_;  // D-034: the open datasheet windows
    std::vector<std::string> reasons_;  // the last rejection, rule-cited
    DicePopup dice_;                    // D-033: the rolls waiting to be shown
    std::optional<core::GameState> before_dice_;  // the state drawn while they show (before the roll's action)
    std::size_t log_before_dice_ = 0;   // the log entries shown while they show
    std::size_t dice_seen_ = 0;         // log entries already turned into dice pop-ups
    UndoGroups undo_groups_;            // D-039: the player's inputs, for Undo and "Undo last step"
    std::optional<std::vector<core::GameAction>> end_warning_;  // D-038: the end-phase actions waiting on it
    bool open_end_warning_ = false;
    bool victory_pending_ = false;      // D-038: the game ended while played; show the pop-up
    int scale_percent_ = 100;           // D-038: the UI scale slider's value while dragged
    bool scale_slider_active_ = false;
    bool fit_pending_ = true;
    bool scroll_log_ = true;
    bool files_open_ = false;
    float bar_height_ = 0.0f;     // the phase bar's height, measured each frame
    float action_height_ = 0.0f;  // the action bar's height under it (D-030)
    float action_drawn_height_ = 0.0f;  // its height as drawn this frame (taller in the custom-shot editor)
    float auto_scale_ = 1.0f;     // the screen's scale (D-028)
    float applied_scale_ = 0.0f;  // the scale the ImGui style was last built for
    ImGuiStyle base_style_;
    std::vector<Circle> ring_circles_;  // this frame's rings, for the board tooltip
    char path_[512] = {};
    char save_path_[512] = {};
    char export_path_[512] = {};
    std::string status_;
    // M38 (D-055): every option (the start screen's and the sidebar's toggles, the seed, the UI scale) lives here.
    AppSettings settings_;
    AppSettings saved_settings_;  // as last written to the settings file
    bool persist_ = false;        // false in a scripted (CLI) run: the settings file is never touched
    std::optional<fs::path> scenario_override_;  // --scenario
    BotSides bot_sides_{false, false};  // M38: which sides the bot plays (client state, not GameState; D-054)
    BotLevel game_bot_level_ = BotLevel::Easy;  // M39: this game's bot level (from the settings, or the save)
    ai::BotDriver driver_;              // M39 (D-054): paces the bot's decisions
    WatchControls watch_;               // M45: Pause, Step and the speed; M48: Pause is in the sidebar
    std::string bot_reason_;            // why the bot did its last action
    ai::BotLog bot_log_;                // M45: the bot's reasons in the log (client state only, never saved)
    StartScreen start_screen_;
    bool menu_requested_ = false;  // "Main menu" was clicked; the start screen opens next frame
    bool quit_ = false;            // the start screen's Quit
};

}  // namespace
}  // namespace wh40k::client

int main(int argc, char** argv) {
    using namespace wh40k::client;
    Options options;
    try {
        options = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wh40k_client_2d: %s\n", e.what());
        return 2;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    const std::string title = "VibeHammer " + std::string(wh40k::core::engine_version());
    InitWindow(kWindowWidth, kWindowHeight, title.c_str());
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);  // Esc cancels a board selection (D-029), it doesn't quit
    rlImGuiSetup(true);

    // D-028: open maximized and scale the UI to the screen: the larger of the monitor's DPI scale and its
    // height relative to 1080 pixels (some high-DPI screens report a DPI scale of 1).
    MaximizeWindow();
    const int monitor = GetCurrentMonitor();
    const float dpi = GetWindowScaleDPI().y;
    const float by_height = static_cast<float>(GetMonitorHeight(monitor)) / 1080.0f;
    // A crisp UI font where the system has one; ImGui 1.92 rasterises it at whatever size is used.
    const fs::path ui_font = "C:/Windows/Fonts/segoeui.ttf";
    std::error_code font_error;
    if (fs::exists(ui_font, font_error)) {
        ImGuiIO& io = ImGui::GetIO();
        io.FontDefault = io.Fonts->AddFontFromFileTTF(ui_font.string().c_str(), kFontSize);
    }

    // M38 (D-055): with no flags, the start screen and the saved settings. Any flag: a scripted run with the
    // defaults and the flags' overrides, which never reads or writes the settings file.
    const bool scripted = argc > 1;
    AppSettings settings = scripted ? AppSettings{} : load_settings(settings_path());
    if (options.seed) {
        settings.fixed_seed = true;  // D-038: --seed fixes it
        settings.seed = *options.seed;
    }
    if (options.bot_level) settings.bot_level = *options.bot_level;  // M39
    if (options.bot_speed) settings.bot_speed = *options.bot_speed;
    App app(settings, !scripted, options.scenario, !scripted || options.menu, options.bots);
    app.set_auto_scale(std::max({1.0f, dpi, by_height}));
    if (options.replay) app.load_replay(*options.replay);
    if (options.frame) app.go_to(*options.frame);
    app.auto_steps(options.steps);
    app.bot_steps(options.bot_steps);  // M39
    app.cli_dice(options.dice);
    app.select(options.select_unit, options.select_model);

    // --screenshot: render a few frames (ImGui needs them for layout), save the last one, exit.
    int frames_left = options.screenshot ? 4 : -1;
    bool zoom_applied = false;
    while (!WindowShouldClose() && !app.quit_requested() && frames_left != 0) {
        BeginDrawing();
        rlImGuiBegin();
        app.frame();
        if (options.zoom && !zoom_applied) {
            app.set_zoom(*options.zoom);  // after the first fit
            zoom_applied = true;
        }
        rlImGuiEnd();
        if (frames_left == 1) {
            Image shot = LoadImageFromScreen();
            ExportImage(shot, options.screenshot->c_str());
            UnloadImage(shot);
        }
        EndDrawing();
        if (frames_left > 0) --frames_left;
    }

    app.save_settings_if_changed();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
