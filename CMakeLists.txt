cmake_minimum_required(VERSION 3.20)
project(cactus-compiler
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Cactus DSL Compiler — kid-friendly game language"
)

# ── C++20 Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── FetchContent Dependencies ───────────────────────────────────────────────────
include(FetchContent)

# Catch2 — Testing framework
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)

# EnTT — ECS library (header-only, for EnTT backend)
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.1
    GIT_SHALLOW    TRUE
)

# Raylib — Presentation API (for generated code validation)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_SHALLOW    TRUE
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version" FORCE)
FetchContent_MakeAvailable(Catch2 EnTT raylib)

# ── Compiler Warnings (after FetchContent so they don't apply to deps) ──────────
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ── Optional clang-tidy during build ───────────────────────────────────────────
option(CACTUS_ENABLE_BUILD_CLANG_TIDY "Run clang-tidy during normal project builds" ON)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE AND CACTUS_ENABLE_BUILD_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--fix;--fix-errors;--fix-notes")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE} — enabled for normal build targets")
elseif(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE} — normal build integration disabled")
else()
    message(STATUS "clang-tidy not found — normal build integration disabled")
endif()

# ── Common Library ──────────────────────────────────────────────────────────────
add_library(cactus_common STATIC
    src/common/error_reporter.cpp
    src/common/string_pool.cpp
)
target_include_directories(cactus_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ── Reusable C++ Runtime Libraries ─────────────────────────────────────────────
add_library(cactus_runtime_common STATIC
    src/common/cactus_runtime.cpp
)
target_include_directories(cactus_runtime_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cactus_runtime_common PUBLIC cactus_common)

add_library(cactus_runtime_cpp_manual STATIC
    src/backends/cpp-manual/runtime.cpp
)
target_include_directories(cactus_runtime_cpp_manual PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cpp-manual
)
target_link_libraries(cactus_runtime_cpp_manual PUBLIC cactus_runtime_common raylib)

add_library(cactus_runtime_cpp_entt STATIC
    src/backends/cpp-entt/runtime.cpp
)
target_include_directories(cactus_runtime_cpp_entt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cpp-entt
)
target_link_libraries(cactus_runtime_cpp_entt PUBLIC cactus_runtime_common EnTT::EnTT raylib)

# ── Frontend Library ────────────────────────────────────────────────────────────
add_library(cactus_frontend STATIC
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/semantic_analyzer.cpp
    src/frontend/module_resolver.cpp
    src/frontend/module_artifact.cpp
    src/frontend/program_linker.cpp
    src/frontend/data_file.cpp
)
target_include_directories(cactus_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cactus_frontend PUBLIC cactus_common)

# ── Backend: C++ Manual SoA ─────────────────────────────────────────────────────
add_library(cactus_backend_cpp_manual STATIC
    src/backends/cpp-manual/soa_emitter.cpp
    src/backends/cpp-manual/system_emitter.cpp
    src/backends/cpp-manual/event_emitter.cpp
    src/backends/cpp-manual/cpp_manual_codegen.cpp
)
target_include_directories(cactus_backend_cpp_manual PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cpp-manual
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(cactus_backend_cpp_manual PUBLIC cactus_frontend cactus_runtime_cpp_manual)

# ── Backend: C++ EnTT ───────────────────────────────────────────────────────────
add_library(cactus_backend_cpp_entt STATIC
    src/backends/cpp-entt/component_emitter.cpp
    src/backends/cpp-entt/system_emitter.cpp
    src/backends/cpp-entt/event_emitter.cpp
    src/backends/cpp-entt/cpp_entt_codegen.cpp
)
target_include_directories(cactus_backend_cpp_entt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cpp-entt
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(cactus_backend_cpp_entt 
    PUBLIC 
        cactus_frontend 
        cactus_backend_cpp_manual 
        cactus_runtime_cpp_entt
        EnTT::EnTT)

# ── CLI Executable ──────────────────────────────────────────────────────────────
add_executable(cactus src/main.cpp)
target_link_libraries(cactus PRIVATE
    cactus_frontend
    cactus_backend_cpp_manual
    cactus_backend_cpp_entt
)
# Stdlib directory is baked in at compile time so the compiler can always find std.core
target_compile_definitions(cactus PRIVATE
    CACTUS_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
)

# ── Tests ───────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
