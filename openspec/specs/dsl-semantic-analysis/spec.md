## Requirements

### Requirement: Type resolution
The semantic analyzer SHALL resolve all type references in the AST to concrete TypeInfo objects. Struct names, trait names, enum names, and parameterized types (`list[T]`) SHALL be resolved against declared types. Unresolved type references SHALL produce an error.

#### Scenario: Struct type resolves
- **WHEN** a field references type `Position` and a `struct Position:` is declared
- **THEN** the analyzer resolves the field's type to a TypeInfo with kind=Struct and name="Position"

#### Scenario: Unknown type reference
- **WHEN** a field references type `Foo` and no `struct Foo:` or `trait Foo:` is declared
- **THEN** the analyzer reports an error "unknown type 'Foo'" with the source location

### Requirement: Const-string enforcement
The semantic analyzer SHALL reject string literals (`"..."`) that appear outside of `const` blocks. String literals inside `trait`, `unit`, `system`, `func`, `view`, and `event` bodies SHALL produce a compile error.

#### Scenario: String literal in trait body rejected
- **WHEN** a trait field has a default value of `"hello"`
- **THEN** the analyzer reports an error "string literals are only allowed in const blocks"

#### Scenario: String literal in const block accepted
- **WHEN** a `const:` block contains `GREETING = "Hello"`
- **THEN** the analyzer accepts it and interns the string in the StringPool

### Requirement: Func purity enforcement
The semantic analyzer SHALL verify that `func` declarations are pure: no `emit` statements, no mutation of external state, no `world` access. Violations SHALL produce a compile error.

#### Scenario: Emit in func rejected
- **WHEN** a `func` body contains an `emit` statement
- **THEN** the analyzer reports an error "emit is not allowed in pure functions"

#### Scenario: World access in func rejected
- **WHEN** a `func` body references `world` or accesses global mutable state
- **THEN** the analyzer reports an error "world access is not allowed in pure functions"

### Requirement: No recursion in func
The semantic analyzer SHALL detect recursive calls in `func` declarations (direct and indirect) and report them as errors. This is required for GPU safety.

#### Scenario: Direct recursion rejected
- **WHEN** `func factorial(n: int) -> int:` calls `factorial(n - 1)` in its body
- **THEN** the analyzer reports an error "recursion is not allowed in func declarations"

#### Scenario: Indirect recursion rejected
- **WHEN** `func a()` calls `func b()` which calls `func a()`
- **THEN** the analyzer reports an error for the recursive cycle

### Requirement: Scope resolution
The semantic analyzer SHALL build a scope tree and resolve all variable references to their declarations. Undeclared variable references SHALL produce an error. Shadowing within the same scope SHALL produce a warning.

#### Scenario: Variable resolves to declaration
- **WHEN** a system handler references `position` and the filtered trait has a field `position`
- **THEN** the analyzer resolves the reference to that field's declaration

#### Scenario: Undeclared variable
- **WHEN** an expression references `foo` and no `foo` is declared in any enclosing scope
- **THEN** the analyzer reports an error "undeclared identifier 'foo'"

### Requirement: Persist and sync modifier validation
The semantic analyzer SHALL validate that `persist` and `sync` modifiers are only applied to `var` fields (not `let` fields). Using `persist` or `sync` on a `let` field SHALL produce a compile error.

#### Scenario: Persist on var accepted
- **WHEN** a trait field is declared as `persist var health: int`
- **THEN** the analyzer accepts it and sets is_persist=true on the TypeInfo

#### Scenario: Persist on let rejected
- **WHEN** a trait field is declared as `persist let name: string`
- **THEN** the analyzer reports an error "persist modifier can only be used on var fields"

#### Scenario: Sync on var accepted
- **WHEN** a trait field is declared as `sync var position: vec3`
- **THEN** the analyzer accepts it and sets is_sync=true on the TypeInfo

### Requirement: System filter validation
The semantic analyzer SHALL verify that all trait names referenced in system `filter:` clauses correspond to declared traits.

#### Scenario: Valid filter traits
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and both traits are declared
- **THEN** the analyzer accepts the filter clause

#### Scenario: Unknown trait in filter
- **WHEN** a system has `filter:` listing `NonExistent` and that trait is not declared
- **THEN** the analyzer reports an error "unknown trait 'NonExistent' in system filter"

### Requirement: Event validation
The semantic analyzer SHALL verify that all `emit` statements reference declared events, and that event handler parameter signatures match the event's declared fields.

#### Scenario: Emit of declared event
- **WHEN** a system handler contains `emit Damage(amount: 10)` and `event Damage:` is declared with field `amount: int`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit of undeclared event
- **WHEN** a system handler contains `emit Foo()` and no `event Foo:` is declared
- **THEN** the analyzer reports an error "undeclared event 'Foo'"

### Requirement: Dependency graph construction
The semantic analyzer SHALL build a dependency graph of systems based on their trait access patterns (read/write) and event relationships. This graph SHALL be included in the DecoratedProgram for backend optimization.

#### Scenario: Independent systems detected
- **WHEN** system A reads trait Position and system B reads trait Inventory with no overlap
- **THEN** the dependency graph marks them as independent (parallelizable)

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

### Requirement: Template declaration validation
The semantic analyzer SHALL validate `template` declarations with the same rules as `unit` declarations: all traits in `apply:` must be declared, all field names in `config:` must belong to applied traits, and `let` fields with no default value are errors. Additionally, the analyzer SHALL track templates as spawnable archetypes for spawn-site validation.

#### Scenario: Template with undeclared trait rejected
- **WHEN** a `template Foo:` lists `apply: UnknownTrait`
- **THEN** the analyzer SHALL report an error: "undeclared trait 'UnknownTrait'"

#### Scenario: Template with invalid config field rejected
- **WHEN** a `template` sets a field in `config:` that belongs to no applied trait
- **THEN** the analyzer SHALL report an error: "unknown field '<name>' in template config"

### Requirement: Spawn-site validation
At each `spawn TemplateName(...)` call site, the semantic analyzer SHALL verify the template exists, all override fields are valid, and all required fields are provided.

#### Scenario: Spawn of undeclared template rejected
- **WHEN** `spawn UnknownFoo()` is used
- **THEN** the analyzer SHALL report an error: "undefined template 'UnknownFoo'"

#### Scenario: Spawn of unit (not template) rejected
- **WHEN** `spawn Player()` is used and `Player` is a `unit`, not a `template`
- **THEN** the analyzer SHALL report an error: "'Player' is a unit, not a template; use `spawn` only with `template` declarations"

#### Scenario: Spawn with missing required field rejected
- **WHEN** a template has a `var` field with no `config:` default and the spawn site omits it
- **THEN** the analyzer SHALL report an error: "required field '<name>' not set for template '<T>'"

### Requirement: `destroy` statement context validation
The semantic analyzer SHALL verify that `destroy` only appears inside a system event handler body.

#### Scenario: Destroy in func body rejected
- **WHEN** `destroy` appears inside a `func` declaration
- **THEN** the analyzer SHALL report an error: "`destroy` only allowed inside system event handlers"

### Requirement: `load` statement module reference validation
The semantic analyzer SHALL verify that the module name in a `load` statement is reachable via the module's `use` declarations. `load` is also restricted to system event handler bodies.

#### Scenario: Load of unreachable module rejected
- **WHEN** `load unknown.module` is used and that module was not imported via `use`
- **THEN** the analyzer SHALL report an error: "unknown module 'unknown.module'; add `use unknown.module` to import it"

### Requirement: `enable`/`disable` trait membership validation
The semantic analyzer SHALL verify that the trait named in an `enable` or `disable` statement is in the `apply:` block of at least one entity archetype that could match the enclosing system's filter.

#### Scenario: Disable of trait present in matching archetype accepted
- **WHEN** a template has `Frozen` in its `apply:` and the enclosing system's filter matches that template
- **THEN** `disable Frozen` is accepted without error

### Requirement: Lifecycle handler signature validation
The semantic analyzer SHALL verify that `on spawn()`, `on destroy()`, `on load()`, and `on unload()` handlers have empty parameter lists.

#### Scenario: on spawn with parameters rejected
- **WHEN** `on spawn(x: float):` appears in a system
- **THEN** the analyzer SHALL report an error: "lifecycle handler 'spawn' does not accept parameters"

### Requirement: `exclude:` trait reference validation
The semantic analyzer SHALL verify that all trait names listed in an `exclude:` block are declared traits.

#### Scenario: Exclude with undeclared trait rejected
- **WHEN** `exclude: SomeTrait` is used and `SomeTrait` is not declared anywhere
- **THEN** the analyzer SHALL report an error: "undeclared trait 'SomeTrait' in exclude clause"

#### Scenario: Exclude with declared marker trait accepted
- **WHEN** `exclude: Frozen` is used and `trait Frozen` is declared (even as a marker trait)
- **THEN** the exclude clause is valid

### Requirement: Filter and exclude validation — both optional; no filter matches all
The semantic analyzer SHALL validate that all trait names in `filter:` and `exclude:` blocks are declared traits. Both `filter:` and `exclude:` are optional. A system with no `filter:` block matches all entities. A system with no `filter:` cannot access trait fields.

#### Scenario: No filter block is valid (match-all)
- **WHEN** a system has no `filter:` block
- **THEN** the analyzer accepts the system as valid; it processes all entities

#### Scenario: Field access without filter rejected
- **WHEN** a system has no `filter:` block but its handler body reads or writes a trait field
- **THEN** the analyzer SHALL report an error: "trait field '<name>' not accessible — no filter clause declares this trait"
