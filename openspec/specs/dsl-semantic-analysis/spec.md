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
The semantic analyzer SHALL verify that user-defined `func` declarations (those with `is_extern = false`) are pure: no `emit` statements, no mutation of external state, no `world` access. Violations SHALL produce a compile error. `extern func` declarations (those with `is_extern = true`) SHALL be skipped entirely — purity is not enforced over backend-provided functions.

#### Scenario: Emit in func rejected
- **WHEN** a non-extern `func` body contains an `emit` statement
- **THEN** the analyzer reports an error "emit is not allowed in pure functions"

#### Scenario: World access in func rejected
- **WHEN** a `func` body references `world` or accesses global mutable state
- **THEN** the analyzer reports an error "world access is not allowed in pure functions"

#### Scenario: Extern func skipped by purity check
- **WHEN** `pub extern func play_sfx(id: sound_id)` is declared (which may have side effects in C++)
- **THEN** the analyzer does NOT report a purity violation for it

### Requirement: No recursion in func
The semantic analyzer SHALL detect recursive calls in non-extern `func` declarations (direct and indirect) and report them as errors. `extern func` declarations SHALL be excluded from the call graph — calling an extern func from a user func does not create a call-graph edge for the extern func itself.

#### Scenario: Direct recursion rejected
- **WHEN** `func factorial(n: int) -> int:` calls `factorial(n - 1)` in its body
- **THEN** the analyzer reports an error "recursion is not allowed in func declarations"

#### Scenario: Indirect recursion rejected
- **WHEN** `func a()` calls `func b()` which calls `func a()`
- **THEN** the analyzer reports an error for the recursive cycle

#### Scenario: Extern func call does not cause false recursion error
- **WHEN** a user func calls an extern func of the same name (e.g. wrapping it)
- **THEN** the analyzer does NOT report a recursion error

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
The semantic analyzer SHALL verify that all `emit` statements reference declared events. All event handlers SHALL have no parameter list (the new syntax has none). The analyzer SHALL NOT enforce separate rules for lifecycle vs. user event handlers — both are validated by resolving the event name against declared event types (including std.core lifecycle events).

#### Scenario: Emit of declared event accepted
- **WHEN** a system handler contains `emit Damage(amount = 10)` and `event Damage:` is declared with field `amount: int`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit of undeclared event rejected
- **WHEN** a system handler contains `emit Foo()` and no `event Foo:` is declared
- **THEN** the analyzer reports an error "undeclared event 'Foo'"

#### Scenario: Handler for undeclared event rejected
- **WHEN** `on GhostSignal:` appears and no `event GhostSignal` is declared in scope
- **THEN** the analyzer reports an error "undeclared event 'GhostSignal'"

#### Scenario: Handler for stdlib lifecycle event accepted
- **WHEN** `on tick:` appears in a system body
- **THEN** the analyzer resolves `tick` from std.core and accepts the handler

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

### Requirement: Implicit event variable binding in event handlers
The semantic analyzer SHALL introduce one implicit read-only local variable in every event handler body:
- If the handler has an `as alias` clause, the variable name is the alias; otherwise it is the event name.
- The variable's type is the resolved event struct type.
- The variable is read-only: assigning to any field via this variable SHALL be rejected.
- The variable is scoped to the handler body only.
- A handler alias that conflicts with a name already bound in the enclosing system scope (e.g., a filter alias) SHALL produce an error.

This replaces the previous `event` implicit object (for user events) and the previous injected `dt` parameter (for lifecycle events).

#### Scenario: tick.dt access accepted
- **WHEN** `on tick:` handler body contains `pos.x = pos.x + vel.x * tick.dt`
- **THEN** the analyzer resolves `tick.dt` as `float` and accepts the expression

#### Scenario: tick alias access accepted
- **WHEN** `on tick as t:` handler body contains `pos.x = pos.x + t.dt`
- **THEN** the analyzer resolves `t.dt` as the `dt` field of the `tick` event type

#### Scenario: User event name access accepted
- **WHEN** `on PlayerDamaged:` handler body contains `h.health = h.health - PlayerDamaged.amount`
- **THEN** the analyzer resolves `PlayerDamaged.amount` as `int`

#### Scenario: User event alias access accepted
- **WHEN** `on PlayerDamaged as dmg:` handler body contains `h.health = h.health - dmg.amount`
- **THEN** the analyzer resolves `dmg.amount` as `int`

#### Scenario: Event variable field assignment rejected
- **WHEN** a handler body contains `tick.dt = 0.0`
- **THEN** the analyzer reports an error: "event fields are read-only; cannot assign to 'tick.dt'"

#### Scenario: Handler alias conflicts with filter alias rejected
- **WHEN** a system has filter `Position as t` and a handler `on tick as t:`
- **THEN** the analyzer reports an error: "handler alias 't' conflicts with filter alias 't' already in scope"

#### Scenario: Spawn handler body has no accessible event fields
- **WHEN** `on spawn:` handler body contains `spawn.dt`
- **THEN** the analyzer reports an error: "event 'spawn' has no field 'dt'"

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

### Requirement: `after:` system name resolution
The semantic analyzer SHALL resolve each system name in an `after:` clause against the set of all declared systems in the current `DecoratedProgram`. If a name does not resolve to a system declaration, the analyzer SHALL report an error.

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
The semantic analyzer SHALL run a depth-first cycle detection algorithm over the combined system ordering graph. Any cycle SHALL be reported as a compile error that includes the cycle path.

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

### Requirement: `apply:` alias uniqueness validation
The semantic analyzer SHALL verify that no two `apply:` entries in the same unit or template declare the same alias.

#### Scenario: Duplicate alias rejected
- **WHEN** `apply:` contains `Position as p` and `Velocity as p` in the same unit
- **THEN** the analyzer reports: "duplicate alias 'p' in apply block"

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

### Requirement: Qualified `spawn()` override argument key resolution
The semantic analyzer SHALL resolve `spawn` override argument keys using the same rules as `config:` key resolution, but against the template's applied traits.

#### Scenario: Bare spawn key resolved when unambiguous
- **WHEN** `spawn Enemy(patrol_speed = 5.0)` and only `EnemyAI` has `patrol_speed`
- **THEN** the key resolves to `EnemyAI.patrol_speed`

#### Scenario: Ambiguous bare spawn key produces error
- **WHEN** two of a template's applied traits both have a field `speed` and `spawn Foo(speed = 1.0)` uses bare form
- **THEN** the analyzer reports: "ambiguous field 'speed' in spawn override; qualify as 'TraitA.speed' or 'TraitB.speed'"

#### Scenario: TraitName-qualified spawn key resolves
- **WHEN** `spawn Enemy(EnemyAI.patrol_speed = 5.0)` is used
- **THEN** the key resolves to `EnemyAI.patrol_speed`

### Requirement: `ResolvedFunc` produced in `DecoratedProgram`
The semantic analyzer SHALL populate a `funcs` map in `DecoratedProgram` containing a `ResolvedFunc` entry for every `func` and `extern func` declaration in the analyzed program. The `ResolvedFunc` struct SHALL include: `name`, `is_pub`, `is_extern`, resolved parameter types, and resolved return type.

```
struct ResolvedParam {
    string name;
    TypeInfo type;
};

struct ResolvedFunc {
    string name;
    bool is_pub;
    bool is_extern;
    list<ResolvedParam> params;
    optional<TypeInfo> return_type;
};
```

#### Scenario: Extern func produces ResolvedFunc with is_extern = true
- **WHEN** `pub extern func lerp(a, b, t: float) float` is analyzed
- **THEN** `DecoratedProgram.funcs["lerp"]` contains a `ResolvedFunc` with `is_extern = true`, `is_pub = true`, three params of type float, and return type float

#### Scenario: User func produces ResolvedFunc with is_extern = false
- **WHEN** `pub func clamp_int(v, lo, hi: int) int:` is analyzed
- **THEN** `DecoratedProgram.funcs["clamp_int"]` contains a `ResolvedFunc` with `is_extern = false`

### Requirement: Pub extern funcs exported in `ImportedSymbols`
The semantic analyzer SHALL include `pub extern func` declarations in the `ImportedSymbols.funcs` map when extracting pub symbols from a module. Non-pub extern funcs SHALL NOT be exported.

#### Scenario: Pub extern func appears in ImportedSymbols
- **WHEN** a module declares `pub extern func lerp(a, b, t: float) float` and its pub symbols are extracted
- **THEN** `ImportedSymbols.funcs["lerp"]` is present with `is_extern = true`

#### Scenario: Non-pub extern func not exported
- **WHEN** a module declares `extern func internal()` without `pub`
- **THEN** `ImportedSymbols.funcs` does NOT contain `"internal"`
