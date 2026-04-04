## 1. AST Changes

- [x] 1.1 Add `SortKey { std::string alias; std::string field; bool descending = false; SourceLocation location; }` struct to `src/frontend/ast.h`
- [x] 1.2 Add `std::vector<SortKey> order_by;` field to `SystemNode` in `src/frontend/ast.h`

## 2. Parser Changes

- [x] 2.1 Add `parseOrderByClause()` method to `src/frontend/parser.cpp`: parse `order by :` INDENT then one or more `IDENTIFIER.IDENTIFIER [asc|desc]` lines DEDENT
- [x] 2.2 Hook `parseOrderByClause()` into `parseSystemDecl()` after `exclude:` and before event handlers
- [x] 2.3 Handle `order` and `by` as contextual keywords (not reserved, but recognized in system context)
- [x] 2.4 Handle `asc` and `desc` as contextual direction keywords in sort key parsing
- [x] 2.5 Update `src/frontend/parser.h` with `parseOrderByClause()` declaration
- [x] 2.6 Update `tests/test_parser.cpp`: add tests for single-key, multi-key, default direction, and no-order-by cases

## 3. Semantic Analyzer Changes

- [x] 3.1 Add `validateOrderByClause()` method to `src/frontend/semantic_analyzer.cpp`
- [x] 3.2 Validate each sort key alias is declared in the system's filter block
- [x] 3.3 Validate each sort key field exists on the referenced trait
- [x] 3.4 Validate each sort key field type is scalar-comparable (`int`, `float`, `bool`); for `vec2`/`vec3` fields, resolve member access to `float`
- [x] 3.5 Validate that a system with `order by:` has a `filter:` clause
- [x] 3.6 Update `src/frontend/semantic_analyzer.h` with new method declaration
- [x] 3.7 Update `tests/test_semantic.cpp`: add tests for valid order by, alias-not-in-filter error, non-orderable type error, vec2-member valid/invalid cases

## 4. EnTT Backend Codegen

- [x] 4.1 In `src/backends/cpp-entt/system_emitter.cpp`, check if `system_node.order_by` is non-empty before generating each handler's iteration loop
- [x] 4.2 Generate `registry.sort<T>([&](entt::entity a, entt::entity b) { ... })` call where `T` is the component type of the first sort key's alias
- [x] 4.3 Generate the multi-key comparator body: if-chain for primary/secondary keys, using `<` for asc and `>` for desc
- [x] 4.4 Place the generated sort call immediately before the `auto view = registry.view<...>()` call for each handler
- [x] 4.5 Update `tests/test_codegen_entt.cpp`: add tests for single-key sort codegen, multi-key codegen, and no-order-by (no sort call generated)
