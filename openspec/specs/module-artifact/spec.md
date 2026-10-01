## Purpose

Define the `.cmod` binary module artifact format, including serialization/deserialization of `DecoratedProgram`, public symbol extraction, versioning, and build-directory management.
## Requirements
### Requirement: Serialize DecoratedProgram to binary .cmod file
The module artifact system SHALL serialize a module's `DecoratedProgram` (resolved traits, structs, enums, dependency graph, string pool, and AST) to a binary `.cmod` file in the `build/` folder. The file path SHALL mirror the module's qualified name: module `enemies.walker` → `build/enemies.walker.cmod`.

#### Scenario: Single module serialized
- **WHEN** module `player` is compiled successfully
- **THEN** the system writes `build/player.cmod` containing the full `DecoratedProgram`

#### Scenario: Dotted module name maps to artifact path
- **WHEN** module `enemies.walker` is compiled successfully
- **THEN** the system writes `build/enemies.walker.cmod`

### Requirement: Deserialize .cmod file back to DecoratedProgram
The module artifact system SHALL load a `.cmod` file and reconstruct the `DecoratedProgram` with all resolved types, dependency graph, string pool, and AST intact. The deserialized program SHALL be identical to the original.

#### Scenario: Round-trip serialization
- **WHEN** a `DecoratedProgram` with 2 traits, 1 enum, and 3 rule dependencies is serialized then deserialized
- **THEN** the deserialized program contains the same 2 traits, 1 enum, and 3 rule dependencies with identical field data

### Requirement: Extract public symbols from .cmod artifact
The module artifact system SHALL provide a function to extract only the `pub`-marked symbols from a `.cmod` artifact into an `ImportedSymbols` struct, without loading the full AST into memory.

#### Scenario: Extract pub symbols only
- **WHEN** module `player` has `pub trait Position:` and non-pub `trait PlayerPhysics:`
- **THEN** extracting public symbols returns only `Position` in the `ImportedSymbols.traits` map

#### Scenario: All pub symbol kinds extracted
- **WHEN** a module has pub traits, pub structs, pub enums, pub events, pub funcs, and pub units
- **THEN** all pub symbol kinds are present in the extracted `ImportedSymbols`

### Requirement: Build directory management
The module artifact system SHALL create the `build/` directory if it does not exist. Existing `.cmod` files SHALL be overwritten on recompilation.

#### Scenario: Build directory created
- **WHEN** the `build/` directory does not exist and a module is compiled
- **THEN** the system creates `build/` and writes the `.cmod` file

#### Scenario: Stale artifact overwritten
- **WHEN** `build/player.cmod` already exists from a previous compilation
- **THEN** the system overwrites it with the new compilation result

### Requirement: Binary format includes version header
The `.cmod` binary format SHALL include a magic number and version byte at the start of the file. Loading a `.cmod` with an incompatible version SHALL produce a clear error.

#### Scenario: Version mismatch detected
- **WHEN** a `.cmod` file was produced by an older compiler version with a different format version
- **THEN** the system reports an error "incompatible module artifact version in 'player.cmod'; please recompile"

### Requirement: Funcs section in `.cmod` binary format
The module artifact binary format SHALL include a `funcs` section that serializes all `ResolvedFunc` entries from `DecoratedProgram.funcs`. The `CURRENT_VERSION` constant SHALL be incremented to 2 to reflect this format change. Artifacts produced with version 1 SHALL be rejected when loaded.

Binary layout addition (after existing enums section):
```
[funcs section]
  uint32: count
  for each func:
    string: name
    bool:   is_pub
    bool:   is_extern
    uint32: param_count
    for each param:
      string: name
      TypeInfo: type
    bool:   has_return_type
    TypeInfo: return_type (if has_return_type)
```

#### Scenario: Extern func round-trips through artifact save/load
- **WHEN** a module containing `pub extern func lerp(a, b, t: float) float` is saved to `.cmod` and reloaded
- **THEN** the loaded `DecoratedProgram.funcs["lerp"]` has `is_extern = true`, `is_pub = true`, and correct param/return types

#### Scenario: User func round-trips through artifact
- **WHEN** a module containing `pub func compute(x: float) float:` is saved to `.cmod` and reloaded
- **THEN** the loaded `DecoratedProgram.funcs["compute"]` has `is_extern = false`

#### Scenario: Version 1 artifact rejected
- **WHEN** a `.cmod` file with version byte `1` is loaded after this change
- **THEN** the artifact loader reports a version mismatch error and returns `nullopt`

### Requirement: Pub extern funcs in `extract_pub_symbols`
The `extract_pub_symbols` function SHALL read the funcs section and include `pub` extern funcs in the returned `ImportedSymbols.funcs` map.

#### Scenario: extract_pub_symbols includes pub extern funcs
- **WHEN** `extract_pub_symbols` is called on a `.cmod` containing `pub extern func lerp`
- **THEN** `ImportedSymbols.funcs["lerp"]` is present with `is_extern = true` and correct signature

### Requirement: Execution declarations and graph round-trip
Module artifacts SHALL serialize and deserialize external-event provenance, public phase declarations, phase dependencies and fields, canonical handler identities, per-handler domain variants, pair binding names and trait identities, binding-qualified reads, projected outputs, field-level read and write access, named-entity accesses and implicit requirements, remaining contract capabilities, explicit ordering, and handler execution-graph edges including their field-level provenance, without collapsing them into rule-level summaries. Public entities and their trait sets SHALL round-trip. Named-entity expressions in the serialized AST (template blueprints) SHALL round-trip with their canonical entity and trait identities. Rule bodies and `when:` clauses are not serialized; their named reads and implicit requirements reach the artifact through the handler contracts. The artifact format version SHALL be incremented.

#### Scenario: Handler graph survives round-trip
- **WHEN** a module containing selectionless, unary, and pair handlers is saved and loaded
- **THEN** every canonical handler, domain, pair binding, contract set, trigger kind, and graph edge is identical after loading

#### Scenario: Imported pair trait identity survives round-trip
- **WHEN** a pair binding selects a trait through a module alias
- **THEN** the loaded artifact retains its canonical trait identity without requiring the source alias

#### Scenario: Field-level access survives round-trip
- **WHEN** a module has one handler that writes `Match.over` and one that reads all fields of `Match`, and it is saved and loaded
- **THEN** the loaded contracts record the `Match.over` write and the all-fields read, and the conflict edge's field provenance is identical

#### Scenario: Named-entity access and when: survive round-trip
- **WHEN** a module has a rule with `when: not Game.Match.over` whose handler writes `Game.Match.score`, and it is saved and loaded
- **THEN** the loaded handler contract keeps the named read from `when:`, the named write, and the requirement on `(Game, Match)`, all with `Game`'s and `Match`'s canonical identities

#### Scenario: Public entities and entity values survive round-trip
- **WHEN** a module declares `pub entity Game` carrying `Match`, and a `pub template` overrides `rival = Boss`, and it is saved and loaded
- **THEN** the imported symbols list `Game` with its canonical identity and trait set, and the template blueprint's `rival` value keeps `Boss`'s canonical identity

#### Scenario: Old artifact version is rejected
- **WHEN** the linker reads an artifact predating named-entity access serialization
- **THEN** it reports an incompatible artifact version and requests recompilation

### Requirement: Public runtime symbols export through artifacts
Public external events and public phases SHALL be included in extracted imported symbols with canonical identity and the metadata needed for semantic validation by dependent modules.

#### Scenario: Imported phase metadata is available
- **WHEN** a dependent module imports a public periodic phase
- **THEN** its artifact symbols expose the phase trigger identity, fields, and cadence/completion metadata required for resolution

### Requirement: Rule dependency graph renamed in `.cmod` binary format
The module artifact binary format's serialized system-dependency section is renamed to a rule-dependency section, matching the `system` → `rule` DSL keyword rename (`SystemDependency` → `RuleDependency`). The `CURRENT_VERSION` constant SHALL be incremented to 10 to reflect this format change. Artifacts produced with version 9 or earlier SHALL be rejected when loaded.

#### Scenario: Version 9 artifact rejected
- **WHEN** a `.cmod` file with version byte `9` is loaded after this change
- **THEN** the artifact loader reports a version mismatch error and returns `nullopt`

#### Scenario: Rule dependency graph round-trips
- **WHEN** a module containing rule `after:` ordering edges is saved to `.cmod` and reloaded
- **THEN** the loaded program's rule dependency graph is identical to the original

### Requirement: Event producer records in `.cmod` binary format
The module artifact binary format SHALL serialize and deserialize typed event-producer records, preserving each producer's kind and the canonical identity of the event and consumer it relates, without collapsing non-handler producers into handler-emitted edges or discarding them. The `CURRENT_VERSION` constant SHALL be incremented to 13 to reflect this format change. Artifacts produced with version 12 or earlier SHALL be rejected when loaded.

#### Scenario: Producer kinds round-trip
- **WHEN** a program whose graph contains handler-emitted, host external-source, scheduler-boundary, and commit-synthesized producers is saved to a `.cmod` artifact and loaded back
- **THEN** each producer edge returns with its kind, canonical event identity, and consumer identity intact

#### Scenario: Old artifact version is rejected
- **WHEN** a `.cmod` file with version byte `12` is loaded after this change
- **THEN** the artifact loader reports an incompatible artifact version error and returns `nullopt`

#### Scenario: Linked program preserves producer kinds
- **WHEN** several module artifacts carrying producer records are linked into one program
- **THEN** the merged execution graph reports the same producer kinds as single-module analysis of equivalent sources

### Requirement: Trait lifecycle trigger kinds in `.cmod` binary format
The module artifact binary format SHALL serialize and deserialize the added-trigger and removed-trigger handler trigger kinds, preserving each handler's trigger kind and the canonical identity of its trait. When reading, a trigger kind value outside the known set SHALL be rejected as a malformed artifact rather than accepted. The `CURRENT_VERSION` constant SHALL be incremented to 20 to reflect this format change. Artifacts produced with version 19 or earlier SHALL be rejected when loaded.

#### Scenario: Lifecycle triggers round-trip
- **WHEN** a module declaring `on added Dying` and `on removed Burning as old` is written to `.cmod` and read back
- **THEN** both handlers keep their trigger kinds, trait identities, and canonical handler identities

#### Scenario: Unknown trigger kind rejected
- **WHEN** an artifact contains a handler trigger kind byte outside the known set
- **THEN** loading reports a malformed-artifact error instead of producing a handler

#### Scenario: Version 19 artifact rejected
- **WHEN** a version-19 `.cmod` is loaded
- **THEN** loading reports an incompatible-version error and returns no program

