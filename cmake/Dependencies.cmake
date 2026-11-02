# Third-party dependencies, fetched with FetchContent and pinned to exact release tags.
# Don't add dependencies here without asking (CLAUDE.md).
include(FetchContent)

set(FETCHCONTENT_QUIET ON)

# ---- nlohmann_json (core) ----
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
    SYSTEM)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")

# ---- GoogleTest (tests) ----
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
    SYSTEM)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK            OFF CACHE BOOL "" FORCE)

# ---- raylib (client) ----
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
    SYSTEM)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

# ---- Dear ImGui (client). v1.92.7 is the version rlImGui tag Raylib_6_0 is built against. ----
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7
    GIT_SHALLOW    TRUE)

# ---- rlImGui (client): raylib backend for Dear ImGui ----
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        Raylib_6_0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(nlohmann_json googletest raylib imgui rlimgui)

# imgui and rlImGui ship no CMake build; wrap them in one static library used by the client only.
add_library(imgui_rlimgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(imgui_rlimgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR})
target_link_libraries(imgui_rlimgui PUBLIC raylib)
