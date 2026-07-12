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
- **WHEN** source imports `use std.physics.flat as phys` and a system filter references `phys.Body as body`
- **THEN** the resolved filter entry carries the trait symbol identity for `std.physics.flat.Body` and code generation consumes that identity directly

#### Scenario: Prelude event resolves before codegen
- **WHEN** a system handler declares `on tick:`
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
