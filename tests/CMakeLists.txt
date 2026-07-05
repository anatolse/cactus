# ── Catch2 Test Targets ──────────────────────────────────────────────────────────

# Test fixture directory available as compile definition
add_compile_definitions(CACTUS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

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

# Program linker tests
add_executable(test_program_linker test_program_linker.cpp)
target_link_libraries(test_program_linker PRIVATE cactus_frontend Catch2::Catch2WithMain)
add_test(NAME ProgramLinkerTests COMMAND test_program_linker)

# Multi-module integration tests
add_executable(test_multi_module_integration test_multi_module_integration.cpp)
target_link_libraries(test_multi_module_integration PRIVATE cactus_frontend Catch2::Catch2WithMain)
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

    CACTUS_EXAMPLE_SPLIT_SCREEN_SOURCE="examples/split-screen/split_screen.cactus"
    CACTUS_EXAMPLE_SPLIT_SCREEN_GENERATED_CPP="${SPLIT_SCREEN_GENERATED_CPP}"
    CACTUS_EXAMPLE_SPLIT_SCREEN_TARGET="example_split_screen_generated"

    CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_SOURCE="examples/split-screen-forest-bombs/forest_bombs.cactus"
    CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP="${SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP}"
    CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_TARGET="example_split_screen_forest_bombs_generated"
)

add_test(NAME ExampleCppCompilationTests COMMAND test_example_cpp_compilation)

# add_custom_command(
#     TARGET test_example_cpp_compilation POST_BUILD
#     COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "^ExampleCppCompilationTests$" -C $<CONFIG>
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     COMMENT "Running focused example C++ compilation integration check"
# )
