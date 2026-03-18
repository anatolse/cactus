## ADDED Requirements

### Requirement: `let` and `var` local variable declaration statements
The parser SHALL accept `let` and `var` as statement forms inside event handler bodies and `func` bodies. Type annotation is optional; when omitted the type is inferred from the initializer expression.

```ebnf
let_decl = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_decl = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
```

The `statement` production is updated to include `let_decl` and `var_decl`.

#### Scenario: Let declaration without type annotation parsed
- **WHEN** `let speed = 5.0` appears in a handler body
- **THEN** the parser produces a `LetDecl` node with `name = "speed"`, no explicit type, and a float literal initializer

#### Scenario: Var declaration with type annotation parsed
- **WHEN** `var count: int = 0` appears in a handler body
- **THEN** the parser produces a `VarDecl` node with `name = "count"`, `type = int`, and integer literal initializer `0`

#### Scenario: Let declaration with expression initializer parsed
- **WHEN** `let dir = math.forward(Transform.rotation)` appears in a handler body
- **THEN** the parser produces a `LetDecl` node with a method call expression as the initializer

---

### Requirement: `spawn` as primary expression returning `entity_id`
In addition to the statement form, the parser SHALL accept `spawn` as a primary expression. The result is an `entity_id` value that can be assigned or used in expressions.

```ebnf
spawn_expr   = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" ;
primary_expr = ... | spawn_expr ;
```

The statement form `spawn Foo(...)` on its own line is now sugar for a `spawn_expr` whose result is discarded.

#### Scenario: Spawn expression assigned to let binding
- **WHEN** `let enemy = spawn Enemy(pos = vec2(400.0, 200.0))` appears in a handler body
- **THEN** the parser produces a `LetDecl` with a `SpawnExpr` initializer returning an `entity_id`

#### Scenario: Spawn statement (discard result) still valid
- **WHEN** `spawn Enemy(pos = vec2(400.0, 200.0))` appears as a standalone statement
- **THEN** the parser produces an `ExprStmt` wrapping a `SpawnExpr` (result discarded)

---

## MODIFIED Requirements

### Requirement: `filter:` and `exclude:` both optional in system grammar (BREAKING for old bracket syntax)
Both `filter:` and `exclude:` are optional on system declarations. The parser SHALL accept systems with `filter:` only, `exclude:` only, both, or neither. The old bracket-list syntax `filter: [A, B, C]` is rejected.

Each `filter:` entry now supports an optional `as IDENTIFIER` alias. Updated EBNF:

```ebnf
system_decl    = "system" IDENTIFIER ":" NEWLINE INDENT
                 [ filter_clause ]
                 [ exclude_clause ]
                 { event_handler }
                 DEDENT ;

filter_clause  = "filter" ":" NEWLINE INDENT
                 { filter_entry }
                 DEDENT ;

filter_entry   = dotted_name [ "as" IDENTIFIER ] NEWLINE ;

exclude_clause = "exclude" ":" NEWLINE INDENT
                 { IDENTIFIER NEWLINE }
                 DEDENT ;
```

Note: `target_clause` is removed entirely (see REMOVED section).

#### Scenario: System with filter only parsed correctly
- **WHEN** a system has `filter:` but no `exclude:`
- **THEN** the parser produces a `SystemDecl` with `filter` list and empty `exclude` list

#### Scenario: System with exclude only parsed correctly
- **WHEN** a system has `exclude:` but no `filter:`
- **THEN** the parser produces a `SystemDecl` with empty `filter` list and populated `exclude` list

#### Scenario: System with no filter or exclude parsed correctly
- **WHEN** a system has neither `filter:` nor `exclude:`
- **THEN** the parser produces a `SystemDecl` with empty `filter` and `exclude` lists (match-all)

#### Scenario: Old bracket syntax produces parse error
- **WHEN** source contains `filter: [Position, EnemyAI]`
- **THEN** the parser SHALL report an error: "unexpected '['; use indented block syntax for filter"

#### Scenario: Filter entry with as alias parsed
- **WHEN** a system has a filter entry `Position as pos`
- **THEN** the parser produces a `FilterEntry` with `trait_name = "Position"` and `alias = "pos"`

#### Scenario: Filter entry without alias parsed (trait name is implicit alias)
- **WHEN** a system has a filter entry `Position` with no `as` clause
- **THEN** the parser produces a `FilterEntry` with `trait_name = "Position"` and `alias = nil` (semantic analyzer uses trait name as default)

#### Scenario: Filter entry with qualified name and alias
- **WHEN** a system has a filter entry `phys.Body as body`
- **THEN** the parser produces a `FilterEntry` with `trait_name = "phys.Body"` and `alias = "body"`

---

### Requirement: `destroy` statement grammar
The parser SHALL accept `destroy` as a statement inside event handler bodies. The statement optionally takes an expression evaluating to an `entity_id` to destroy a specific entity. With no expression, it destroys the current entity.

```ebnf
destroy_stmt = "destroy" [ expression ] NEWLINE ;
```

#### Scenario: Destroy current entity (no argument) parsed
- **WHEN** `destroy` appears on its own line inside a handler
- **THEN** the parser produces a `DestroyStmt` with no target expression

#### Scenario: Destroy specific entity by ID parsed
- **WHEN** `destroy PlayerComposition.gun` appears in a handler
- **THEN** the parser produces a `DestroyStmt` with a member access expression as the target

#### Scenario: Destroy with entity_id variable parsed
- **WHEN** `destroy enemy_id` appears in a handler
- **THEN** the parser produces a `DestroyStmt` with an identifier expression as the target

---

### Requirement: `emit` statement grammar
The parser SHALL accept `emit` as a statement inside event handler bodies with an optional `to expression` suffix for targeted dispatch.

```ebnf
emit_stmt = "emit" IDENTIFIER "(" [ arg_list ] ")" [ "to" expression ] NEWLINE ;
```

#### Scenario: Broadcast emit (no target) parsed
- **WHEN** `emit PlayerDamaged(amount = 5)` appears in a handler
- **THEN** the parser produces an `EmitStmt` with `event_name = "PlayerDamaged"`, one arg, and `target = nil`

#### Scenario: Targeted emit parsed
- **WHEN** `emit Damage(amount = 10) to EnemyAI.target` appears in a handler
- **THEN** the parser produces an `EmitStmt` with `event_name = "Damage"`, one arg, and a target expression

#### Scenario: Targeted emit to spawned entity
- **WHEN** `emit Configure(value = 5) to enemy_id` appears in a handler
- **THEN** the parser produces an `EmitStmt` with a target identifier expression

---

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `event`, `func`, `template`.

Note: `view` and `interface` are no longer supported top-level declarations.

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level
- **THEN** the parser reports an error with the source location and expected declaration types

#### Scenario: view keyword produces error
- **WHEN** `view` appears as a top-level keyword
- **THEN** the parser reports an error: "'view' is not supported in this version"

#### Scenario: interface keyword produces error
- **WHEN** `interface` appears as a top-level keyword
- **THEN** the parser reports an error: "'interface' is not supported in this version"

---

### Requirement: Unit parsing with apply and config blocks
The parser SHALL parse `[pub] unit Name:` blocks containing `apply:` (list of traits) and optional `config:` (field overrides). The `child:` block is no longer supported.

#### Scenario: Unit with apply and config
- **WHEN** the source contains `unit Cactus:` with `apply:` listing traits and `config:` with field assignments
- **THEN** the parser produces a UnitNode with an ApplyBlock and a ConfigBlock

#### Scenario: Unit with child block produces error
- **WHEN** `child:` appears inside a unit declaration
- **THEN** the parser reports an error: "'child:' is not supported; use spawn in on spawn() handlers instead"

---

### Requirement: `template` declaration grammar
The parser SHALL accept `template_decl` as a new top-level declaration, structurally identical to `unit_decl` except using the `TEMPLATE` keyword. `child:` is not supported.

```ebnf
template_decl = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                apply_block
                [ config_block ]
                DEDENT ;
```

#### Scenario: Template with apply and config parsed
- **WHEN** source contains a complete `template` declaration with `apply:` and `config:` blocks
- **THEN** the parser produces a `TemplateDecl` AST node with `apply` and `config` children

#### Scenario: Template with pub modifier parsed
- **WHEN** `pub template Foo:` appears in source
- **THEN** the parser produces a `TemplateDecl` node with `is_pub = true`

#### Scenario: Template with child block produces error
- **WHEN** `child:` appears inside a template declaration
- **THEN** the parser reports an error: "'child:' is not supported; use spawn in on spawn() handlers instead"

---

### Requirement: Lifecycle event handler grammar
The parser SHALL accept `on` handlers for all built-in lifecycle event names. Updated list includes `input`, `fixed_tick`, and `late_tick` in addition to existing names.

```ebnf
lifecycle_name = "spawn" | "destroy" | "load" | "unload"
               | "tick" | "input" | "fixed_tick" | "late_tick" ;
```

Lifecycle handlers and their signatures:
- `on tick(dt: float):` — one float param
- `on fixed_tick(dt: float):` — one float param
- `on late_tick(dt: float):` — one float param
- `on input():` — no params
- `on spawn():` — no params
- `on destroy():` — no params
- `on load():` — no params
- `on unload():` — no params

#### Scenario: on fixed_tick handler parsed
- **WHEN** `on fixed_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and one `dt: float` param

#### Scenario: on late_tick handler parsed
- **WHEN** `on late_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and one `dt: float` param

#### Scenario: on input handler parsed
- **WHEN** `on input():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and empty param list

#### Scenario: on spawn handler parsed
- **WHEN** `on spawn():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "spawn"` and empty param list

#### Scenario: on unload handler parsed
- **WHEN** `on unload():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "unload"` and empty param list

---

### Requirement: Module path support in `module` and `use` declarations
The parser SHALL accept dotted identifiers in `module` and `use` declarations. The parser SHALL also support `as` aliases in `use` declarations.

#### Scenario: Dotted module declaration
- **WHEN** `module enemies.walker` appears at the top level
- **THEN** the parser produces a `ModuleNode` with `name = "enemies.walker"`

#### Scenario: Use with alias
- **WHEN** `use phys.body as b` appears
- **THEN** the parser produces a `UseNode` with `module_name = "phys.body"` and `alias = "b"`

---

## REMOVED Requirements

### Requirement: View parsing with UI element tree
**Reason:** `view` is deferred from v0.1. No semantics are defined for retained UI in this version.
**Migration:** Remove `view` declarations from source files. UI can be modeled as ECS traits read by a rendering backend system.

### Requirement: `target:` clause in system grammar
**Reason:** `target: gpu` has no defined type rules or memory model in v0.1. `target: cpu` is the only target and is implicit.
**Migration:** Remove any `target: cpu` or `target: gpu` clauses from system declarations.
