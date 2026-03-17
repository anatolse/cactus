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
