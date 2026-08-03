## Purpose
Define the rules for typed symbol identity in the Cactus compiler: how every module-scope declaration is identified, how canonical strings are derived from that identity, and where semantic resolution must complete so that linking and code generation operate on typed symbol IDs rather than source-spelled strings.

## Requirements

### Requirement: Typed module symbol identity
The compiler SHALL represent every module-scope declaration identity as a typed symbol identifier containing the declaration kind, declaring module, and local declaration name. A canonical string such as `std.physics.flat.Body` SHALL be derived from this typed identity for diagnostics, artifacts, and generated-name derivation, but semantic and backend logic MUST NOT recover identity by parsing source-spelled strings.

#### Scenario: Same local name in different modules has distinct identity
- **WHEN** module `std.transform.flat` declares `pub trait WorldTransform` and module `std.transform.volume` declares `pub trait WorldTransform`
- **THEN** semantic analysis represents them as distinct trait `SymbolId`s with different declaring modules

#### Scenario: Canonical string derived from typed identity
- **WHEN** a trait symbol has module `std.physics.flat` and local name `Body`
- **THEN** its diagnostic/artifact canonical string is `std.physics.flat.Body`

### Requirement: Explicit module identity
Every Cactus source file SHALL declare exactly one explicit module name. The compiler SHALL use that module name as the declaring module for every module-scope declaration in the file and SHALL NOT create empty-module canonical identities.

#### Scenario: Declaration receives explicit module identity
- **WHEN** source file declares `module game.player` and then `trait Health`
- **THEN** the trait is assigned a symbol identity with module `game.player` and local name `Health`

#### Scenario: Empty module identity is never produced
- **WHEN** a source file is successfully parsed and semantically analyzed
- **THEN** every module-scope symbol identity in the resulting semantic representation has a non-empty declaring module

### Requirement: One namespace per module
Each module SHALL have one local namespace for all module-scope declaration kinds. The compiler SHALL reject duplicate local names within the same module even when the declarations have different kinds.

#### Scenario: Cross-kind duplicate rejected
- **WHEN** module `game.combat` declares `event Hit` and `struct Hit:`
- **THEN** semantic analysis reports a duplicate module-scope declaration for `Hit`

#### Scenario: Same local name in different modules accepted
- **WHEN** module `game.player` declares `trait State` and module `game.enemy` declares `enum State:`
- **THEN** the compiler accepts both declarations because their module identities differ

### Requirement: Semantic-resolution boundary
After semantic analysis, every reference to a module-scope declaration that is consumed by linking or code generation SHALL carry a resolved typed `SymbolId`. Later phases SHALL NOT resolve aliases, imported module qualifiers, unqualified imported names, or stdlib source spellings.

#### Scenario: Aliased filter resolves before codegen
- **WHEN** source imports `use std.physics.flat as phys` and a rule filter references `phys.Body as body`
- **THEN** the resolved filter entry carries the trait symbol identity for `std.physics.flat.Body` and code generation consumes that identity directly

#### Scenario: Prelude event resolves before codegen
- **WHEN** a rule handler declares `on tick:`
- **THEN** semantic analysis resolves the handler event to the event symbol identity `std.core.tick`

### Requirement: Oberon-style import bindings
Ordinary imports SHALL bind a module namespace or alias and SHALL NOT inject imported declarations into unqualified lookup. The `std.core` prelude is the only import exception: prelude symbols may be written unqualified but still resolve to `std.core.*` identities.

#### Scenario: Qualified imported trait accepted
- **WHEN** source imports `use std.physics.flat as phys` and applies `phys.Body`
- **THEN** semantic analysis resolves the trait reference to `std.physics.flat.Body`

#### Scenario: Unique unqualified imported trait rejected
- **WHEN** source imports `use std.physics.flat as phys` and applies `Body` without a local `Body` declaration
- **THEN** semantic analysis rejects the reference and instructs the author to use `phys.Body` or the full module path

#### Scenario: std.core prelude accepted unqualified
- **WHEN** source references `Parent` or `tick` without explicitly importing `std.core`
- **THEN** semantic analysis may accept the reference and resolves it to the corresponding `std.core` symbol identity

### Requirement: Uniform name resolution routine
The semantic analyzer SHALL resolve every symbol reference — declaration-level and expression-level, qualified and unqualified — through a single name-resolution routine over a single scope environment with fixed precedence: expression-scope bindings (locals, filter aliases, handler-event aliases), then module qualifiers, then module-local declarations, then the `std.core` prelude. The referenced declaration kind SHALL be checked after lookup succeeds, and a kind mismatch SHALL produce a diagnostic naming both the expected and the found kind.

#### Scenario: Kind mismatch reported after successful lookup
- **WHEN** a rule filter references `m.clamp` where `use std.math as m` and `clamp` is a func
- **THEN** semantic analysis reports an error stating a trait was expected but func `std.math.clamp` was found, rather than reporting the name as unknown

#### Scenario: Declaration-level and expression-level references resolve identically
- **WHEN** `phys.Body` appears in a rule filter and the same spelling appears inside a handler expression
- **THEN** both resolve through the same routine to the same trait symbol identity `std.physics.flat.Body`

#### Scenario: Expression binding shadows module qualifier
- **WHEN** a handler declares a local binding `inp` and the module also imports `use std.input as inp`
- **THEN** `inp.x` inside that handler resolves against the local binding, not the module namespace

### Requirement: Alias and canonical module qualifiers are interchangeable
For every reference form, an imported module SHALL be addressable both by its `use` alias and by its canonical module path, resolving to identical symbol identities. Module-path qualifiers SHALL be matched by the longest dotted prefix that names a known module.

#### Scenario: Canonical path resolves where alias is registered
- **WHEN** source imports `use std.input as inp` and references `std.input.Key.A`
- **THEN** semantic analysis resolves it to the same enum-member identity as `inp.Key.A`

#### Scenario: Longest module prefix wins
- **WHEN** source references `std.input.Key.A` with `std.input` imported
- **THEN** the qualifier is matched as module `std.input` with symbol `Key` and member `A`, not as an unknown module `std.input.Key`

### Requirement: Expression-level enum member resolution
When a member-access chain's head resolves to an enum type symbol, the semantic analyzer SHALL validate the final segment against the enum's declared members, attach the resolved enum symbol identity and member to the expression while preserving the source spelling, and type the expression as that enum. An unknown member SHALL be a compile error naming the enum's canonical identity.

#### Scenario: Alias-qualified enum member resolved and typed
- **WHEN** `inp.Key.A` appears with `use std.input as inp`
- **THEN** the expression carries the enum identity `std.input.Key` with member `A` and is typed as that enum

#### Scenario: Unknown enum member rejected
- **WHEN** `inp.Key.Azerty` appears with `use std.input as inp`
- **THEN** semantic analysis reports that `Azerty` is not a member of enum `std.input.Key`

#### Scenario: Imported enum members validated from module exports
- **WHEN** a module references a member of an enum declared in another module (directly or via artifact-linked imports)
- **THEN** validation uses the exported enum member list, and resolution succeeds or fails identically to the in-module case

### Requirement: Resolution failure is a diagnostic
When a reference that linking or code generation consumes fails to resolve, the semantic analyzer SHALL report a diagnostic at the reference's source location. Absence of a resolved symbol identity on such a reference after an analysis that reported no errors SHALL be treated as an internal compiler error by downstream phases, not silently tolerated with fallback behavior.

#### Scenario: Failed resolution reports instead of silently decorating
- **WHEN** an input declaration property references a name that resolves to no known symbol
- **THEN** semantic analysis reports an unknown-symbol error at that property's location and compilation fails

#### Scenario: Downstream phase rejects missing resolution loudly
- **WHEN** code generation encounters a consumed reference without a resolved symbol identity despite error-free analysis
- **THEN** generation fails with an internal error rather than emitting fallback output

### Requirement: Canonical phase symbols and handler triggers
Phase declarations SHALL have canonical symbol identities and SHALL participate in the uniform module-scope resolution routine. Decorated handler triggers SHALL store a canonical symbol plus an explicit phase-or-event kind, and downstream phases MUST NOT infer trigger meaning from source spelling.

#### Scenario: Standard tick phase resolves through prelude
- **WHEN** a rule declares `on tick` without explicitly importing std.core
- **THEN** the handler resolves to canonical phase `std.core.tick`

#### Scenario: Qualified custom phase resolves
- **WHEN** a handler references an imported public phase through its module qualifier
- **THEN** semantic analysis stores that phase's canonical symbol identity
