add_subdirectory(logic)

add_executable(wh40k_client_2d
    main.cpp
    board_view.cpp
    camera.cpp
    decision_panel.cpp
    dice_popup.cpp
    help_widgets.cpp
    overlays.cpp)

target_link_libraries(wh40k_client_2d PRIVATE wh40k_client_logic imgui_rlimgui raylib)
# The viewer loads data/scenarios and tests/replays from the source tree.
target_compile_definitions(wh40k_client_2d PRIVATE WH40K_REPO_ROOT="${PROJECT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(wh40k_client_2d PRIVATE /W4 /permissive- /utf-8)
endif()

set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT wh40k_client_2d)
