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
The semantic analyzer SHALL reject string literals (`"..."`) that appear outside of `const` blocks, `asset` declarations, or the first argument position of a recognized `std.text.format` call. String literals in other `trait`, `unit`, `system`, `func`, and `event` expression positions SHALL produce a compile error.

#### Scenario: String literal in trait body rejected
- **WHEN** a trait field has a default value of `"hello"`
- **THEN** the analyzer reports an error "string literals are only allowed in const blocks"

#### Scenario: String literal in const block accepted
- **WHEN** a `const:` block contains `GREETING = "Hello"`
- **THEN** the analyzer accepts it and interns the string in the StringPool

#### Scenario: String literal in asset declaration accepted
- **WHEN** `asset Theme: music = "audio/theme.ogg"` appears in a source file
- **THEN** the analyzer accepts the string literal as an asset path without error

#### Scenario: Format string literal accepted in recognized format call
- **WHEN** a system handler or pure function contains `text.format("HP: {}", hp)` and `text` resolves to `std.text`
- **THEN** the analyzer accepts the first string literal as a format string without reporting the const-string error

#### Scenario: Non-format string argument remains rejected
- **WHEN** authored code calls some ordinary function as `log("HP: {}", hp)` and that call is not recognized as `std.text.format`
- **THEN** the analyzer rejects the string literal outside a const block

### Requirement: Semantic analyzer validates `std.text.format` calls
The semantic analyzer SHALL recognize calls resolved to `std.text.format`, infer their result type as `string`, and validate their format string and arguments. The first argument MUST be a string literal. Format-string validation SHALL detect malformed braces, escaped braces, automatic/manual placeholder mode, placeholder arity, and manual placeholder indexes.

#### Scenario: Format call returns string
- **WHEN** authored code binds `let label = text.format("Score: {}", score)`
- **THEN** the analyzer infers `label` as type `string`

#### Scenario: Non-literal format string rejected
- **WHEN** authored code calls `text.format(fmt, score)` where `fmt` is a variable or const identifier rather than a literal at the call site
- **THEN** the analyzer reports that the first argument to `std.text.format` must be a string literal

#### Scenario: Too few arguments rejected
- **WHEN** authored code calls `text.format("{} {}", a)`
- **THEN** semantic analysis reports a placeholder/argument count mismatch

#### Scenario: Manual placeholder index out of range rejected
- **WHEN** authored code calls `text.format("{1}", a)`
- **THEN** semantic analysis reports that placeholder index `1` has no corresponding format argument

#### Scenario: Malformed brace rejected
- **WHEN** authored code calls `text.format("value={", value)`
- **THEN** semantic analysis reports a malformed format string

#### Scenario: Unsupported argument type rejected
- **WHEN** authored code calls `text.format("{}", some_vec2)` where the argument type is `vec2`
- **THEN** semantic analysis reports that the argument type is not supported by `std.text.format`

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
The semantic analyzer SHALL verify that all `emit` statements reference declared events. All event handlers SHALL have no parameter list (the new syntax has none). The analyzer SHALL NOT enforce separate rules for lifecycle vs. user event handlers — both are validated by resolving the event name against declared event types (including std.core lifecycle events). Event payload fields are declared without trait-style modifiers and are implicitly immutable members of the event type.

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

#### Scenario: Event field is implicitly immutable
- **WHEN** `event Damage:` is declared with `amount: int`
- **THEN** the analyzer treats `amount` as a read-only field of the `Damage` event type

### Requirement: Dependency graph construction
The semantic analyzer SHALL build a canonical handler execution graph from phase dependencies, per-handler inferred or declared contracts, explicit handler ordering, data/effect conflicts, event production and consumption, and activation commits. The graph SHALL be included in the DecoratedProgram and SHALL preserve independent nodes as parallelizable dependency levels.

#### Scenario: Handler contracts are not merged by system
- **WHEN** one system has a tick handler that writes Position and a Damaged handler that writes Health
- **THEN** the graph records those accesses on separate handler nodes

#### Scenario: Filter selection adds no read
- **WHEN** a handler filters Position but does not access its data
- **THEN** Position is not added to the handler reads set

#### Scenario: Event edge is inferred
- **WHEN** one handler may emit Contact and another handles Contact
- **THEN** the graph records the producer-to-consumer event-flow relation

### Requirement: Phase and external-event semantic validation
The semantic analyzer SHALL resolve external-event and phase declarations canonically, type-check phase field initializers, validate phase cadence constants and dependency acyclicity, and resolve each handler trigger unambiguously as either a phase or an event.

Each declared phase field's initializer SHALL be a single member-chain expression (e.g. `frame.dt`, `fixed_tick.alpha`) that reads a field of the phase's runtime root event or an allowed upstream phase. The analyzer SHALL retain the resolved binding — source kind (root event vs. upstream phase), resolved source symbol, and member name — on the decorated phase field so downstream code generation can reproduce the initializer without re-resolving it. A phase field initializer that is not a plain member-chain expression (for example, a literal, unary, or binary composition) SHALL be rejected with an error, since no runtime lowering is defined for it.

#### Scenario: Trigger kind is preserved
- **WHEN** `on tick` resolves to a phase and `on Damaged` resolves to an event
- **THEN** the decorated handlers preserve distinct phase-trigger and event-trigger kinds

#### Scenario: Resolved binding recorded for downstream codegen
- **WHEN** `phase tick` declares `after: fixed_tick` and `dt: float = frame.dt`
- **THEN** the decorated phase field for `tick.dt` records that it is bound to the runtime root event's `dt` field

#### Scenario: Upstream phase binding recorded for downstream codegen
- **WHEN** `phase render` declares `alpha: float = fixed_tick.alpha`
- **THEN** the decorated phase field for `render.alpha` records that it is bound to upstream phase `fixed_tick`'s `alpha` field

#### Scenario: Compound phase field initializer rejected
- **WHEN** a phase field declares `dt: float = frame.dt * 2.0`
- **THEN** semantic analysis reports that the phase field initializer must be a plain member-chain read of upstream activation data

### Requirement: Accept imported symbols from dependency modules
The semantic analyzer SHALL accept imported public symbols from dependency modules keyed by module path or alias. Imported symbols SHALL be resolved only through an explicit module qualifier or alias, except for symbols provided by the implicit `std.core` prelude. Unique unqualified imported-symbol lookup SHALL NOT be performed for ordinary modules.

#### Scenario: Qualified trait resolution via module path
- **WHEN** module `enemies` does `use player` and references `player.Position`
- **THEN** the semantic analyzer resolves the trait reference to the canonical `player.Position` symbol identity

#### Scenario: Qualified trait resolution via alias
- **WHEN** module `enemies` does `use player as p` and references `p.Position`
- **THEN** the semantic analyzer resolves the trait reference to the canonical `player.Position` symbol identity via the alias

#### Scenario: Unqualified ordinary imported symbol rejected
- **WHEN** `Position` is exported by exactly one imported non-`std.core` module and no local declaration named `Position` exists
- **THEN** the semantic analyzer reports that `Position` must be referenced through its module path or import alias

#### Scenario: std.core prelude symbol resolves unqualified
- **WHEN** a module references lifecycle event `tick` without an explicit `use std.core`
- **THEN** the semantic analyzer resolves the reference to the canonical `std.core.tick` event symbol identity

### Requirement: Ambiguous unqualified reference produces error
The semantic analyzer SHALL report an error when an unqualified module-scope reference cannot resolve locally or through the `std.core` prelude. If matching imported symbols exist, the diagnostic SHALL name the matching modules and instruct the author to use qualified access.

#### Scenario: Ambiguous trait name
- **WHEN** module `A` and module `B` both export `pub trait Config:`, and module `C` references unqualified `Config`
- **THEN** the analyzer reports "ambiguous reference 'Config': defined in module A and module B; use qualified access to disambiguate"

#### Scenario: Unique imported trait still requires qualification
- **WHEN** module `A` exports `pub trait Config:` and module `C` imports only `A` but references unqualified `Config`
- **THEN** the analyzer reports that `Config` is imported from module `A` and must be referenced as `A.Config` or through an alias

### Requirement: Semantic analyzer produces resolved module-scope references
The semantic analyzer SHALL produce a resolved representation in which every module-scope reference used by later phases carries a typed symbol identity. This includes type references, trait applications, filters, excludes, query filters, trait-match arms, event handlers, emit statements, system `after:` references, template uses, spawn sites, module-scope function calls, assets, inputs, and constants.

#### Scenario: Filter entry stores resolved trait identity
- **WHEN** a system filter references `phys.Body as body` through `use std.physics.flat as phys`
- **THEN** the semantic representation stores the trait symbol identity `std.physics.flat.Body` on that filter entry

#### Scenario: Add statement stores resolved trait identity
- **WHEN** a handler contains `add phys.Body to target`
- **THEN** the semantic representation stores the trait symbol identity `std.physics.flat.Body` on the add statement

#### Scenario: after clause stores resolved system identity
- **WHEN** a system has `after:` containing `render.SpriteRenderer`
- **THEN** the semantic representation stores the referenced system's canonical symbol identity rather than the authored string

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

### Requirement: Trait field modifiers are invalid on event fields
The semantic analyzer SHALL reject trait-oriented field modifiers when they appear in an `event` declaration body. `let`, `var`, `persist`, `sync`, and other trait field modifiers MUST NOT be accepted as event-field metadata.

#### Scenario: let event field rejected
- **WHEN** `event Tick:` contains `let dt: float`
- **THEN** the compiler reports an error indicating event fields use bare `name: type` syntax

#### Scenario: sync event field rejected
- **WHEN** `event NetMessage:` contains `sync var sequence: int`
- **THEN** the compiler reports an error indicating trait field modifiers are not allowed in event declarations

### Requirement: Targeted emit validation
The semantic analyzer SHALL verify that the expression in an `emit ... to expression` statement evaluates to type `entity_id`.

#### Scenario: Targeted emit with entity_id field accepted
- **WHEN** `emit Damage(amount = 10) to EnemyAI.target` and `EnemyAI.target` is of type `entity_id`
- **THEN** the analyzer accepts the targeted emit

#### Scenario: Targeted emit with non-entity_id expression rejected
- **WHEN** `emit Damage(amount = 10) to Position.x` and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "emit target must be of type entity_id, got float"

### Requirement: `destroy entity_id` validation
The semantic analyzer SHALL verify that when `destroy` is given an expression argument, the expression evaluates to type `entity_id`. Without argument, it removes the current entity (always valid inside a system handler). The `self` keyword SHALL satisfy this requirement because it has type `entity_id` in handler context.

#### Scenario: Destroy with entity_id expression accepted
- **WHEN** `destroy PlayerComposition.gun` and `PlayerComposition.gun` is of type `entity_id`
- **THEN** the analyzer accepts the destroy statement

#### Scenario: Destroy with non-entity_id expression rejected
- **WHEN** `destroy Position.x` and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "destroy argument must be of type entity_id, got float"

#### Scenario: Destroy self accepted
- **WHEN** `destroy self` appears inside a system handler
- **THEN** the analyzer accepts the destroy statement because `self` has type `entity_id`

### Requirement: Pair domains resolve to canonical binding metadata
The semantic analyzer SHALL resolve every pair trait entry to a canonical trait identity and SHALL produce ordered binding metadata for each pair handler. It SHALL reject duplicate binding names, duplicate or ambiguous binding-local aliases, unknown traits, invalid domain combinations, and pair domains with other than two non-empty bindings.

#### Scenario: Imported pair traits resolve canonically
- **WHEN** binding `body` selects `tf.WorldTransform` through an imported module alias
- **THEN** semantic output stores the canonical WorldTransform identity under `body`

#### Scenario: Duplicate pair binding is rejected
- **WHEN** both entries in a pair clause are named `body`
- **THEN** semantic analysis reports a duplicate pair binding

### Requirement: Pair member access is resolved and typed
The semantic analyzer SHALL type a pair binding as `entity_id` and SHALL resolve binding-relative trait, alias, field, and aggregate-member paths against that binding's selected traits. Access to a trait not selected by that binding SHALL be rejected.

#### Scenario: Binding trait field has declared type
- **WHEN** `body.Collider.mask` refers to an integer field
- **THEN** the expression is typed as `int` and records a bound read of body.Collider

#### Scenario: Cross-binding trait access is rejected
- **WHEN** only `wall` selects Solid and code reads `body.Solid`
- **THEN** semantic analysis reports that Solid is unavailable on `body`

### Requirement: Pair mutability and implicit-entity restrictions are validated
The semantic analyzer SHALL reject durable writes through pair-bound traits, `self`, implicit-self projection/add/remove/destroy forms, and writable trait matching on a pair binding. Explicit entity targets and spawning SHALL be validated under their existing type and command rules.

#### Scenario: Explicit pair target passes entity type checking
- **WHEN** a pair handler emits Contact to `body`
- **THEN** targeted emit validation accepts the binding's `entity_id` type

#### Scenario: Bare project is rejected in pair handler
- **WHEN** a pair handler contains `project Contacting` with no `to` expression
- **THEN** semantic analysis reports that no implicit entity exists

### Requirement: Pair contracts expose precise and conservative accesses
For a pair handler, semantic analysis SHALL infer the pair domain, binding-qualified trait reads, a conservative canonical trait-read union, projected outputs distinct from durable writes, emitted events, structural commands, and external effects from all reachable handler code. Pair selection alone SHALL add no read.

#### Scenario: Same trait on both bindings remains distinguishable
- **WHEN** a handler reads `body.Transform` and `wall.Transform`
- **THEN** bound reads record both binding accesses while the conservative read set contains canonical Transform once

#### Scenario: Projection is not a durable write
- **WHEN** a pair handler projects GroundContact to `body`
- **THEN** GroundContact is recorded as a projected output and not as a durable pair-bound write

### Requirement: `self` type-checks as `entity_id` in system handlers
The semantic analyzer SHALL type `self` as `entity_id` only inside unary entity-selected system handler bodies. It SHALL reject `self` in selectionless and pair handler bodies because those domains have no unique current entity.

#### Scenario: `self` accepted as add target
- **WHEN** a unary entity handler contains `add Parent to self`
- **THEN** the analyzer accepts `self` as a valid `entity_id` target expression

#### Scenario: `self` accepted in entity field assignment
- **WHEN** a unary entity handler assigns `Parent.parent = self`
- **THEN** the analyzer accepts the assignment because `Parent.parent` and `self` are both `entity_id`

#### Scenario: `self` rejected in pair handler
- **WHEN** a pair handler references `self`
- **THEN** the analyzer reports that pair handlers require an explicit binding

#### Scenario: `self` rejected in selectionless handler
- **WHEN** a selectionless handler references `self`
- **THEN** the analyzer reports that the handler has no current entity

### Requirement: `self` is rejected outside handler world context
The semantic analyzer SHALL report an error when `self` appears outside a system event handler body.

#### Scenario: `self` in func rejected
- **WHEN** a `func` returns `self`
- **THEN** the analyzer reports that `self` requires a system event handler context

#### Scenario: `self` in unit initializer rejected
- **WHEN** a unit trait assignment sets `parent = self`
- **THEN** the analyzer reports that `self` is unavailable during archetype initialization

### Requirement: hierarchy traits are semantically compatible only within one spatial dimension
The semantic analyzer SHALL reject entities that mix flat and volume hierarchy transform traits on the same entity.

#### Scenario: flat local with volume world rejected
- **WHEN** an entity applies `std.transform.flat.LocalTransform` and `std.transform.volume.WorldTransform`
- **THEN** the analyzer reports an error indicating that flat and volume hierarchy transform traits cannot be mixed on one entity

#### Scenario: flat hierarchy set accepted
- **WHEN** an entity applies `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`
- **THEN** the analyzer accepts the trait combination

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
The semantic analyzer SHALL validate `template` declarations by checking that every nested trait entry names a declared trait, every field assignment inside a trait block belongs to that trait, and every archetype-body `use` entry resolves to a template using the existing local/imported symbol resolution rules. Archetype-body `use` entries SHALL reject non-public templates from imported modules.

#### Scenario: Template with undeclared trait rejected
- **WHEN** a `template Foo:` contains a nested trait entry `UnknownTrait:`
- **THEN** the analyzer SHALL report an error: "undeclared trait 'UnknownTrait'"

#### Scenario: Template with invalid nested field rejected
- **WHEN** a `template` assigns a field inside `Health:` that is not declared on `Health`
- **THEN** the analyzer SHALL report an error naming the unknown trait field

#### Scenario: Template use of non-template rejected
- **WHEN** a template body contains `use Health` and `Health` resolves to a trait rather than a template
- **THEN** the analyzer SHALL report that archetype-body template uses must reference templates

#### Scenario: Imported private template rejected
- **WHEN** an archetype body uses a template from another module that is not `pub`
- **THEN** the analyzer reports that the template is not importable

### Requirement: Template composition flattening
The semantic analyzer SHALL flatten archetype-body `use TemplateName` entries in declaration order before backend generation and spawn-site validation. Flattening SHALL merge duplicate trait entries field-by-field, where later entries override earlier assignments for the same field, and SHALL reject cyclic template-use graphs.

#### Scenario: Local entry overrides composed template field
- **WHEN** `BossEnemy` uses `EnemyBase` and then defines `Health.health = 50`
- **THEN** the flattened `BossEnemy` archetype uses `Health.health = 50` while preserving other `Health` fields from `EnemyBase`

#### Scenario: Template-use cycle rejected
- **WHEN** `template A` uses `B` and `template B` uses `A`
- **THEN** semantic analysis reports a cyclic template-use error

#### Scenario: Marker duplicates collapse
- **WHEN** two used templates both apply marker trait `Persistent`
- **THEN** the flattened archetype contains one `Persistent` marker entry

### Requirement: Spawn-site validation
At each block-structured `spawn TemplateName:` call site, the semantic analyzer SHALL verify the template exists, every overridden trait exists on the template's flattened archetype, all overridden fields are valid for that trait, and all required fields remain satisfied after applying overrides.

#### Scenario: Spawn of undeclared template rejected
- **WHEN** `spawn UnknownFoo:` is used
- **THEN** the analyzer SHALL report an error: "undefined template 'UnknownFoo'"

#### Scenario: Spawn of entity (not template) rejected
- **WHEN** `spawn Player:` is used and `Player` is an `entity`, not a `template`
- **THEN** the analyzer SHALL report an error: "'Player' is an entity, not a template"

#### Scenario: Spawn overriding trait not present on template rejected
- **WHEN** `spawn Enemy:` contains an override block for `Loot:` but `Enemy` does not define `Loot`
- **THEN** the analyzer SHALL report an error naming the unknown trait override for template `Enemy`

#### Scenario: Spawn with missing required field rejected
- **WHEN** a template has a required field with no default or template initializer and the `spawn` site still leaves it unset
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

### Requirement: `add` statement semantic validation
The semantic analyzer SHALL validate `add` statements as follows: the trait name resolves to a declared trait, supplied field names and values match the trait definition, any `to expr` target has type `entity_id`, and `add` appears only inside system event handler bodies.

#### Scenario: Cross-entity target type check
- **WHEN** `add Frozen to some_float` appears where `some_float` is of type `float`
- **THEN** the semantic analyzer SHALL report: "`to` target must be of type `entity_id`"

### Requirement: `remove` statement semantic validation
The semantic analyzer SHALL validate `remove` statements as follows: the trait name resolves to a declared trait, any `from expr` target has type `entity_id`, and `remove` appears only inside system event handler bodies.

#### Scenario: Remove unknown trait
- **WHEN** `remove Phantom` appears and `Phantom` is not declared
- **THEN** the semantic analyzer SHALL report: "undeclared trait 'Phantom'"

### Requirement: Trait field default value validation
The semantic analyzer SHALL validate field default value expressions in trait declarations. The default expression MUST type-check against the field's declared type. Default expressions MUST be constant-foldable.

#### Scenario: Default value type mismatch
- **WHEN** `var count: int = 3.14` appears in a trait
- **THEN** the semantic analyzer SHALL report a type error: "default value type 'float' does not match field type 'int'"

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
**Reason**: Archetype declarations no longer support `apply:` aliases.
**Migration**: Replace alias-based archetype configuration with nested trait blocks.

### Requirement: Qualified `config:` key resolution
**Reason**: `config:` blocks are removed. Trait ownership is explicit in the nested syntax, so key qualification rules are unnecessary.
**Migration**: Move field assignments into the owning trait block in the unit or template body.

### Requirement: Qualified `spawn()` override argument key resolution
**Reason**: `spawn` no longer uses flat override arguments. Nested trait override blocks replace prefixed key resolution.
**Migration**: Move spawn override fields into the appropriate nested trait block under `spawn TemplateName:`.

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

### Requirement: `order by:` semantic validation
The semantic analyzer SHALL validate `order by:` clauses in system declarations with the following rules:
1. Each sort key alias MUST be declared in the system's `filter:` block
2. Each sort key field MUST exist on the trait bound to that alias
3. The resolved field type MUST be a scalar-comparable type: `int`, `float`, or `bool`
4. For `vec2`/`vec3` field members (e.g., `p.pos.y`): the member name must be a valid component (`x`, `y`, `z`) and resolves to `float`
5. A system with `order by:` MUST have a `filter:` clause

#### Scenario: Valid single-key order by
- **WHEN** `order by: s.layer asc` and `Sprite as s` in filter and `Sprite.layer` is `int`
- **THEN** the semantic analyzer accepts it

#### Scenario: Sort key alias not in filter
- **WHEN** `order by: h.value asc` and `Health as h` is not in `filter:`
- **THEN** the semantic analyzer SHALL report: "sort key alias 'h' is not declared in filter:"

#### Scenario: Sort key field does not exist on trait
- **WHEN** `order by: s.missing_field asc` and `Sprite` has no field `missing_field`
- **THEN** the semantic analyzer SHALL report: "sort key: trait 'Sprite' has no field 'missing_field'"

#### Scenario: vec2 direct sort key rejected
- **WHEN** `order by: p.pos asc` and `Position.pos` is type `vec2`
- **THEN** the semantic analyzer SHALL report: "sort key 'p.pos' has type 'vec2' which is not orderable; use a scalar field or member"

#### Scenario: vec2 member sort key accepted
- **WHEN** `order by: p.pos.y desc` and `Position.pos` is type `vec2`
- **THEN** the semantic analyzer accepts it, resolving the type as `float`

#### Scenario: order by on filterless system rejected
- **WHEN** a system has `order by:` but no `filter:` block
- **THEN** the semantic analyzer SHALL report: "`order by:` requires a `filter:` clause"

### Requirement: Semantic validation of `TraitMatchStmt`
The semantic analyzer SHALL validate `TraitMatchStmt` nodes as follows:
1. The subject expression MUST have type `entity_id`
2. Each `TraitMatchArm.trait_name` MUST resolve to a declared trait in scope
3. If the trait has fields, an alias is optional; if declared, it MUST not conflict with any in-scope name
4. If the trait is a marker (no fields), an alias MUST NOT be declared
5. The wildcard arm `_ =>` is optional and, if present, MUST be the last arm
6. `TraitMatchStmt` MUST only appear inside system event handler bodies
7. Aliases introduced in one arm are NOT in scope in other arms

#### Scenario: Valid entity_id subject accepted
- **WHEN** `match c.other:` and `c.other` is type `entity_id`
- **THEN** the semantic analyzer proceeds with trait pattern matching mode

#### Scenario: Non-entity_id subject at statement level rejected
- **WHEN** `match some_int:` at statement position and `some_int` is type `int`
- **THEN** the semantic analyzer SHALL report: "statement-level `match` subject must be of type `entity_id`"

#### Scenario: Trait arm with unknown trait rejected
- **WHEN** arm `Phantom as p =>` and `Phantom` is not declared
- **THEN** the semantic analyzer SHALL report: "undeclared trait 'Phantom'"

#### Scenario: Alias conflicts with filter binding rejected
- **WHEN** system has `filter: Position as p` and arm is `Boss as p =>`
- **THEN** the semantic analyzer SHALL report: "match arm alias 'p' conflicts with filter alias 'p'"

#### Scenario: Marker trait with alias rejected
- **WHEN** arm `Invincible as inv =>` and `Invincible` has no fields
- **THEN** the semantic analyzer SHALL report: "marker trait 'Invincible' has no fields; alias 'as inv' is not allowed"

#### Scenario: Wildcard before trait arm rejected
- **WHEN** `_ =>` arm appears before a trait arm
- **THEN** the semantic analyzer SHALL report: "wildcard arm `_ =>` must be the last arm"

#### Scenario: Arm alias in scope only within its arm body
- **WHEN** `Boss as b =>` arm ends and subsequent arm `EnemyAI as e =>` begins
- **THEN** `b` is no longer in scope; `e` is in scope only within its arm body

### Requirement: Semantic analyzer validates bounded foreach
The semantic analyzer SHALL validate foreach statements by requiring the iterable expression to have type `list[T]`, binding the loop variable as a read-only local of type `T` inside the loop body, and rejecting assignments to the loop variable.

#### Scenario: Foreach over list accepted
- **WHEN** `contacts` has type `list[phys.QueryContact2D]` and a handler contains `for contact in contacts:`
- **THEN** the semantic analyzer accepts the loop and binds `contact` as `phys.QueryContact2D` within the loop body

#### Scenario: Foreach over non-list rejected
- **WHEN** a handler contains `for x in 42:`
- **THEN** the semantic analyzer reports that foreach requires a `list[T]` iterable

#### Scenario: Loop variable is read-only
- **WHEN** a foreach body assigns directly to the loop variable
- **THEN** the semantic analyzer reports that foreach loop variables are read-only

### Requirement: Semantic analyzer restricts foreach context
Bounded foreach statements SHALL be valid in system event handlers and rejected in pure `func` bodies for v1.

#### Scenario: Foreach in handler accepted
- **WHEN** a system event handler iterates a list value with `for item in items:`
- **THEN** the semantic analyzer accepts the statement subject to ordinary type checks

#### Scenario: Foreach in pure func rejected
- **WHEN** a user `func` body contains `for item in items:`
- **THEN** the semantic analyzer reports that bounded foreach is only allowed in handler/world-aware contexts

### Requirement: Semantic analyzer validates project statements
The semantic analyzer SHALL validate `project` statements similarly to `add` statements: the trait name must resolve to a declared trait, field assignments must refer to fields on that trait, field values must type-check, and any `to` target expression must have type `entity_id`. If no target is supplied, the statement targets `self` and therefore requires a current entity context.

#### Scenario: Project declared trait to self accepted
- **WHEN** a handler on an entity-filtered system contains `project GroundContact: normal = n`
- **THEN** the semantic analyzer accepts it if `GroundContact.normal` exists and `n` has the correct type

#### Scenario: Project target must be entity_id
- **WHEN** a handler contains `project Highlighted to 123`
- **THEN** the semantic analyzer reports that the projection target must have type `entity_id`

#### Scenario: Unknown projected trait rejected
- **WHEN** a handler contains `project GhostFact`
- **THEN** the semantic analyzer reports that `GhostFact` is not a declared trait

#### Scenario: Unknown projected field rejected
- **WHEN** a handler projects `GroundContact` with an assignment to an undeclared field
- **THEN** the semantic analyzer reports an unknown field error

### Requirement: Semantic analyzer rejects incompatible projected traits
Projecting a trait with `persist` or `sync` fields SHALL be rejected in v1 because those modifiers describe durable storage behavior and are incompatible with transient projected overlays.

#### Scenario: Persist projected trait rejected
- **WHEN** a trait has a `persist` field and authored code attempts to `project` that trait
- **THEN** the semantic analyzer reports that persistent traits cannot be projected

#### Scenario: Sync projected trait rejected
- **WHEN** a trait has a `sync` field and authored code attempts to `project` that trait
- **THEN** the semantic analyzer reports that synced traits cannot be projected

### Requirement: Semantic analyzer models projected trait filter access
When a system filters on a trait, semantic analysis SHALL treat the alias type the same whether the trait is supplied by durable storage or projected overlay storage at runtime. Projected overlays do not change the static field type of the alias.

#### Scenario: Projected trait alias has trait field types
- **WHEN** a system filters `DamageFlash as flash`
- **THEN** `flash.color` type-checks according to the declared `DamageFlash` trait fields

### Requirement: Semantic analyzer validates query trait filters
The semantic analyzer SHALL validate that every trait referenced in a query-filter bracket resolves to a declared trait in scope using the same local/imported symbol rules as other trait-aware language constructs.

The semantic analyzer SHALL distinguish a query expression from a plain extern-function call by recognizing a resolved extern-function callee used with attached query-filter bracket metadata in a recognized query namespace.

#### Scenario: Query function resolved through imported module path
- **WHEN** authored code imports `std.query` and uses `std.query.count[EnemyAI]()`
- **THEN** the semantic analyzer resolves the callee as a query function from the imported module namespace rather than requiring a built-in keyword form

#### Scenario: Plain extern function without query filter remains ordinary call
- **WHEN** authored code uses `std.math.sqrt(x)`
- **THEN** the semantic analyzer treats it as a normal extern-function call because no query-filter metadata is attached

#### Scenario: Extern function with query filter in recognized namespace becomes query expression
- **WHEN** authored code uses `std.query.first[Boss]()`
- **THEN** the semantic analyzer treats it as a recognized query expression because the resolved callee is in a query namespace and the call carries query-filter metadata

#### Scenario: Valid query filter traits resolve successfully
- **WHEN** authored code uses `query.count[EnemyAI, not Dead]()` and both `EnemyAI` and `Dead` are declared or imported traits
- **THEN** the semantic analyzer accepts the query filter

#### Scenario: Unknown query filter trait is rejected
- **WHEN** authored code uses `query.first[GhostBoss]()` and `GhostBoss` is not a declared or imported trait
- **THEN** the semantic analyzer reports an error for the undeclared trait in the query filter

### Requirement: Semantic analyzer assigns query return types
The semantic analyzer SHALL assign fixed return types to recognized query expressions: `exists` returns `bool`, `count` returns `int`, `first`, `nearest`, and `parent` return `entity_id`, and `all` / overlap-style plural queries return `list[entity_id]`.

#### Scenario: First query inferred as entity_id
- **WHEN** authored code binds `let target = query.first[Boss]()`
- **THEN** the semantic analyzer infers `target` as type `entity_id`

#### Scenario: All query inferred as list of entity ids
- **WHEN** authored code binds `let hits = query.all[Enemy]()`
- **THEN** the semantic analyzer infers `hits` as type `list[entity_id]`

#### Scenario: Parent query inferred as entity_id
- **WHEN** authored code binds `let owner = query.parent(of = child_id)`
- **THEN** the semantic analyzer infers `owner` as type `entity_id`

### Requirement: Query expressions require world-aware context
Query expressions SHALL be treated as world-access operations. They MUST be accepted inside system event handlers and rejected inside pure `func` bodies.

#### Scenario: Query inside system handler accepted
- **WHEN** authored code evaluates `query.exists[Boss]()` inside `on tick:`
- **THEN** the semantic analyzer accepts the expression

#### Scenario: Query inside pure func rejected
- **WHEN** authored code evaluates `query.count[EnemyAI]()` inside a non-extern `func`
- **THEN** the semantic analyzer reports that query expressions require world access and are not allowed in pure functions

### Requirement: Semantic analyzer validates named arguments for recognized queries
The semantic analyzer SHALL validate the required named arguments for recognized spatial query operations and SHALL type-check each provided argument.

#### Scenario: Nearest query requires from argument
- **WHEN** authored code uses `query.nearest[Transform, Enemy]()` with no `from =` argument
- **THEN** the semantic analyzer reports that `nearest` requires a `from` argument

#### Scenario: Overlap box validates center and size argument types
- **WHEN** authored code uses `query.overlap_box[Pickup](center = p, size = s)` in `std.physics.flat.query`
- **THEN** the semantic analyzer validates `center` and `size` against the expected 2D spatial types

#### Scenario: Overlap circle validates center and radius argument types
- **WHEN** authored code uses `query.overlap_circle[Pickup](center = p, radius = r)` in `std.physics.flat.query`
- **THEN** the semantic analyzer validates `center` as a 2D point and `radius` as `float`

#### Scenario: Raycast validates origin direction and distance arguments
- **WHEN** authored code uses `query.raycast[Wall](origin = p, dir = d, max_dist = dist)`
- **THEN** the semantic analyzer validates the spatial argument set required by the active flat or volume query namespace

#### Scenario: Parent query validates entity argument
- **WHEN** authored code uses `query.parent(of = some_expr)`
- **THEN** the semantic analyzer validates that `some_expr` has type `entity_id`

### Requirement: Entity declarations replace unit declarations

The semantic analyzer SHALL treat `entity` declarations as the module/scene-load ECS entity instance construct. Legacy `unit` declarations SHALL NOT be accepted by the active language surface.

#### Scenario: Inline entity accepted
- **WHEN** `entity Player:` contains valid nested trait entries
- **THEN** semantic analysis accepts the entity as one load-time entity instance

#### Scenario: Legacy unit declaration rejected
- **WHEN** a parsed program contains a legacy `unit Player:` declaration
- **THEN** semantic analysis reports that `unit` has been renamed to `entity`

### Requirement: Template-backed entity references resolve to templates

The semantic analyzer SHALL resolve every `entity Name from TemplateName:` reference to a template declaration using existing local and imported symbol rules.

#### Scenario: Template-backed entity from local template accepted
- **WHEN** `entity Gem1 from BlueGem:` references a local `template BlueGem`
- **THEN** semantic analysis accepts the entity

#### Scenario: Template-backed entity from non-template rejected
- **WHEN** `entity Gem1 from Collectible:` references a trait rather than a template
- **THEN** semantic analysis reports that template-backed entities must instantiate templates

#### Scenario: Template-backed entity from private imported template rejected
- **WHEN** an entity references a non-public template from another module
- **THEN** semantic analysis reports that the template is not importable

#### Scenario: Template-backed entity from undefined template rejected
- **WHEN** `entity Gem1 from MissingGem:` references no visible template
- **THEN** semantic analysis reports an undefined template error

### Requirement: Template-backed entity overrides merge with template archetype

The semantic analyzer SHALL construct a flattened entity archetype by starting from the referenced template's flattened archetype and applying the entity declaration's nested trait override entries field-by-field.

#### Scenario: Template-backed entity overrides transform position
- **WHEN** `BlueGem` provides default shape and collectible traits and `entity Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** the entity archetype contains the template traits plus the overridden world transform field

#### Scenario: Template-backed entity partial override preserves template fields
- **WHEN** `Enemy` initializes `Health.max_hp` and `Health.hp`, and `entity Boss from Enemy:` overrides only `Health.hp`
- **THEN** the entity archetype uses the override for `hp` and preserves the template value for `max_hp`

#### Scenario: Unknown override field rejected
- **WHEN** a template-backed entity override assigns a field that does not exist on the target trait
- **THEN** semantic analysis reports an unknown field error for the entity

#### Scenario: Required field may be satisfied by entity override
- **WHEN** a template leaves a required field unset and `entity Gem1 from GemTemplate:` provides that field in an override block
- **THEN** semantic analysis accepts the entity

#### Scenario: Required field missing rejected
- **WHEN** a template leaves a required field unset and `entity Gem1 from GemTemplate:` also omits that field
- **THEN** semantic analysis reports a required field error for the entity

### Requirement: Entity declaration names are not entity_id constants

Entity declaration names SHALL be used for diagnostics and generated-symbol stability, but SHALL NOT introduce author-visible `entity_id` constants in this change.

#### Scenario: Entity name is not a value expression
- **WHEN** handler code attempts to use `Gem1` as an expression solely because `entity Gem1 from BlueGem:` exists
- **THEN** semantic analysis does not resolve `Gem1` as an `entity_id` value

### Requirement: Template-backed entity ordering is deterministic

The semantic analyzer SHALL preserve source declaration order among inline entities and template-backed entities so backends can instantiate them deterministically.

#### Scenario: Mixed entity order preserved
- **WHEN** a module declares `entity A:`, then `entity B from T:`, then `entity C:`
- **THEN** the decorated program records the entities in the source declaration order `A`, `B`, `C`

