## Requirements

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `event`, `func`, `extern func`, `template`, `asset`, `input`.

Note: `view` and `interface` are not supported top-level declarations in v0.2.

```ebnf
declaration = module_decl | use_decl | const_block | struct_decl
            | enum_decl | trait_decl | unit_decl | template_decl | system_decl
            | event_decl | func_decl | extern_func_decl | asset_decl | input_decl ;
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

#### Scenario: Extern func declaration in program
- **WHEN** `pub extern func lerp(a, b, t: float) float` appears at the top level
- **THEN** the parser produces a ProgramNode containing a `FuncNode` with `is_extern = true`

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
The parser SHALL parse `[pub] unit Name:` blocks as a sequence of nested trait entries. Each entry is either a bare trait name for a marker trait or a trait name followed by `:` and an indented field-assignment block. `apply:` and `config:` blocks are not part of unit syntax.

#### Scenario: Unit with nested trait block parsed
- **WHEN** the source contains `unit Cactus:` with `Position:` followed by indented field assignments
- **THEN** the parser produces a UnitNode containing a trait entry for `Position` and its nested assignments

#### Scenario: Unit with marker trait parsed
- **WHEN** the source contains `unit Cactus:` with a bare `Persistent` entry in the body
- **THEN** the parser produces a UnitNode containing a marker-trait entry for `Persistent`

### Requirement: `template` declaration grammar

The parser SHALL accept `template_decl` as a top-level declaration whose body is a sequence of nested trait entries, structurally identical to `unit_decl` except using the `TEMPLATE` keyword. `apply:` and `config:` blocks are not part of template syntax.

```ebnf
template_decl = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                { archetype_trait_entry }
                DEDENT ;
```

#### Scenario: Template with nested trait blocks parsed
- **WHEN** source contains `template Enemy:` followed by `Position:` and indented field assignments
- **THEN** the parser produces a `TemplateDecl` AST node containing a trait entry for `Position`

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
The parser SHALL accept `event` declarations with no colon and no body. The event body (colon + indented block) is optional, consistent with the marker trait pattern:

```ebnf
event_decl = [ "pub" ] "event" IDENTIFIER
             [ ":" NEWLINE INDENT
               { field_decl }
               DEDENT ] ;
```

#### Scenario: Marker event (no colon) parsed
- **WHEN** `pub event spawn` appears at the top level with no colon and no body
- **THEN** the parser produces an `EventDecl` with `name = "spawn"` and empty `fields` list

#### Scenario: Event with fields still parsed
- **WHEN** `event PlayerDamaged:` followed by an indented body appears
- **THEN** the parser produces an `EventDecl` with the declared fields (existing behavior unchanged)

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
