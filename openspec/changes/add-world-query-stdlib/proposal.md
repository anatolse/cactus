## Why

The stdlib currently exposes backend-provided math, input, rendering, and passive physics features, but it lacks a declarative way to query the world at runtime. This gap forces author code and stdlib systems to work around missing engine-facing query capabilities, especially for camera logic, ECS lookups, and spatial selection.

## What Changes

- Add a new stdlib query surface for world/ECS queries under `std.query`.
- Add physics query surfaces for spatial queries under `std.physics.flat.query` and `std.physics.volume.query`.
- Introduce bracketed trait-filter syntax for query expressions, including multi-trait intersection and negative trait filters.
- Define total return semantics for query expressions that yield `entity_id`, aligning empty-query results with existing stale-handle semantics.
- Restrict query expressions to world-aware contexts and define semantic validation for their filter arguments and named parameters.
- Extend backend support so query expressions compile to efficient runtime world/physics lookups.

## Capabilities

### New Capabilities
- `stdlib-world-query`: stdlib query APIs for ECS/world and spatial querying, including total `entity_id` result semantics.

### Modified Capabilities
- `dsl-parser`: add grammar support for bracketed query filters on expressions and named-argument query calls.
- `dsl-semantic-analysis`: validate query expressions, trait filters, negative filters, return types, and world-access restrictions.
- `backend-cpp-entt`: compile stdlib query expressions to EnTT/runtime-backed ECS and spatial query operations.
- `stdlib-physics`: extend physics stdlib requirements with query namespaces and trait-filtered spatial queries.

## Impact

- Affects parser, AST, semantic analyzer, stdlib module definitions, and backend code generation.
- Introduces a new author-facing query API such as `query.exists[Boss]()`, `query.count[EnemyAI, not Dead]()`, and `query.nearest[Transform, Enemy](from = p)`.
- Unblocks stdlib systems and game logic that need declarative world lookups without exposing imperative engine plumbing.