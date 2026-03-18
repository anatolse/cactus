## 1. Project Scaffolding

- [x] 1.1 Create top-level CMakeLists.txt with C++20 standard, FetchContent for Catch2, EnTT, and Raylib
- [x] 1.2 Create directory structure: `src/common/`, `src/frontend/`, `src/backends/cpp-manual/`, `src/backends/cpp-entt/`, `src/backends/rust/` (placeholder README), `tests/`, `tests/fixtures/`, `examples/cactus_shop/`, `spec/`
- [x] 1.3 Create `tests/CMakeLists.txt` with Catch2 test targets
- [x] 1.4 Create `.clang-tidy` config with modern C++ checks (modernize-*, readability-*, bugprone-*, performance-*)
- [x] 1.5 Create `.clang-format` config with consistent style (BasedOnStyle: LLVM or Google, IndentWidth: 4, ColumnLimit: 120)
- [x] 1.6 Verify empty project builds with CMake

## 2. Language Specification

- [x] 2.1 Write `spec/cactus_dsl_spec.md` with formal EBNF grammar covering all constructs (module, use, const, struct, enum, trait, unit, system, view, event, func, interface)
- [x] 2.2 Document keyword table, operator precedence, and type system rules in the spec
- [x] 2.3 Document semantic constraints (const-string rule, func purity, no recursion, persist/sync validation)

## 3. Example Game Files

- [x] 3.1 Write `examples/cactus_shop/main.cactus` — entry point module
- [x] 3.2 Write `examples/cactus_shop/world.cactus` — world configuration and terrain
- [x] 3.3 Write `examples/cactus_shop/player.cactus` — player controller with persist/sync fields
- [x] 3.4 Write `examples/cactus_shop/shop.cactus` — shop items, inventory, purchase logic
- [x] 3.5 Write `examples/cactus_shop/ui.cactus` — HUD and shop UI views

## 4. Common Utilities

- [x] 4.1 Implement `src/common/source_location.h` — SourceLocation struct (filename, line, column)
- [x] 4.2 Implement `src/common/error_reporter.h/.cpp` — error/warning reporting with source locations
- [x] 4.3 Implement `src/common/string_pool.h/.cpp` — interned string table (intern, lookup, contains)
- [x] 4.4 Implement `src/common/types.h` — TypeKind enum, TypeInfo struct, built-in type definitions

## 5. Lexer

- [x] 5.1 Define `src/frontend/token.h` — TokenType enum (all keywords, operators, literals, INDENT/DEDENT/NEWLINE/EOF) and Token struct
- [x] 5.2 Implement `src/frontend/lexer.h/.cpp` — indentation-sensitive tokenizer with indent stack, keyword recognition, numeric/string/hex-color literals, comment skipping, operator tokenization
- [x] 5.3 Write `tests/test_lexer.cpp` — tests for INDENT/DEDENT, keywords vs identifiers, int/float literals, string literals, hex colors, comments, operators, tab rejection, error cases
- [x] 5.4 Create `tests/fixtures/minimal_trait.cactus` — single trait with one var field for lexer/parser testing

## 6. AST Node Types

- [x] 6.1 Define `src/frontend/ast.h` — all AST node types: ProgramNode, ModuleNode, UseNode, ConstBlockNode, StructNode, EnumNode, TraitNode, UnitNode, SystemNode, ViewNode, EventNode, FuncNode, InterfaceNode, FieldNode, EventHandlerNode, ExprNode variants, StmtNode variants

## 7. Parser

- [x] 7.1 Implement `src/frontend/parser.h/.cpp` — recursive descent parser: parse_program, parse_module, parse_use, parse_const_block, parse_struct, parse_enum
- [x] 7.2 Implement parser: parse_trait (with field modifiers persist/sync/pub/let/var and event handlers)
- [x] 7.3 Implement parser: parse_unit (apply, config, child blocks), parse_system (filter, target, event handlers)
- [x] 7.4 Implement parser: parse_func, parse_event, parse_view, parse_interface
- [x] 7.5 Implement parser: parse_expression (precedence climbing), parse_lambda, parse_pipeline, parse_match, parse_if
- [x] 7.6 Write `tests/test_parser.cpp` — tests for each construct, expression precedence, pipeline chains, error recovery
- [x] 7.7 Create `tests/fixtures/simple_system.cactus` and `tests/fixtures/cactus_shop_mini.cactus` test fixtures

## 8. Semantic Analyzer

- [x] 8.1 Implement `src/frontend/semantic_analyzer.h/.cpp` — resolve_modules, resolve_types (struct/trait/enum/list[T] resolution)
- [x] 8.2 Implement semantic analyzer: check_const_strings (reject string literals outside const blocks)
- [x] 8.3 Implement semantic analyzer: check_func_purity (no emit, no world access, no mutation) and check_no_recursion (direct + indirect)
- [x] 8.4 Implement semantic analyzer: resolve_scopes (scope tree, variable resolution)
- [x] 8.5 Implement semantic analyzer: infer_types (expression type inference, lambda parameter inference)
- [x] 8.6 Implement semantic analyzer: validate persist/sync modifiers (only on var fields), validate_system_filters, validate_event_usage
- [x] 8.7 Implement semantic analyzer: build_dependency_graph (system read/write analysis for parallelism)
- [x] 8.8 Define `src/frontend/decorated_ast.h` — DecoratedProgram with resolved types, scopes, dependency graph, string pool snapshot
- [x] 8.9 Write `tests/test_semantic.cpp` — tests for type resolution, const-string enforcement, func purity, no recursion, scope resolution, persist/sync validation, filter validation, event validation

## 9. C++ Manual SoA Backend

- [x] 9.1 Implement `src/backends/cpp-manual/soa_emitter.h/.cpp` — SoA storage class generation from traits, POD struct generation
- [x] 9.2 Implement `src/backends/cpp-manual/system_emitter.h/.cpp` — system update functions with map/filter/reduce loop generation
- [x] 9.3 Implement `src/backends/cpp-manual/event_emitter.h/.cpp` — event struct, buffer, and dispatch generation
- [x] 9.4 Implement `src/backends/cpp-manual/cpp_manual_codegen.h/.cpp` — main codegen orchestrator with persist serialization hooks, sync replication hooks, and Raylib main loop generation
- [x] 9.5 Write `tests/test_codegen_manual.cpp` — tests for SoA struct output, system loops, event buffers, persist/sync hooks, compilable output verification

## 10. C++ EnTT Backend

- [x] 10.1 Implement `src/backends/cpp-entt/component_emitter.h/.cpp` — EnTT component struct and tag generation from traits
- [x] 10.2 Implement `src/backends/cpp-entt/system_emitter.h/.cpp` — system functions using registry.view<>().each() with map/filter/reduce
- [x] 10.3 Implement `src/backends/cpp-entt/event_emitter.h/.cpp` — EnTT dispatcher event structs and sink connections
- [x] 10.4 Implement `src/backends/cpp-entt/cpp_entt_codegen.h/.cpp` — main codegen orchestrator with entity creation from units, persist/sync hooks, and Raylib+EnTT main loop generation
- [x] 10.5 Write `tests/test_codegen_entt.cpp` — tests for component structs, registry views, dispatcher setup, entity creation, compilable output verification

## 11. CLI Entry Point

- [x] 11.1 Implement `src/main.cpp` — argument parsing (input file, --backend cpp-manual|cpp-entt, --output), full pipeline execution (lex → parse → analyze → generate), error reporting with source locations, exit codes
- [x] 11.2 Test CLI with example cactus shop files end-to-end

## 12. Integration Testing

- [x] 12.1 Compile cactus shop example through full pipeline with cpp-manual backend and verify generated C++ compiles
- [x] 12.2 Compile cactus shop example through full pipeline with cpp-entt backend and verify generated C++ compiles
- [x] 12.3 Verify persist/sync fields produce correct serialization and replication code in both backends
