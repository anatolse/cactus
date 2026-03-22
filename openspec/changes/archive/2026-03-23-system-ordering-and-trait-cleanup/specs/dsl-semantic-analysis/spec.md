## REMOVED Requirements

### Requirement: Trait handler processing
**Reason**: Trait bodies no longer accept event handlers or func declarations. The parser rejects them before semantic analysis. No semantic validation of trait-level handlers is needed.
**Migration**: Move any logic from trait handlers into a dedicated `system` declaration with an appropriate `filter:` clause.

## ADDED Requirements

### Requirement: `after:` system name resolution
The semantic analyzer SHALL resolve each system name in an `after:` clause against the set of all declared systems in the current `DecoratedProgram` (which includes all linked modules). If a name does not resolve to a system declaration, the analyzer SHALL report an error.

#### Scenario: Known system name resolves
- **WHEN** `after: MovementSystem` is declared and `system MovementSystem:` is present in the linked program
- **THEN** the semantic analyzer records the ordering edge without error

#### Scenario: Unknown system name rejected
- **WHEN** `after: GhostSystem` is declared and no system named `GhostSystem` exists
- **THEN** the analyzer reports: "unknown system 'GhostSystem' in after clause"

#### Scenario: Trait name used in `after:` is rejected
- **WHEN** `after: Position` is declared and `Position` is a trait, not a system
- **THEN** the analyzer reports: "'Position' is not a system"

### Requirement: `after:` cycle detection
The semantic analyzer SHALL run a depth-first cycle detection algorithm over the combined system ordering graph (declaration order within a module, plus all explicit `after:` edges). Any cycle SHALL be reported as a compile error that includes the cycle path.

#### Scenario: Direct cycle rejected
- **WHEN** `system A: after: B` and `system B: after: A`
- **THEN** the analyzer reports an error including the cycle: "cycle in system ordering: A → B → A"

#### Scenario: Indirect cycle rejected
- **WHEN** A → B → C → A transitively via `after:` declarations
- **THEN** the analyzer reports the cycle path: "cycle in system ordering: A → B → C → A"

#### Scenario: No cycle in linear chain
- **WHEN** `system C: after: B`, `system B: after: A`, and A has no `after:`
- **THEN** the analyzer accepts all three systems

### Requirement: `after:` edges stored in SystemInfo within DecoratedProgram
The semantic analyzer SHALL populate the `after_systems` field of each `SystemInfo` in `DecoratedProgram.systems` with the list of system names from validated `after:` clauses.

#### Scenario: `after_systems` populated for systems with `after:` clause
- **WHEN** `system UI: after: Scene` is analyzed
- **THEN** `DecoratedProgram` contains `SystemInfo` for `UI` with `after_systems = ["Scene"]`

#### Scenario: `after_systems` is empty for systems without `after:` clause
- **WHEN** a system has no `after:` clause
- **THEN** `SystemInfo.after_systems` is an empty vector

## ADDED Requirements (config/spawn qualification)

### Requirement: `apply:` alias uniqueness validation
The semantic analyzer SHALL verify that no two `apply:` entries in the same unit or template declare the same alias. An alias colliding with another alias or with the bare trait name of another entry SHALL produce a compile error.

#### Scenario: Duplicate alias rejected
- **WHEN** `apply:` contains `Position as p` and `Velocity as p` in the same unit
- **THEN** the analyzer reports: "duplicate alias 'p' in apply block"

#### Scenario: Alias colliding with trait name of another entry rejected
- **WHEN** `apply:` contains `Position as Health` and `Health` is also applied
- **THEN** the analyzer reports: "alias 'Health' conflicts with applied trait name 'Health'"

### Requirement: Qualified `config:` key resolution
The semantic analyzer SHALL resolve each `config:` key against the applied traits of the enclosing unit or template. Bare keys are resolved by searching all applied traits for a matching field name. Dotted keys resolve the first component as an alias or trait name, then the second as a field of that trait.

#### Scenario: Bare key resolved unambiguously
- **WHEN** only `Health` has a field `health` and `config:` contains bare `health = 100`
- **THEN** the key resolves to `Health.health`

#### Scenario: Ambiguous bare key produces error
- **WHEN** both `TraitA` and `TraitB` have a field `pos` and `config:` contains bare `pos = ...`
- **THEN** the analyzer reports: "ambiguous field 'pos' in config; qualify as 'TraitA.pos' or 'TraitB.pos'"

#### Scenario: Dotted key with valid alias resolves
- **WHEN** `apply:` has `Position as p` and `config:` contains `p.position = vec3(...)`
- **THEN** the key resolves to `Position.position`

#### Scenario: Dotted key with trait name (implicit alias) resolves
- **WHEN** `apply:` has `Health` (no alias) and `config:` contains `Health.health = 100`
- **THEN** the key resolves to `Health.health`

#### Scenario: Unknown first component in dotted key rejected
- **WHEN** `config:` contains `Unknown.field = 5` and `Unknown` is not an alias or applied trait
- **THEN** the analyzer reports: "unknown trait or alias 'Unknown' in config key"

#### Scenario: Unknown field in qualified key rejected
- **WHEN** `config:` contains `Health.notafield = 5` and `Health` has no field `notafield`
- **THEN** the analyzer reports: "trait 'Health' has no field 'notafield'"

### Requirement: Qualified `spawn()` override argument key resolution
The semantic analyzer SHALL resolve `spawn` override argument keys using the same rules as `config:` key resolution, but against the template's applied traits rather than a unit's.

#### Scenario: Bare spawn key resolved when unambiguous
- **WHEN** `spawn Enemy(patrol_speed = 5.0)` and only `EnemyAI` has `patrol_speed`
- **THEN** the key resolves to `EnemyAI.patrol_speed`

#### Scenario: Ambiguous bare spawn key produces error
- **WHEN** two of a template's applied traits both have a field `speed` and `spawn Foo(speed = 1.0)` uses bare form
- **THEN** the analyzer reports: "ambiguous field 'speed' in spawn override; qualify as 'TraitA.speed' or 'TraitB.speed'"

#### Scenario: TraitName-qualified spawn key resolves
- **WHEN** `spawn Enemy(EnemyAI.patrol_speed = 5.0)` is used
- **THEN** the key resolves to `EnemyAI.patrol_speed`
