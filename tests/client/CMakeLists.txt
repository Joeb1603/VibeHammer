# wh40k_client_tests: tests for wh40k_client_logic (D-024). wh40k_tests stays core-only (CLAUDE.md).
add_executable(wh40k_client_tests
    auto_actions_test.cpp
    qol_test.cpp
    undo_groups_test.cpp
    reach_area_test.cpp
    m28_test.cpp
    m29_test.cpp
    m30_test.cpp
    m31_test.cpp
    m34_test.cpp
    m35_test.cpp
    m36_test.cpp
    m37_test.cpp
    m38_test.cpp
    m39_test.cpp
    m42_test.cpp
    m45_test.cpp
    m46_test.cpp
    m47_test.cpp
    m48_test.cpp
    m49_test.cpp
    m50_test.cpp
    m51_test.cpp
    m53_test.cpp
    rules_help_test.cpp
    client_logic_test.cpp)

target_link_libraries(wh40k_client_tests PRIVATE wh40k_client_logic GTest::gtest_main)
# The shared test helpers (tests/test_helpers.hpp, tests/support/) use only core and gtest.
target_include_directories(wh40k_client_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(wh40k_client_tests PRIVATE
    WH40K_REPO_ROOT="${PROJECT_SOURCE_DIR}"
    WH40K_REPLAY_DIR="${PROJECT_SOURCE_DIR}/tests/replays")

if(MSVC)
    target_compile_options(wh40k_client_tests PRIVATE /W4 /permissive- /utf-8 /bigobj)
endif()

include(GoogleTest)  # needed in this directory too: it sets the discovery script path
gtest_discover_tests(wh40k_client_tests DISCOVERY_MODE PRE_TEST)
