## Context

Cactus DSL currently models all entities as statically named singletons (`unit Player:`, `unit WalkerEnemy1:`, etc.) that are instantiated at program start and live forever. This forces developers to enumerate every entity instance explicitly in source code, making level layouts verbose and preventing any runtime dynamism. The existing module compilation pipeline already separates code (`.cpp`) from metadata (`.cmod`), establishing a precedent for data-code separation that extends naturally to a per-module entity data file.

The language is also missing precision filtering: systems can only opt-in to traits, not opt-out. And traits are always data-bearing structs — there is no cost-free marker component.

The compiler targets the cpp-manual SoA backend. This change extends only that backend — the cpp-entt backend is out of scope.

## Goals / Non-Goals

**Goals:**
- Add `template` declarations as multi-instance blueprints, symmetric in syntax with `unit`
- Add `spawn`/`destroy` statements, symmetric with `emit`/event dispatch
- Add `load module.name` statement that treats a module as a scene (deferred to end-of-frame)
- Add `on spawn()`, `on destroy()`, `on load()` lifecycle handlers on systems
- Add marker (empty-body) traits and `trait Persistent` scene-survival semantics
- Add `enable`/`disable` trait toggle statements for per-entity runtime trait activation
- Unify `filter:` and `exclude:` to indented-list syntax matching `apply:`
- Emit a `_data.bin` file per module containing all `unit` instance data (static entity layout)

**Non-Goals:**
- Dynamic trait addition/removal (traits must be declared in `apply:` at compile time)
- Multiple simultaneous active scenes / scene stacking
- Asset streaming or async loading
- Networked scene synchronization
- GPU-side spawn/destroy

## Decisions

### D1: `template` as a top-level keyword (not a modifier on `unit`)

**Decision**: `template EnemyWalker:` is a first-class declaration, not `unit template EnemyWalker:`.

**Rationale**: Symmetric with `event`/`emit` — the pattern is: *declare type, then create instance*. `event Foo:` / `emit Foo(...)` maps cleanly to `template Foo:` / `spawn Foo(...)`. A modifier would break this symmetry and require the parser to treat `unit` differently based on a following keyword.

**Alternative considered**: `unit template EnemyWalker:` — rejected because it conflates two declaration kinds and makes the `unit` keyword context-sensitive.

### D2: Modules as scenes — `load module.name` replaces a dedicated `scene` declaration

**Decision**: There is no `scene` keyword. A module's `unit` declarations define its static entity layout. `load module.name` unloads non-persistent entities and instantiates the target module's static layout from its data file.

**Rationale**: The module system already provides namespacing, dependency tracking, and compilation units. Adding `scene` would duplicate this infrastructure. Modules-as-scenes reuse everything and keep the mental model simple: "a level is a module." The `main` module (root) acts as the persistent context; loadable modules are level modules.

**Alternative considered**: Dedicated `scene Foo: units: [...]` declaration — rejected because it introduces a new declaration kind with identical semantics to what modules already provide.

### D3: `load` is a statement that fires `on load()` after entity instantiation (deferred to end-of-frame)

**Decision**: `load levels.level1` is a statement usable inside system handlers. It is deferred to the end of the current frame. After entity instantiation, `on load()` fires on all active systems.

**Rationale**: Mid-frame scene transitions would invalidate iterators in systems that are currently executing. Deferring to end-of-frame is the standard game engine approach (Unity, Godot). `on load()` as a distinct lifecycle event separates "entity ready" (`on spawn()`) from "level fully loaded and all entities ready" (`on load()`).

**Alternative considered**: Immediate execution — rejected due to iterator invalidation risk during system traversal.

### D4: Trait enable/disable — static membership, dynamic activation via per-entity bitmask

**Decision**: An entity's trait set is fixed at spawn time (defined by `apply:`). `enable`/`disable` statements toggle whether a trait is *active* (visible to system filters) using a **per-entity trait active bitmask**. Each trait in the program is assigned a unique bit index at compile time. The bitmask is stored as part of each entity's record in the SoA storage. System filters are compiled to bitmask predicates: a filter matches when `(entity.trait_mask & filter_mask) == filter_mask` and `(entity.trait_mask & exclude_mask) == 0`.

**Rationale**: A bitmask is the most cache-friendly and branch-free way to represent per-entity trait activation in a SoA layout. System iteration becomes a single bitwise AND per entity. The bitmask approach is also natural for `destroy`/scene cleanup: sweep the entity array and test the `Persistent` bit. Disable/enable are single `|=` / `&= ~` bitmask operations — zero overhead beyond the flag flip.

**Alternative considered**: Full dynamic add/remove — rejected as too complex and inconsistent with GPU-target safety constraints. EnTT-style component storage — not needed; we target the SoA backend only.

### D5: `destroy` uses swap-and-delete for O(1) entity removal

**Decision**: When an entity is destroyed (via `destroy` or scene cleanup), its slot in the SoA arrays is filled by moving the *last* entity in the array into that slot, then decrementing the count. This is the classic "swap-and-delete" (or "swap-and-pop") technique.

**Rationale**: SoA arrays must stay packed for cache-efficient iteration. Leaving gaps would require tracking free slots and complicating the iteration loop. Swap-and-delete maintains the invariant that all `[0, count)` slots are valid, and deletion is O(1) regardless of entity count. The cost is that entity ordering is not preserved after a deletion — which is acceptable since Cactus systems do not expose or depend on entity ordering.

**Alternative considered**: Free-list (mark slot free, fill from free list on next spawn) — rejected because it complicates the iteration loop (must skip free slots) and undermines cache locality.

### D7: `exclude:` uses indented-list syntax; `filter:` migrates to same syntax

**Decision**: Both `filter:` and `exclude:` use indented trait lists, matching `apply:`. The old `filter: [A, B]` bracket syntax is removed (**BREAKING**).

**Rationale**: Consistency. `apply:`, `config:`, `child:` all use indented blocks. Having `filter:` use bracket syntax was already an inconsistency. Unifying all list-style clauses to indented blocks makes the language visually coherent.

**Alternative considered**: Keep brackets for `filter:`, add brackets for `exclude:` — rejected because it perpetuates the inconsistency.

### D8: Static unit data serialized to `_data.bin` per module

**Decision**: Each compiled module produces `<module_name>_data.bin` alongside generated `.cpp`. The data file contains all `unit` instance field values and initial trait states. At runtime, `load` reads this file to instantiate entities without executing generated init code per-entity.

**Rationale**: Separates static layout data from executable code, enabling level data to be shipped independently. Enables fast level-switching (read data file → instantiate) without relying on generated C++ initializers. `template` declarations have no data file entry — they are purely compile-time blueprints instantiated via `spawn` at runtime.

**Alternative considered**: Embed unit data as static C++ arrays in generated code — rejected because it couples level layout to compilation and prevents runtime data file swapping.

## Risks / Trade-offs

- **[Risk] Breaking filter syntax** → All existing `.cactus` files using `filter: [...]` must be migrated to indented form. Mitigation: the compiler emits a clear error message pointing to the old syntax with a fix suggestion; examples are updated as part of this change.

- **[Risk] `enable`/`disable` scope is always "current entity"** → Systems cannot toggle traits on other entities directly; they must `emit` an event that the target entity's system handles. This is intentional (maintains ECS message-passing discipline) but may surprise users. Mitigation: document clearly with examples.

- **[Risk] Deferred `load` timing** → If two systems both call `load` in the same frame, this is a programming error (ambiguous intent). Mitigation: the runtime SHALL detect multiple deferred `load` calls in the same frame and report a runtime error: "multiple `load` calls in a single frame; only one `load` per frame is allowed."

- **[Risk] `_data.bin` format versioning** → Data file format changes require re-compilation of all modules. Mitigation: embed a format version header in the data file; compiler rejects mismatched files with a clear error.

- **[Risk] Lifecycle handler ordering** → Order in which `on spawn()` / `on load()` fire across multiple systems is deterministic only if systems are processed in declaration order. Mitigation: define spec: lifecycle handlers fire in the order systems are declared in source, matching `on tick()` ordering.

### D9: `filter:` and `exclude:` are both optional — no filter means match all entities

**Decision**: Both `filter:` and `exclude:` are optional on system declarations. A system with no `filter:` block matches all entities (filter_mask = 0). A system with no `exclude:` excludes nothing. A system may have only `exclude:` (no `filter:`), making it useful for global lifecycle handlers like scene cleanup. Field access within handler bodies is only valid for traits listed in `filter:`.

**Rationale**: This eliminates the need for a universal base marker (`trait Entity`). The bitmask implementation handles it naturally: filter_mask = 0 means `(trait_mask & 0) == 0` is always true, so no filter check is needed. A no-filter system with only `exclude:` + lifecycle handlers is the ideal shape for `std.SceneCleanup`. Previously "empty filter is an error" is replaced by "no filter = match all."

**Alternative considered**: Keeping filter required, adding a `trait Entity` auto-applied base marker — rejected because optional filter achieves the same goal with simpler language rules.

### D10: `Persistent` and `SceneCleanup` live in the `std.core` module; `load` runtime is filter-unaware

**Decision**: `trait Persistent` and `system SceneCleanup` are defined in a standard library module `std.core`. They are explicitly imported with `use std.core`. The `load` runtime operation performs three phases in order: (1) emit `on unload()` to all active systems, (2) instantiate new entities from data file + emit `on spawn()` per entity, (3) emit `on load()` to all active systems. The runtime has zero knowledge of `Persistent` — cleanup is entirely handled by `std.SceneCleanup.on_unload()`. A new `on unload()` lifecycle handler complements `on spawn()`, `on destroy()`, and `on load()`.

**Rationale**: Expressing cleanup as Cactus code in `std` rather than runtime magic makes the behavior visible, overridable, and consistent with the language's own rules. Users who don't import `std.core` get no automatic cleanup (useful for custom scene management). Two-phase lifecycle (`on unload` before instantiation, `on load` after) ensures cleanup runs before new entities arrive, preventing accidental destruction of freshly spawned entities.

**Alternative considered**: Runtime magic cleanup with `Persistent` as a compiler-reserved keyword — rejected because it bakes policy into the implementation and can't be overridden.

## Resolved Decisions

- **Spawn overrides**: `spawn` can override **any** field of the template — including fields that already have a `config:` default. The caller may override as many or as few fields as desired. Unset fields (no `config:` default and not provided at spawn) are a compile error.

- **`_data.bin` wire format**: Flat binary optimized for fast sequential load/save. Format: magic bytes (`CDAT`) + format version (uint16) + entity count (uint32), followed by entity records packed in declaration order. Each record: archetype name length + name bytes + packed field values in declaration order (fixed-width types, no padding between fields) + trait active bitmask (uint64). No offsets table — sequential read only. This maximizes read speed (single `fread` of the whole file).

- **`load` scope**: `load` is valid inside **any** system event handler, including handlers in the root module (`main.cactus`). The root module's `unit`s are always present (they're the persistent context) — `load` only affects the non-persistent entity set.

- **Multiple `load` calls per frame**: If two or more `load` calls are deferred in the same frame, the runtime reports an error: "multiple `load` calls in a single frame — only one `load` per frame is allowed." This is a programming error, not a silently handled edge case.
