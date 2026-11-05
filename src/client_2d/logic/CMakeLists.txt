# wh40k_client_logic: the client's non-graphics code (D-024). It links wh40k_core only: no raylib, ImGui
# or other rendering/device headers (checked on every build), so wh40k_client_tests can test it.
add_library(wh40k_client_logic STATIC
    action_text.cpp
    casualty_plan.cpp
    decision_view.cpp
    event_text.cpp
    melee_plan.cpp
    move_plan.cpp
    overlay_plan.cpp
    phase_track.cpp
    session.cpp
    shot_plan.cpp
    timeline.cpp)

target_include_directories(wh40k_client_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wh40k_client_logic PUBLIC wh40k_core)

if(MSVC)
    target_compile_options(wh40k_client_logic PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(wh40k_client_logic PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_custom_target(wh40k_client_logic_include_check
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DLABEL=src/client_2d/logic
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckCoreIncludes.cmake
    COMMENT "Checking src/client_2d/logic for forbidden includes"
    VERBATIM)
add_dependencies(wh40k_client_logic wh40k_client_logic_include_check)
