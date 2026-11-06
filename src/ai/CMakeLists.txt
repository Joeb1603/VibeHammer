# wh40k_ai (M39, D-054): the bot opponent. Headless: it links wh40k_client_logic (ai -> logic -> core) and,
# like core and logic, must not include raylib, ImGui or OS headers (checked on every build).
add_library(wh40k_ai STATIC
    arena.cpp
    baseline_bot.cpp
    bot.cpp
    bot_driver.cpp
    fallback.cpp
    simulator.cpp
    state_hash.cpp)

target_include_directories(wh40k_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wh40k_ai PUBLIC wh40k_client_logic)

find_package(Threads REQUIRED)
target_link_libraries(wh40k_ai PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(wh40k_ai PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(wh40k_ai PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_custom_target(wh40k_ai_include_check
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DLABEL=src/ai
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckCoreIncludes.cmake
    COMMENT "Checking src/ai for forbidden includes"
    VERBATIM)
add_dependencies(wh40k_ai wh40k_ai_include_check)

# wh40k_bot_arena: bot vs bot games, headless (run it in Release for the strength numbers, D-054).
add_executable(wh40k_bot_arena arena_main.cpp)
target_link_libraries(wh40k_bot_arena PRIVATE wh40k_ai)
target_compile_definitions(wh40k_bot_arena PRIVATE WH40K_REPO_ROOT="${PROJECT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(wh40k_bot_arena PRIVATE /W4 /WX /permissive- /utf-8)
endif()
