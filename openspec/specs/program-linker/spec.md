## Requirements

### Requirement: Merge multiple DecoratedPrograms from .cmod artifacts
The program linker SHALL load per-module `.cmod` artifacts from the `build/` folder and merge all `DecoratedProgram` instances into a single combined semantic program suitable for code generation. Traits, structs, enums, funcs, events, templates, systems, dependency graphs, and string pools SHALL be merged using typed canonical symbol identities. Each artifact is loaded one at a time to avoid holding all module artifacts in memory simultaneously during the merge.

#### Scenario: Two modules with distinct types
- **WHEN** `build/player.cmod` defines trait `player.Position` and `build/enemies.cmod` defines trait `enemies.EnemyAI`
- **THEN** the merged program contains both trait symbol identities

#### Scenario: Same local trait name from different modules merged
- **WHEN** `build/flat.cmod` defines trait `flat.WorldTransform` and `build/volume.cmod` defines trait `volume.WorldTransform`
- **THEN** the merged program contains both trait symbol identities without overwriting either declaration

#### Scenario: String pools merged
- **WHEN** module `player` has const `"Cactus Game"` and module `shop` has const `"Buy Item"`
- **THEN** the merged program's string pool contains both interned strings

### Requirement: Duplicate symbol detection
The program linker SHALL detect duplicate canonical symbol identities and report them as errors. Same local names from different modules SHALL NOT be linker duplicates. Cross-kind same-local declarations within one module are invalid under the one-namespace rule and SHALL be rejected before or during linking if they appear in loaded artifacts.

#### Scenario: Duplicate canonical trait identity rejected
- **WHEN** two artifacts both define trait symbol identity `game.Position`
- **THEN** the linker reports a duplicate canonical symbol error for `game.Position`

#### Scenario: Same local trait name from different modules accepted
- **WHEN** module `A` defines `pub trait Position:` and module `B` also defines `pub trait Position:`, and both are linked
- **THEN** the linker accepts both because their canonical identities are `A.Position` and `B.Position`

#### Scenario: Same-name across different kinds in one module rejected
- **WHEN** loaded artifacts for module `A` contain both `pub struct Item:` and `pub trait Item:`
- **THEN** the linker reports a duplicate module-scope symbol for canonical name `A.Item`

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
- **WHEN** module `enemies` has a system with `filter:` including `Position` and `EnemyAI` where `Position` is from module `player`
- **THEN** the merged program lists `Position` before `EnemyAI` and both before the enemy system

### Requirement: Resolved program for code generation
The program linker SHALL provide code generation with a merged resolved semantic representation whose module-scope references are already resolved to typed symbol identities. Code generation MUST NOT require a combined raw AST to resolve imported symbols, aliases, or module qualifiers.

#### Scenario: Merged resolved systems retain declaring module identity
- **WHEN** modules `A` and `B` both declare a system named `Update`
- **THEN** the merged resolved program provides distinct system identities `A.Update` and `B.Update` to code generation

#### Scenario: Codegen does not need UseNode lookup
- **WHEN** a module references an imported trait through alias `phys.Body`
- **THEN** the linked resolved program exposes the referenced trait identity to code generation without requiring codegen to inspect the original `use std.physics.flat as phys` declaration

### Requirement: Const block merging
The program linker SHALL merge all const blocks from all modules. Duplicate const names across modules SHALL produce an error.

#### Scenario: Distinct constants merged
- **WHEN** module `player` has `MOVE_SPEED = 6.0` and module `level` has `TILE_SIZE = 32`
- **THEN** the merged program's const pool contains both constants

#### Scenario: Duplicate constant name
- **WHEN** module `A` has `MAX_HEALTH = 100` and module `B` also has `MAX_HEALTH = 200`
- **THEN** the linker reports an error "duplicate constant 'MAX_HEALTH' defined in module A and module B"
