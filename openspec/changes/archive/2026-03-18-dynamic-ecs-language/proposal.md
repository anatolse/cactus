## Why

Cactus DSL currently has no mechanism for dynamic entity creation, scene transitions, or fine-grained trait filtering. Every entity must be statically named and always exists — `WalkerEnemy1`, `WalkerEnemy2`, `WalkerEnemy3` — making levels verbose, inflexible, and impossible to change at runtime. Adding templates, module-based scene loading, marker traits, and trait toggling unlocks the full dynamic ECS workflow games require.

## What Changes

- **NEW** `template` declaration — a reusable entity blueprint (symmetric with `unit`); not auto-instantiated, spawned explicitly at runtime
- **NEW** `spawn TemplateName(field = value, ...)` statement — creates a new entity from a template (symmetric with `emit EventName(...)`)
- **NEW** `destroy` statement — removes the current entity from the world (inside a system handler)
- **NEW** `load module.name` statement — unloads non-persistent entities and loads a module's static units from its compiled data file
- **NEW** `on spawn()` handler — fires on systems after a new matching entity is created
- **NEW** `on destroy()` handler — fires on systems before a matching entity is removed
- **NEW** `on unload()` handler — fires on systems **before** new entities are created during `load` (Phase 1 of 3); ideal for scene teardown
- **NEW** `on load()` handler — fires on systems after all new entities are created (Phase 3 of 3); ideal for level setup scripting
- **NEW** Marker traits — `trait Frozen`, `trait Dead` etc. with no colon or body; zero-cost tag component
- **NEW** `enable TraitName` / `disable TraitName` statements — activate or deactivate a trait on the current entity at runtime
- **NEW** Default inactive trait in `apply:` — `TraitName: disabled` marks a trait as present but initially inactive
- **NEW** `exclude:` block on systems — indented list of traits to exclude from processing (symmetric with `filter:`)
- **BREAKING** `filter:` block syntax changes from bracket list `[A, B]` to indented list (symmetric with `apply:`)
- **NEW** `filter:` is now optional — a system with no `filter:` matches all entities (filter_mask = 0); only lifecycle ops are valid in handler body without a filter
- **NEW** `std.core` standard library module — ships with the compiler; provides `pub trait Persistent` and `pub system SceneCleanup` (destroys non-persistent entities on `on unload()`); explicitly imported with `use std.core`
- **NEW** Compiler emits a `_data.bin` data file per module — contains serialized `unit` instance data; `template` declarations produce no data file entries

## Capabilities

### New Capabilities

- `dsl-templates`: The `template` top-level declaration and `spawn`/`destroy` statements — entity blueprint instantiation symmetric with `event`/`emit`; lifecycle `on spawn()` and `on destroy()` hooks
- `dsl-scene-loading`: The `load` statement, modules-as-scenes execution model, three-phase lifecycle (`on unload()` → instantiate → `on load()`), `std.core` module with `trait Persistent` and `system SceneCleanup`
- `dsl-trait-modifiers`: Marker (empty body) traits, `enable`/`disable` statements, `disabled` default in `apply:` blocks, optional `filter:` (match-all when absent), and the `exclude:` system clause

### Modified Capabilities

- `dsl-lexer`: New keywords — `template`, `spawn`, `destroy`, `load`, `unload`, `enable`, `disable`, `exclude`, `disabled`
- `dsl-parser`: New grammar rules for `template_decl`, `spawn_stmt`, `destroy_stmt`, `load_stmt`, `enable_stmt`, `disable_stmt`, `exclude_clause`, lifecycle handlers (`on unload()` added), marker trait syntax, optional `filter:` block
- `dsl-semantic-analysis`: Validation rules for template field completeness, optional filter (match-all), field access requires filter, `enable`/`disable` membership, lifecycle handler signature enforcement

## Impact

- **Lexer/Parser**: 9 new keywords; `filter:` syntax changes to indented block (BREAKING); `filter:` becomes optional
- **Semantic Analyzer**: Spawn-site validation, trait toggle membership, optional filter validation, field-access-requires-filter enforcement
- **Code Generator (cpp-manual SoA backend only)**: Trait bitmask, spawn factory functions, swap-and-delete destroy, optional filter/exclude loop conditions, 3-phase load dispatch
- **Compiler CLI**: Module compilation produces `<module>_data.bin`; `unit` instances serialized, `template` instances are not
- **Standard Library**: New `std/core.cactus` shipped with the compiler providing `Persistent` and `SceneCleanup`
- **Existing examples**: `platformer.cactus`, `cactus_shop` examples — `filter: [...]` syntax must be migrated; add `use std.core` for scene cleanup
