# Run with: cmake -DCORE_DIR=<path> -P CheckCoreIncludes.cmake
# Fails if any file under src/core includes a rendering, UI or OS/device header.
# wh40k_core may depend only on the standard library and nlohmann_json (CLAUDE.md, Architecture).
# Also used for src/client_2d/logic (D-024), with LABEL naming the directory in messages.
if(NOT CORE_DIR)
    message(FATAL_ERROR "CheckCoreIncludes: CORE_DIR not set")
endif()
if(NOT LABEL)
    set(LABEL "src/core")
endif()

file(GLOB_RECURSE core_files
    "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp" "${CORE_DIR}/*.c" "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.inl")

# Matched against the lower-cased line, so the check is case-insensitive.
set(include_prefix "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]*/)?")
set(forbidden_names "(raylib|raymath|rlgl|rlimgui|imgui[a-z_]*|imconfig|windows|winuser|wingdi|glfw[0-9]*)")
set(forbidden_regex "${include_prefix}${forbidden_names}(\\.h|\\.hpp)?[>\"]")

set(violations "")
foreach(f IN LISTS core_files)
    file(STRINGS "${f}" lines REGEX "#[ \t]*include")
    foreach(line IN LISTS lines)
        string(TOLOWER "${line}" lower)
        if(lower MATCHES "${forbidden_regex}")
            file(RELATIVE_PATH rel "${CORE_DIR}" "${f}")
            string(APPEND violations "\n  ${LABEL}/${rel}: ${line}")
        endif()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR
        "${LABEL} must not include raylib, ImGui, Windows or other rendering/device headers:${violations}")
endif()
