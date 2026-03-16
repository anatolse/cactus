## ADDED Requirements

### Requirement: Merge multiple DecoratedPrograms from .cmod artifacts
The program linker SHALL load per-module `.cmod` artifacts from the `build/` folder and merge all `DecoratedProgram` instances into a single combined `DecoratedProgram` suitable for code generation. Traits, structs, enums, dependency graphs, and string pools SHALL all be merged. Each artifact is loaded one at a time to avoid holding all modules in memory simultaneously during the merge.

#### Scenario: Two modules with distinct types
- **WHEN** `build/player.cmod` defines trait `Position` and `build/enemies.cmod` defines trait `EnemyAI`
- **THEN** the merged program contains both `Position` and `EnemyAI` in its traits map

#### Scenario: String pools merged
- **WHEN** module `player` has const `"Cactus Game"` and module `shop` has const `"Buy Item"`
- **THEN** the merged program's string pool contains both interned strings

### Requirement: Duplicate symbol detection
The program linker SHALL detect when two modules export the same pub symbol name into the same importing scope and report it as an error. Modules with the same filename in different folders (e.g., `enemies.physics` and `lib.physics`) are distinct modules with distinct qualified names — they only conflict if both are imported into the same module and export the same pub symbol name.

#### Scenario: Duplicate trait name from different modules
- **WHEN** module `A` defines `pub trait Position:` and module `B` also defines `pub trait Position:`, and both are imported into module `C`
- **THEN** the linker reports an error "duplicate symbol 'Position' defined in module A and module B"

#### Scenario: Same-name across different kinds
- **WHEN** module `A` defines `pub struct Item:` and module `B` defines `pub trait Item:`
- **THEN** the linker reports an error "duplicate symbol 'Item' defined in module A and module B"

#### Scenario: Same filename in different folders, no conflict
- **WHEN** `enemies/physics.cactus` defines `pub trait EnemyPhysics:` and `lib/physics.cactus` defines `pub trait RigidBody:`, and both are imported
- **THEN** the linker accepts both — no conflict since the pub symbol names are different

### Requirement: Pub visibility enforcement
The program linker SHALL verify that all cross-module symbol references target symbols marked as `pub`. Referencing a non-pub symbol from another module SHALL produce an error with a helpful diagnostic.

#### Scenario: Pub trait used cross-module
- **WHEN** module `player` declares `pub trait Position:` and module `enemies` uses `Position` in a filter
- **THEN** the linker accepts the cross-module reference

#### Scenario: Non-pub trait used cross-module
- **WHEN** module `player` declares `trait PlayerPhysics:` (no `pub`) and module `enemies` references `PlayerPhysics`
- **THEN** the linker reports an error "trait 'PlayerPhysics' is not public in module 'player'; did you mean to mark it as 'pub'?"

### Requirement: Declaration ordering in merged output
The program linker SHALL order declarations in the merged program such that all dependencies come before their dependents: enums and structs first, then traits, then events, then units, then systems and funcs. Within each category, declarations from dependency modules SHALL precede those from dependent modules.

#### Scenario: Cross-module trait dependency
- **WHEN** module `enemies` has a system with `filter: [Position, EnemyAI]` where `Position` is from module `player`
- **THEN** the merged program lists `Position` before `EnemyAI` and both before the enemy system

### Requirement: Combined AST for codegen
The program linker SHALL produce a combined `ProgramNode` containing all declarations from all modules in dependency order. This combined AST SHALL be assigned to the merged `DecoratedProgram.ast` field for backend consumption.

#### Scenario: Merged AST contains all declarations
- **WHEN** 3 modules each have traits, systems, and events
- **THEN** the merged `ProgramNode.declarations` vector contains all declarations from all 3 modules

### Requirement: Const block merging
The program linker SHALL merge all const blocks from all modules. Duplicate const names across modules SHALL produce an error.

#### Scenario: Distinct constants merged
- **WHEN** module `player` has `MOVE_SPEED = 6.0` and module `level` has `TILE_SIZE = 32`
- **THEN** the merged program's const pool contains both constants

#### Scenario: Duplicate constant name
- **WHEN** module `A` has `MAX_HEALTH = 100` and module `B` also has `MAX_HEALTH = 200`
- **THEN** the linker reports an error "duplicate constant 'MAX_HEALTH' defined in module A and module B"
