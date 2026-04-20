# ── Catch2 Test Targets ──────────────────────────────────────────────────────────

# Test fixture directory available as compile definition
add_compile_definitions(CACTUS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

find_program(CLANG_FORMAT_EXE
    NAMES clang-format
    PATHS
        "C:/Program Files/LLVM/bin"
        "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
)

find_program(CLANG_TIDY_EXE
    NAMES clang-tidy clang-tidy.exe
    PATHS
        "C:/Program Files/LLVM/bin"
        "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
)

if(CLANG_FORMAT_EXE)
    message(STATUS "clang-format found for example compilation checks: ${CLANG_FORMAT_EXE}")
else()
    message(STATUS "clang-format not found — example compilation checks will report this at test time")
endif()

set(CACTUS_GENERATED_EXAMPLES_DIR "${CMAKE_BINARY_DIR}/generated_examples")
file(MAKE_DIRECTORY "${CACTUS_GENERATED_EXAMPLES_DIR}")

set(BLUE_SQUARE_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/blue-square.generated.cpp")
if(NOT EXISTS "${BLUE_SQUARE_GENERATED_CPP}")
    file(WRITE "${BLUE_SQUARE_GENERATED_CPP}" "#include \"backends/cpp-entt/runtime.h\"\nnamespace cactus::runtime::entt_backend {\nProjectConfig generated_project_config() noexcept { return {800, 600, \"Blue Square\", 60}; }\nvoid generated_setup_dispatcher(entt::dispatcher&) {}\nvoid generated_init_project(entt::registry&) {}\nvoid generated_update_project(entt::registry&, entt::dispatcher&, float) {}\nvoid generated_render_project(entt::registry&, entt::dispatcher&) {}\n}\n")
endif()

add_executable(example_blue_square_generated EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/examples/blue-square/game/main.cpp"
    "${BLUE_SQUARE_GENERATED_CPP}"
)
set_source_files_properties("${BLUE_SQUARE_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
target_compile_features(example_blue_square_generated PRIVATE cxx_std_20)
target_link_libraries(example_blue_square_generated PRIVATE cactus_runtime_cpp_entt raylib EnTT::EnTT)
target_include_directories(example_blue_square_generated PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_property(TARGET example_blue_square_generated PROPERTY CXX_CLANG_TIDY "")

set(STDLIB_EXTERN_ENTT_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/stdlib-extern-entt.generated.cpp")
if(NOT EXISTS "${STDLIB_EXTERN_ENTT_GENERATED_CPP}")
    file(WRITE "${STDLIB_EXTERN_ENTT_GENERATED_CPP}" "#include \"backends/cpp-entt/runtime.h\"\nnamespace cactus::runtime::entt_backend {\nProjectConfig generated_project_config() noexcept { return {800, 600, \"Stdlib Extern Coverage\", 60}; }\nvoid generated_setup_dispatcher(entt::dispatcher&) {}\nvoid generated_init_project(entt::registry&) {}\nvoid generated_update_project(entt::registry&, entt::dispatcher&, float) {}\nvoid generated_render_project(entt::registry&, entt::dispatcher&) {}\n}\n")
endif()

add_executable(example_stdlib_extern_entt_generated EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/examples/blue-square/game/main.cpp"
    "${STDLIB_EXTERN_ENTT_GENERATED_CPP}"
)
set_source_files_properties("${STDLIB_EXTERN_ENTT_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
target_compile_features(example_stdlib_extern_entt_generated PRIVATE cxx_std_20)
target_link_libraries(example_stdlib_extern_entt_generated PRIVATE cactus_runtime_cpp_entt raylib EnTT::EnTT)
target_include_directories(example_stdlib_extern_entt_generated PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_property(TARGET example_stdlib_extern_entt_generated PROPERTY CXX_CLANG_TIDY "")

set(STDLIB_EXTERN_MANUAL_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/stdlib-extern-manual.generated.cpp")
if(NOT EXISTS "${STDLIB_EXTERN_MANUAL_GENERATED_CPP}")
    file(WRITE "${STDLIB_EXTERN_MANUAL_GENERATED_CPP}" "#include \"backends/cpp-manual/runtime.h\"\nnamespace cactus::runtime::manual_backend {\nProjectConfig generated_project_config() noexcept { return {800, 600, \"Stdlib Extern Coverage\", 60}; }\nvoid generated_init_project() {}\nvoid generated_update_project(float) {}\nvoid generated_render_project() {}\n}\n")
endif()

add_executable(example_stdlib_extern_manual_generated EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/tests/example_manual_generated_main.cpp"
    "${STDLIB_EXTERN_MANUAL_GENERATED_CPP}"
)
set_source_files_properties("${STDLIB_EXTERN_MANUAL_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
target_compile_features(example_stdlib_extern_manual_generated PRIVATE cxx_std_20)
target_link_libraries(example_stdlib_extern_manual_generated PRIVATE cactus_runtime_cpp_manual raylib)
target_include_directories(example_stdlib_extern_manual_generated PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_property(TARGET example_stdlib_extern_manual_generated PROPERTY CXX_CLANG_TIDY "")

set(MESH_RENDERER_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/mesh-renderer.generated.cpp")
if(NOT EXISTS "${MESH_RENDERER_GENERATED_CPP}")
    file(WRITE "${MESH_RENDERER_GENERATED_CPP}" "#include \"backends/cpp-entt/runtime.h\"\nnamespace cactus::runtime::entt_backend {\nProjectConfig generated_project_config() noexcept { return {800, 600, \"Mesh Renderer\", 60}; }\nvoid generated_setup_dispatcher(entt::dispatcher&) {}\nvoid generated_init_project(entt::registry&) {}\nvoid generated_update_project(entt::registry&, entt::dispatcher&, float) {}\nvoid generated_render_project(entt::registry&, entt::dispatcher&) {}\n}\n")
endif()

add_executable(example_mesh_renderer_generated EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/examples/blue-square/game/main.cpp"
    "${MESH_RENDERER_GENERATED_CPP}"
)
set_source_files_properties("${MESH_RENDERER_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
target_compile_features(example_mesh_renderer_generated PRIVATE cxx_std_20)
target_link_libraries(example_mesh_renderer_generated PRIVATE cactus_runtime_cpp_entt raylib EnTT::EnTT)
target_include_directories(example_mesh_renderer_generated PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_property(TARGET example_mesh_renderer_generated PROPERTY CXX_CLANG_TIDY "")

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

# Manual SoA codegen tests
add_executable(test_codegen_manual test_codegen_manual.cpp)
target_link_libraries(test_codegen_manual PRIVATE cactus_backend_cpp_manual Catch2::Catch2WithMain)
add_test(NAME CodegenManualTests COMMAND test_codegen_manual)

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
target_link_libraries(test_runtime_stdlib PRIVATE cactus_runtime_common cactus_runtime_cpp_manual Catch2::Catch2WithMain)
target_link_libraries(test_runtime_stdlib PRIVATE cactus_runtime_cpp_entt)
target_compile_definitions(test_runtime_stdlib PRIVATE
    CACTUS_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME RuntimeStdlibTests COMMAND test_runtime_stdlib)

# Example C++ compilation integration tests
add_executable(test_example_cpp_compilation test_example_cpp_compilation.cpp)
target_link_libraries(test_example_cpp_compilation PRIVATE Catch2::Catch2WithMain)
add_dependencies(test_example_cpp_compilation cactus)
set_property(TARGET test_example_cpp_compilation PROPERTY CXX_CLANG_TIDY "")

if(CLANG_FORMAT_EXE)
    set(CACTUS_TEST_CLANG_FORMAT "${CLANG_FORMAT_EXE}")
else()
    set(CACTUS_TEST_CLANG_FORMAT "")
endif()

if(CLANG_TIDY_EXE)
    set(CACTUS_TEST_CLANG_TIDY "${CLANG_TIDY_EXE}")
else()
    set(CACTUS_TEST_CLANG_TIDY "")
endif()

target_compile_definitions(test_example_cpp_compilation PRIVATE
    CACTUS_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CACTUS_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
    CACTUS_COMPILER_PATH="$<TARGET_FILE:cactus>"
    CACTUS_CMAKE_COMMAND="${CMAKE_COMMAND}"
    CACTUS_BUILD_CONFIG="${CMAKE_BUILD_TYPE}"
    CACTUS_CLANG_FORMAT_PATH="${CACTUS_TEST_CLANG_FORMAT}"
    CACTUS_CLANG_TIDY_PATH="${CACTUS_TEST_CLANG_TIDY}"
    CACTUS_EXAMPLE_BLUE_SQUARE_SOURCE="examples/blue-square/square.cactus"
    CACTUS_EXAMPLE_BLUE_SQUARE_GENERATED_CPP="${BLUE_SQUARE_GENERATED_CPP}"
    CACTUS_EXAMPLE_BLUE_SQUARE_TARGET="example_blue_square_generated"
    CACTUS_EXAMPLE_STDLIB_EXTERN_SOURCE="examples/stdlib_extern_coverage.cactus"
    CACTUS_EXAMPLE_STDLIB_EXTERN_ENTT_GENERATED_CPP="${STDLIB_EXTERN_ENTT_GENERATED_CPP}"
    CACTUS_EXAMPLE_STDLIB_EXTERN_ENTT_TARGET="example_stdlib_extern_entt_generated"
    CACTUS_EXAMPLE_STDLIB_EXTERN_MANUAL_GENERATED_CPP="${STDLIB_EXTERN_MANUAL_GENERATED_CPP}"
    CACTUS_EXAMPLE_STDLIB_EXTERN_MANUAL_TARGET="example_stdlib_extern_manual_generated"
    CACTUS_EXAMPLE_MESH_RENDERER_SOURCE="examples/mesh-renderer/main.cactus"
    CACTUS_EXAMPLE_MESH_RENDERER_GENERATED_CPP="${MESH_RENDERER_GENERATED_CPP}"
    CACTUS_EXAMPLE_MESH_RENDERER_TARGET="example_mesh_renderer_generated"
)

add_test(NAME ExampleCppCompilationTests COMMAND test_example_cpp_compilation)

add_custom_command(
    TARGET test_example_cpp_compilation POST_BUILD
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "^ExampleCppCompilationTests$" -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running focused example C++ compilation integration check"
)
