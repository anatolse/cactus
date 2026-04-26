## 1. AST Changes — Trait Cleanup

- [x] 1.1 Remove `std::vector<EventHandlerNode> handlers` field from `TraitNode` in `src/frontend/ast.hpp`

## 2. AST Changes — System Ordering

- [x] 2.1 Add `std::vector<std::string> after_systems` field to `SystemNode` in `src/frontend/ast.hpp`
- [x] 2.2 Add `std::vector<std::string> after_systems` field to `SystemInfo` in `src/frontend/semantic_analyzer.hpp`

## 3. AST Changes — Config/Spawn Qualification

- [x] 3.1 Add `std::optional<std::string> alias` field to `ApplyEntry` in `src/frontend/ast.hpp`
- [x] 3.2 Replace `ConfigAssignment.name` (`std::string`) with a `ConfigKey` struct holding `std::string prefix` (empty if bare) and `std::string field` in `src/frontend/ast.hpp`
- [x] 3.3 Apply the same `ConfigKey` struct to `SpawnStmt` override pairs (currently `std::vector<std::pair<std::string, ExprNode>>`)

## 4. Lexer — Add `after` Keyword

- [x] 4.1 Add `AFTER` token type to `TokenType` enum in `src/frontend/token.hpp`
- [x] 4.2 Register `"after"` → `AFTER` in the keyword map in `src/frontend/lexer.cpp`

## 5. Parser — Trait Body Restriction

- [x] 5.1 In `src/frontend/parser.cpp`, locate the trait body parsing loop and remove the branches that accept `on` (event handler) and `func` tokens
- [x] 5.2 Add a clear parse error for `on` inside a trait body: `"event handlers are not allowed in trait bodies; declare a system instead"`
- [x] 5.3 Add a clear parse error for `func` inside a trait body: `"func declarations are not allowed in trait bodies; use a top-level func instead"`

## 6. Parser — System `after:` Clause

- [x] 6.1 Add `parse_after_clause()` that reads `AFTER COLON NEWLINE INDENT { IDENTIFIER NEWLINE } DEDENT` (same block structure as `filter:` and `exclude:`) and returns `std::vector<std::string>`; report error if the block is empty
- [x] 6.2 In `parse_system_decl()`, after parsing `filter:` and `exclude:`, check for `AFTER` token and call `parse_after_clause()`; assign result to `SystemNode.after_systems`
- [x] 6.3 Emit a parse error if an `after:` block appears after an `on` handler has already been parsed in the system body

## 7. Parser — Apply Alias and Dotted Config/Spawn Keys

- [x] 7.1 In `parse_apply_entry()`, after parsing the trait dotted_name, check for `AS` token and parse the alias identifier; store in `ApplyEntry.alias` (before the optional `: disabled`)
- [x] 7.2 In `parse_config_assign()`, change key parsing to attempt `IDENTIFIER [ DOT IDENTIFIER ]` and store as `ConfigKey`
- [x] 7.3 In `parse_spawn_args()`, apply the same dotted key parsing so spawn override argument names support `TraitName.field`

## 8. Semantic Analyzer — Trait Handler Cleanup

- [x] 8.1 Remove any code in `src/frontend/semantic_analyzer.cpp` that visits or validates `TraitNode.handlers`

## 9. Semantic Analyzer — System `after:` Validation

- [x] 9.1 After all systems are registered, iterate over each `SystemNode.after_systems` and verify each name resolves to a system; report `"unknown system '<name>' in after clause"` or `"'<name>' is not a system"` as appropriate
- [x] 9.2 Build the `after:` ordering adjacency graph and run DFS cycle detection; on cycle found, report `"cycle in system ordering: A → B → … → A"` with the full path
- [x] 9.3 Populate `SystemInfo.after_systems` in `DecoratedProgram` for each system

## 10. Semantic Analyzer — Apply Alias and Config/Spawn Key Resolution

- [x] 10.1 When validating a unit or template's `apply:` block, build an alias→trait map: for each `ApplyEntry`, register the trait name as implicit alias and the explicit alias (if any); report errors for duplicate aliases
- [x] 10.2 In config block validation, resolve each `ConfigAssignment` key: bare keys search all applied trait fields (ambiguity → error); dotted keys look up the prefix in the alias map then the field in the trait
- [x] 10.3 In spawn argument validation, apply the same dotted-key resolution using the spawned template's `apply:` alias map

## 11. Spec Updates (`spec/cactus_dsl_spec.md`)

- [x] 11.1 §3.6 (Trait grammar): remove `event_handler | func_decl` from `trait_decl` EBNF and update prose
- [x] 11.2 §3.8 (Unit) and §3.8a (Template): update `apply_entry` EBNF to include optional `as IDENTIFIER`; update `config_assign` EBNF to use `config_key = IDENTIFIER [ "." IDENTIFIER ]`; add examples
- [x] 11.3 §3.9 (System grammar): add `after_clause` to `system_decl` EBNF and add a render-pass ordering example
- [x] 11.4 §7 (Execution Model): add note on `after:` constraints, cycle detection, and topological ordering

## 12. Tests

- [x] 12.1 Parser test: trait body with `on tick():` produces a parse error
- [x] 12.2 Parser test: `after: SystemA, SystemB` is parsed correctly into `SystemNode.after_systems`
- [x] 12.3 Parser test: `apply: Position as pos` records `alias = "pos"` in `ApplyEntry`
- [x] 12.4 Parser test: `config:` key `Health.health = 100` is parsed as dotted `ConfigKey("Health", "health")`
- [x] 12.5 Semantic test: `after:` referencing an unknown system reports the expected error
- [x] 12.6 Semantic test: direct `after:` cycle (A after B, B after A) reports a cycle error
- [x] 12.7 Semantic test: valid linear `after:` chain (A → B → C) passes and produces correct `after_systems` in `DecoratedProgram`
- [x] 12.8 Semantic test: ambiguous bare config key reports the expected disambiguation error
- [x] 12.9 Semantic test: dotted config key `TraitName.field` resolves correctly
- [x] 12.10 Semantic test: duplicate alias in `apply:` block reports error
