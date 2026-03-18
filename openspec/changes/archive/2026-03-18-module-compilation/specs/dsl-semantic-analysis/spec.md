## ADDED Requirements

### Requirement: Accept imported symbols from dependency modules
The semantic analyzer SHALL accept an `ImportedSymbols` parameter containing pub-exported types (traits, structs, enums, events, funcs, units) from dependency modules, keyed by module path (or alias). These imported symbols SHALL be available for type resolution via qualified access or unqualified access when unique.

#### Scenario: Qualified trait resolution via module path
- **WHEN** module `enemies` does `use player` and references `player.Position`
- **THEN** the semantic analyzer resolves `Position` from the `player` module's pub symbols

#### Scenario: Qualified trait resolution via alias
- **WHEN** module `enemies` does `use player as p` and references `p.Position`
- **THEN** the semantic analyzer resolves `Position` from the `player` module's pub symbols via the alias

#### Scenario: Unqualified access for unique symbol
- **WHEN** `Position` is exported by only one imported module and no local declaration exists with that name
- **THEN** the semantic analyzer resolves the unqualified `Position` reference to the imported trait

### Requirement: Ambiguous unqualified reference produces error
The semantic analyzer SHALL report an error when an unqualified symbol name matches pub symbols from multiple imported modules. The error message SHALL list the conflicting modules and suggest using qualified access or aliases.

#### Scenario: Ambiguous trait name
- **WHEN** module `A` exports `pub trait Config:` and module `B` also exports `pub trait Config:`, and module `C` uses both and references unqualified `Config`
- **THEN** the analyzer reports "ambiguous reference 'Config': defined in module A and module B; use 'A.Config' or 'B.Config' to disambiguate"

### Requirement: Filter clause aliases for trait fields
The semantic analyzer SHALL support `as` aliases in system `filter:` clauses: `filter: [module.Trait as alias]`. The alias SHALL be usable to access the trait's fields in the system body.

#### Scenario: Filter alias used for field access
- **WHEN** a system has `filter: [phys.Body as b, render.Sprite as s]`
- **THEN** `b.x` resolves to the `x` field of `Body`, and `s.x` resolves to the `x` field of `Sprite`

#### Scenario: Filter alias with unqualified trait
- **WHEN** a system has `filter: [Position as pos]` where `Position` is unique (no conflict)
- **THEN** `pos.x` resolves to the `x` field of `Position`

### Requirement: Trait field disambiguation in systems
The semantic analyzer SHALL require qualification when multiple filtered traits have fields with the same name. If field names are unique across filtered traits, unqualified access SHALL be allowed.

#### Scenario: Conflicting field names require qualification
- **WHEN** a system filters `[Body, Sprite]` and both have a field `x`
- **THEN** accessing bare `x` produces an error "ambiguous field 'x': defined in traits Body and Sprite; use 'Body.x' or 'Sprite.x'"

#### Scenario: Unique field names need no qualification
- **WHEN** a system filters `[Position, Velocity]` where Position has `x, y` and Velocity has `dx, dy` (no overlap)
- **THEN** accessing `x`, `y`, `dx`, `dy` unqualified is accepted

### Requirement: Non-pub symbol access produces helpful error
The semantic analyzer SHALL report a clear error when a module references a symbol that exists in a dependency module but is not marked `pub`. The error message SHALL suggest adding the `pub` modifier.

#### Scenario: Non-pub trait referenced
- **WHEN** module `enemies` references `player.PlayerPhysics` which exists in `player.cactus` but without `pub`
- **THEN** the analyzer reports "trait 'PlayerPhysics' is not public in module 'player'; did you mean to mark it as 'pub'?"

### Requirement: Backward compatibility with single-file mode
The semantic analyzer SHALL continue to work identically for single-file programs when no `ImportedSymbols` are provided. The parameter SHALL default to empty.

#### Scenario: Single-file compilation unchanged
- **WHEN** a single `.cactus` file with no `module`/`use` declarations is analyzed without imported symbols
- **THEN** the analyzer produces the same `DecoratedProgram` as before this change
