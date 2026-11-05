// wh40k_client_2d (M12): hotseat play and replay viewing. The client never mutates game state itself: every
// game change is a GameAction submitted through GameSession, which validates it with the engine before
// applying it (D-024). Replays are stepped through Timeline (read-only).
//
// Usage: wh40k_client_2d [--replay <file>] [--frame <n>] [--zoom <px per mm>] [--screenshot <file.png>]
//                        [--steps <n>] [--select <unit id>] [--select-model <model id>] [--seed <n>]
//                        [--scenario <file>]
//   --steps n: in a new game, click the first decision button n times (for screenshots and smoke tests).
//   --seed n: the dice seed for the first new game (M13; the panel can change it for later games).
//   --scenario file: the scenario for new games (default data/scenarios/v1_skirmish.json).

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "board_view.hpp"
#include "camera.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/units.hpp"
#include "core/version.hpp"
#include "decision_panel.hpp"
#include "decision_view.hpp"
#include "event_text.hpp"
#include "move_plan.hpp"
#include "overlays.hpp"
#include "session.hpp"
#include "timeline.hpp"

namespace wh40k::client {
namespace {

namespace fs = std::filesystem;

constexpr int kWindowWidth = 1400;
constexpr int kWindowHeight = 860;
constexpr float kPanelWidth = 470.0f;
constexpr float kHandlePx = 11.0f;
constexpr float kDragThresholdPx = 3.0f;
const fs::path kRepoRoot{WH40K_REPO_ROOT};
const fs::path kScenario = kRepoRoot / "data/scenarios/v1_skirmish.json";
const fs::path kReplayDir = kRepoRoot / "tests/replays";
const fs::path kSavePath = kRepoRoot / "saves/game.json";              // gitignored
const fs::path kExportPath = kRepoRoot / "replays/exported_replay.json";  // gitignored

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
        } catch (const std::exception&) {
            throw std::invalid_argument("invalid value for " + key + ": '" + value + "'");
        }
    }
    return o;
}

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

float distance_px(Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

enum class Mode { Play, Replay };

// A drag on the board: one model's next straight line, or the whole unit (the "Move unit" handle).
struct Drag {
    bool whole_unit = false;
    core::ModelId model;     // whole_unit: unused
    core::UnitId unit;
    core::Position grab;     // world position where the drag started
    core::Position current;  // world position of the cursor now
    Vector2 grab_screen{0, 0};
};

class App {
public:
    App(std::optional<std::uint64_t> seed, std::optional<std::string> scenario)
        : scenario_path_(scenario ? fs::path(*scenario) : kScenario) {
        try {
            seed_ = seed ? *seed : core::load_scenario(scenario_path_, kRepoRoot).dice.seed;
        } catch (const std::exception&) {
            seed_ = 0;
        }
        new_game();
        std::snprintf(path_, sizeof(path_), "%s", (kReplayDir / "m11_full_game.json").string().c_str());
        std::snprintf(save_path_, sizeof(save_path_), "%s", kSavePath.string().c_str());
        std::snprintf(export_path_, sizeof(export_path_), "%s", kExportPath.string().c_str());
    }

    void new_game() {
        try {
            // M13: the dice seed is chosen at game start (shown and editable in the panel).
            start_session(GameSession::new_game(core::load_scenario(scenario_path_, kRepoRoot), seed_));
            status_ = "New game: V1 scenario, seed " + std::to_string(seed_) +
                      ". Hotseat: each side acts when the panel names it.";
        } catch (const std::exception& e) {
            status_ = std::string("Could not load the scenario: ") + e.what();
        }
    }

    void start_session(GameSession session) {
        session_.emplace(std::move(session));
        mode_ = Mode::Play;
        panel_.reset();  // the editors belong to the old session
        ++generation_;
        reasons_.clear();
        drag_.reset();
        fit_pending_ = true;
        scroll_log_ = true;
    }

    void save_game() {
        if (!session_) return;
        try {
            session_->save(save_path_);
            status_ = std::string("Saved to ") + save_path_;
        } catch (const std::exception& e) {
            status_ = std::string("Could not save: ") + e.what();
        }
    }

    void load_game() {
        try {
            start_session(GameSession::load(save_path_));
            status_ = std::string("Loaded ") + save_path_ + " (seed " + std::to_string(session_->seed()) + ")";
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

    // --steps: click the first decision button (never a template) `n` times.
    void auto_steps(int n) {
        for (int i = 0; i < n && mode_ == Mode::Play && session_ && session_->pending(); ++i) {
            const DecisionView view = make_decision_view(session_->legal());
            if (view.buttons.empty()) break;
            submit(view.buttons.front());
        }
    }

    void select(std::optional<unsigned> unit, std::optional<unsigned> model) {
        if (unit) selected_unit_ = core::UnitId{*unit};
        if (model) selected_model_ = core::ModelId{*model};
    }

    void set_zoom(float px_per_mm) { camera_.set_zoom(px_per_mm, board_centre()); }

    void frame(bool layout_fixed) {
        ClearBackground(Color{24, 26, 30, 255});
        if (!has_view()) {
            // No game and no replay (e.g. the scenario could not be loaded): show why, and the panel can
            // still load a save or a replay.
            DrawText(status_.c_str(), 20, 20, 18, RAYWHITE);
            draw_ui(layout_fixed);
            return;
        }
        if (fit_pending_ || IsWindowResized()) {
            camera_.fit(state().battlefield.width_mm, state().battlefield.depth_mm, board_area());
            fit_pending_ = false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) handle_mouse();
        if (!io.WantCaptureKeyboard) handle_keys();

        draw_board(state(), camera_);
        draw_overlays(state(), camera_, overlays_, selected_unit_, selected_model_, friendly_side());
        draw_selection_and_moves();
        draw_ui(layout_fixed);
    }

private:
    // Something to show: a live game in Play mode, a loaded replay in Replay mode.
    bool has_view() const { return mode_ == Mode::Play ? session_.has_value() : timeline_.size() > 0; }

    // Precondition: has_view().
    const core::GameState& state() const {
        return mode_ == Mode::Play ? session_->state() : timeline_.frame(frame_).state;
    }

    core::Side friendly_side() const {
        return mode_ == Mode::Play ? session_->deciding_side() : core::active_side(state());
    }

    Rectangle board_area() const {
        return Rectangle{0.0f, 0.0f, static_cast<float>(GetScreenWidth()) - kPanelWidth,
                         static_cast<float>(GetScreenHeight())};
    }

    Vector2 board_centre() const {
        const Rectangle a = board_area();
        return Vector2{a.x + a.width * 0.5f, a.y + a.height * 0.5f};
    }

    // Every game change goes through here (and so through validate_action).
    void submit(const core::GameAction& action) {
        const core::ValidationResult r = session_->submit(action);
        reasons_ = r.reasons;
        if (r.ok()) scroll_log_ = true;
    }

    // The unit being moved model by model, if it is this player's MoveModels decision.
    std::optional<core::UnitId> moving_unit() const {
        if (mode_ != Mode::Play || !session_->pending()) return std::nullopt;
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
        if (hovered && !drag_) draw_tooltip(*hovered);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) press(mouse, hovered);
        if (drag_) drag_->current = camera_.to_world(mouse);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && drag_) release(mouse);
    }

    void press(Vector2 mouse, const std::optional<HoveredModel>& hovered) {
        const auto moving = moving_unit();
        if (const auto handle = unit_handle_screen(); handle && distance_px(*handle, mouse) <= kHandlePx) {
            drag_ = Drag{true, {}, *moving, camera_.to_world(mouse), camera_.to_world(mouse), mouse};
            return;
        }
        if (!hovered) {
            selected_model_.reset();
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
        }
    }

    void release(Vector2 mouse) {
        const Drag d = *drag_;
        drag_.reset();
        if (distance_px(d.grab_screen, mouse) < kDragThresholdPx) return;  // a click, not a drag
        if (d.whole_unit) {
            // The "Move unit" handle: one ProposeModelMove per model, each validated by the engine.
            std::vector<std::string> all;
            for (const auto& a : translate_unit(state(), d.unit, d.current.x - d.grab.x, d.current.y - d.grab.y)) {
                submit(a);
                all.insert(all.end(), reasons_.begin(), reasons_.end());
            }
            reasons_ = all;
            return;
        }
        submit(extend_path(state(), d.model, d.current));
    }

    void handle_keys() {
        if (mode_ != Mode::Replay) return;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) go_to(frame_ + 1);
        if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && frame_ > 0) go_to(frame_ - 1);
        if (IsKeyPressed(KEY_HOME)) go_to(0);
        if (IsKeyPressed(KEY_END)) go_to(timeline_.size() - 1);
    }

    void draw_tooltip(const HoveredModel& h) {
        const core::Model& m = *h.model;
        ImGui::BeginTooltip();
        ImGui::Text("%s (unit %u, %s)", h.unit->name.c_str(), h.unit->id.v, side_name(h.unit->side).c_str());
        ImGui::Text("Model %u: %s", m.id.v, m.type_name.c_str());
        ImGui::Text("Wounds %d/%d, base %.1f mm", m.wounds_remaining, m.characteristics.wounds, m.base.diameter_mm);
        ImGui::Text("Position (%.2f\", %.2f\")", core::mm_to_inches(m.position.x), core::mm_to_inches(m.position.y));
        if (h.unit->battle_shocked) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Battle-shocked");
        ImGui::EndTooltip();
    }

    void draw_ruler(Vector2 at, const std::optional<Ruler>& r) {
        if (!r) return;
        char text[64];
        std::snprintf(text, sizeof(text), "%.2f\" / %.2f\"", static_cast<double>(core::mm_to_inches(r->moved_mm)),
                      static_cast<double>(core::mm_to_inches(r->max_mm)));
        DrawText(text, static_cast<int>(at.x) + 14, static_cast<int>(at.y) - 22, 18, r->over ? RED : RAYWHITE);
    }

    void draw_selection_and_moves() {
        const core::GameState& gs = state();
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
            DrawPoly(h, 4, kHandlePx, 45.0f, Color{255, 220, 80, 230});
            DrawPolyLines(h, 4, kHandlePx, 45.0f, BLACK);
        }
        // The drag preview and the live ruler.
        if (drag_) {
            const Vector2 cursor = camera_.to_screen(drag_->current);
            if (drag_->whole_unit) {
                const float dx = drag_->current.x - drag_->grab.x;
                const float dy = drag_->current.y - drag_->grab.y;
                const core::Unit* u = gs.find_unit(drag_->unit);
                std::optional<Ruler> worst;
                for (const core::Model& m : u->models) {
                    const core::Position to{m.position.x + dx, m.position.y + dy, 0.0f};
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
                    DrawLineEx(camera_.to_screen(m->position), cursor, 2.0f, YELLOW);
                    DrawCircleV(cursor, camera_.to_px(m->base.diameter_mm * 0.5f), Color{255, 255, 255, 70});
                    draw_ruler(cursor, ruler(gs, m->id, drag_->current));
                }
            }
        }
    }

    void draw_ui(bool layout_fixed) {
        const ImGuiCond cond = layout_fixed ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        const float x = static_cast<float>(GetScreenWidth()) - kPanelWidth;
        const float h = static_cast<float>(GetScreenHeight());
        ImGui::SetNextWindowPos(ImVec2(x, 0.0f), cond);
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth, h * 0.44f), cond);
        ImGui::Begin("VibeHammer");
        draw_game_section();
        ImGui::End();
        if (!has_view()) return;

        ImGui::SetNextWindowPos(ImVec2(x, h * 0.44f), cond);
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth, h * 0.32f), cond);
        ImGui::Begin(mode_ == Mode::Play ? "Decision" : "Replay");
        if (mode_ == Mode::Play) {
            draw_decision();
        } else {
            draw_replay_controls();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(x, h * 0.76f), cond);
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth, h * 0.24f), cond);
        ImGui::Begin("Log");
        draw_logs();
        ImGui::End();
    }

    void draw_game_section() {
        if (ImGui::Button("New game (V1 scenario)")) new_game();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputScalar("seed", ImGuiDataType_U64, &seed_);
        const bool playing = mode_ == Mode::Play && session_.has_value();
        if (playing) {
            ImGui::Text("This game's seed: %llu", static_cast<unsigned long long>(session_->seed()));
            ImGui::SameLine();
            ImGui::BeginDisabled(!session_->can_undo());
            if (ImGui::Button("Undo")) {
                session_->undo();  // M13: rebuilt from the initial state without the last action
                panel_.reset();    // the rebuilt state may have the same step count as the editors' old one
                ++generation_;
                reasons_.clear();
                drag_.reset();
                scroll_log_ = true;
            }
            ImGui::EndDisabled();
        }
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
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
        if (!has_view()) return;
        ImGui::Separator();
        const core::GameState& s = state();
        const core::Stage stage = s.flow.stage;
        ImGui::Text("Round %d/%d   %s", s.flow.battle_round, s.battle_rounds,
                    stage != core::Stage::NotStarted && stage != core::Stage::GameOver
                        ? ("Turn: " + side_name(core::turn_side(s))).c_str()
                        : "");
        ImGui::Text("%s phase, %s (%s)", std::string(core::phase_name(core::phase_of(stage))).c_str(),
                    std::string(core::stage_name(stage)).c_str(), std::string(core::stage_rule(stage)).c_str());
        ImGui::Text("CP: Side A %d, Side B %d", s.command_points[0], s.command_points[1]);
        for (const core::Unit& u : s.units) {
            ImGui::Text("%s: %s (unit %u), %zu/%d models%s", side_name(u.side).c_str(), u.name.c_str(), u.id.v,
                        u.models.size(), u.starting_strength, u.battle_shocked ? ", battle-shocked" : "");
        }
        if (s.result) ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "%s", s.result->reason.c_str());
        ImGui::Separator();
        ImGui::Checkbox("Coherency", &overlays_.coherency);
        ImGui::SameLine();
        ImGui::Checkbox("Engagement", &overlays_.engagement);
        ImGui::SameLine();
        ImGui::Checkbox("Ranges", &overlays_.ranges);
        ImGui::SameLine();
        ImGui::Checkbox("Charge", &overlays_.charge);
        if (ImGui::Button("Fit board")) fit_pending_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("click: select, left-drag: move, right-drag: pan, wheel: zoom");
    }

    void draw_decision() {
        if (!reasons_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Not allowed:");
            for (const std::string& r : reasons_) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.55f, 1));
                ImGui::BulletText("%s", r.c_str());
                ImGui::PopStyleColor();
            }
            if (ImGui::SmallButton("Dismiss")) reasons_.clear();
            ImGui::Separator();
        }
        if (const auto chosen = panel_.draw(*session_, selected_model_)) submit(*chosen);
    }

    void draw_replay_controls() {
        ImGui::TextWrapped("%s", timeline_.source().c_str());
        if (!timeline_.error().empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", timeline_.error().c_str());
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
        int slider = static_cast<int>(frame_);
        if (ImGui::SliderInt("##frame", &slider, 0, static_cast<int>(last))) go_to(static_cast<std::size_t>(slider));
        ImGui::TextUnformatted(timeline_.frame(frame_).label.c_str());
        if (state().pending) ImGui::TextWrapped("Waiting for: %s", core::describe(*state().pending).c_str());
    }

    // The log as display lines, rebuilt only when the log changes (not formatted every frame).
    struct LogLine {
        std::string text;
        bool label = false;  // a step header (drawn dimmed)
        bool roll = false;   // shown on the Dice tab
    };

    void refresh_log_cache() {
        const std::size_t count = mode_ == Mode::Play ? session_->log().size() : frame_ + 1;
        const LogKey key{generation_, mode_, count};
        if (log_key_ == key) return;
        log_key_ = key;
        log_lines_.clear();
        auto add_entry = [&](const std::string& label, bool first, const std::vector<core::GameEvent>& events) {
            if (!first) log_lines_.push_back(LogLine{"-- " + label, true, false});
            for (const auto& e : events) log_lines_.push_back(LogLine{format_event(e), false, is_roll_event(e)});
        };
        if (mode_ == Mode::Play) {
            for (std::size_t i = 0; i < session_->log().size(); ++i) {
                add_entry(session_->log()[i].label, false, session_->log()[i].events);
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
                } else {
                    ImGui::TextUnformatted(line.text.c_str());
                }
            }
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
        friend bool operator==(const LogKey&, const LogKey&) = default;
    };

    Mode mode_ = Mode::Play;
    fs::path scenario_path_;
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
    std::optional<core::UnitId> selected_unit_;
    std::optional<core::ModelId> selected_model_;
    std::optional<Drag> drag_;
    std::vector<std::string> reasons_;  // the last rejection, rule-cited
    bool fit_pending_ = true;
    bool scroll_log_ = true;
    char path_[512] = {};
    char save_path_[512] = {};
    char export_path_[512] = {};
    std::uint64_t seed_ = 0;
    std::string status_;
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
    rlImGuiSetup(true);

    App app(options.seed, options.scenario);
    if (options.replay) app.load_replay(*options.replay);
    if (options.frame) app.go_to(*options.frame);
    app.auto_steps(options.steps);
    app.select(options.select_unit, options.select_model);

    // --screenshot: render a few frames (ImGui needs them for layout), save the last one, exit.
    int frames_left = options.screenshot ? 4 : -1;
    bool zoom_applied = false;
    while (!WindowShouldClose() && frames_left != 0) {
        BeginDrawing();
        rlImGuiBegin();
        app.frame(options.screenshot.has_value());
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

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
