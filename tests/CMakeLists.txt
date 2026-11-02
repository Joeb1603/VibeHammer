add_executable(wh40k_tests
    version_test.cpp)

target_link_libraries(wh40k_tests PRIVATE wh40k_core GTest::gtest_main)

if(MSVC)
    target_compile_options(wh40k_tests PRIVATE /W4 /permissive-)
endif()

include(GoogleTest)
gtest_discover_tests(wh40k_tests DISCOVERY_MODE PRE_TEST)
