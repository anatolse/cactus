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

if(CLANG_FORMAT_EXE)
    message(STATUS "clang-format found for example compilation checks: ${CLANG_FORMAT_EXE}")
else()
    message(STATUS "clang-format not found — example compilation checks will report this at test time")
endif()

set(CACTUS_GENERATED_EXAMPLES_DIR "${CMAKE_BINARY_DIR}/generated_examples")
file(MAKE_DIRECTORY "${CACTUS_GENERATED_EXAMPLES_DIR}")

set(BLUE_SQUARE_GENERATED_CPP "${CACTUS_GENERATED_EXAMPLES_DIR}/blue-square.generated.cpp")
if(NOT EXISTS "${BLUE_SQUARE_GENERATED_CPP}")
    file(WRITE "${BLUE_SQUARE_GENERATED_CPP}" "int main() { return 0; }\n")
endif()

add_executable(example_blue_square_generated EXCLUDE_FROM_ALL "${BLUE_SQUARE_GENERATED_CPP}")
set_source_files_properties("${BLUE_SQUARE_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
target_compile_features(example_blue_square_generated PRIVATE cxx_std_20)
target_link_libraries(example_blue_square_generated PRIVATE raylib EnTT::EnTT)
set_property(TARGET example_blue_square_generated PROPERTY CXX_CLANG_TIDY "")

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
)

add_test(NAME ExampleCppCompilationTests COMMAND test_example_cpp_compilation)

add_custom_command(
    TARGET test_example_cpp_compilation POST_BUILD
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "^ExampleCppCompilationTests$" -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running focused example C++ compilation integration check"
)
