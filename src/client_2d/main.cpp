#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include <string>

#include "core/version.hpp"

int main() {
    constexpr int kWindowWidth = 1280;
    constexpr int kWindowHeight = 800;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    const std::string version(wh40k::core::engine_version());
    const std::string title = "VibeHammer " + version;
    InitWindow(kWindowWidth, kWindowHeight, title.c_str());
    SetTargetFPS(60);
    rlImGuiSetup(true);

    bool show_demo = true;
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(Color{30, 32, 36, 255});

        rlImGuiBegin();
        ImGui::Begin("VibeHammer");
        ImGui::Text("Core engine version: %s", version.c_str());
        ImGui::Checkbox("Show ImGui demo", &show_demo);
        ImGui::End();
        if (show_demo) {
            ImGui::ShowDemoWindow(&show_demo);
        }
        rlImGuiEnd();

        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
