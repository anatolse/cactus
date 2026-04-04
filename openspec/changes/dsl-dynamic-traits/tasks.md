## 1. AST Changes

- [x] 1.1 Remove `EnableStmt` and `DisableStmt` structs from `src/frontend/ast.h`; remove them from `StmtNode::Variant`
- [x] 1.2 Remove `ApplyEntry.initially_active` field from `ApplyEntry` struct in `src/frontend/ast.h`
- [x] 1.3 Add `AddTraitStmt { trait_name: string, args: vector<FieldAssignment>, target_expr: optional<ExprNode> }` to `ast.h` and `StmtNode::Variant`
- [x] 1.4 Add `RemoveTraitStmt { trait_name: string, target_expr: optional<ExprNode> }` to `ast.h` and `StmtNode::Variant`

## 2. Lexer Changes

- [x] 2.1 Remove `enable` and `disable` from the keyword table in `src/frontend/lexer.cpp`
- [x] 2.2 Add `add`, `remove`, `to`, `from` as keywords (or context-sensitive identifiers) in `src/frontend/lexer.cpp` and `src/frontend/lexer.h`
- [x] 2.3 Update `test_lexer.cpp`: remove enable/disable keyword tests; add add/remove/to/from keyword tests

## 3. Parser Changes

- [x] 3.1 Remove `parseEnableStmt()` and `parseDisableStmt()` from `src/frontend/parser.cpp`
- [x] 3.2 Remove `: disabled` annotation parsing from `parseApplyEntry()` in `src/frontend/parser.cpp`
- [x] 3.3 Add `parseAddTraitStmt()` in `src/frontend/parser.cpp`: parse `add IDENTIFIER ["to" expr] ":" NEWLINE INDENT { field_assignment } DEDENT` for data traits, or bare `add IDENTIFIER ["to" expr]` for markers
- [x] 3.4 Add `parseRemoveTraitStmt()` in `src/frontend/parser.cpp`: parse `remove IDENTIFIER ["from" expr]`
- [x] 3.5 Hook `add` and `remove` into the statement dispatch in `parseStatement()`
- [x] 3.6 Add trait field default value parsing in `parseFieldDecl()`: accept `"=" expr` after the type ref
- [x] 3.7 Update `src/frontend/parser.h` with new parser method declarations
- [x] 3.8 Update `tests/test_parser.cpp`: remove enable/disable/disabled tests; add add/remove statement parsing tests including bare, block-with-fields, and cross-entity targeting forms; add field default value parsing tests

## 4. Semantic Analyzer Changes

- [x] 4.1 Remove all `enable`/`disable` statement validation logic from `src/frontend/semantic_analyzer.cpp`
- [x] 4.2 Implement `validateAddTraitStmt()` in `src/frontend/semantic_analyzer.cpp`: validate trait name exists, all required fields supplied, field names match trait fields, field value types match, `to` target is `entity_id`, only in handler context
- [x] 4.3 Implement `validateRemoveTraitStmt()` in `src/frontend/semantic_analyzer.cpp`: validate trait name exists, `from` target is `entity_id`, only in handler context
- [x] 4.4 Implement trait field default value validation: default expression must type-check and be constant-foldable
- [x] 4.5 Update `src/frontend/semantic_analyzer.h` with new validation method declarations
- [x] 4.6 Update `tests/test_semantic.cpp`: remove enable/disable tests; add add/remove validation tests
- [x] 4.7 Update `tests/test_semantic_dynamic.cpp`: update all dynamic trait tests to use block-based add/remove syntax

## 5. EnTT Backend Codegen

- [x] 5.1 Remove `EnableStmt` and `DisableStmt` code generation from `src/backends/cpp-entt/system_emitter.cpp`
- [x] 5.2 Remove `initially_active` conditional logic from spawn code generation in `src/backends/cpp-entt/component_emitter.cpp`
- [x] 5.3 Add `AddTraitStmt` code generation: emit `registry.emplace_or_replace<T>(entity, ...)` with field initializers from the block
- [x] 5.4 Add `RemoveTraitStmt` code generation: emit `registry.remove<T>(entity)`
- [x] 5.5 Handle cross-entity targeting (`to`/`from` expressions) in both add/remove codegen paths
- [x] 5.6 Update `tests/test_codegen_entt.cpp`: remove enable/disable codegen tests; add add/remove codegen tests

## 6. Manual Backend Codegen

- [x] 6.1 Remove `EnableStmt` and `DisableStmt` code generation from `src/backends/cpp-manual/system_emitter.cpp`
- [x] 6.2 Remove `initially_active` conditional logic from spawn code generation
- [x] 6.3 Add `AddTraitStmt` and `RemoveTraitStmt` code generation analogous to the EnTT backend
- [x] 6.4 Update `tests/test_codegen_manual.cpp`

## 7. Example and Fixture Migration

- [x] 7.1 Migrate `examples/platformer/` files: replace any `enable`/`disable`/`: disabled` with `add`/`remove`/marker trait patterns
- [x] 7.2 Migrate `examples/cactus_shop/` files: same migration
- [x] 7.3 Migrate `examples/blue-square/` files: same migration
- [x] 7.4 Migrate `examples/dsl_showcase.cactus`: same migration
- [x] 7.5 Migrate `tests/fixtures/` files: update all fixture files that use `enable`/`disable`/`: disabled`