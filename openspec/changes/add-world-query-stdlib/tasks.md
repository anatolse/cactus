## 1. Parser and AST support

- [ ] 1.1 Add AST nodes/fields for query filter predicates and query call named arguments.
- [ ] 1.2 Extend postfix expression parsing to recognize module-qualified or aliased `member[query-filters](...)` query syntax.
- [ ] 1.3 Add parser tests covering positive filters, negative filters, and named spatial query arguments.

## 2. Semantic analysis

- [ ] 2.1 Resolve query callees through normal module-path or alias lookup and validate trait names used inside query filter brackets.
- [ ] 2.2 Infer return types for recognized world and spatial query expressions.
- [ ] 2.3 Enforce world-access restrictions and required named arguments for query expressions.
- [ ] 2.4 Add semantic tests for invalid traits, invalid contexts, and query result typing.

## 3. Stdlib surface

- [ ] 3.1 Add `std.query` declarations for ECS/world query operations.
- [ ] 3.2 Add `std.physics.flat.query` declarations for 2D spatial query operations.
- [ ] 3.3 Add `std.physics.volume.query` declarations for 3D spatial query operations.
- [ ] 3.4 Update examples or stubbed stdlib systems to reference the new query API where appropriate.

## 4. Backend code generation

- [ ] 4.1 Lower `std.query` expressions to efficient EnTT-backed registry queries.
- [ ] 4.1a Add support for relationship queries such as `query.parent(of = id)` using backend hierarchy data.
- [ ] 4.2 Implement total `entity_id` fallback behavior for empty `first` and `nearest` queries.
- [ ] 4.3 Lower flat and volume spatial queries, including box/circle/sphere overlaps and raycasts, with trait filtering and named argument handling.
- [ ] 4.4 Add backend tests covering ECS queries, spatial queries, and empty-result behavior.

## 5. Verification

- [ ] 5.1 Run the parser, semantic, and backend test suites affected by query support.
- [ ] 5.2 Review the change artifacts for consistency with the final implementation surface.