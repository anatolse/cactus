## Requirements

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `event`, `func`, `template`, `asset`, `input`.

Note: `view` and `interface` are not supported top-level declarations in v0.2.

```ebnf
declaration = module_decl | use_decl | const_block | struct_decl
            | enum_decl | trait_decl | unit_decl | template_decl | system_decl
            | event_decl | func_decl | asset_decl | input_decl ;
```

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Asset declaration in program
- **WHEN** the source contains `asset PlayerMesh: mesh = "player.glb"` at the top level
- **THEN** the parser produces a ProgramNode containing an `AssetDecl` node

#### Scenario: Input declaration in program
- **WHEN** the source contains an `input Jump: button` declaration at the top level
- **THEN** the parser produces a ProgramNode containing an `InputDecl` node

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level
- **THEN** the parser reports an error with the source location and expected declaration types

### Requirement: Marker trait grammar (body is optional)
The parser SHALL accept `trait` declarations with no colon and no body. The trait body (colon + indented block) is optional:

```ebnf
trait_decl = [ "pub" ] "trait" IDENTIFIER
             [ ":" NEWLINE INDENT
               { field_decl | event_handler | func_decl }
               DEDENT ] ;
```

#### Scenario: Marker trait (no colon) parsed
- **WHEN** `trait Persistent` appears at the top level (no colon, no body)
- **THEN** the parser produces a `TraitDecl` with `name = "Persistent"` and empty `fields` list

#### Scenario: Marker trait with pub modifier parsed
- **WHEN** `pub trait Frozen` appears at the top level
- **THEN** the parser produces a `TraitDecl` with `is_pub = true` and empty `fields` list

#### Scenario: Regular trait with body still parsed
- **WHEN** `trait Health:` followed by an indented body appears
- **THEN** the parser parses it as a normal data trait (existing behavior)

### Requirement: Trait parsing with field modifiers
The parser SHALL parse `trait Name:` blocks containing fields with modifiers (`let`, `var`, `persist`, `sync`, `pub`) and event handlers (`on event_name(params):`). Fields SHALL support default value expressions.

#### Scenario: Trait with persist and sync fields
- **WHEN** the source contains `trait Player:` with fields `persist var health: int = 100` and `sync var position: vec3`
- **THEN** the parser produces a TraitNode with two FieldNodes, the first having persist=true and the second having sync=true

#### Scenario: Trait with event handler
- **WHEN** the source contains `trait Damageable:` with `on damage(amount: int):` block
- **THEN** the parser produces a TraitNode containing an EventHandlerNode with event_name "damage"

### Requirement: Unit parsing with apply and config blocks
The parser SHALL parse `[pub] unit Name:` blocks containing `apply:` (list of traits) and optional `config:` (field overrides). The `child:` block is not supported.

#### Scenario: Unit with apply and config
- **WHEN** the source contains `unit Cactus:` with `apply:` listing traits and `config:` with field assignments
- **THEN** the parser produces a UnitNode with an ApplyBlock and a ConfigBlock

### Requirement: `disabled` trait state in `apply:` block
The parser SHALL accept an optional `: disabled` annotation on trait entries within `apply:` blocks. This applies to both `unit_decl` and `template_decl`. The EBNF for an apply entry becomes:

```ebnf
apply_entry = IDENTIFIER [ ":" "disabled" ] NEWLINE ;
apply_block = "apply" ":" NEWLINE INDENT
              { apply_entry }
              DEDENT ;
```

#### Scenario: Trait with disabled annotation parsed
- **WHEN** `Frozen: disabled` appears inside `apply:`
- **THEN** the parser produces an apply entry with `trait_name = "Frozen"` and `initially_active = false`

#### Scenario: Trait without annotation defaults to active
- **WHEN** `Position` appears inside `apply:` with no annotation
- **THEN** the parser produces an apply entry with `trait_name = "Position"` and `initially_active = true`

### Requirement: `template` declaration grammar
The parser SHALL accept `template_decl` as a new top-level declaration, structurally identical to `unit_decl` except using the `TEMPLATE` keyword. The `child:` block is not supported.

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

### Requirement: `filter:` and `exclude:` both optional in system grammar
Both `filter:` and `exclude:` are optional on system declarations. The parser SHALL accept systems with `filter:` only, `exclude:` only, both, or neither. The old bracket-list syntax `filter: [A, B, C]` is rejected.

Each `filter:` entry supports an optional `as IDENTIFIER` alias for field access. Updated EBNF:

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

#### Scenario: Filter entry without alias parsed
- **WHEN** a system has a filter entry `Position` with no `as` clause
- **THEN** the parser produces a `FilterEntry` with `trait_name = "Position"` and `alias = nil`

#### Scenario: Filter entry with qualified name and alias
- **WHEN** a system has a filter entry `phys.Body as body`
- **THEN** the parser produces a `FilterEntry` with `trait_name = "phys.Body"` and `alias = "body"`

### Requirement: `let` and `var` local variable declaration statements
The parser SHALL accept `let` and `var` as statement forms inside event handler bodies and `func` bodies. Type annotation is optional.

```ebnf
let_decl = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_decl = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
```

#### Scenario: Let declaration without type annotation parsed
- **WHEN** `let speed = 5.0` appears in a handler body
- **THEN** the parser produces a `LetDecl` node with `name = "speed"`, no explicit type, and a float literal initializer

#### Scenario: Var declaration with type annotation parsed
- **WHEN** `var count: int = 0` appears in a handler body
- **THEN** the parser produces a `VarDecl` node with `name = "count"`, `type = int`, and integer literal initializer `0`

#### Scenario: Let declaration with expression initializer parsed
- **WHEN** `let dir = math.forward(Transform.rotation)` appears in a handler body
- **THEN** the parser produces a `LetDecl` node with a method call expression as the initializer

### Requirement: `spawn` as primary expression returning `entity_id`
The parser SHALL accept `spawn` as a primary expression in addition to the statement form.

```ebnf
spawn_expr   = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" ;
primary_expr = ... | spawn_expr ;
```

#### Scenario: Spawn expression assigned to let binding
- **WHEN** `let enemy = spawn Enemy(pos = vec2(400.0, 200.0))` appears in a handler body
- **THEN** the parser produces a `LetDecl` with a `SpawnExpr` initializer

#### Scenario: Spawn statement (discard result) still valid
- **WHEN** `spawn Enemy(pos = vec2(400.0, 200.0))` appears as a standalone statement
- **THEN** the parser produces an `ExprStmt` wrapping a `SpawnExpr`

### Requirement: `destroy` statement grammar
The parser SHALL accept `destroy` as a statement inside event handler bodies. Without an expression, it destroys the current entity. With an expression, it destroys the specified entity.

```ebnf
destroy_stmt = "destroy" [ expression ] NEWLINE ;
```

#### Scenario: Destroy current entity (no argument) parsed
- **WHEN** `destroy` appears on its own line inside a handler
- **THEN** the parser produces a `DestroyStmt` with no target expression

#### Scenario: Destroy specific entity by ID parsed
- **WHEN** `destroy PlayerComposition.gun` appears in a handler
- **THEN** the parser produces a `DestroyStmt` with a member access expression as the target

### Requirement: `emit` statement grammar
The parser SHALL accept `emit` as a statement with an optional `to expression` suffix for targeted dispatch.

```ebnf
emit_stmt = "emit" IDENTIFIER "(" [ arg_list ] ")" [ "to" expression ] NEWLINE ;
```

#### Scenario: Broadcast emit (no target) parsed
- **WHEN** `emit PlayerDamaged(amount = 5)` appears in a handler
- **THEN** the parser produces an `EmitStmt` with `event_name = "PlayerDamaged"` and `target = nil`

#### Scenario: Targeted emit parsed
- **WHEN** `emit Damage(amount = 10) to EnemyAI.target` appears in a handler
- **THEN** the parser produces an `EmitStmt` with a target expression

### Requirement: `load` statement grammar
The parser SHALL accept `load` as a statement inside event handler bodies, followed by a dotted module name.

```ebnf
load_stmt = "load" dotted_name NEWLINE ;
```

#### Scenario: Load with dotted module name parsed
- **WHEN** `load levels.level1` appears in a handler
- **THEN** the parser produces a `LoadStmt` with `module_name = "levels.level1"`

#### Scenario: Load with simple module name parsed
- **WHEN** `load ui` appears in a handler
- **THEN** the parser produces a `LoadStmt` with `module_name = "ui"`

### Requirement: `enable` and `disable` statement grammar
The parser SHALL accept `enable` and `disable` as statements inside event handler bodies.

```ebnf
enable_stmt  = "enable"  IDENTIFIER NEWLINE ;
disable_stmt = "disable" IDENTIFIER NEWLINE ;
```

#### Scenario: Enable statement parsed
- **WHEN** `enable Frozen` appears in a handler
- **THEN** the parser produces an `EnableStmt` with `trait_name = "Frozen"`

#### Scenario: Disable statement parsed
- **WHEN** `disable EnemyAI` appears in a handler
- **THEN** the parser produces a `DisableStmt` with `trait_name = "EnemyAI"`

### Requirement: Lifecycle event handler grammar
The parser SHALL accept `on` handlers for all built-in lifecycle event names:

```ebnf
event_handler = "on" event_name "(" [ param_list ] ")" ":" NEWLINE INDENT
                { statement }
                DEDENT ;

event_name = "tick" | "fixed_tick" | "late_tick"
           | "spawn" | "destroy" | "load" | "unload"
           | "input" | IDENTIFIER ;
```

Built-in lifecycle handlers and their required signatures:

| Handler | Parameters | Phase |
|---|---|---|
| `on input()` | none | Input |
| `on fixed_tick(dt: float)` | `dt: float` | Physics |
| `on tick(dt: float)` | `dt: float` | General |
| `on late_tick(dt: float)` | `dt: float` | Post |
| `on spawn()` | none | Entity |
| `on destroy()` | none | Entity |
| `on load()` | none | Scene |
| `on unload()` | none | Scene |

#### Scenario: on spawn handler parsed
- **WHEN** `on spawn():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "spawn"` and empty param list

#### Scenario: on destroy handler parsed
- **WHEN** `on destroy():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "destroy"` and empty param list

#### Scenario: on load handler parsed
- **WHEN** `on load():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "load"` and empty param list

#### Scenario: on unload handler parsed
- **WHEN** `on unload():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "unload"` and empty param list

#### Scenario: on input() handler parsed (no parameters)
- **WHEN** `on input():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and empty param list

#### Scenario: on fixed_tick handler parsed
- **WHEN** `on fixed_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and one typed parameter `dt: float`

#### Scenario: on late_tick handler parsed
- **WHEN** `on late_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and one typed parameter `dt: float`

### Requirement: `asset_decl` grammar production
The parser SHALL parse `asset` declarations as top-level declarations.

```ebnf
asset_decl = [ "pub" ] "asset" IDENTIFIER ":" asset_type "=" STRING_LITERAL NEWLINE ;
asset_type = "mesh" | "texture" | "sound" | "music" | "font" | "material" ;
```

#### Scenario: Asset declaration at top level parsed
- **WHEN** `asset ShotSound: sound = "audio/shot.wav"` appears at the top level
- **THEN** the parser produces an `AssetDecl` node with `name = "ShotSound"`, `asset_type = sound`, `path = "audio/shot.wav"`

#### Scenario: Pub asset declaration parsed
- **WHEN** `pub asset SharedMesh: mesh = "models/shared.glb"` appears at the top level
- **THEN** the parser produces an `AssetDecl` with `is_pub = true`

#### Scenario: All six asset types accepted
- **WHEN** asset declarations use each of `mesh`, `texture`, `sound`, `music`, `font`, `material`
- **THEN** the parser accepts each and produces an `AssetDecl` with the corresponding asset type

### Requirement: `input_decl` grammar production
The parser SHALL parse `input` declarations as top-level declarations.

```ebnf
input_decl = [ "pub" ] "input" IDENTIFIER ":" ( "button" | "axis" ) NEWLINE INDENT
             { input_prop }
             DEDENT ;
input_prop = IDENTIFIER "=" expression NEWLINE ;
```

#### Scenario: Button input declaration parsed
- **WHEN** `input Jump: button` with body `key = Key.Space` and `gamepad = GamepadButton.South` appears
- **THEN** the parser produces an `InputDecl` with `name = "Jump"`, `kind = button`, and two `InputProp` children

#### Scenario: Axis input declaration with invert parsed
- **WHEN** an axis input includes `invert = true`
- **THEN** the parser produces an `InputProp` with `key = "invert"` and a `BoolLiteral(true)` value expression

### Requirement: Expression parsing with precedence
The parser SHALL parse expressions using precedence climbing, supporting binary operators, unary operators, member access, function calls, lambda expressions, and spawn expressions.

#### Scenario: Binary expression with correct precedence
- **WHEN** the source contains `a + b * c`
- **THEN** the parser produces a BinaryExpr with `+` at the root and `*` as the right child

#### Scenario: Lambda expression
- **WHEN** the source contains `x => x * 2`
- **THEN** the parser produces a LambdaExpr with parameter "x" and a BinaryExpr body

### Requirement: Func parsing with purity contract
The parser SHALL parse `[pub] func name(params) [-> type]:` blocks with a body of statements.

#### Scenario: Pure function with return type
- **WHEN** the source contains `func distance(a: vec3, b: vec3) -> float:`
- **THEN** the parser produces a FuncNode with two parameters and return type float

### Requirement: Const block parsing
The parser SHALL parse `const:` blocks containing name-value assignments where values are string literals, number literals, or hex color literals.

#### Scenario: Const block with strings
- **WHEN** the source contains `const:` with `SHOP_TITLE = "Cactus Shop"` and `MAX_ITEMS = 50`
- **THEN** the parser produces a ConstBlockNode with two ConstAssignment entries

### Requirement: Match expression parsing
The parser SHALL parse `match expr:` blocks with pattern arms.

#### Scenario: Match on enum
- **WHEN** the source contains `match state:` with arms for different enum variants
- **THEN** the parser produces a MatchExpr with the matched expression and a list of arms

### Requirement: Module path support in `module` and `use` declarations
The parser SHALL accept dotted identifiers in `module` and `use` declarations. The parser SHALL also support `as` aliases in `use` declarations.

#### Scenario: Dotted module declaration
- **WHEN** `module enemies.walker` appears at the top level
- **THEN** the parser produces a `ModuleNode` with `name = "enemies.walker"`

#### Scenario: Use with alias
- **WHEN** `use phys.body as b` appears
- **THEN** the parser produces a `UseNode` with `module_name = "phys.body"` and `alias = "b"`
