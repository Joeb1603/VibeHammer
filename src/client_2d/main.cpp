// wh40k_client_2d (M6): read-only viewer. Renders a GameState and steps through replays.
// The client never mutates game state itself: it only calls the engine API (via Timeline) on its own copies.
//
// Usage: wh40k_client_2d [--replay <file>] [--frame <n>] [--zoom <px per mm>] [--screenshot <file.png>]

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
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
#include "event_text.hpp"
#include "timeline.hpp"

namespace wh40k::client {
namespace {

namespace fs = std::filesystem;

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 800;
constexpr float kPanelWidth = 430.0f;
const fs::path kRepoRoot{WH40K_REPO_ROOT};
const fs::path kScenario = kRepoRoot / "data/scenarios/v1_skirmish.json";
const fs::path kReplayDir = kRepoRoot / "tests/replays";

struct Options {
    std::optional<std::string> replay;
    std::optional<std::size_t> frame;
    std::optional<float> zoom;
    std::optional<std::string> screenshot;
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const std::string value = argv[i + 1];
        if (key == "--replay") o.replay = value;
        else if (key == "--frame") o.frame = static_cast<std::size_t>(std::stoul(value));
        else if (key == "--zoom") o.zoom = std::stof(value);
        else if (key == "--screenshot") o.screenshot = value;
    }
    return o;
}

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

class Viewer {
public:
    Viewer() {
        new_game();
        std::snprintf(path_, sizeof(path_), "%s", (kReplayDir / "m5_full_game_all_pass.json").string().c_str());
    }

    void new_game() {
        try {
            set_timeline(Timeline::from_state(core::load_scenario(kScenario, kRepoRoot), "New game: V1 scenario"));
        } catch (const std::exception& e) {
            status_ = std::string("Could not load the scenario: ") + e.what();
        }
    }

    bool load_replay(const fs::path& requested) {
        fs::path path = requested;
        if (!fs::exists(path) && fs::exists(kRepoRoot / path)) {
            path = kRepoRoot / path;
        }
        try {
            const core::Replay replay = core::load_replay(path);
            set_timeline(Timeline::from_replay(replay, kRepoRoot, "Replay: " + path.filename().string()));
            status_ = replay.description;
            return true;
        } catch (const std::exception& e) {
            status_ = std::string("Could not load replay: ") + e.what();
            return false;
        }
    }

    void go_to(std::size_t frame) {
        frame_ = std::min(frame, timeline_.size() - 1);
        scroll_log_ = true;
    }

    void set_zoom(float px_per_mm) { camera_.set_zoom(px_per_mm, board_centre()); }

    void frame(bool layout_fixed) {
        if (fit_pending_ || IsWindowResized()) {
            const core::GameState& s = state();
            camera_.fit(s.battlefield.width_mm, s.battlefield.depth_mm, board_area());
            fit_pending_ = false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) handle_mouse();
        if (!io.WantCaptureKeyboard) handle_keys();

        ClearBackground(Color{24, 26, 30, 255});
        draw_board(state(), camera_);
        draw_ui(layout_fixed);
    }

private:
    const core::GameState& state() const { return timeline_.frame(frame_).state; }

    Rectangle board_area() const {
        return Rectangle{0.0f, 0.0f, static_cast<float>(GetScreenWidth()) - kPanelWidth,
                         static_cast<float>(GetScreenHeight())};
    }

    Vector2 board_centre() const {
        const Rectangle a = board_area();
        return Vector2{a.x + a.width * 0.5f, a.y + a.height * 0.5f};
    }

    void set_timeline(Timeline t) {
        timeline_ = std::move(t);
        frame_ = 0;
        fit_pending_ = true;
        scroll_log_ = true;
    }

    void handle_mouse() {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            camera_.zoom_by(std::pow(1.15f, wheel), GetMousePosition());  // zoom to cursor
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            camera_.pan(GetMouseDelta());
        }
        if (const auto hovered = model_at(state(), camera_, GetMousePosition())) {
            const core::Model& m = *hovered->model;
            ImGui::BeginTooltip();
            ImGui::Text("%s (unit %u, %s)", hovered->unit->name.c_str(), hovered->unit->id.v,
                        side_name(hovered->unit->side).c_str());
            ImGui::Text("Model %u: %s", m.id.v, m.type_name.c_str());
            ImGui::Text("Wounds %d/%d, base %.1f mm", m.wounds_remaining, m.characteristics.wounds,
                        m.base.diameter_mm);
            ImGui::Text("Position (%.2f\", %.2f\")", core::mm_to_inches(m.position.x),
                        core::mm_to_inches(m.position.y));
            ImGui::EndTooltip();
        }
    }

    void handle_keys() {
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) go_to(frame_ + 1);
        if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && frame_ > 0) go_to(frame_ - 1);
        if (IsKeyPressed(KEY_HOME)) go_to(0);
        if (IsKeyPressed(KEY_END)) go_to(timeline_.size() - 1);
    }

    void draw_ui(bool layout_fixed) {
        const ImGuiCond cond = layout_fixed ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        const float x = static_cast<float>(GetScreenWidth()) - kPanelWidth;
        const float h = static_cast<float>(GetScreenHeight());
        ImGui::SetNextWindowPos(ImVec2(x, 0.0f), cond);
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth, h * 0.6f), cond);
        ImGui::Begin("VibeHammer");
        draw_source_section();
        ImGui::Separator();
        draw_frame_controls();
        ImGui::Separator();
        draw_state_section();
        ImGui::Separator();
        draw_view_section();
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(x, h * 0.6f), cond);
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth, h * 0.4f), cond);
        ImGui::Begin("Event log");
        draw_event_log();
        ImGui::End();
    }

    void draw_source_section() {
        ImGui::TextWrapped("%s", timeline_.source().c_str());
        if (!timeline_.error().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", timeline_.error().c_str());
        }
        if (!status_.empty()) {
            ImGui::TextWrapped("%s", status_.c_str());
        }
        ImGui::InputText("##path", path_, sizeof(path_));
        ImGui::SameLine();
        if (ImGui::Button("Load replay")) {
            load_replay(path_);
        }
        if (fs::is_directory(kReplayDir) && ImGui::TreeNode("tests/replays")) {
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
            ImGui::TreePop();
        }
        if (ImGui::Button("New game (V1 scenario)")) {
            new_game();
            status_.clear();
        }
    }

    void draw_frame_controls() {
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
        if (ImGui::SliderInt("##frame", &slider, 0, static_cast<int>(last))) {
            go_to(static_cast<std::size_t>(slider));
        }
        ImGui::TextUnformatted(timeline_.frame(frame_).label.c_str());
    }

    void draw_state_section() {
        const core::GameState& s = state();
        const core::Stage stage = s.flow.stage;
        ImGui::Text("Battle round: %d / %d", s.flow.battle_round, s.battle_rounds);
        if (stage != core::Stage::NotStarted && stage != core::Stage::GameOver) {
            ImGui::Text("Turn: %s", side_name(core::turn_side(s)).c_str());
        }
        ImGui::Text("Phase: %s", std::string(core::phase_name(core::phase_of(stage))).c_str());
        ImGui::Text("Step: %s (%s)", std::string(core::stage_name(stage)).c_str(),
                    std::string(core::stage_rule(stage)).c_str());
        ImGui::Text("Active side: %s", side_name(core::active_side(s)).c_str());
        if (s.pending) {
            ImGui::TextWrapped("Waiting for: %s", core::describe(*s.pending).c_str());
        } else {
            ImGui::TextUnformatted("Waiting for: nothing");
        }
        if (s.result) {
            ImGui::TextWrapped("Result: %s", s.result->reason.c_str());
        }
        for (const core::Unit& u : s.units) {
            ImGui::Text("%s: %s (unit %u), %zu/%d models", side_name(u.side).c_str(), u.name.c_str(), u.id.v,
                        u.models.size(), u.starting_strength);
        }
    }

    void draw_view_section() {
        ImGui::Text("Zoom: %.2f px/mm", camera_.px_per_mm());
        ImGui::Text("32 mm base = %.1f px, 28.5 mm base = %.1f px", camera_.to_px(32.0f), camera_.to_px(28.5f));
        if (ImGui::Button("Fit board")) fit_pending_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("wheel: zoom, right-drag: pan, arrows: step");
    }

    void draw_event_log() {
        ImGui::BeginChild("log");
        for (std::size_t f = 0; f <= frame_; ++f) {
            const Frame& fr = timeline_.frame(f);
            if (f > 0) ImGui::TextDisabled("-- %s", fr.label.c_str());
            for (const auto& e : fr.events) {
                ImGui::TextUnformatted(format_event(e).c_str());
            }
        }
        if (scroll_log_) {
            ImGui::SetScrollHereY(1.0f);
            scroll_log_ = false;
        }
        ImGui::EndChild();
    }

    Timeline timeline_;
    std::size_t frame_ = 0;
    BoardCamera camera_;
    bool fit_pending_ = true;
    bool scroll_log_ = true;
    char path_[512] = {};
    std::string status_;
};

}  // namespace
}  // namespace wh40k::client

int main(int argc, char** argv) {
    using namespace wh40k::client;
    const Options options = parse_args(argc, argv);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    const std::string title = "VibeHammer " + std::string(wh40k::core::engine_version());
    InitWindow(kWindowWidth, kWindowHeight, title.c_str());
    SetTargetFPS(60);
    rlImGuiSetup(true);

    Viewer viewer;
    if (options.replay) viewer.load_replay(*options.replay);
    if (options.frame) viewer.go_to(*options.frame);

    // --screenshot: render a few frames (ImGui needs them for layout), save the last one, exit.
    int frames_left = options.screenshot ? 4 : -1;
    bool zoom_applied = false;
    while (!WindowShouldClose() && frames_left != 0) {
        BeginDrawing();
        rlImGuiBegin();
        viewer.frame(options.screenshot.has_value());
        if (options.zoom && !zoom_applied) {
            viewer.set_zoom(*options.zoom);  // after the first fit
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
