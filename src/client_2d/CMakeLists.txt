add_executable(wh40k_client_2d
    main.cpp
    board_view.cpp
    camera.cpp
    event_text.cpp
    timeline.cpp)

target_link_libraries(wh40k_client_2d PRIVATE wh40k_core imgui_rlimgui raylib)
# The viewer loads data/scenarios and tests/replays from the source tree.
target_compile_definitions(wh40k_client_2d PRIVATE WH40K_REPO_ROOT="${PROJECT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(wh40k_client_2d PRIVATE /W4 /permissive- /utf-8)
endif()

set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT wh40k_client_2d)
