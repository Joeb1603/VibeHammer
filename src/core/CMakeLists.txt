add_library(wh40k_core STATIC
    version.cpp
    data/scenario_loader.cpp
    data/unit_loader.cpp
    dice/dice.cpp
    engine/actions.cpp
    engine/advance.cpp
    engine/attack_flow.cpp
    engine/charge.cpp
    engine/engine.cpp
    engine/events.cpp
    engine/movement.cpp
    engine/replay.cpp
    engine/shooting.cpp
    engine/turn_phases.cpp
    geometry/closest.cpp
    geometry/coherency.cpp
    geometry/distance.cpp
    geometry/engagement.cpp
    geometry/path.cpp
    geometry/visibility.cpp
    model/dice_expr.cpp
    model/keywords.cpp
    model/weapon.cpp
    rules/attack_sequence.cpp
    rules/attacks.cpp
    rules/charge.cpp
    rules/leadership.cpp
    rules/mortal_wounds.cpp
    rules/move_types.cpp
    rules/shooting.cpp
    serialization/engine_json.cpp
    serialization/flow_json.cpp
    serialization/json.cpp
    state/decision.cpp
    state/flow.cpp
    state/game_state.cpp
    state/queries.cpp)

# Consumers include core headers as "core/....hpp".
target_include_directories(wh40k_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(wh40k_core PUBLIC cxx_std_20)
target_link_libraries(wh40k_core PUBLIC nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(wh40k_core PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(wh40k_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Architecture guard: fail the build if any core file includes raylib/ImGui/Windows headers.
# Runs on every build, before wh40k_core compiles.
add_custom_target(wh40k_core_include_check
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckCoreIncludes.cmake
    COMMENT "Checking src/core for forbidden includes"
    VERBATIM)
add_dependencies(wh40k_core wh40k_core_include_check)
