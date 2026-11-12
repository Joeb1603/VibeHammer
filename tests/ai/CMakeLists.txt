# wh40k_ai_tests: tests for the bot (M39+, D-054).
add_executable(wh40k_ai_tests
    m39_test.cpp
    m40_test.cpp
    m41_test.cpp
    m42_test.cpp
    m43_test.cpp
    m44_test.cpp
    m45_test.cpp
    m46_test.cpp
    m47_test.cpp
    m48_test.cpp
    m54_test.cpp
    m56_test.cpp
    m60_test.cpp
    m61_test.cpp
    m62_test.cpp
    m63_test.cpp
    m65_test.cpp
    m66_test.cpp
    m67_test.cpp
    m68_test.cpp
    m72_test.cpp
    m73_test.cpp
    m74_test.cpp
    m75_test.cpp
    m77_test.cpp
    m78_test.cpp
    m79_test.cpp
    m80_test.cpp
    m81_test.cpp
    m83_test.cpp
    m86_test.cpp
    m87_test.cpp)

target_link_libraries(wh40k_ai_tests PRIVATE wh40k_ai GTest::gtest_main)
target_include_directories(wh40k_ai_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(wh40k_ai_tests PRIVATE
    WH40K_REPO_ROOT="${PROJECT_SOURCE_DIR}"
    WH40K_REPLAY_DIR="${PROJECT_SOURCE_DIR}/tests/replays")

if(MSVC)
    target_compile_options(wh40k_ai_tests PRIVATE /W4 /permissive- /utf-8 /bigobj)
endif()

include(GoogleTest)
gtest_discover_tests(wh40k_ai_tests DISCOVERY_MODE PRE_TEST)
