add_executable(wh40k_client_2d
    main.cpp)

target_link_libraries(wh40k_client_2d PRIVATE wh40k_core imgui_rlimgui raylib)

if(MSVC)
    target_compile_options(wh40k_client_2d PRIVATE /W4 /permissive-)
endif()

set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT wh40k_client_2d)
