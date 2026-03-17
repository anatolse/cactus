## ADDED Requirements

### Requirement: `load` statement performs a three-phase scene transition
The language SHALL support a `load` statement inside system event handlers. `load module.name` designates the named module as the next active scene. The transition is deferred to the end of the current frame. The runtime has **no knowledge of `Persistent`** — cleanup is delegated to systems via `on unload()`. The three phases are:

1. **Unload phase**: `on unload()` fires on all active systems. Systems such as `std.SceneCleanup` use this to destroy non-persistent entities.
2. **Instantiate phase**: All `unit` declarations in the target module are instantiated from `_data.bin`; `on spawn()` fires per new entity. `template` declarations become available for `spawn`.
3. **Load phase**: `on load()` fires on all active systems. Level-setup systems use this to spawn template instances and configure state.

```
system GameManager:
    filter:
        GameState

    on LevelComplete():
        load levels.level2

    on PlayerDied(lives: int):
        if lives <= 0:
            load ui.game_over
        else:
            load levels.level1
```

#### Scenario: Load transitions to target module at end of frame
- **WHEN** `load levels.level1` executes mid-frame
- **THEN** the transition does NOT happen immediately; it is deferred until after all systems finish processing the current frame

#### Scenario: Multiple load calls in same frame — runtime error
- **WHEN** two separate systems both call `load` in the same frame
- **THEN** the runtime SHALL report an error: "multiple `load` calls in a single frame — only one `load` per frame is allowed"

#### Scenario: Load triggers on unload() then instantiates new entities
- **WHEN** `load levels.level2` transitions to a new module
- **THEN** `on unload()` fires first; systems such as `std.SceneCleanup` may destroy entities during this phase; then new entities are instantiated

#### Scenario: Load instantiates target module's units
- **WHEN** `load levels.level1` completes
- **THEN** all `unit` declarations in `levels.level1` are instantiated as new entities

#### Scenario: Load valid in any system handler including root module
- **WHEN** `load levels.level1` appears inside a system handler in the root module (`main.cactus`)
- **THEN** it is valid; the root module's own `unit` entities are unaffected (they are persistent by nature of being in the root module)

#### Scenario: Load outside event handler (invalid)
- **WHEN** `load` appears at module top-level or inside a `func` body
- **THEN** the compiler SHALL report an error: "`load` only allowed inside system event handlers"

#### Scenario: Load of unknown module
- **WHEN** `load some.unknown` references a module not reachable via `use` declarations
- **THEN** the compiler SHALL report an error: "unknown module 'some.unknown'"

---

### Requirement: `on load()` lifecycle handler on systems
Systems MAY declare an `on load():` handler. This handler fires once after every `load` transition completes — after all non-persistent entities are removed, all target module entities are instantiated, and `on spawn()` handlers have fired. `on load()` is the correct place for level setup scripting (spawning template instances, configuring initial state).

```
system LevelSetup:
    filter:
        LevelState

    on load():
        spawn Enemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0, patrol_max_x = 550.0)
        spawn Enemy(pos = vec2(800.0, 568.0), patrol_min_x = 700.0, patrol_max_x = 1000.0)
        spawn Gem(pos = vec2(300.0, 410.0))
```

#### Scenario: On load fires after all spawn handlers complete
- **WHEN** a `load` transition completes
- **THEN** `on load()` fires after `on spawn()` has fired for all newly instantiated entities

#### Scenario: On load in persistent system (always-present system)
- **WHEN** a system in the root module has `on load()`
- **THEN** that handler fires on every `load` transition, regardless of which module is loaded

#### Scenario: On load fires even when loaded module has no units
- **WHEN** a module with no `unit` declarations is loaded
- **THEN** `on load()` still fires (there are just no entities created)

---

### Requirement: Modules as scenes — execution model
A module's `unit` declarations define its static entity layout. When a module is the active loaded module, its `unit` entities exist in the world. The root (`main.cactus`) module's `unit` declarations are always instantiated at program start and are never unloaded. All other modules' units exist only when that module is the active loaded module.

#### Scenario: Root module units always exist
- **WHEN** the program starts
- **THEN** all `unit` declarations in the root module are instantiated immediately

#### Scenario: Non-root module units require load
- **WHEN** a module `levels.level1` is compiled but `load levels.level1` has not been called
- **THEN** no entities from `levels.level1` exist in the world

#### Scenario: Template declarations do not auto-instantiate on load
- **WHEN** `load levels.level1` transitions to `levels.level1`
- **THEN** `template` declarations in `levels.level1` are NOT automatically instantiated; only `unit` declarations are

---

### Requirement: Module data file contains static unit instance data
The compiler SHALL produce a `<module>_data.bin` binary file for each compiled module. This file contains all `unit` instance configurations (field values and initial trait active/disabled states) from that module. At runtime, `load` reads this data file to instantiate entities. `template` declarations produce no entries in the data file.

#### Scenario: Data file produced per module
- **WHEN** a module containing `unit` declarations is compiled
- **THEN** a `<module_name>_data.bin` file is emitted alongside the generated `.cpp` file

#### Scenario: Template not in data file
- **WHEN** a module containing only `template` declarations (no `unit`) is compiled
- **THEN** the `_data.bin` file is empty (or not emitted)

#### Scenario: Data file version mismatch rejected
- **WHEN** the runtime attempts to load a `_data.bin` file compiled with a different format version
- **THEN** the runtime SHALL reject the file and report a clear error: "data file version mismatch: expected <N>, got <M>"

---

### Requirement: `on unload()` lifecycle handler fires before scene instantiation
Systems MAY declare an `on unload():` handler. This handler fires during the **first phase** of a `load` transition — before any new entities are instantiated from the data file. It is the correct place for scene teardown: destroying non-persistent entities, saving state, emitting departure effects.

#### Scenario: on unload fires before new entities are created
- **WHEN** `load levels.level2` is triggered
- **THEN** `on unload()` fires while only the old scene's entities exist (no new entities yet)

#### Scenario: on unload runs in systems matching current entities
- **WHEN** a system with `exclude: std.Persistent` has `on unload()` with `destroy`
- **THEN** all non-persistent entities are destroyed before the new scene loads

#### Scenario: on unload in systems with no filter fires for all entities
- **WHEN** a system with no `filter:`, `exclude: std.Persistent`, and `on unload(): destroy` is active
- **THEN** all entities without `Persistent` active are destroyed

---

### Requirement: `std.core` module provides `Persistent` and `SceneCleanup`
The standard library module `std.core` SHALL export `trait Persistent` (a marker trait) and `system SceneCleanup` (a no-filter system that destroys non-persistent entities on `on unload()`). Projects import this with `use std.core`. Without `use std.core`, no automatic scene cleanup occurs.

```
# std/core.cactus — standard library (shipped with the compiler)
module std.core

pub trait Persistent    # marker — entity survives scene unload when SceneCleanup is active

pub system SceneCleanup:
    exclude:
        Persistent

    on unload():
        destroy
```

Usage in user code:
```
module main

use std.core            # brings in Persistent + SceneCleanup

pub unit Player:
    apply:
        std.Persistent  # or unqualified: Persistent (if unique)
        Position
        Health
    config:
        pos = vec2(100.0, 300.0)
        health = 100
```

#### Scenario: Project with std.core — non-persistent entities cleaned up on load
- **WHEN** `use std.core` is imported and `load levels.level2` fires
- **THEN** `std.SceneCleanup.on_unload()` destroys all entities without `Persistent` active

#### Scenario: Project without std.core — no automatic cleanup
- **WHEN** `std.core` is NOT imported and `load levels.level2` fires
- **THEN** no entities are destroyed; the developer is responsible for custom cleanup

#### Scenario: Persistent entity destroyed by explicit destroy regardless
- **WHEN** `destroy` is explicitly called on an entity with `Persistent` active
- **THEN** the entity IS removed — `Persistent` only prevents cleanup by `SceneCleanup`, not explicit `destroy`

#### Scenario: Disabled Persistent does not protect
- **WHEN** an entity has `Persistent` in its `apply:` block but `Persistent` is currently `disable`d
- **THEN** `SceneCleanup`'s `exclude: Persistent` no longer excludes it; the entity IS destroyed on unload
