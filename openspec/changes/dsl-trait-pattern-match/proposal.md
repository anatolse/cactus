## Why

Event handlers often need to respond differently based on the type of entity involved in an event. The canonical example is collision handling: when an entity collides with something, the response depends on whether the other entity is a boss, an enemy, a collectible, or an obstacle. Currently this requires manual checks using extern functions or opaque entity IDs with no clean dispatch mechanism. Trait pattern matching on `entity_id` values provides type-safe, zero-overhead double dispatch that composes naturally with the ECS model.

## What Changes

- Introduce `match expr:` as a **statement-level** construct (distinct from the existing expression-level `MatchExpr`)
- When the subject expression has type `entity_id`, the `match` performs **trait pattern matching**: each arm tests whether the entity has a given trait, and optionally binds the trait's data to an alias
- Trait match arms use the syntax: `TraitName [as alias] =>` followed by a statement body
- An optional wildcard arm `_ =>` matches when no trait arm matched
- If no arm matches and no wildcard is present, execution continues with no effect (open world — no exhaustiveness required)
- Events may carry `entity_id` fields (the `entity_id` type is already in the type system)
- The existing `on event as alias:` syntax provides the event binding (already supported in `EventHandlerNode`)

## Capabilities

### New Capabilities
- `dsl-trait-pattern-match`: The `match entity_id:` statement with trait pattern arms, including optional alias binding, wildcard arm, first-match-wins semantics, and compile-time trait name validation

### Modified Capabilities
- `dsl-parser`: New `trait_match_stmt` production; distinguish from existing `MatchExpr` by subject type context
- `dsl-semantic-analysis`: Type-driven dispatch — when subject type is `entity_id`, validate arms as trait names and alias bindings; regular value match when subject is enum/primitive
- `backend-cpp-entt`: Code generation compiles each arm to `registry.try_get<Trait>(entity)` checks in an if/else-if chain

## Impact

- `src/frontend/ast.h`: Add `TraitMatchArm`, `WildcardArm`, `TraitMatchStmt` to `StmtNode::Variant`
- `src/frontend/parser.cpp/h`: New `parseTraitMatchStmt()` or extended `parseMatchStmt()` with type-discriminated parsing
- `src/frontend/semantic_analyzer.cpp/h`: Type-driven match validation
- `src/backends/cpp-entt/system_emitter.cpp`: Generate `try_get` if/else-if chains
- Test files: `test_parser.cpp`, `test_semantic.cpp`, `test_codegen_entt.cpp`
