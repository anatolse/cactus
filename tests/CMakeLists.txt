# ── Catch2 Test Targets ──────────────────────────────────────────────────────────

# Test fixture directory available as compile definition
add_compile_definitions(CACTUS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# ── Fake raylib support library (headless behavioral tests only) ───────────────
# Test-only: provides the scripted input/window state and call-log recording
# that cactus::runtime::raylib wrappers (src/backends/cpp-entt/raylib_io.hpp)
# call into when built with CACTUS_RAYLIB_FAKE defined. Never linked into a
# real example executable.
add_library(cactus_raylib_fake STATIC EXCLUDE_FROM_ALL
    fake_raylib/fake_raylib.cpp
)
target_include_directories(cactus_raylib_fake PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(cactus_raylib_fake PUBLIC raylib)
target_compile_features(cactus_raylib_fake PRIVATE cxx_std_20)
set_property(TARGET cactus_raylib_fake PROPERTY CXX_CLANG_TIDY "")

# ── Headless-fake variant of the C++ EnTT runtime ───────────────────────────
# runtime.cpp recompiled with CACTUS_RAYLIB_FAKE defined, since the shared
# cactus_runtime_cpp_entt target (root CMakeLists.txt) is compiled once,
# without the flag, and reused by every real example executable plus
# test_runtime_stdlib. CACTUS_RAYLIB_FAKE is PUBLIC so any headless test
# executable linking this target automatically compiles its own sources
# (including the #include'd generated .cpp) with the flag too. Test-only;
# never linked into a real example.
add_library(cactus_runtime_cpp_entt_headless_fake STATIC EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/src/backends/cpp-entt/runtime.cpp
)
target_include_directories(cactus_runtime_cpp_entt_headless_fake PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backends/cpp-entt
)
target_compile_definitions(cactus_runtime_cpp_entt_headless_fake PUBLIC
    CACTUS_RAYLIB_FAKE
)
target_link_libraries(cactus_runtime_cpp_entt_headless_fake PUBLIC
    cactus_runtime_common
    EnTT::EnTT
    raylib
    cactus_raylib_fake
)
set_property(TARGET cactus_runtime_cpp_entt_headless_fake PROPERTY CXX_CLANG_TIDY "")

# Lexer tests
add_executable(test_lexer test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME LexerTests COMMAND test_lexer)

# Parser tests
add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME ParserTests COMMAND test_parser)

# Semantic analysis tests
add_executable(test_semantic test_semantic.cpp)
target_link_libraries(test_semantic PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME SemanticTests COMMAND test_semantic)

# Unified name resolution tests (unified-name-resolution change)
add_executable(test_name_resolution test_name_resolution.cpp)
target_link_libraries(test_name_resolution PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME NameResolutionTests COMMAND test_name_resolution)

# EnTT codegen tests
add_executable(test_codegen_entt test_codegen_entt.cpp)
target_link_libraries(test_codegen_entt PRIVATE cactus_backend_cpp_entt Catch2::Catch2WithMain)
add_test(NAME CodegenEnttTests COMMAND test_codegen_entt)

# Module resolver tests
add_executable(test_module_resolver test_module_resolver.cpp)
target_link_libraries(test_module_resolver PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME ModuleResolverTests COMMAND test_module_resolver)

# Module artifact tests
add_executable(test_module_artifact test_module_artifact.cpp)
target_link_libraries(test_module_artifact PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME ModuleArtifactTests COMMAND test_module_artifact)

# Semantic analyzer multi-module tests
add_executable(test_semantic_modules test_semantic_modules.cpp)
target_link_libraries(test_semantic_modules PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME SemanticModulesTests COMMAND test_semantic_modules)

# Symbol identity tests
add_executable(test_symbol_identity test_symbol_identity.cpp)
target_link_libraries(test_symbol_identity PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME SymbolIdentityTests COMMAND test_symbol_identity)

# Program linker tests
add_executable(test_program_linker test_program_linker.cpp)
target_link_libraries(test_program_linker PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME ProgramLinkerTests COMMAND test_program_linker)

# Multi-module integration tests
add_executable(test_multi_module_integration test_multi_module_integration.cpp)
target_link_libraries(test_multi_module_integration PRIVATE cactus_frontend cactus_backend_cpp_entt Catch2::Catch2WithMain)
add_test(NAME MultiModuleIntegrationTests COMMAND test_multi_module_integration)

# Dynamic ECS semantic tests (dynamic-ecs-language change)
add_executable(test_semantic_dynamic test_semantic_dynamic.cpp)
target_link_libraries(test_semantic_dynamic PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME SemanticDynamicTests COMMAND test_semantic_dynamic)

# Data file writer/reader tests (dynamic-ecs-language change)
add_executable(test_data_file test_data_file.cpp)
target_link_libraries(test_data_file PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME DataFileTests COMMAND test_data_file)

# Shared/backend runtime stdlib tests
add_executable(test_runtime_stdlib test_runtime_stdlib.cpp)
target_link_libraries(test_runtime_stdlib PRIVATE cactus_runtime_common cactus_runtime_cpp_entt Catch2::Catch2WithMain)
target_compile_definitions(test_runtime_stdlib PRIVATE
    CACTUS_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME RuntimeStdlibTests COMMAND test_runtime_stdlib)

# Sweep-and-prune broad phase tests (add-sap-broadphase change)
add_executable(test_runtime_sap_broadphase test_runtime_sap_broadphase.cpp)
target_link_libraries(test_runtime_sap_broadphase PRIVATE cactus_runtime_common cactus_runtime_cpp_entt Catch2::Catch2WithMain)
add_test(NAME RuntimeSapBroadphaseTests COMMAND test_runtime_sap_broadphase)

# Activation/event-scheduler runtime tests (extract-codegen-runtime-scaffolding change)
add_executable(test_runtime_activation_scheduler test_runtime_activation_scheduler.cpp)
target_link_libraries(test_runtime_activation_scheduler PRIVATE cactus_runtime_common cactus_runtime_cpp_entt Catch2::Catch2WithMain)
add_test(NAME RuntimeActivationSchedulerTests COMMAND test_runtime_activation_scheduler)

# Projected-trait tracker runtime tests (extract-codegen-runtime-scaffolding change)
add_executable(test_runtime_projected_trait_tracker test_runtime_projected_trait_tracker.cpp)
target_link_libraries(test_runtime_projected_trait_tracker PRIVATE cactus_runtime_common cactus_runtime_cpp_entt Catch2::Catch2WithMain)
add_test(NAME RuntimeProjectedTraitTrackerTests COMMAND test_runtime_projected_trait_tracker)

# Headless behavioral tests (example-behavior-tests capability)
cactus_add_headless_behavior_test(test_blue_square_headless_behavior
    CACTUS_SOURCE examples/blue-square/square.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_blue_square_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_mesh_renderer_headless_behavior
    CACTUS_SOURCE examples/mesh-renderer/main.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_mesh_renderer_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_model_animation_headless_behavior
    CACTUS_SOURCE examples/model-animation/main.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_model_animation_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_model_tint_headless_behavior
    CACTUS_SOURCE tests/fixtures/model_tint_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_model_tint_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_cursor_capture_headless_behavior
    CACTUS_SOURCE tests/fixtures/cursor_capture_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_cursor_capture_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_first_person_arena_headless_behavior
    CACTUS_SOURCE examples/first-person-arena/main.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_first_person_arena_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_hierarchy_query_headless_behavior
    CACTUS_SOURCE tests/fixtures/hierarchy_query_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_hierarchy_query_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_lexical_local_mutation_headless_behavior
    CACTUS_SOURCE tests/fixtures/lexical_local_mutation_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_lexical_local_mutation_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_standard_ui_layout_headless_behavior
    CACTUS_SOURCE tests/fixtures/standard_ui_layout_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_standard_ui_layout_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_standard_ui_render_headless_behavior
    CACTUS_SOURCE tests/fixtures/standard_ui_layout_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_standard_ui_render_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_pointer_candidates_headless_behavior
    CACTUS_SOURCE tests/fixtures/pointer_candidates_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_pointer_candidates_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_standard_ui_animation_headless_behavior
    CACTUS_SOURCE tests/fixtures/standard_ui_layout_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_standard_ui_animation_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_standard_ui_example_headless_behavior
    CACTUS_SOURCE examples/standard-ui/main.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_standard_ui_example_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_pair_handler_return_headless_behavior
    CACTUS_SOURCE tests/fixtures/pair_handler_return_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_pair_handler_return_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_spatial_join_tuples_headless_behavior
    CACTUS_SOURCE tests/fixtures/spatial_join_tuples_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_spatial_join_tuples_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_pairs_order_by_headless_behavior
    CACTUS_SOURCE tests/fixtures/pairs_order_by_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_pairs_order_by_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_collision_predicates_headless_behavior
    CACTUS_SOURCE tests/fixtures/collision_predicates_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_collision_predicates_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_bouncy_balls_3d_headless_behavior
    CACTUS_SOURCE examples/bouncy-balls-3d/main.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_bouncy_balls_3d_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_bouncy_bubbles_headless_behavior
    CACTUS_SOURCE examples/bouncy-bubbles/main.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_bouncy_bubbles_headless_behavior.cpp
)

cactus_add_headless_behavior_test(test_particle_burst_headless_behavior
    CACTUS_SOURCE examples/particle-burst/particle_burst.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_particle_burst_headless_behavior.cpp
)

# Real-GL visual test (example-behavior-tests capability's render-pass
# extension): proves per-instance rendering actually holds on a real GPU,
# which CACTUS_RAYLIB_FAKE structurally cannot exercise (see
# cactus_add_render_pass_visual_test above).
cactus_add_render_pass_visual_test(test_particle_burst_visual
    CACTUS_SOURCE examples/particle-burst/particle_burst.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_particle_burst_visual.cpp
)

# Real-GL visual test (stdlib-render-models capability): proves a
# ModelRenderer.color alpha below 255 actually renders dimmer/more
# transparent, which CACTUS_RAYLIB_FAKE structurally cannot exercise.
cactus_add_render_pass_visual_test(test_model_alpha_visual
    CACTUS_SOURCE tests/fixtures/model_tint_runtime.cactus
    BACKEND cpp-entt
    TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_model_alpha_visual.cpp
)

add_executable(test_example_cpp_compilation test_example_cpp_compilation.cpp)
target_link_libraries(test_example_cpp_compilation PRIVATE Catch2::Catch2WithMain)
add_dependencies(test_example_cpp_compilation cactus)
set_property(TARGET test_example_cpp_compilation PROPERTY CXX_CLANG_TIDY "")


target_compile_definitions(test_example_cpp_compilation PRIVATE
    CACTUS_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CACTUS_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
    CACTUS_COMPILER_PATH="$<TARGET_FILE:cactus>"
    CACTUS_CMAKE_COMMAND="${CMAKE_COMMAND}"
    CACTUS_BUILD_CONFIG="${CMAKE_BUILD_TYPE}"
    CACTUS_CLANG_FORMAT_PATH="${CLANG_FORMAT_EXE}"
    CACTUS_CLANG_TIDY_PATH="${CLANG_TIDY_EXE}"

    CACTUS_EXAMPLE_BLUE_SQUARE_SOURCE="examples/blue-square/square.cactus"
    CACTUS_EXAMPLE_BLUE_SQUARE_GENERATED_CPP="${BLUE_SQUARE_GENERATED_CPP}"
    CACTUS_EXAMPLE_BLUE_SQUARE_TARGET="example_blue_square_generated"

    CACTUS_EXAMPLE_STDLIB_EXTERN_SOURCE="examples/stdlib_extern_coverage.cactus"
    CACTUS_EXAMPLE_STDLIB_EXTERN_ENTT_GENERATED_CPP="${STDLIB_EXTERN_ENTT_GENERATED_CPP}"
    CACTUS_EXAMPLE_STDLIB_EXTERN_ENTT_TARGET="example_stdlib_extern_entt_generated"

    CACTUS_EXAMPLE_MESH_RENDERER_SOURCE="examples/mesh-renderer/main.cactus"
    CACTUS_EXAMPLE_MESH_RENDERER_GENERATED_CPP="${MESH_RENDERER_GENERATED_CPP}"
    CACTUS_EXAMPLE_MESH_RENDERER_TARGET="example_mesh_renderer_generated"

    CACTUS_EXAMPLE_MODEL_RENDERER_SOURCE="examples/model-renderer/main.cactus"
    CACTUS_EXAMPLE_MODEL_RENDERER_GENERATED_CPP="${MODEL_RENDERER_GENERATED_CPP}"
    CACTUS_EXAMPLE_MODEL_RENDERER_TARGET="example_model_renderer_generated"

    CACTUS_EXAMPLE_FIRST_PERSON_ARENA_SOURCE="examples/first-person-arena/main.cactus"
    CACTUS_EXAMPLE_FIRST_PERSON_ARENA_GENERATED_CPP="${FIRST_PERSON_ARENA_GENERATED_CPP}"
    CACTUS_EXAMPLE_FIRST_PERSON_ARENA_TARGET="example_first_person_arena_generated"

    CACTUS_EXAMPLE_PAIR_CONTACT_SOURCE="examples/pair-contact/main.cactus"
    CACTUS_EXAMPLE_PAIR_CONTACT_GENERATED_CPP="${PAIR_CONTACT_GENERATED_CPP}"
    CACTUS_EXAMPLE_PAIR_CONTACT_TARGET="example_pair_contact_generated"

    CACTUS_EXAMPLE_BOUNCY_BUBBLES_SOURCE="examples/bouncy-bubbles/main.cactus"
    CACTUS_EXAMPLE_BOUNCY_BUBBLES_GENERATED_CPP="${BOUNCY_BUBBLES_GENERATED_CPP}"
    CACTUS_EXAMPLE_BOUNCY_BUBBLES_TARGET="example_bouncy_bubbles_generated"

    CACTUS_EXAMPLE_PLATFORMER_SOURCE="examples/platformer/platformer.cactus"
    CACTUS_EXAMPLE_PLATFORMER_GENERATED_CPP="${PLATFORMER_GENERATED_CPP}"
    CACTUS_EXAMPLE_PLATFORMER_TARGET="example_platformer_generated"

    CACTUS_EXAMPLE_WAVING_LABEL_2D_SOURCE="examples/waving-label-2d/waving_label_2d.cactus"
    CACTUS_EXAMPLE_WAVING_LABEL_2D_GENERATED_CPP="${WAVING_LABEL_2D_GENERATED_CPP}"
    CACTUS_EXAMPLE_WAVING_LABEL_2D_TARGET="example_waving_label_2d_generated"

    CACTUS_EXAMPLE_WAVING_LABEL_3D_SOURCE="examples/waving-label-3d/waving_label_3d.cactus"
    CACTUS_EXAMPLE_WAVING_LABEL_3D_GENERATED_CPP="${WAVING_LABEL_3D_GENERATED_CPP}"
    CACTUS_EXAMPLE_WAVING_LABEL_3D_TARGET="example_waving_label_3d_generated"

    CACTUS_EXAMPLE_EDITOR_TEST_SOURCE="examples/editor-test/editor-test.cactus"
    CACTUS_EXAMPLE_EDITOR_TEST_GENERATED_CPP="${EDITOR_TEST_GENERATED_CPP}"
    CACTUS_EXAMPLE_EDITOR_TEST_TARGET="example_editor_test_generated"

    CACTUS_EXAMPLE_EDITOR_3D_SOURCE="examples/editor-3d/main.cactus"
    CACTUS_EXAMPLE_EDITOR_3D_GENERATED_CPP="${EDITOR_3D_GENERATED_CPP}"
    CACTUS_EXAMPLE_EDITOR_3D_TARGET="example_editor_3d_generated"

    CACTUS_EXAMPLE_SPLIT_SCREEN_SOURCE="examples/split-screen/split_screen.cactus"
    CACTUS_EXAMPLE_SPLIT_SCREEN_GENERATED_CPP="${SPLIT_SCREEN_GENERATED_CPP}"
    CACTUS_EXAMPLE_SPLIT_SCREEN_TARGET="example_split_screen_generated"

    CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_SOURCE="examples/split-screen-forest-bombs/forest_bombs.cactus"
    CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP="${SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP}"
    CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_TARGET="example_split_screen_forest_bombs_generated"

    CACTUS_EXAMPLE_STANDARD_UI_SOURCE="examples/standard-ui/main.cactus"
    CACTUS_EXAMPLE_STANDARD_UI_GENERATED_CPP="${STANDARD_UI_GENERATED_CPP}"
    CACTUS_EXAMPLE_STANDARD_UI_TARGET="example_standard_ui_generated"

    CACTUS_EXAMPLE_PARTICLE_BURST_SOURCE="examples/particle-burst/particle_burst.cactus"
    CACTUS_EXAMPLE_PARTICLE_BURST_GENERATED_CPP="${PARTICLE_BURST_GENERATED_CPP}"
    CACTUS_EXAMPLE_PARTICLE_BURST_TARGET="example_particle_burst_generated"

    CACTUS_EXAMPLE_GRADIENT_SQUARE_SOURCE="examples/gradient-square/gradient_square.cactus"
    CACTUS_EXAMPLE_GRADIENT_SQUARE_GENERATED_CPP="${GRADIENT_SQUARE_GENERATED_CPP}"
    CACTUS_EXAMPLE_GRADIENT_SQUARE_TARGET="example_gradient_square_generated"

    CACTUS_EXTERNAL_HANDLER_LINK_TARGET="example_external_handler_user_library"
    CACTUS_EXTERNAL_HANDLER_MISSING_TARGET="example_external_handler_missing_implementations"
)

add_test(NAME ExampleCppCompilationTests COMMAND test_example_cpp_compilation)

# add_custom_command(
#     TARGET test_example_cpp_compilation POST_BUILD
#     COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "^ExampleCppCompilationTests$" -C $<CONFIG>
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     COMMENT "Running focused example C++ compilation integration check"
# )
