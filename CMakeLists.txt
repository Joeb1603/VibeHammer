cmake_minimum_required(VERSION 3.28)

project(VibeHammer
    VERSION 0.1.0
    DESCRIPTION "Warhammer 40,000 11th Edition rules engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/client_2d)
add_subdirectory(tests)
add_subdirectory(tests/client)
