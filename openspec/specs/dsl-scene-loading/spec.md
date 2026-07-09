## Requirements

### Requirement: `load` statement performs a three-phase scene transition
The language SHALL support a `load` statement inside system event handlers. `load module.name` designates the named module as the next active scene. The transition is deferred to the end of the current frame. The three phases are:

1. **Unload phase**: `on unload()` fires on all active systems.
2. **Instantiate phase**: All `entity` declarations in the target module are instantiated from `_data.bin`; `on spawn()` fires per new entity. `template` declarations become available for `spawn` and template-backed entity construction.
3. **Load phase**: `on load()` fires on all active systems.

#### Scenario: Load transitions to target module at end of frame
- **WHEN** `load levels.level1` executes mid-frame
- **THEN** the transition is deferred until after all systems finish processing the current frame

#### Scenario: Multiple load calls in same frame — runtime error
- **WHEN** two separate systems both call `load` in the same frame
- **THEN** the runtime SHALL report an error: "multiple `load` calls in a single frame — only one `load` per frame is allowed"

#### Scenario: Load triggers on unload() then instantiates new entities
- **WHEN** `load levels.level2` transitions to a new module
- **THEN** `on unload()` fires first; then new entities are instantiated; then `on load()` fires

#### Scenario: Load instantiates target module's entities
- **WHEN** `load levels.level1` completes
- **THEN** all inline and template-backed `entity` declarations in `levels.level1` are instantiated as new entities

#### Scenario: Load outside event handler (invalid)
- **WHEN** `load` appears at module top-level or inside a `func` body
- **THEN** the compiler SHALL report an error: "`load` only allowed inside system event handlers"

#### Scenario: Load of unknown module
- **WHEN** `load some.unknown` references a module not reachable via `use` declarations
- **THEN** the compiler SHALL report an error: "unknown module 'some.unknown'"

### Requirement: `on load()` lifecycle handler on systems
Systems MAY declare an `on load():` handler. This handler fires once after every `load` transition completes — after all non-persistent entities are removed, all target module entities are instantiated, and `on spawn()` handlers have fired.

#### Scenario: On load fires after all spawn handlers complete
- **WHEN** a `load` transition completes
- **THEN** `on load()` fires after `on spawn()` has fired for all newly instantiated entities

#### Scenario: On load fires even when loaded module has no entities
- **WHEN** a module with no `entity` declarations is loaded
- **THEN** `on load()` still fires (there are just no entities created)

### Requirement: Modules as scenes — execution model
A module's `entity` declarations SHALL define its static entity layout. The root (`main.cactus`) module's `entity` declarations SHALL always be instantiated at program start and SHALL never be unloaded. All other modules' entities SHALL exist only when that module is the active loaded module.

#### Scenario: Root module entities always exist
- **WHEN** the program starts
- **THEN** all `entity` declarations in the root module are instantiated immediately

#### Scenario: Template declarations do not auto-instantiate on load
- **WHEN** `load levels.level1` transitions to `levels.level1`
- **THEN** `template` declarations in `levels.level1` are NOT automatically instantiated; only `entity` declarations are

### Requirement: Module data file contains static unit instance data
The compiler SHALL produce a `<module>_data.bin` binary file for each compiled module containing all inline and template-backed `entity` instance configurations. At runtime, `load` reads this data file to instantiate entities. `template` declarations produce no entries in the data file except insofar as their flattened data is used by template-backed entities.

#### Scenario: Data file produced per module
- **WHEN** a module containing `entity` declarations is compiled
- **THEN** a `<module_name>_data.bin` file is emitted alongside the generated `.cpp` file

#### Scenario: Data file version mismatch rejected
- **WHEN** the runtime attempts to load a `_data.bin` file compiled with a different format version
- **THEN** the runtime SHALL reject the file and report a clear error: "data file version mismatch: expected <N>, got <M>"

### Requirement: `on unload()` lifecycle handler fires before scene instantiation
Systems MAY declare an `on unload():` handler. This handler fires during the first phase of a `load` transition — before any new entities are instantiated from the data file.

#### Scenario: on unload fires before new entities are created
- **WHEN** `load levels.level2` is triggered
- **THEN** `on unload()` fires while only the old scene's entities exist (no new entities yet)

#### Scenario: on unload runs in systems matching current entities
- **WHEN** a system with `exclude: std.Persistent` has `on unload()` with `destroy`
- **THEN** all non-persistent entities are destroyed before the new scene loads

### Requirement: `std.core` module provides `Persistent` and `SceneCleanup`
The standard library module `std.core` SHALL export `pub trait Persistent` (a marker trait) and `pub system SceneCleanup` (an exclude-Persistent system that destroys non-persistent entities on `on unload()`). Projects import this with `use std.core`.

#### Scenario: Project with std.core — non-persistent entities cleaned up on load
- **WHEN** `use std.core` is imported and `load levels.level2` fires
- **THEN** `std.SceneCleanup.on_unload()` destroys all entities without `Persistent` active

#### Scenario: Project without std.core — no automatic cleanup
- **WHEN** `std.core` is NOT imported and `load levels.level2` fires
- **THEN** no entities are destroyed; the developer is responsible for custom cleanup

#### Scenario: Disabled Persistent does not protect
- **WHEN** an entity has `Persistent` in its `apply:` block but `Persistent` is currently disabled
- **THEN** `SceneCleanup`'s `exclude: Persistent` no longer excludes it; the entity IS destroyed on unload
