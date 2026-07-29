## Requirements
### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. The first top-level declaration MUST be exactly one `module` declaration. Supported declarations after the module declaration are: `use`, `const`, `struct`, `enum`, `trait`, `entity`, `system`, `event`, `func`, `extern func`, `template`, `asset`, and `input`.

Note: `view`, `interface`, and legacy `unit` are not supported top-level declarations in this language version.

```ebnf
program     = module_decl { declaration } EOF ;
declaration = use_decl | const_block | struct_decl
            | enum_decl | trait_decl | entity_decl | template_decl | system_decl
            | event_decl | func_decl | extern_func_decl | asset_decl | input_decl ;
```

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Missing module declaration rejected
- **WHEN** the source begins with `trait Position` and has no preceding `module` declaration
- **THEN** the parser reports that a source file must start with a module declaration

#### Scenario: Duplicate module declaration rejected
- **WHEN** the source contains two `module` declarations
- **THEN** the parser reports that only one module declaration is allowed

#### Scenario: Asset declaration in program
- **WHEN** the source contains `module game.assets` followed by `asset PlayerMesh: mesh = "player.glb"` at the top level
- **THEN** the parser produces a ProgramNode containing an `AssetDecl` node

#### Scenario: Input declaration in program
- **WHEN** the source contains `module game.input` followed by an `input Jump: button` declaration at the top level
- **THEN** the parser produces a ProgramNode containing an `InputDecl` node

#### Scenario: Extern func declaration in program
- **WHEN** `module game.math` is followed by `pub extern func lerp(a, b, t: float) float` at the top level
- **THEN** the parser produces a ProgramNode containing a `FuncNode` with `is_extern = true`

#### Scenario: Entity declaration in program
- **WHEN** the source contains `module game.scene` followed by `entity Player:` at the top level
- **THEN** the parser produces a ProgramNode containing an entity declaration node

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level after the module declaration
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

### Requirement: Entity parsing with nested archetype entries
The parser SHALL parse `[pub] entity Name:` blocks as a sequence of archetype entries. Each entry is either a body-level `use TemplateName` entry, a bare trait name for a marker trait, or a trait name followed by `:` and an indented field-assignment block. `apply:` and `config:` blocks are not part of entity syntax.

#### Scenario: Entity with nested trait block parsed
- **WHEN** the source contains `entity Cactus:` with `Position:` followed by indented field assignments
- **THEN** the parser produces an entity declaration containing a trait entry for `Position` and its nested assignments

#### Scenario: Entity with marker trait parsed
- **WHEN** the source contains `entity Cactus:` with a bare `Persistent` entry in the body
- **THEN** the parser produces an entity declaration containing a marker-trait entry for `Persistent`

#### Scenario: Entity with template use parsed
- **WHEN** the source contains `entity Walker1:` with an indented `use WalkerEnemy` entry
- **THEN** the parser produces an entity declaration containing an archetype template-use entry for `WalkerEnemy`

#### Scenario: Pub entity parsed
- **WHEN** the source contains `pub entity Player:`
- **THEN** the parser produces an entity declaration with `is_pub = true`

### Requirement: `template` declaration grammar

The parser SHALL accept `template_decl` as a top-level declaration whose body is a sequence of archetype entries, structurally identical to inline `entity` declarations except using the `TEMPLATE` keyword. Body-level `use TemplateName` entries in this context are template-composition entries, not module imports. `apply:` and `config:` blocks are not part of template syntax.

```ebnf
template_decl = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                { archetype_entry }
                DEDENT ;

archetype_entry = template_use_entry | archetype_trait_entry ;
template_use_entry = "use" dotted_name NEWLINE ;
```

#### Scenario: Template with nested trait blocks parsed
- **WHEN** source contains `template Enemy:` followed by `Position:` and indented field assignments
- **THEN** the parser produces a `TemplateDecl` AST node containing a trait entry for `Position`

#### Scenario: Template with pub modifier parsed
- **WHEN** `pub template Foo:` appears in source
- **THEN** the parser produces a `TemplateDecl` node with `is_pub = true`

#### Scenario: Template with template use parsed
- **WHEN** source contains `template WalkerEnemy:` followed by an indented `use EnemyBase` entry
- **THEN** the parser produces a `TemplateDecl` AST node containing an archetype template-use entry for `EnemyBase`

#### Scenario: Qualified archetype template use parsed
- **WHEN** source contains `use enemies.WalkerEnemy` inside a template or entity body
- **THEN** the parser records the used template name as a dotted name for archetype template composition

### Requirement: Template-backed entity declarations

The parser SHALL accept top-level `entity` declarations that instantiate a named entity from a template reference with nested trait override entries.

```ebnf
entity_decl = [ "pub" ] "entity" IDENTIFIER [ "from" dotted_name ] ":" NEWLINE INDENT
              { archetype_entry }
              DEDENT ;
```

When the optional `from dotted_name` clause is present, body entries SHALL be interpreted as overrides applied to the referenced template's flattened archetype.

#### Scenario: Template-backed entity from local template parsed
- **WHEN** source contains `entity Gem1 from BlueGem:` followed by nested trait override entries
- **THEN** the parser produces an entity declaration with name `Gem1`, template reference `BlueGem`, and the parsed override entries

#### Scenario: Template-backed entity from qualified template parsed
- **WHEN** source contains `entity Gem1 from items.BlueGem:`
- **THEN** the parser records the template reference as the dotted name `items.BlueGem`

#### Scenario: Template-backed entity from aliased template parsed
- **WHEN** source contains `use items.gems as gems` and `entity Gem1 from gems.BlueGem:`
- **THEN** the parser records the template reference as the dotted name `gems.BlueGem`

#### Scenario: Entity declarations are top-level only
- **WHEN** `entity Gem1 from BlueGem:` appears inside a system handler
- **THEN** the parser reports that entity declarations are only valid at the top level

### Requirement: Legacy unit declarations rejected

The parser SHALL reject legacy `unit` declarations. Authors SHALL use `entity` declarations for module/scene-load entity instances.

#### Scenario: Legacy unit declaration rejected
- **WHEN** source contains `unit Player:` at the top level
- **THEN** the parser reports that `unit` has been renamed to `entity`

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
The parser SHALL accept `spawn` as a primary expression and statement using block syntax instead of parenthesized flat arguments.

```ebnf
spawn_expr   = "spawn" IDENTIFIER ":" NEWLINE INDENT
               { archetype_trait_entry }
               DEDENT ;
primary_expr = ... | spawn_expr ;
```

#### Scenario: Spawn expression assigned to let binding
- **WHEN** `let enemy = spawn Enemy:` appears with an indented `Position:` override block
- **THEN** the parser produces a `LetDecl` with a `SpawnExpr` initializer carrying nested override entries

#### Scenario: Spawn statement discard result valid
- **WHEN** `spawn Enemy:` appears as a standalone statement with an indented override block
- **THEN** the parser produces a statement wrapping a `SpawnExpr`

### Requirement: `destroy` statement grammar
The parser SHALL accept `destroy` as a statement inside event handler bodies. Without an expression, it destroys the current entity. With an expression, it destroys the specified entity. The `self` keyword SHALL be accepted as a valid target expression.

```ebnf
destroy_stmt = "destroy" [ expression ] NEWLINE ;
```

#### Scenario: Destroy current entity (no argument) parsed
- **WHEN** `destroy` appears on its own line inside a handler
- **THEN** the parser produces a `DestroyStmt` with no target expression

#### Scenario: Destroy specific entity by ID parsed
- **WHEN** `destroy PlayerComposition.gun` appears in a handler
- **THEN** the parser produces a `DestroyStmt` with a member access expression as the target

#### Scenario: Destroy self parsed
- **WHEN** `destroy self` appears in a handler
- **THEN** the parser produces a `DestroyStmt` with a `self` target expression

### Requirement: `self` parses as a reserved primary expression
The parser SHALL recognize `self` as a reserved keyword primary expression rather than as an identifier.

#### Scenario: `self` parsed in member assignment
- **WHEN** a handler contains `Parent.parent = self`
- **THEN** the parser produces a dedicated `self` expression node in the assignment value

#### Scenario: `self` parsed as destroy target
- **WHEN** a handler contains `destroy self`
- **THEN** the parser produces a `DestroyStmt` whose target expression is the `self` expression node

### Requirement: `emit` statement grammar
The parser SHALL accept `emit` as a statement using block syntax for payload initialization and an optional `to expression` suffix before the colon for targeted dispatch.

```ebnf
emit_stmt = "emit" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
            { IDENTIFIER "=" expression NEWLINE }
            DEDENT ;
```

#### Scenario: Broadcast emit block parsed
- **WHEN** `emit PlayerDamaged:` appears with indented payload field assignments
- **THEN** the parser produces an `EmitStmt` with `event_name = "PlayerDamaged"`, no target, and the parsed payload assignments

#### Scenario: Targeted emit parsed
- **WHEN** `emit Damage to EnemyAI.target:` appears with indented payload field assignments
- **THEN** the parser produces an `EmitStmt` with a target expression and the parsed payload assignments

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

### Requirement: `add` statement parsing
The parser SHALL recognize `add IDENTIFIER` as a statement inside event handler bodies. The `add` statement has two forms: bare (for markers and all-defaulted traits) and block (for data traits with field assignments). An optional `to expr` clause may appear after the trait name to specify a target entity. The `add` and `to` keywords SHALL be added to the keyword list.

```ebnf
add_stmt = "add" IDENTIFIER ["to" expr] NEWLINE
         | "add" IDENTIFIER ["to" expr] ":" NEWLINE INDENT
           { IDENTIFIER "=" expr NEWLINE }
           DEDENT ;
```

#### Scenario: bare add statement parsed
- **WHEN** `add Frozen` appears in a handler body
- **THEN** the parser produces an `AddTraitStmt` with `trait_name = "Frozen"`, empty field assignments, no `target_expr`

### Requirement: `remove` statement parsing
The parser SHALL recognize `remove IDENTIFIER` as a statement inside event handler bodies. An optional `from expr` clause may follow to specify a target entity. The `remove` and `from` keywords SHALL be added to the keyword list.

```ebnf
remove_stmt = "remove" IDENTIFIER ["from" expr] NEWLINE ;
```

#### Scenario: bare remove statement parsed
- **WHEN** `remove Frozen` appears in a handler body
- **THEN** the parser produces a `RemoveTraitStmt` with `trait_name = "Frozen"` and no `target_expr`

### Requirement: Lifecycle event handler grammar
The parser SHALL accept `on` handlers using the parameter-free syntax. The `( param_list )` is removed entirely. An optional `as IDENTIFIER` alias clause is added after the event name. The event name accepts both reserved lifecycle keywords and user-defined identifiers.

```ebnf
event_handler = "on" event_name [ "as" IDENTIFIER ] ":" NEWLINE INDENT
                { statement }
                DEDENT ;

event_name = "tick" | "fixed_tick" | "late_tick"
           | "spawn" | "destroy" | "load" | "unload"
           | "input" | IDENTIFIER ;
```

The handler no longer carries a parameter list node; instead, `EventHandlerNode` has an optional `alias: string` field.

#### Scenario: on tick handler parsed without parameters
- **WHEN** `on tick:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "tick"`, no params, and `alias = nil`

#### Scenario: on tick with alias parsed
- **WHEN** `on tick as t:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "tick"` and `alias = "t"`

#### Scenario: on fixed_tick handler parsed without parameters
- **WHEN** `on fixed_tick:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and `alias = nil`

#### Scenario: on late_tick handler parsed
- **WHEN** `on late_tick:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and `alias = nil`

#### Scenario: on spawn handler parsed
- **WHEN** `on spawn:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "spawn"` and `alias = nil`

#### Scenario: on destroy handler parsed
- **WHEN** `on destroy:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "destroy"` and `alias = nil`

#### Scenario: on load handler parsed
- **WHEN** `on load:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "load"` and `alias = nil`

#### Scenario: on unload handler parsed
- **WHEN** `on unload:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "unload"` and `alias = nil`

#### Scenario: on input() handler parsed (no parameters)
- **WHEN** `on input:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and `alias = nil`

#### Scenario: User event handler with alias parsed
- **WHEN** `on PlayerDamaged as dmg:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "PlayerDamaged"` and `alias = "dmg"`

### Requirement: Marker event declaration (body is optional)
The parser SHALL accept `event` declarations with no colon and no body. The event body (colon + indented block) is optional. When an event has a body, each payload field SHALL use bare field syntax rather than trait-style field syntax.

```ebnf
event_decl = [ "pub" ] "event" IDENTIFIER
             [ ":" NEWLINE INDENT
               { event_field_decl }
               DEDENT ] ;

event_field_decl = IDENTIFIER ":" type_ref [ "=" expression ] NEWLINE ;
```

#### Scenario: Marker event (no colon) parsed
- **WHEN** `pub event spawn` appears at the top level with no colon and no body
- **THEN** the parser produces an `EventDecl` with `name = "spawn"` and empty `fields` list

#### Scenario: Event with bare fields parsed
- **WHEN** `event PlayerDamaged:` is followed by `amount: int` in its indented body
- **THEN** the parser produces an `EventDecl` with one field named `amount` of type `int`

#### Scenario: Event field with default value parsed
- **WHEN** `event Spawned:` is followed by `kind: int = 1` in its indented body
- **THEN** the parser produces an `EventDecl` whose field stores the default expression for `kind`

#### Scenario: Event field with let rejected
- **WHEN** `event Tick:` is followed by `let dt: float` in its indented body
- **THEN** the parser reports an error indicating event fields use bare `name: type` syntax

#### Scenario: Event field with var rejected
- **WHEN** `event Damage:` is followed by `var amount: int` in its indented body
- **THEN** the parser reports an error indicating event fields cannot use trait field modifiers

#### Scenario: Event field with persist rejected
- **WHEN** `event Snapshot:` is followed by `persist var frame: int` in its indented body
- **THEN** the parser reports an error indicating event fields cannot use trait field modifiers

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
The parser SHALL parse `[pub] func name(params) [type]:` blocks with a body of statements. The parser SHALL additionally parse `[pub] extern func name(params) [type]` declarations without a colon or body. The `is_extern` flag on `FuncNode` distinguishes the two forms. The `->` arrow token is **not used** in function declarations; the return type follows the closing `)` directly.

```ebnf
func_decl        = [ "pub" ] "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ]
                   ":" NEWLINE INDENT { statement } DEDENT ;

extern_func_decl = [ "pub" ] "extern" "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ] NEWLINE ;
```

#### Scenario: Pure function with return type (no arrow)
- **WHEN** the source contains `func distance(a: vec3, b: vec3) float:`
- **THEN** the parser produces a FuncNode with `is_extern = false`, two parameters, and return type float

#### Scenario: Extern func with return type, no body, no arrow
- **WHEN** the source contains `pub extern func lerp(a, b, t: float) float`
- **THEN** the parser produces a FuncNode with `is_extern = true`, `is_pub = true`, three parameters, return type float, and empty body

#### Scenario: Extern func with no return type
- **WHEN** the source contains `extern func reset()`
- **THEN** the parser produces a FuncNode with `is_extern = true`, no return type, and empty body

#### Scenario: Non-extern func missing body is a parse error
- **WHEN** `func compute(x: float) float` appears without a colon and body
- **THEN** the parser reports an error: "expected ':'"

#### Scenario: Arrow token in func declaration is a parse error
- **WHEN** `func compute(x: float) -> float:` appears (with `->`)
- **THEN** the parser reports an error: "unexpected '->'; return type follows ')' directly without '->' in func declarations"

#### Scenario: Multiple extern funcs on consecutive lines parse correctly
- **WHEN** `pub extern func sin(a: float) float` is immediately followed by `pub extern func cos(a: float) float`
- **THEN** both are parsed as separate FuncNode declarations with no body-collision error

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

#### Scenario: Top-level use remains module import
- **WHEN** source contains `use std.physics.flat as phys` at the top level
- **THEN** the parser produces a module import node rather than an archetype template-use entry

### Requirement: Trait declaration body restricted to field declarations only
The parser SHALL accept only `field_decl` entries inside a trait body. Event handlers (`on ...`) and `func` declarations are not valid inside a trait body. Encountering `on` or `func` inside a trait block SHALL produce a parse error with a helpful message.

```ebnf
trait_decl      = [ "pub" ] "trait" IDENTIFIER
                  [ ":" NEWLINE INDENT
                    { field_decl }
                    DEDENT ] ;
```

#### Scenario: Trait with only field declarations accepted
- **WHEN** a trait body contains only `let` and `var` field declarations
- **THEN** the parser accepts the trait body

#### Scenario: Event handler inside trait body rejected
- **WHEN** a trait body contains `on tick(dt: float):`
- **THEN** the parser reports an error: "event handlers are not allowed in trait bodies; declare a system instead"

#### Scenario: Func inside trait body rejected
- **WHEN** a trait body contains `func helper() float:`
- **THEN** the parser reports an error: "func declarations are not allowed in trait bodies; use a top-level func instead"

### Requirement: `after:` clause parsing in system declarations
The parser SHALL parse an optional `after:` clause inside a system body using the same indented block structure as `filter:` and `exclude:`. The `after:` clause MUST appear after any `filter:` and `exclude:` blocks and before the first event handler.

```ebnf
after_clause    = "after" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
```

The keyword `after` is added to the lexer keyword set with token type `AFTER`.

#### Scenario: `after:` with single entry parsed correctly
- **WHEN** a system body contains an `after:` block with one indented system name
- **THEN** the parser populates `SystemNode.after_systems` with that one name

#### Scenario: `after:` with multiple entries parsed correctly
- **WHEN** a system body contains an `after:` block with `SystemA` and `SystemB` on separate lines
- **THEN** the parser populates `SystemNode.after_systems` with `["SystemA", "SystemB"]`

#### Scenario: Empty `after:` block is a parse error
- **WHEN** a system body contains `after:` with an empty indented block
- **THEN** the parser reports an error: "after: block must contain at least one system name"

### Requirement: Optional `as` alias in `apply:` entries of units and templates
**Reason**: Archetype declarations no longer use `apply:` entries. Trait ownership is expressed structurally through nested trait blocks, so archetype-local aliases are unnecessary.
**Migration**: Replace `apply:` entries and alias-qualified config keys with nested trait blocks under the trait name.

### Requirement: Dotted key form in `config:` assignments and `spawn` override arguments
**Reason**: `config:` blocks and flat parenthesized spawn override arguments are removed. Nested trait blocks make field ownership explicit without dotted keys.
**Migration**: Move each field assignment under its owning trait block in the unit/template or spawn body.

### Requirement: `order by:` clause parsing in system declarations
The parser SHALL recognize an optional `order by:` block in system declarations, positioned between the `filter:`/`exclude:` clauses and the event handler list. The `order by:` block contains one or more sort key lines, each consisting of a dotted alias-field expression followed by an optional direction keyword.

```ebnf
system_decl     = "system" IDENTIFIER ":" INDENT
                  [filter_clause]
                  [exclude_clause]
                  [order_by_clause]
                  handler+
                  DEDENT ;

order_by_clause = "order" "by" ":" INDENT sort_key+ DEDENT ;
sort_key        = IDENTIFIER "." IDENTIFIER ["asc" | "desc"] NEWLINE ;
```

`order` and `by` are contextual keywords in this production. `asc` and `desc` are contextual direction keywords.

#### Scenario: order by clause with single key parsed
- **WHEN** a system contains `order by:` with one indented `s.layer asc` line
- **THEN** the parser produces a `SystemNode` with `order_by = [{alias="s", field="layer", descending=false}]`

#### Scenario: order by clause with multiple keys parsed
- **WHEN** a system contains `order by:` with `s.layer` then `p.pos.y desc`
- **THEN** the parser produces `order_by` with two entries: `{alias="s", field="layer", descending=false}` and `{alias="p", field="pos.y", descending=true}`

#### Scenario: order by with default asc direction
- **WHEN** a sort key line has no direction keyword
- **THEN** the parser produces a `SortKey` with `descending = false`

#### Scenario: system without order by has empty order_by
- **WHEN** a system declaration has no `order by:` block
- **THEN** `SystemNode.order_by` is an empty vector

### Requirement: Statement-level `match` parsing
The parser SHALL recognize `match expr ":"` at statement position as a `TraitMatchStmt`. This is distinct from the existing `MatchExpr` (expression-level). The trait match arms use `IDENTIFIER ["as" IDENTIFIER] "=>"` syntax; the wildcard arm uses `"_" "=>"`.

```ebnf
trait_match_stmt = "match" expr ":" INDENT trait_match_arm+ DEDENT ;
trait_match_arm  = trait_arm | wildcard_arm ;
trait_arm        = IDENTIFIER ["as" IDENTIFIER] "=>" INDENT stmt+ DEDENT ;
wildcard_arm     = "_" "=>" INDENT stmt+ DEDENT ;
```

The `match` keyword is already in the lexer (used by `MatchExpr`). The parser distinguishes statement vs. expression context by position. At statement position, `match expr:` always produces a `TraitMatchStmt`; type validation (entity_id vs. other) is deferred to semantic analysis.

#### Scenario: Simple trait match with alias parsed
- **WHEN** `match c.other:` followed by `Boss as b =>` and a body is parsed at statement position
- **THEN** the parser produces a `TraitMatchStmt` with one `TraitMatchArm{trait="Boss", alias="b", body=[...]}`

#### Scenario: Trait match with no alias parsed
- **WHEN** `Spike =>` arm appears with no `as` clause
- **THEN** the parser produces `TraitMatchArm{trait="Spike", alias=nullopt, body=[...]}`

#### Scenario: Wildcard arm parsed
- **WHEN** `_ =>` arm appears as last arm
- **THEN** the parser produces a `WildcardArm{body=[...]}`

#### Scenario: Multiple arms parsed in order
- **WHEN** match has `Boss as b =>`, then `EnemyAI as e =>`, then `_ =>`
- **THEN** the `TraitMatchStmt` contains arms in declaration order: `[TraitArm(Boss,b), TraitArm(EnemyAI,e), WildcardArm]`

### Requirement: Parser recognizes bounded foreach statements
The parser SHALL recognize `for IDENTIFIER in expression:` at statement position and parse the following indented block as the foreach body.

```ebnf
foreach_stmt = "for" IDENTIFIER "in" expression ":" NEWLINE INDENT { statement } DEDENT ;
```

#### Scenario: Parse foreach over identifier list
- **WHEN** source contains `for item in items:` followed by an indented body
- **THEN** the parser produces a foreach statement with loop variable `item`, iterable expression `items`, and the parsed body statements

#### Scenario: Parse foreach over query expression
- **WHEN** source contains `for hit in query.all[Enemy]():`
- **THEN** the parser accepts the query expression as the iterable expression

### Requirement: Parser recognizes project statements
The parser SHALL recognize `project TraitName [to expression]` at statement position. The statement SHALL allow either a bare marker projection followed by newline or a colon-introduced field assignment block.

```ebnf
project_stmt = "project" IDENTIFIER [ "to" expression ] NEWLINE
             | "project" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT { field_assignment } DEDENT ;
```

#### Scenario: Parse marker projection
- **WHEN** source contains `project Grounded`
- **THEN** the parser produces a project statement with trait name `Grounded`, no explicit target, and no field assignments

#### Scenario: Parse projection with payload
- **WHEN** source contains `project GroundContact: normal = n`
- **THEN** the parser produces a project statement with field assignment payload

#### Scenario: Parse targeted projection
- **WHEN** source contains `project InExplosion to hit.entity:`
- **THEN** the parser records the target expression `hit.entity`

### Requirement: Parser SHALL guarantee forward progress on all inputs
The parser MUST guarantee forward progress through the token stream, even when encountering syntax errors. The parser SHALL NOT enter infinite loops or hang indefinitely on any input, including malformed programs.

#### Scenario: Parser completes on malformed input without hanging
- **WHEN** the parser encounters a syntax error in any parsing loop
- **THEN** the parser SHALL advance past the error and continue parsing or terminate within bounded time

#### Scenario: Parser with unexpected token in struct field list
- **WHEN** parsing a struct body encounters an unexpected token (e.g., `garbage` instead of field name)
- **THEN** the parser SHALL report the error and skip to the next valid synchronization point without infinite looping

#### Scenario: Parser with malformed trait body
- **WHEN** parsing a trait body with missing colons or invalid syntax
- **THEN** the parser SHALL report errors and complete parsing within bounded time

#### Scenario: Parser with deeply nested malformed blocks
- **WHEN** parsing deeply nested blocks with syntax errors at multiple levels
- **THEN** the parser SHALL recover at each level and complete without memory exhaustion

### Requirement: Parser SHALL implement panic-mode error recovery
The parser MUST implement panic-mode synchronization that skips tokens until reaching a safe recovery point. Synchronization points SHALL include statement boundaries (NEWLINE, DEDENT), declaration keywords, and end-of-file.

#### Scenario: Synchronization to NEWLINE boundary
- **WHEN** an error occurs mid-statement
- **THEN** the parser SHALL skip tokens until finding NEWLINE, DEDENT, or EOF

#### Scenario: Synchronization to declaration keyword
- **WHEN** an error occurs in a declaration
- **THEN** the parser SHALL skip until finding a declaration keyword (TRAIT, SYSTEM, FUNC, STRUCT, ENUM, MODULE, USE, CONST, EVENT, ENTITY, TEMPLATE, VIEW, INTERFACE, ASSET, INPUT)

#### Scenario: Synchronization to DEDENT boundary
- **WHEN** an error occurs inside an indented block
- **THEN** the parser SHALL skip tokens until finding DEDENT or EOF to exit the block

#### Scenario: Synchronization stops at EOF
- **WHEN** synchronization is seeking a recovery point
- **THEN** the parser SHALL stop at EOF_TOKEN and not read past the end of the token stream

### Requirement: Parser SHALL support multi-error reporting
The parser SHOULD continue parsing after encountering errors to report multiple syntax errors in a single compilation pass, when possible without compromising error recovery quality.

#### Scenario: Multiple independent errors reported in one pass
- **WHEN** a source file contains syntax errors in multiple independent declarations
- **THEN** the parser SHALL report errors for each declaration rather than stopping at the first error

#### Scenario: Parser continues after struct parsing error
- **WHEN** a struct definition has a syntax error
- **THEN** the parser SHALL synchronize, report the error, and attempt to parse subsequent declarations

#### Scenario: Cascading errors are minimized
- **WHEN** an error occurs and synchronization happens
- **THEN** the parser SHALL NOT report spurious errors for tokens that were skipped during synchronization

### Requirement: Parser error messages SHALL indicate error location and expected tokens
When the parser encounters an unexpected token, error messages MUST include the source location and indicate what tokens were expected. This requirement applies to both the existing `consume()` method errors and any new synchronization error messages.

#### Scenario: Error message includes source location
- **WHEN** `consume()` fails with an unexpected token
- **THEN** the error message SHALL include the file, line, and column of the unexpected token

#### Scenario: Error message indicates expected token
- **WHEN** `consume(COLON, "expected ':'")` fails
- **THEN** the error message SHALL indicate that `:` was expected and what was found instead

#### Scenario: Synchronization does not report additional errors for skipped tokens
- **WHEN** the parser synchronizes by skipping multiple tokens
- **THEN** the parser SHALL NOT report separate errors for each skipped token, only for the original unexpected token

### Requirement: Query expression grammar supports bracketed trait filters on member calls
The parser SHALL accept query expressions formed from a normal member expression followed by a bracketed query-filter list and a call suffix. The bracketed filter list SHALL support comma-separated positive traits and `not TraitName` negative predicates.

#### Scenario: Parse module-qualified world query
- **WHEN** source contains `std.query.exists[Boss]()` in expression position
- **THEN** the parser treats `std.query.exists` as the member target and parses the bracketed query filter and call suffix on that target

#### Scenario: Parse world query with one trait filter
- **WHEN** source contains `query.exists[Boss]()` in expression position and `query` is an imported alias
- **THEN** the parser produces an expression node representing the member target `query.exists`, the filter list `[Boss]`, and an empty call argument list

#### Scenario: Parse query with negative trait filter
- **WHEN** source contains `query.count[EnemyAI, not Dead]()`
- **THEN** the parser accepts `not Dead` as a negative query-filter predicate in the bracket list

### Requirement: Query call grammar supports named arguments
The parser SHALL accept named arguments in query call expressions using `IDENTIFIER = expr` syntax inside the call parentheses.

#### Scenario: Parse nearest query with named argument
- **WHEN** source contains `query.nearest[Transform, Enemy](from = player_pos)`
- **THEN** the parser produces a query call expression with a named argument `from`

#### Scenario: Parse overlap query with multiple named arguments
- **WHEN** source contains `query.overlap_box[Pickup](center = p, size = s)`
- **THEN** the parser accepts both named arguments and preserves their names in the AST

### Requirement: External event and phase parsing
The parser SHALL accept `[pub] extern event` and `phase` as module-scope declarations. A phase body SHALL parse `from:`, `after:`, `every:`, `max:`, and typed initialized field entries in their defined structural positions.

#### Scenario: Frame and phase chain parse
- **WHEN** source declares external frame plus input, fixed_tick, tick, late_tick, and render phases
- **THEN** the AST preserves event fields, phase dependencies, cadence expressions, limits, and field initializers

### Requirement: External handler contract parsing
The parser SHALL accept `on Trigger:` blocks inside `extern system` declarations and parse their `after`, `reads`, `writes`, `emits`, `commands`, and `effects` clauses. It SHALL accept a leading handler `after:` block in a regular handler before executable statements.

#### Scenario: External render handler parses
- **WHEN** SpriteRenderer declares `on render` with reads and graphics effects
- **THEN** the AST contains an external render handler and its contract entries

#### Scenario: Filterless external handler parses
- **WHEN** InputSource declares only `on input` with `emits: InputSample`
- **THEN** parsing succeeds without requiring a filter clause

