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
The semantic analyzer SHALL reject string literals (`"..."`) that appear outside of `const` blocks or `asset` declarations. String literals inside `trait`, `unit`, `system`, `func`, and `event` bodies SHALL produce a compile error.

#### Scenario: String literal in trait body rejected
- **WHEN** a trait field has a default value of `"hello"`
- **THEN** the analyzer reports an error "string literals are only allowed in const blocks"

#### Scenario: String literal in const block accepted
- **WHEN** a `const:` block contains `GREETING = "Hello"`
- **THEN** the analyzer accepts it and interns the string in the StringPool

#### Scenario: String literal in asset declaration accepted
- **WHEN** `asset Theme: music = "audio/theme.ogg"` appears in a source file
- **THEN** the analyzer accepts the string literal as an asset path without error

### Requirement: Func purity enforcement
The semantic analyzer SHALL verify that `func` declarations are pure: no `emit` statements, no mutation of external state, no `world` access. Violations SHALL produce a compile error.

#### Scenario: Emit in func rejected
- **WHEN** a `func` body contains an `emit` statement
- **THEN** the analyzer reports an error "emit is not allowed in pure functions"

#### Scenario: World access in func rejected
- **WHEN** a `func` body references `world` or accesses global mutable state
- **THEN** the analyzer reports an error "world access is not allowed in pure functions"

### Requirement: No recursion in func
The semantic analyzer SHALL detect recursive calls in `func` declarations (direct and indirect) and report them as errors.

#### Scenario: Direct recursion rejected
- **WHEN** `func factorial(n: int) -> int:` calls `factorial(n - 1)` in its body
- **THEN** the analyzer reports an error "recursion is not allowed in func declarations"

#### Scenario: Indirect recursion rejected
- **WHEN** `func a()` calls `func b()` which calls `func a()`
- **THEN** the analyzer reports an error for the recursive cycle

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
The semantic analyzer SHALL verify that all trait names referenced in system `filter:` clauses correspond to declared traits (local or imported).

#### Scenario: Valid filter traits
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and both traits are declared
- **THEN** the analyzer accepts the filter clause

#### Scenario: Unknown trait in filter
- **WHEN** a system has `filter:` listing `NonExistent` and that trait is not declared
- **THEN** the analyzer reports an error "unknown trait 'NonExistent' in system filter"

### Requirement: Event validation
The semantic analyzer SHALL verify that all `emit` statements reference declared events. User event handler parameter lists SHALL be empty — handlers access event fields via the implicit `event` object. The analyzer SHALL verify that `on EventName():` has an empty parameter list for user-defined events.

#### Scenario: Emit of declared event accepted
- **WHEN** a system handler contains `emit Damage(amount = 10)` and `event Damage:` is declared with field `amount: int`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit of undeclared event rejected
- **WHEN** a system handler contains `emit Foo()` and no `event Foo:` is declared
- **THEN** the analyzer reports an error "undeclared event 'Foo'"

#### Scenario: User event handler with parameters rejected
- **WHEN** `on PlayerDamaged(amount: int):` appears with a non-empty parameter list
- **THEN** the analyzer reports an error: "user event handlers must have empty parameter list; access fields via 'event.amount'"

#### Scenario: Lifecycle handler parameters validated as before
- **WHEN** `on tick(dt: float):` appears
- **THEN** the analyzer accepts it (lifecycle handlers retain their parameter signatures)

### Requirement: Dependency graph construction
The semantic analyzer SHALL build a dependency graph of systems based on their trait access patterns and event relationships. This graph SHALL be included in the DecoratedProgram.

#### Scenario: Independent systems detected
- **WHEN** system A reads trait Position and system B reads trait Inventory with no overlap
- **THEN** the dependency graph marks them as independent (parallelizable)

### Requirement: Accept imported symbols from dependency modules
The semantic analyzer SHALL accept an `ImportedSymbols` parameter containing pub-exported types from dependency modules, keyed by module path or alias.

#### Scenario: Qualified trait resolution via module path
- **WHEN** module `enemies` does `use player` and references `player.Position`
- **THEN** the semantic analyzer resolves `Position` from the `player` module's pub symbols

#### Scenario: Qualified trait resolution via alias
- **WHEN** module `enemies` does `use player as p` and references `p.Position`
- **THEN** the semantic analyzer resolves `Position` from the `player` module's pub symbols via the alias

#### Scenario: Unqualified access for unique symbol
- **WHEN** `Position` is exported by only one imported module and no local declaration exists
- **THEN** the semantic analyzer resolves the unqualified `Position` reference to the imported trait

### Requirement: Ambiguous unqualified reference produces error
The semantic analyzer SHALL report an error when an unqualified symbol name matches pub symbols from multiple imported modules.

#### Scenario: Ambiguous trait name
- **WHEN** module `A` and module `B` both export `pub trait Config:`, and module `C` references unqualified `Config`
- **THEN** the analyzer reports "ambiguous reference 'Config': defined in module A and module B; use qualified access to disambiguate"

### Requirement: Filter clause aliases for trait fields
The semantic analyzer SHALL support `as` aliases in system `filter:` clauses. Both the alias and the trait name are valid access paths. Unqualified field access (without any prefix) is not permitted.

#### Scenario: Filter alias used for field access
- **WHEN** a system has a filter entry `phys.Body as b`
- **THEN** `b.x` resolves to the `x` field of `Body`

#### Scenario: Filter with no alias uses trait name as access path
- **WHEN** a system has a filter entry `Position` with no alias
- **THEN** `Position.x` resolves to the `x` field of `Position`

### Requirement: Field access validation — mandatory alias.field in system handlers
The semantic analyzer SHALL enforce that all trait field accesses within system handler bodies use the `alias.field` or `TraitName.field` form. Bare unqualified identifiers that resolve to trait fields SHALL be rejected.

#### Scenario: Alias.field access accepted
- **WHEN** a system has `filter: Position as pos` and the handler body contains `pos.x += 1.0`
- **THEN** the analyzer accepts the access

#### Scenario: Trait name as implicit alias accepted
- **WHEN** a system has `filter: Position` (no alias) and the handler body contains `Position.x += 1.0`
- **THEN** the analyzer accepts `Position.x`

#### Scenario: Bare field name rejected
- **WHEN** a system filters on `Position` and the handler body contains bare `x += 1.0`
- **THEN** the analyzer reports an error: "unqualified field access 'x' not allowed; use 'Position.x' or declare an alias"

### Requirement: Local variable scope in system handlers
The semantic analyzer SHALL maintain a per-handler local variable scope. `let` declarations introduce immutable bindings; `var` declarations introduce mutable bindings. Re-declaration of an existing local in the same scope SHALL produce an error.

#### Scenario: Let binding immutable after declaration
- **WHEN** a handler declares `let speed = 5.0` and then assigns `speed = 6.0`
- **THEN** the analyzer reports an error: "cannot reassign immutable binding 'speed'"

#### Scenario: Var binding reassignable
- **WHEN** a handler declares `var count = 0` and then assigns `count = count + 1`
- **THEN** the analyzer accepts both statements

#### Scenario: Re-declaration in same scope rejected
- **WHEN** a handler declares `let speed = 5.0` and later declares `let speed = 6.0` in the same block
- **THEN** the analyzer reports an error: "redeclaration of local 'speed' in the same scope"

### Requirement: Implicit `event` object in user event handlers
The semantic analyzer SHALL make an implicit `event` object available in the scope of user-defined event handlers. The `event` object's type is the event being handled; fields are accessible as `event.fieldname`. The `event` object is read-only.

This implicit object is NOT available in lifecycle handlers (`tick`, `fixed_tick`, `late_tick`, `input`, `spawn`, `destroy`, `load`, `unload`).

#### Scenario: event.field access in user event handler
- **WHEN** a system handles `on PlayerDamaged():` and `event PlayerDamaged:` has field `var amount: int`
- **THEN** `event.amount` in the handler body resolves to the `amount` field of the event payload

#### Scenario: event object is read-only
- **WHEN** a handler body contains `event.amount = 99`
- **THEN** the analyzer reports an error: "event fields are read-only; cannot assign to 'event.amount'"

### Requirement: Targeted emit validation
The semantic analyzer SHALL verify that the expression in an `emit ... to expression` statement evaluates to type `entity_id`.

#### Scenario: Targeted emit with entity_id field accepted
- **WHEN** `emit Damage(amount = 10) to EnemyAI.target` and `EnemyAI.target` is of type `entity_id`
- **THEN** the analyzer accepts the targeted emit

#### Scenario: Targeted emit with non-entity_id expression rejected
- **WHEN** `emit Damage(amount = 10) to Position.x` and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "emit target must be of type entity_id, got float"

### Requirement: `destroy entity_id` validation
The semantic analyzer SHALL verify that when `destroy` is given an expression argument, the expression evaluates to type `entity_id`. Without argument, it removes the current entity (always valid inside a system handler).

#### Scenario: Destroy with entity_id expression accepted
- **WHEN** `destroy PlayerComposition.gun` and `PlayerComposition.gun` is of type `entity_id`
- **THEN** the analyzer accepts the destroy statement

#### Scenario: Destroy with non-entity_id expression rejected
- **WHEN** `destroy Position.x` and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "destroy argument must be of type entity_id, got float"

### Requirement: Non-pub symbol access produces helpful error
The semantic analyzer SHALL report a clear error when a module references a symbol that exists in a dependency module but is not marked `pub`.

#### Scenario: Non-pub trait referenced
- **WHEN** module `enemies` references `player.PlayerPhysics` which exists in `player.cactus` but without `pub`
- **THEN** the analyzer reports "trait 'PlayerPhysics' is not public in module 'player'; did you mean to mark it as 'pub'?"

### Requirement: Backward compatibility with single-file mode
The semantic analyzer SHALL continue to work identically for single-file programs when no `ImportedSymbols` are provided.

#### Scenario: Single-file compilation unchanged
- **WHEN** a single `.cactus` file with no `module`/`use` declarations is analyzed without imported symbols
- **THEN** the analyzer produces the same `DecoratedProgram` as before this change

### Requirement: Template declaration validation
The semantic analyzer SHALL validate `template` declarations: all traits in `apply:` must be declared, all field names in `config:` must belong to applied traits.

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
- **THEN** the analyzer SHALL report an error: "'Player' is a unit, not a template"

#### Scenario: Spawn with missing required field rejected
- **WHEN** a template has a `var` field with no `config:` default and the spawn site omits it
- **THEN** the analyzer SHALL report an error: "required field '<name>' not set for template '<T>'"

### Requirement: `destroy` statement context validation
The semantic analyzer SHALL verify that `destroy` only appears inside a system event handler body.

#### Scenario: Destroy in func body rejected
- **WHEN** `destroy` appears inside a `func` declaration
- **THEN** the analyzer SHALL report an error: "`destroy` only allowed inside system event handlers"

### Requirement: `load` statement module reference validation
The semantic analyzer SHALL verify that the module name in a `load` statement is reachable via the module's `use` declarations.

#### Scenario: Load of unreachable module rejected
- **WHEN** `load unknown.module` is used and that module was not imported via `use`
- **THEN** the analyzer SHALL report an error: "unknown module 'unknown.module'; add `use unknown.module` to import it"

### Requirement: Lifecycle handler signature validation
The semantic analyzer SHALL verify that lifecycle handlers have the correct parameter signatures.

| Handler | Expected signature |
|---|---|
| `on tick(dt: float):` | one `float` param named `dt` |
| `on fixed_tick(dt: float):` | one `float` param named `dt` |
| `on late_tick(dt: float):` | one `float` param named `dt` |
| `on input():` | empty param list |
| `on spawn():` | empty param list |
| `on destroy():` | empty param list |
| `on load():` | empty param list |
| `on unload():` | empty param list |

#### Scenario: on fixed_tick with wrong param type rejected
- **WHEN** `on fixed_tick(dt: int):` appears
- **THEN** the analyzer reports an error: "lifecycle handler 'fixed_tick' expects parameter 'dt: float'"

#### Scenario: on input with parameters rejected
- **WHEN** `on input(key: int):` appears
- **THEN** the analyzer reports an error: "lifecycle handler 'input' does not accept parameters"

#### Scenario: on tick with correct signature accepted
- **WHEN** `on tick(dt: float):` appears
- **THEN** the analyzer accepts it

### Requirement: `exclude:` trait reference validation
The semantic analyzer SHALL verify that all trait names listed in an `exclude:` block are declared traits.

#### Scenario: Exclude with undeclared trait rejected
- **WHEN** `exclude: SomeTrait` is used and `SomeTrait` is not declared anywhere
- **THEN** the analyzer SHALL report an error: "undeclared trait 'SomeTrait' in exclude clause"

#### Scenario: Exclude with declared marker trait accepted
- **WHEN** `exclude: Frozen` is used and `trait Frozen` is declared (even as a marker trait)
- **THEN** the exclude clause is valid

### Requirement: Filter and exclude validation — both optional; no filter matches all
The semantic analyzer SHALL validate that all trait names in `filter:` and `exclude:` blocks are declared traits. Both are optional. A system with no `filter:` matches all entities and cannot access trait fields.

#### Scenario: No filter block is valid (match-all)
- **WHEN** a system has no `filter:` block
- **THEN** the analyzer accepts the system as valid; it processes all entities

#### Scenario: Field access without filter rejected
- **WHEN** a system has no `filter:` block but its handler body reads or writes a trait field
- **THEN** the analyzer SHALL report an error: "trait field '<name>' not accessible — no filter clause declares this trait"

### Requirement: Asset declaration registration
The semantic analyzer SHALL register `asset` declarations in the module symbol table, mapping the declared identifier name to its corresponding opaque ID TypeKind.

#### Scenario: Asset name resolves to opaque ID type in config
- **WHEN** `asset PlayerMesh: mesh = "player.glb"` is declared and used as `mesh = PlayerMesh` in a unit config
- **THEN** the analyzer resolves `PlayerMesh` to type `mesh_id` and accepts the assignment

#### Scenario: Undeclared asset identifier rejected
- **WHEN** a config block references `UnknownAsset` which has no `asset` declaration in scope
- **THEN** the analyzer reports an undeclared identifier error

### Requirement: Input declaration registration
The semantic analyzer SHALL register `input` declarations in the module symbol table, mapping the declared identifier name to `InputButton` or `InputAxis`.

#### Scenario: Button input name resolves to InputButton type
- **WHEN** `input Jump: button` is declared and `Jump` is referenced in a query call
- **THEN** the analyzer resolves `Jump` to type `InputButton`

#### Scenario: Axis input name resolves to InputAxis type
- **WHEN** `input MoveX: axis` is declared and `MoveX` is referenced in a query call
- **THEN** the analyzer resolves `MoveX` to type `InputAxis`
