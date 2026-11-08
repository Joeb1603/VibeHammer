add_executable(wh40k_tests
    version_test.cpp
    dice_expr_test.cpp
    geometry_basics_test.cpp
    coherency_test.cpp
    engagement_test.cpp
    path_test.cpp
    closest_test.cpp
    dice_test.cpp
    leadership_test.cpp
    attacks_test.cpp
    attack_sequence_test.cpp
    mortal_wounds_test.cpp
    weapon_ability_test.cpp
    datasheet_test.cpp
    roster_datasheet_test.cpp
    scenario_test.cpp
    serialization_test.cpp
    engine_flow_test.cpp
    engine_validation_test.cpp
    attack_flow_test.cpp
    engine_property_test.cpp
    replay_test.cpp
    engine_serialization_test.cpp
    movement_test.cpp
    visibility_test.cpp
    shooting_test.cpp
    charge_test.cpp
    fight_test.cpp
    command_test.cpp
    m54_test.cpp
    m60_abilities_test.cpp
    m61_twin_linked_test.cpp
    m62_random_characteristics_test.cpp)

target_link_libraries(wh40k_tests PRIVATE wh40k_core GTest::gtest_main)
target_include_directories(wh40k_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Lets tests find data/ (scenario unit_file paths are relative to the repo root) and the saved replays.
target_compile_definitions(wh40k_tests PRIVATE
    WH40K_REPO_ROOT="${PROJECT_SOURCE_DIR}"
    WH40K_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replays")

if(MSVC)
    target_compile_options(wh40k_tests PRIVATE /W4 /permissive- /utf-8 /bigobj)
endif()

include(GoogleTest)
gtest_discover_tests(wh40k_tests DISCOVERY_MODE PRE_TEST)
