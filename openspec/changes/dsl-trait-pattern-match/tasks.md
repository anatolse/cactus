## 1. AST Changes

- [x] 1.1 Add `TraitMatchArm { std::string trait_name; std::optional<std::string> alias; std::vector<std::unique_ptr<StmtNode>> body; SourceLocation location; }` to `src/frontend/ast.h`
- [x] 1.2 Add `WildcardMatchArm { std::vector<std::unique_ptr<StmtNode>> body; SourceLocation location; }` to `src/frontend/ast.h`
- [x] 1.3 Add `TraitMatchStmt { std::unique_ptr<ExprNode> subject; std::vector<TraitMatchArm> arms; std::optional<WildcardMatchArm> wildcard; SourceLocation location; }` to `ast.h` and `StmtNode::Variant`

## 2. Parser Changes

- [x] 2.1 Add `parseTraitMatchStmt()` method to `src/frontend/parser.cpp`: at statement position, parse `match expr ":"` then INDENT, then one or more trait arms or one wildcard arm, then DEDENT
- [x] 2.2 Parse trait arm: `IDENTIFIER ["as" IDENTIFIER] "=>" INDENT stmt+ DEDENT`
- [x] 2.3 Parse wildcard arm: `"_" "=>" INDENT stmt+ DEDENT`
- [x] 2.4 Hook `parseTraitMatchStmt()` into `parseStatement()` dispatch on `match` keyword at statement position
- [x] 2.5 Update `src/frontend/parser.h` with new declaration
- [x] 2.6 Update `tests/test_parser.cpp`: add tests for single arm, multi-arm, with/without alias, with/without wildcard, marker trait arm

## 3. Semantic Analyzer Changes

- [x] 3.1 Add `validateTraitMatchStmt()` method to `src/frontend/semantic_analyzer.cpp`
- [x] 3.2 Validate subject expression type is `entity_id`; report error if not
- [x] 3.3 Validate each trait arm's trait name resolves to a declared trait
- [x] 3.4 Validate alias does not conflict with any in-scope binding (filter aliases, event alias, local vars)
- [x] 3.5 Validate that marker traits (no fields) do not have an alias
- [x] 3.6 Validate wildcard arm, if present, is the last arm
- [x] 3.7 Push/pop alias scope for each arm body so aliases don't leak between arms
- [x] 3.8 Validate `TraitMatchStmt` only appears inside system event handler bodies
- [x] 3.9 Update `src/frontend/semantic_analyzer.h`
- [x] 3.10 Update `tests/test_semantic.cpp`: add tests for valid match, non-entity_id subject error, unknown trait error, alias conflict error, marker-with-alias error, wildcard-not-last error

## 4. EnTT Backend Codegen

- [x] 4.1 Add `emitTraitMatchStmt()` to `src/backends/cpp-entt/system_emitter.cpp`
- [x] 4.2 Evaluate subject expression once into a local `entt::entity` variable
- [x] 4.3 For each `TraitMatchArm`: emit `if/else if (auto* alias = registry.try_get<Trait>(subject))` for traits with fields
- [x] 4.4 For marker trait arms: emit `} else if (registry.all_of<Trait>(subject)) {`
- [x] 4.5 For wildcard arm: emit final `} else {` block
- [x] 4.6 For no wildcard: emit no final else; if/else-if chain ends cleanly
- [x] 4.7 Update `tests/test_codegen_entt.cpp`: add tests for try_get codegen, all_of codegen, wildcard else block, no-wildcard no-else

## 5. Standard Physics Event (Optional — enables real usage)

- [x] 5.1 Add `pub event Collision:` with `var other: entity_id`, `var normal: vec2`, `var depth: float` to `stdlib/std/physics/flat.cactus` and `stdlib/std/physics/volume.cactus`
- [x] 5.2 Update platformer example to demonstrate trait pattern matching on collision: use `on collision as c: match c.other:` pattern in `examples/platformer/enemies.cactus` or `player.cactus`
