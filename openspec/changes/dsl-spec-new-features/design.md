## Context

The Cactus DSL currently has a functional core (traits, units, systems, modules, templates) and a complete type system for data. After `dsl-spec-fixes` resolves semantic and type inconsistencies, the spec still cannot describe a real game. Three categories of game-facing functionality are entirely absent: a multi-phase update model, asset file references, and input action declarations.

This change adds three new top-level constructs (`asset`, `input`, and two new event handler names) to close the gap between a "correct but toy" DSL and one that can express a production 3D platformer.

Stakeholders: DSL users (game devs), the parser, semantic analyzer, and code-generation backends.

## Goals / Non-Goals

**Goals:**
- Define `asset` declarations: compile-time typed handles for external resource files
- Define `input` declarations: logical input action bindings (`button`/`axis`) with `std.input` query API
- Define four-phase frame update model: `on input()`, `on fixed_tick(dt)`, `on tick(dt)`, `on late_tick(dt)`
- Amend the string literal rule to allow paths inside `asset` declarations only
- Define new built-in opaque types: `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`, `InputButton`, `InputAxis`
- Create `stdlib/std/input.cactus` with enum constants and query function signatures
- Update `examples/platformer/*.cactus` to use the new constructs

**Non-Goals:**
- Compiler backend implementation (asset loader, input snapshot dispatch, fixed-step accumulator) — separate compiler changes
- Runtime or engine-level implementation
- Network replication of input state
- Mouse cursor / touchscreen / accelerometer inputs (future)

## Decisions

### Decision 1: Four named update phases instead of a general phase ordering system

**Chosen:** Four hard-coded handler names (`input`, `fixed_tick`, `tick`, `late_tick`) with a fixed execution order defined in the spec.

**Alternative A:** A user-defined priority number (`on tick(dt) @priority(10):`). Rejected — priority numbers are opaque and error-prone; games reliably need exactly these four semantic phases.

**Alternative B:** A scheduler object that systems register with at startup. Rejected — this is imperative and requires runtime scaffolding outside the DSL.

**Rationale:** The four phases map directly to established game loop literature (input → physics → update → late). Hard-coding them is a feature: users cannot accidentally create ambiguous ordering between phases, and backends can optimize dispatch knowing the phase structure at compile time.

### Decision 2: Declarative `asset` declarations with opaque typed ID handles

**Chosen:** `asset Name: type = "path"` at the top level. The name resolves to a typed opaque ID (`mesh_id`, `texture_id`, etc.) usable in trait fields and config blocks.

**Alternative A:** Allow string literals in trait `let` fields tagged with a special `@asset` attribute. Rejected — this requires attribute syntax not yet in the DSL and pollutes trait declarations with policy.

**Alternative B:** A `load_asset()` function callable from `on load()`. Rejected — this is imperative and hides the resource manifest from the compiler; static analysis of required assets becomes impossible.

**Rationale:** Declarative asset manifests let the compiler enumerate all required resources at compile time, enabling asset bundling and early missing-file errors. Opaque ID types prevent misuse (e.g., passing a `sound_id` where a `mesh_id` is expected).

### Decision 3: String literals only inside `asset` declarations (targeted exception to the const-string rule)

**Chosen:** Asset path strings are the sole exception to "strings only in const blocks." All other string literals outside `const` blocks remain a parse/semantic error.

**Rationale:** The const-string rule exists to prevent accidental string duplication and to keep string data in a single internable pool. Asset paths are fundamentally different: they are compile-time file system references, not runtime strings. They do not appear in component data or system logic; they appear exactly once in the asset declaration. The exception is narrowly scoped and does not weaken the general rule.

### Decision 4: Declarative input bindings in `input` declarations

**Chosen:** `input Name: button | axis` blocks with `key`/`mouse`/`gamepad`/`negative`/`positive`/etc. properties. Queried via `std.input` functions (`pressed()`, `down()`, `released()`, `axis()`, `axis2()`).

**Alternative:** Raw key query functions only (`input.key_down(Key.Space)`). Rejected — raw queries are not remappable, embed physical key names in game logic, and make it impossible for the DSL to describe an input configuration file.

**Rationale:** Named logical actions are standard in production game engines (Unity Input System, Unreal Enhanced Input, Godot InputMap). Declaring them in the DSL lets the backend generate remapping UI and config file I/O automatically.

## Risks / Trade-offs

- **Parser complexity** — Two new top-level grammar productions (`asset_decl`, `input_decl`) plus two new event handler names. → Mitigate by specifying exact EBNF in the spec so parser changes are unambiguous.
- **`fixed_tick` accumulator model requires backend cooperation** — The spec mandates accumulator semantics, but the DSL cannot express the timestep configuration. → The fixed timestep is a backend/runtime constant; the spec acknowledges this and defers configuration to compiler flags.
- **`std.input` stdlib dependency** — The input query API requires a new stdlib module. → The stdlib file is part of this change's deliverables. Systems using `on input()` without `use std.input` will get a semantic error from the existing undeclared-identifier rule.
- **`late_tick` cascade depth cap** — The spec restricts events fired from `on late_tick()` to be deferred to the next frame to prevent infinite loops. → This is a semantic rule enforced at runtime by the backend; the spec documents it explicitly so backends are not surprised.

## Open Questions

- Should `asset` declarations be permitted inside `template` bodies in future (to allow per-template asset overrides)? For now: no — assets are always module-level.
- Should `InputButton` and `InputAxis` be usable as trait field types (stored per-entity)? For now: yes, but the values are compile-time constants; assigning a non-input-declaration identifier to an `InputButton` field is a semantic error.
