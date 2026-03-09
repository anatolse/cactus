cmake_minimum_required(VERSION 3.20)
project(cactus-platformer
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Cactus Platformer — 2.5D Rayman-inspired example"
)

# ── C++20 Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Cactus Compiler Path ────────────────────────────────────────────────────────
if(NOT DEFINED CACTUS_COMPILER)
    message(FATAL_ERROR
        "CACTUS_COMPILER is not set. Please provide the path to the cactus compiler:\n"
        "  cmake -DCACTUS_COMPILER=/path/to/cactus ..\n"
        "Build the compiler first from the root project."
    )
endif()

if(NOT EXISTS "${CACTUS_COMPILER}")
    message(FATAL_ERROR "CACTUS_COMPILER not found at: ${CACTUS_COMPILER}")
endif()

message(STATUS "Using Cactus compiler: ${CACTUS_COMPILER}")

# ── FetchContent Dependencies ───────────────────────────────────────────────────
include(FetchContent)

# Raylib — Rendering / window / input
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_SHALLOW    TRUE
)

# EnTT — ECS library (header-only)
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.1
    GIT_SHALLOW    TRUE
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version" FORCE)
FetchContent_MakeAvailable(raylib EnTT)

# ── Generate C++ from single platformer.cactus ──────────────────────────────────
set(CACTUS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../platformer.cactus")
set(CACTUS_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/platformer_generated.cpp")

add_custom_command(
    OUTPUT "${CACTUS_GENERATED}"
    COMMAND "${CACTUS_COMPILER}" "${CACTUS_SOURCE}" --backend cpp-entt -o "${CACTUS_GENERATED}"
    DEPENDS "${CACTUS_SOURCE}"
    COMMENT "Generating platformer_generated.cpp from platformer.cactus"
    VERBATIM
)

# ── Game Executable ─────────────────────────────────────────────────────────────
# The generated file contains main() — it IS the program
add_executable(platformer "${CACTUS_GENERATED}")
target_link_libraries(platformer PRIVATE raylib EnTT::EnTT)

# ── Copy Assets ─────────────────────────────────────────────────────────────────
set(ASSETS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
set(ASSETS_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets")

add_custom_command(
    TARGET platformer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_SOURCE_DIR}" "${ASSETS_DEST_DIR}"
    COMMENT "Copying assets to build directory"
)
