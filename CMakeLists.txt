cmake_minimum_required(VERSION 3.20)
project(cactus-compiler
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Cactus DSL Compiler — kid-friendly game language"
)

# ── C++23 Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
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
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE
)

# Raylib — Presentation API (for generated code validation)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version" FORCE)
FetchContent_MakeAvailable(Catch2 EnTT raylib)

# GPU skinning for skeletal animation (dsl-model-animation): loaders skip the
# CPU-side animVertices buffers, so UpdateModelAnimation reduces to bone-matrix
# math and DrawMesh binds bone VBOs. Static model/mesh paths are unaffected.
target_compile_definitions(raylib PRIVATE SUPPORT_GPU_SKINNING=1)

# ── Compiler Warnings (after FetchContent so they don't apply to deps) ──────────
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-missing-field-initializers
        -Wno-c2y-extensions
        #-Wno-deprecated-literal-operator
        #-Wno-incompatible-pointer-types
        #-Wno-tautological-compare
    )
endif()

# ── Optional clang-tidy during build ───────────────────────────────────────────
option(CACTUS_ENABLE_BUILD_CLANG_TIDY "Run clang-tidy during normal project builds" OFF)
find_program(CLANG_FORMAT_EXE
    NAMES clang-format 
)

find_program(CLANG_TIDY_EXE
    NAMES clang-tidy
)

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
target_link_libraries(cactus_runtime_common PUBLIC cactus_common raylib)

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
    src/frontend/execution_graph_scheduler.cpp
)
target_include_directories(cactus_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cactus_frontend PUBLIC cactus_common)

# ── Backend: C++ EnTT ───────────────────────────────────────────────────────────
add_library(cactus_backend_cpp_entt STATIC
    src/backends/cpp-entt/component_emitter.cpp
    src/backends/cpp-entt/type_utils.cpp
    src/backends/cpp-entt/system_emitter.cpp
    src/backends/cpp-entt/event_emitter.cpp
    src/backends/cpp-entt/render_pass_emitter.cpp
    src/backends/cpp-entt/cpp_entt_codegen.cpp
)
target_include_directories(cactus_backend_cpp_entt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cpp-entt
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(cactus_backend_cpp_entt 
    PUBLIC 
        cactus_frontend 
        cactus_runtime_cpp_entt
        EnTT::EnTT)

# ── CLI Executable ──────────────────────────────────────────────────────────────
add_executable(cactus src/main.cpp)
target_link_libraries(cactus PRIVATE
    cactus_frontend
    cactus_backend_cpp_entt
)
# Stdlib directory is baked in at compile time so the compiler can always find std.core
target_compile_definitions(cactus PRIVATE
    CACTUS_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
    CACTUS_CLANG_FORMAT_EXE_PATH="${CACTUS_CLANG_FORMAT_EXE}"
)

# ── Generated Example Executables ─────────────────────────────────────────────
set(CACTUS_GENERATED_EXAMPLES_DIR "${CMAKE_BINARY_DIR}/generated_examples")
file(MAKE_DIRECTORY "${CACTUS_GENERATED_EXAMPLES_DIR}")

function(cactus_add_example TARGET_NAME)
    set(options EXCLUDE_FROM_ALL)
    set(oneValueArgs SOURCE BACKEND OUTPUT)
    set(multiValueArgs EXTRA_SOURCES COMPILE_DEFINITIONS)
    cmake_parse_arguments(CACTUS_EXAMPLE
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    if(NOT CACTUS_EXAMPLE_SOURCE)
        message(FATAL_ERROR "cactus_add_example(${TARGET_NAME}) requires SOURCE")
    endif()
    if(NOT CACTUS_EXAMPLE_BACKEND)
        message(FATAL_ERROR "cactus_add_example(${TARGET_NAME}) requires BACKEND")
    endif()
    if(NOT CACTUS_EXAMPLE_OUTPUT)
        message(FATAL_ERROR "cactus_add_example(${TARGET_NAME}) requires OUTPUT")
    endif()

    set(example_source "${CMAKE_CURRENT_SOURCE_DIR}/${CACTUS_EXAMPLE_SOURCE}")
    get_filename_component(example_source_dir "${example_source}" DIRECTORY)
    file(GLOB cactus_example_cactus_sources CONFIGURE_DEPENDS
        "${example_source_dir}/*.cactus"
    )
    list(SORT cactus_example_cactus_sources)
    set(generated_output "${CACTUS_EXAMPLE_OUTPUT}")

    add_custom_command(
        OUTPUT "${generated_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${CACTUS_GENERATED_EXAMPLES_DIR}>"
        COMMAND $<TARGET_FILE:cactus>
                "$<SHELL_PATH:${example_source}>"
                --backend "${CACTUS_EXAMPLE_BACKEND}"
                --output "$<SHELL_PATH:${generated_output}>"
        DEPENDS cactus ${cactus_example_cactus_sources}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating ${TARGET_NAME} from ${CACTUS_EXAMPLE_SOURCE} (${CACTUS_EXAMPLE_BACKEND})"
        VERBATIM
    )

    set_source_files_properties(${cactus_example_cactus_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

    if(CACTUS_EXAMPLE_EXCLUDE_FROM_ALL)
        set(cactus_example_exclude_from_all EXCLUDE_FROM_ALL)
    else()
        set(cactus_example_exclude_from_all)
    endif()

    add_executable(${TARGET_NAME} ${cactus_example_exclude_from_all}
        "${generated_output}"
        ${cactus_example_cactus_sources}
        ${CACTUS_EXAMPLE_EXTRA_SOURCES}
    )
    set_source_files_properties("${generated_output}" PROPERTIES GENERATED TRUE)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_property(TARGET ${TARGET_NAME} PROPERTY CXX_CLANG_TIDY "")

    if(CACTUS_EXAMPLE_COMPILE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${CACTUS_EXAMPLE_COMPILE_DEFINITIONS})
    endif()

    if(CACTUS_EXAMPLE_BACKEND STREQUAL "cpp-entt")
        target_link_libraries(${TARGET_NAME} PRIVATE cactus_runtime_cpp_entt raylib EnTT::EnTT)
    else()
        message(FATAL_ERROR "Unsupported Cactus example backend '${CACTUS_EXAMPLE_BACKEND}' for target ${TARGET_NAME}")
    endif()

    set(CACTUS_EXAMPLE_${TARGET_NAME}_SOURCE "${CACTUS_EXAMPLE_SOURCE}" PARENT_SCOPE)
    set(CACTUS_EXAMPLE_${TARGET_NAME}_BACKEND "${CACTUS_EXAMPLE_BACKEND}" PARENT_SCOPE)
    set(CACTUS_EXAMPLE_${TARGET_NAME}_GENERATED_CPP "${generated_output}" PARENT_SCOPE)
    set(CACTUS_EXAMPLE_${TARGET_NAME}_TARGET "${TARGET_NAME}" PARENT_SCOPE)
endfunction()

# ── Headless Behavioral Test Executables ──────────────────────────────────────
# Parallel to cactus_add_example, but compiles a curated example's generated
# .cpp with CACTUS_GENERATED_NO_MAIN defined, drives it directly from a
# hand-written Catch2 test file (TEST_SOURCE), and links the CACTUS_RAYLIB_FAKE
# variant of the C++ EnTT runtime + cactus_raylib_fake instead of a real
# window. Test-only: excluded from ALL, registered with ctest.
#
# TEST_SOURCE must #include the generated file via the
# CACTUS_HEADLESS_GENERATED_CPP macro this function defines, e.g.:
#   #define CACTUS_GENERATED_NO_MAIN
#   #include CACTUS_HEADLESS_GENERATED_CPP
# (mirroring tests/example_manual_generated_main.cpp's existing pattern) so
# the generated translation unit is compiled once, together with the test's
# TEST_CASE bodies, rather than as a second, separately-linked object file.
function(cactus_add_headless_behavior_test TARGET_NAME)
    set(oneValueArgs CACTUS_SOURCE BACKEND TEST_SOURCE)
    cmake_parse_arguments(CACTUS_HEADLESS_TEST
        ""
        "${oneValueArgs}"
        ""
        ${ARGN}
    )

    if(NOT CACTUS_HEADLESS_TEST_CACTUS_SOURCE)
        message(FATAL_ERROR "cactus_add_headless_behavior_test(${TARGET_NAME}) requires CACTUS_SOURCE")
    endif()
    if(NOT CACTUS_HEADLESS_TEST_BACKEND)
        message(FATAL_ERROR "cactus_add_headless_behavior_test(${TARGET_NAME}) requires BACKEND")
    endif()
    if(NOT CACTUS_HEADLESS_TEST_TEST_SOURCE)
        message(FATAL_ERROR "cactus_add_headless_behavior_test(${TARGET_NAME}) requires TEST_SOURCE")
    endif()

    set(cactus_source "${CMAKE_SOURCE_DIR}/${CACTUS_HEADLESS_TEST_CACTUS_SOURCE}")
    set(generated_output "${CACTUS_GENERATED_EXAMPLES_DIR}/${TARGET_NAME}.headless.generated.cpp")

    add_custom_command(
        OUTPUT "${generated_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${CACTUS_GENERATED_EXAMPLES_DIR}>"
        COMMAND $<TARGET_FILE:cactus>
                "$<SHELL_PATH:${cactus_source}>"
                --backend "${CACTUS_HEADLESS_TEST_BACKEND}"
                --output "$<SHELL_PATH:${generated_output}>"
        DEPENDS cactus "${cactus_source}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating headless behavioral test fixture for ${TARGET_NAME} (${CACTUS_HEADLESS_TEST_BACKEND})"
        VERBATIM
    )
    set_source_files_properties("${generated_output}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

    add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL
        "${CACTUS_HEADLESS_TEST_TEST_SOURCE}"
        "${generated_output}"
    )
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CACTUS_HEADLESS_GENERATED_CPP="${generated_output}"
    )
    set_property(TARGET ${TARGET_NAME} PROPERTY CXX_CLANG_TIDY "")

    if(CACTUS_HEADLESS_TEST_BACKEND STREQUAL "cpp-entt")
        target_link_libraries(${TARGET_NAME} PRIVATE
            cactus_runtime_cpp_entt_headless_fake
            cactus_raylib_fake
            raylib
            EnTT::EnTT
            Catch2::Catch2WithMain
        )
    else()
        message(FATAL_ERROR
            "Unsupported Cactus headless behavior test backend '${CACTUS_HEADLESS_TEST_BACKEND}' for target ${TARGET_NAME}")
    endif()

    # Registered asset paths (e.g. model assets) are relative to the repo
    # root, not ctest's default per-test working directory (the binary dir
    # where add_test was called, i.e. build/tests) — run from the source
    # root so any example needing real file-backed assets resolves them.
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endfunction()

set(BLUE_SQUARE_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/blue-square.generated.cpp")
set(PAIR_CONTACT_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/pair-contact.generated.cpp")
set(BOUNCY_BUBBLES_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/bouncy-bubbles.generated.cpp")
set(BOUNCY_BALLS_3D_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/bouncy-balls-3d.generated.cpp")
set(STDLIB_EXTERN_ENTT_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/stdlib-extern-entt.generated.cpp")
set(MESH_RENDERER_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/mesh-renderer.generated.cpp")
set(MODEL_RENDERER_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/model-renderer.generated.cpp")
set(MODEL_ANIMATION_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/model-animation.generated.cpp")
set(PLATFORMER_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/platformer.generated.cpp")
set(SHOOTER_SLICE_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/shooter-slice.generated.cpp")
set(CACTUS_SHOP_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/cactus-shop.generated.cpp")
set(WAVING_LABEL_2D_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/waving-label-2d.generated.cpp")
set(WAVING_LABEL_3D_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/waving-label-3d.generated.cpp")
set(EDITOR_TEST_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/editor-test.generated.cpp")
set(EDITOR_3D_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/editor-3d.generated.cpp")
set(SPLIT_SCREEN_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/split-screen.generated.cpp")
set(SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/split-screen-forest-bombs.generated.cpp")
set(STANDARD_UI_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/standard-ui.generated.cpp")
set(EXTERNAL_HANDLER_LINK_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/external-handler-link.generated.cpp")
set(PARTICLE_BURST_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/particle-burst.generated.cpp")
set(GRADIENT_SQUARE_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/gradient-square.generated.cpp")

add_custom_command(
    OUTPUT "${EXTERNAL_HANDLER_LINK_GENERATED_CPP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${CACTUS_GENERATED_EXAMPLES_DIR}>"
    COMMAND $<TARGET_FILE:cactus>
            "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/external_handler_linking.cactus>"
            --backend cpp-entt
            --output "$<SHELL_PATH:${EXTERNAL_HANDLER_LINK_GENERATED_CPP}>"
    DEPENDS cactus "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/external_handler_linking.cactus"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating per-handler user-library ABI fixture"
    VERBATIM
)
set_source_files_properties("${EXTERNAL_HANDLER_LINK_GENERATED_CPP}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

foreach(target_name IN ITEMS
        example_external_handler_user_library
        example_external_handler_missing_implementations)
    add_executable(${target_name} EXCLUDE_FROM_ALL
        tests/example_manual_generated_main.cpp
        "${EXTERNAL_HANDLER_LINK_GENERATED_CPP}"
    )
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(${target_name} PRIVATE
        CACTUS_EXTERNAL_HANDLER_GENERATED_CPP="${EXTERNAL_HANDLER_LINK_GENERATED_CPP}"
    )
    target_link_libraries(${target_name} PRIVATE cactus_runtime_cpp_entt raylib EnTT::EnTT)
    set_property(TARGET ${target_name} PROPERTY CXX_CLANG_TIDY "")
endforeach()
target_compile_definitions(example_external_handler_missing_implementations PRIVATE
    CACTUS_EXTERNAL_HANDLER_MISSING_IMPLEMENTATIONS
)

cactus_add_example(example_blue_square_generated
    SOURCE "examples/blue-square/square.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${BLUE_SQUARE_GENERATED_CPP}"
)

cactus_add_example(example_pair_contact_generated
    SOURCE "examples/pair-contact/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${PAIR_CONTACT_GENERATED_CPP}"
)

cactus_add_example(example_bouncy_bubbles_generated
    SOURCE "examples/bouncy-bubbles/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${BOUNCY_BUBBLES_GENERATED_CPP}"
)

cactus_add_example(example_bouncy_balls_3d_generated
    SOURCE "examples/bouncy-balls-3d/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${BOUNCY_BALLS_3D_GENERATED_CPP}"
)

cactus_add_example(example_stdlib_extern_entt_generated
    SOURCE "examples/stdlib_extern_coverage.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${STDLIB_EXTERN_ENTT_GENERATED_CPP}"
)

cactus_add_example(example_mesh_renderer_generated
    SOURCE "examples/mesh-renderer/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${MESH_RENDERER_GENERATED_CPP}"
)

cactus_add_example(example_model_renderer_generated
    SOURCE "examples/model-renderer/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${MODEL_RENDERER_GENERATED_CPP}"
)

cactus_add_example(example_model_animation_generated
    SOURCE "examples/model-animation/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${MODEL_ANIMATION_GENERATED_CPP}"
)

cactus_add_example(example_platformer_generated
    SOURCE "examples/platformer/platformer.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${PLATFORMER_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_shooter_slice_generated
    SOURCE "examples/shooter-slice.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${SHOOTER_SLICE_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_cactus_shop_generated
    SOURCE "examples/cactus_shop/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${CACTUS_SHOP_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_waving_label_2d_generated
    SOURCE "examples/waving-label-2d/waving_label_2d.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${WAVING_LABEL_2D_GENERATED_CPP}"
)

cactus_add_example(example_waving_label_3d_generated
    SOURCE "examples/waving-label-3d/waving_label_3d.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${WAVING_LABEL_3D_GENERATED_CPP}"
)

cactus_add_example(example_editor_test_generated
    SOURCE "examples/editor-test/editor-test.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${EDITOR_TEST_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_editor_3d_generated
    SOURCE "examples/editor-3d/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${EDITOR_3D_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_split_screen_generated
    SOURCE "examples/split-screen/split_screen.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${SPLIT_SCREEN_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_split_screen_forest_bombs_generated
    SOURCE "examples/split-screen-forest-bombs/forest_bombs.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_standard_ui_generated
    SOURCE "examples/standard-ui/main.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${STANDARD_UI_GENERATED_CPP}"
    EXCLUDE_FROM_ALL
)

cactus_add_example(example_particle_burst_generated
    SOURCE "examples/particle-burst/particle_burst.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${PARTICLE_BURST_GENERATED_CPP}"
)

cactus_add_example(example_gradient_square_generated
    SOURCE "examples/gradient-square/gradient_square.cactus"
    BACKEND "cpp-entt"
    OUTPUT "${GRADIENT_SQUARE_GENERATED_CPP}"
)

# ── Tests ───────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
