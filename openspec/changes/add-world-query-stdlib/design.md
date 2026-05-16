## Context

The language already supports backend-provided stdlib functions through `extern func`, and it already models `entity_id` as a total opaque handle whose operations remain safe even when the handle is stale. However, there is no author-facing world query mechanism for asking questions like "does any entity have this trait?" or "which nearby entity matches these traits?". This gap is already visible in stdlib camera/physics discussions, where declarative systems need runtime lookups but the DSL currently only exposes filter-driven iteration inside systems.

This change crosses several layers at once: parser syntax, AST representation, semantic analysis, stdlib module surface, and backend code generation. It also needs to stay aligned with the language philosophy that engine plumbing belongs in the backend/stdlib rather than authored game code.

## Goals / Non-Goals

**Goals:**
- Add an author-facing query API for world/ECS lookups under `std.query`.
- Add spatial query APIs for 2D and 3D physics namespaces with optional trait filtering.
- Support positive and negative trait filters in query syntax.
- Preserve total `entity_id` semantics by making `first`/`nearest` style queries return an `entity_id` value even when no live entity matches.
- Restrict query expressions to world-aware contexts, similar to existing `exists(entity_id)` rules.
- Keep the surface uniform enough that ECS and spatial queries feel like one family of capabilities.

**Non-Goals:**
- Adding a general-purpose expression generics feature for arbitrary user functions.
- Adding author-defined query operators or user-extensible query backends.
- Defining every possible spatial primitive in this change; only the core query family is required.
- Changing existing system `filter:` syntax to reuse the exact same grammar node.

## Decisions

### Decision: Query expressions use ordinary module-qualified or aliased member calls

The design introduces a new expression form conceptually shaped like:

```cactus
use std.query as query
query.exists[Boss]()
query.count[EnemyAI, not Dead]()

use std.query
std.query.first[PlayerOwned]()

use std.physics.flat.query as query
query.nearest[Transform, Enemy](from = player_pos)
```

The bracket contents are not general type arguments. They are a query-filter grammar containing:
- positive trait requirements: `Boss`, `EnemyAI`
- negative trait requirements: `not Dead`

Query calls are still ordinary module member access in authored code. The new syntax extends how those member calls can carry trait-filter brackets, but it does not introduce a special global `query` keyword or privileged built-in call form. Authors use either the full module path (`std.query.exists[...]()`) or whatever alias they declared with `use ... as ...`.

An illustrative stdlib declaration shape for these runtime-provided query functions would look like:

```cactus
module std.query

pub extern func exists() bool
pub extern func count() int
pub extern func first() entity_id
pub extern func all() list[entity_id]
pub extern func parent(of: entity_id) entity_id

module std.physics.flat.query

pub extern func nearest(from: vec2) entity_id
pub extern func overlap_box(center: vec2, size: vec2) list[entity_id]
pub extern func overlap_circle(center: vec2, radius: float) list[entity_id]
pub extern func raycast(origin: vec2, dir: vec2, max_dist: float) entity_id

module std.physics.volume.query

pub extern func nearest(from: vec3) entity_id
pub extern func overlap_box(center: vec3, size: vec3) list[entity_id]
pub extern func overlap_sphere(center: vec3, radius: float) list[entity_id]
pub extern func raycast(origin: vec3, dir: vec3, max_dist: float) entity_id
```

The trait-filter bracket (`[Boss]`, `[EnemyAI, not Dead]`) is carried by the call expression syntax, not by the declared function parameter list itself. In other words, the extern declaration advertises the callable symbol and ordinary value arguments, while the query filter is parsed as extra DSL-level metadata on that call site.

Backend distinction therefore does **not** come from a special extern declaration form with embedded trait filters. It comes from the analyzed call expression shape:
- ordinary extern call: `std.math.sqrt(x)` → plain extern func call, no query filter metadata
- query extern call: `std.query.first[Boss]()` → extern func callee plus attached query-filter metadata in the AST

The semantic analyzer identifies recognized query namespaces/functions (such as `std.query.exists`, `std.query.count`, `std.query.first`, `std.physics.flat.query.nearest`) and annotates those call sites as query expressions. The backend then lowers those annotated expressions using both:
- the resolved extern function symbol
- the parsed query filter predicates
- the ordinary value/named arguments

So the declaration stays simple, while the *call site* is what makes a query call different from a normal extern function call.

### Decision: Relationship queries live in `std.query` as ordinary query functions

Some world queries are not set-based trait searches but structural relationship lookups. Parent lookup falls into this category, so it should live in `std.query` as an ordinary world query function:

```cactus
std.query.parent(of = child_id)
```

This keeps relationship navigation in the same namespace as other world queries while avoiding awkward trait-filter syntax where no filter is needed. It also aligns with total `entity_id` semantics: if an entity has no parent, or the relationship target is stale, the result is a stale/non-live `entity_id` handle.

**Alternatives considered:**
- Encoding parent lookup as a trait-filtered query: unnatural, because parent is a direct relationship lookup rather than a search over arbitrary matching entities.
- Creating a separate hierarchy namespace: possible later, but unnecessarily fragments the first query surface.

**Alternatives considered:**
- `query.exists(Boss)`: easier to parse, but trait names become value-like and compound filter syntax becomes awkward.
- New statement/special-form syntax: more invasive and less composable in expressions.

### Decision: ECS and spatial queries share the same filter model

`std.query` performs pure trait/world queries. `std.physics.flat.query` and `std.physics.volume.query` perform geometry-based queries intersected with the same trait-filter model.

Conceptually:
- `std.query.*` = trait predicate over the world
- `std.physics.*.query.*` = geometry predicate ∩ trait predicate

This gives a consistent mental model and avoids inventing separate filtering systems for ECS and physics.

**Alternatives considered:**
- Separate unrelated physics APIs with ad-hoc filtering parameters: simpler short term, but fragments the stdlib surface.

### Decision: Query results returning `entity_id` are total

`first[...]()` and `nearest[...]()` return `entity_id`, not an optional/nullable type. If no live entity matches, the backend returns a stale/non-live handle sentinel consistent with the total-semantics model already defined for `entity_id`.

This means authors can pass query results into other entity operations without mandatory guard code. `exists(id)` remains available when code explicitly cares whether the result points to a live entity.

**Alternatives considered:**
- Optional return types: safer for correctness, but inconsistent with the existing total-handle philosophy and noisier in gameplay code.

### Decision: Query expressions are world-access operations

Like `exists(entity_id)`, query expressions require world access and therefore are only valid in system event handlers, extern systems, and other future world-aware contexts. They are not permitted inside pure `func` bodies.

This keeps the purity boundary intact and makes query cost/context explicit.

### Decision: Named arguments are supported for query member calls that need geometry inputs

Spatial queries should support descriptive call shapes like:

```cactus
query.nearest[Transform, Enemy](from = p)
query.overlap_box[Pickup, not Collected](center = p, size = s)
query.overlap_circle[Pickup](center = p, radius = 24.0)
query.overlap_sphere[Pickup](center = p3, radius = 2.0)
query.raycast[Wall, not Trigger](origin = p, dir = d, max_dist = 100.0)
```

Rather than adding a full named-arguments feature for every function in the language, this change scopes named arguments to query call parsing/codegen where argument roles are part of the DSL-facing API contract.

**Alternatives considered:**
- Positional-only spatial query arguments: simpler parser changes, but less readable and more error-prone.

## Risks / Trade-offs

- **Parser complexity grows** → Mitigation: model query filters and query named arguments as narrow-purpose AST additions rather than a full generic-call system.
- **Total `entity_id` results may hide empty-query mistakes** → Mitigation: preserve `exists(id)` and consider debug traces/lints in a future change.
- **Backend query cost may become non-obvious** → Mitigation: keep queries limited to world-aware contexts and implement direct EnTT/runtime paths rather than reflective lookups.
- **Physics query semantics may diverge between flat and volume backends** → Mitigation: define matching capability-level requirements and namespace symmetry.

## Migration Plan

This change is additive. Existing authored code continues to compile unchanged.

Implementation should proceed in layers:
1. Add AST/parser support for query filter brackets and query named arguments.
2. Add semantic rules for query target resolution, filter validation, and world-access restrictions.
3. Add stdlib module declarations for `std.query` and physics query namespaces.
4. Add backend code generation/runtime hooks for ECS and spatial query operations.
5. Add tests and example usage, then update any currently stubbed stdlib systems that depend on world queries.

## Open Questions

- Should `first_with[...]` exist as an alias, or should `first[...]` be the only trait-filtered selector API?
- What exact return shape should raycasts use in v1: `entity_id` only, or a hit struct containing point/normal/distance?
- Should query filter brackets support module-qualified trait names from the first version, or rely on existing import resolution rules for unqualified names?