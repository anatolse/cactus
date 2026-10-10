## Purpose

Define the parser's grammar for top-level declarations, including traits, entities with nested archetypes, templates, and rule `each:`/`without:` clauses, along with rejection of legacy `unit` syntax.
## Requirements
### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. The first top-level declaration MUST be exactly one `module` declaration. Supported declarations after the module declaration are: `use`, `const`, `struct`, `enum`, `trait`, `entity`, `rule`, `event`, `func`, `extern func`, `template`, `asset`, and `input`.

Note: `view`, `interface`, and legacy `unit` are not supported top-level declarations in this language version. The legacy `system` spelling is not a keyword; it MAY appear as an identifier in any identifier position.

```ebnf
program     = module_decl { declaration } EOF ;
declaration = use_decl | const_block | struct_decl
            | enum_decl | trait_decl | entity_decl | template_decl | rule_decl
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

#### Scenario: system used as identifier
- **WHEN** `system` appears as a field name or identifier inside a valid declaration
- **THEN** the parser accepts it as an ordinary identifier without error

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
- **WHEN** `entity Gem1 from BlueGem:` appears inside a rule handler
- **THEN** the parser reports that entity declarations are only valid at the top level

### Requirement: Legacy unit declarations rejected

The parser SHALL reject legacy `unit` declarations. Authors SHALL use `entity` declarations for module/scene-load entity instances.

#### Scenario: Legacy unit declaration rejected
- **WHEN** source contains `unit Player:` at the top level
- **THEN** the parser reports that `unit` has been renamed to `entity`

### Requirement: `each:` and `without:` both optional in rule grammar
Positive and negative unary selection clauses SHALL remain independently optional on rule declarations. The parser SHALL accept positive selection only, negative selection only, both, or neither. `each:` SHALL be the only positive spelling and `without:` the only negative spelling. Absence of selection clauses SHALL retain the existing selectionless domain. Bracket-list selection syntax SHALL remain rejected.

Positive entries SHALL support a dotted trait reference and an optional `as` alias. Negative entries SHALL support dotted trait references without aliases. Each list SHALL accept its existing indented form or a nonempty comma-separated inline form, under the shared inline-list requirements.

```ebnf
each_clause    = "each" ":" trait_list ;
without_clause = "without" ":" name_list ;
trait_entry    = dotted_name [ "as" IDENTIFIER ] ;
trait_list     = NEWLINE INDENT { trait_entry NEWLINE } DEDENT
               | trait_entry { "," trait_entry } [ "," ] NEWLINE ;
name_list      = NEWLINE INDENT { dotted_name NEWLINE } DEDENT
               | dotted_name { "," dotted_name } [ "," ] NEWLINE ;
```

Existing block-form emptiness validation SHALL remain unchanged. `each` and `without` SHALL be contextual words at unary clause positions, not globally reserved words. `filter` and `exclude` SHALL NOT be reserved words; outside a unary clause position they SHALL parse as ordinary identifiers.

At a unary clause position, `filter:` SHALL be rejected with a diagnostic at the clause that tells the author to write `each:`, and `exclude:` SHALL be rejected with a diagnostic that tells the author to write `without:`. After such a diagnostic the parser SHALL parse the clause's entries as the canonical clause, so later diagnostics in the same rule and later declarations are still reported, and SHALL report no further error for that clause alone.

#### Scenario: Rule with each only parsed correctly
- **WHEN** a rule has `each:` but no `without:`
- **THEN** parsing preserves its positive selection and an empty negative selection

#### Scenario: Rule with without only parsed correctly
- **WHEN** a rule has `without:` but no `each:`
- **THEN** parsing preserves an empty positive selection and its negative selection

#### Scenario: Rule with no each or without parsed correctly
- **WHEN** a rule has neither positive nor negative selection and no pair domain
- **THEN** parsing preserves the existing selectionless rule

#### Scenario: Old bracket syntax produces parse error
- **WHEN** source contains `each: [Position, EnemyAI]`
- **THEN** compilation rejects bracket selection and directs the author to an inline comma-separated list or an indented block

#### Scenario: Each entry with as alias parsed
- **WHEN** a positive selection contains `Position as pos`
- **THEN** parsing preserves the trait reference and alias `pos`

#### Scenario: Each entry without alias parsed
- **WHEN** a positive selection contains `Position` without `as`
- **THEN** parsing preserves that reference without an alias

#### Scenario: Each entry with qualified name and alias
- **WHEN** a positive selection contains `phys.Body as body`
- **THEN** parsing preserves the qualified reference `phys.Body` and alias `body`

#### Scenario: Contextual words remain identifiers
- **WHEN** valid authored declarations or handler locals use `each` or `without` as an identifier outside a unary clause position
- **THEN** parsing and name resolution retain their existing identifier behavior

#### Scenario: Former keywords are identifiers
- **WHEN** a trait field, function parameter, handler local, or trait is named `filter` or `exclude`
- **THEN** parsing and name resolution accept it as an ordinary identifier

#### Scenario: filter clause names its replacement
- **WHEN** a rule declares `filter: Position as pos` in block or inline form
- **THEN** compilation reports one error at that clause telling the author to use `each:`
- **AND** the rule's other clauses and later declarations are still parsed and checked

#### Scenario: exclude clause names its replacement
- **WHEN** a rule declares `exclude: Frozen` in block or inline form
- **THEN** compilation reports one error at that clause telling the author to use `without:`

#### Scenario: Duplicate canonical clause
- **WHEN** a rule declares `each:` twice, or `without:` twice
- **THEN** compilation reports a duplicate clause without merging its entries

#### Scenario: Canonical unary clauses cannot accompany pairs
- **WHEN** a rule declares `pairs:` with `each:` or `without:`
- **THEN** compilation rejects the combination under the existing single-domain restriction

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
The parser SHALL accept `on` handlers using the parameter-free syntax. The `( param_list )` is removed entirely. An optional `as IDENTIFIER` alias clause is added after the event name. The event name accepts both reserved lifecycle keywords and user-defined identifiers. The parser SHALL also accept a trait lifecycle trigger: the word `added` or `removed` followed by a trait name. `added` and `removed` are recognized only when a trait name follows them; otherwise they are ordinary identifiers.

```ebnf
event_handler = "on" ( lifecycle_trigger | event_name ) [ "as" IDENTIFIER ] ":" NEWLINE INDENT
                { statement }
                DEDENT ;

lifecycle_trigger = ( "added" | "removed" ) dotted_name ;

event_name = "tick" | "fixed_tick" | "late_tick"
           | "spawn" | "destroy" | "load" | "unload"
           | "input" | IDENTIFIER ;
```

The handler no longer carries a parameter list node; instead, `EventHandlerNode` has an optional `alias: string` field. `spawn` and `destroy` stay parseable as event names only so that semantic analysis can report a migration diagnostic; `std.core` no longer declares them.

#### Scenario: on tick handler parsed without parameters
- **WHEN** `on tick:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "tick"`, no params, and `alias = nil`

#### Scenario: on tick with alias parsed
- **WHEN** `on tick as t:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "tick"` and `alias = "t"`

#### Scenario: on fixed_tick handler parsed without parameters
- **WHEN** `on fixed_tick:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and `alias = nil`

#### Scenario: on late_tick handler parsed
- **WHEN** `on late_tick:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and `alias = nil`

#### Scenario: on load handler parsed
- **WHEN** `on load:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "load"` and `alias = nil`

#### Scenario: on unload handler parsed
- **WHEN** `on unload:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "unload"` and `alias = nil`

#### Scenario: on input() handler parsed (no parameters)
- **WHEN** `on input:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and `alias = nil`

#### Scenario: User event handler with alias parsed
- **WHEN** `on PlayerDamaged as dmg:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "PlayerDamaged"` and `alias = "dmg"`

#### Scenario: on added trigger parsed
- **WHEN** `on added Dying:` appears in a rule body
- **THEN** the parser produces an `EventHandler` marked as an added-trigger on trait name `Dying` with `alias = nil`

#### Scenario: on removed trigger with alias parsed
- **WHEN** `on removed fx.Burning as old:` appears in a rule body
- **THEN** the parser produces an `EventHandler` marked as a removed-trigger on trait name `fx.Burning` with `alias = "old"`

#### Scenario: added without a trait name is an event name
- **WHEN** `on added:` appears in a rule body
- **THEN** the parser produces an ordinary `EventHandler` with `event_name = "added"`

#### Scenario: on spawn handler parsed
- **WHEN** `on spawn:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "spawn"`, and semantic analysis rejects it with a diagnostic naming the replacement `on added <Trait>`

#### Scenario: on destroy handler parsed
- **WHEN** `on destroy:` appears in a rule body
- **THEN** the parser produces an `EventHandler` with `event_name = "destroy"`, and semantic analysis rejects it with a diagnostic naming the replacement (add a marker trait, react with `on added`, then `destroy`)

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
The parser SHALL parse `const:` blocks containing assignments of the form `NAME = expression` or `NAME: type = expression`. The value SHALL be parsed as a general expression; which expressions are valid constants is decided by semantic analysis. A value MAY span several lines inside parentheses or brackets.

#### Scenario: Const block with strings
- **WHEN** the source contains `const:` with `SHOP_TITLE = "Cactus Shop"` and `MAX_ITEMS = 50`
- **THEN** the parser produces a ConstBlockNode with two ConstAssignment entries

#### Scenario: Const value is an expression
- **WHEN** the source contains `const:` with `HALF_PI = math.PI * 0.5`
- **THEN** the parser produces a ConstAssignment whose value is a binary expression

#### Scenario: Typed constant
- **WHEN** the source contains `const:` with `WAVES: list[UnitDef] = []`
- **THEN** the parser produces a ConstAssignment with the declared type `list[UnitDef]` and an empty list value

#### Scenario: Multi-line constant value
- **WHEN** a constant's value is `UnitDef(` followed by one `field = value,` per line and a closing `)`
- **THEN** the parser produces one ConstAssignment whose value is a single call expression

### Requirement: Match expression parsing
The parser SHALL parse a `match` expression: `match` subject `:`, then an indented block of arms `pattern => expression`, one per line. A pattern is an enum member path, an `int` or `bool` literal (a negative `int` literal included), a constant name, or the wildcard `_`. The arm block SHALL end the expression without consuming the line break that ends the enclosing statement, so a `match` expression may be followed by further statements in the same block.

```ebnf
match_expr    = "match" expression ":" NEWLINE INDENT
                match_expr_arm { match_expr_arm }
                DEDENT ;
match_expr_arm = value_pattern "=>" expression NEWLINE ;
value_pattern = dotted_name | [ "-" ] INTEGER | "true" | "false" | "_" ;
```

#### Scenario: Match on enum
- **WHEN** the source contains `match state:` with arms for different enum variants
- **THEN** the parser produces a MatchExpr with the matched expression and a list of arms

#### Scenario: Match expression followed by another statement
- **WHEN** a handler body contains `let s = match t.mode:` with two arms, followed by `t.speed = s` at the same indentation as the `let`
- **THEN** the parser accepts both statements and the handler body holds two statements in source order

#### Scenario: Match expression with literal and wildcard patterns
- **WHEN** the source contains a `match` expression with arms `0 => 1.0`, `-1 => 2.0` and `_ => 0.0`
- **THEN** the parser produces a MatchExpr with three arms whose patterns are the literal `0`, the literal `-1` and the wildcard

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
- **THEN** the parser reports an error: "event handlers are not allowed in trait bodies; declare a rule instead"

#### Scenario: Func inside trait body rejected
- **WHEN** a trait body contains `func helper() float:`
- **THEN** the parser reports an error: "func declarations are not allowed in trait bodies; use a top-level func instead"

### Requirement: `after:` clause parsing in rule declarations
The parser SHALL parse an optional `after:` clause inside a rule body using the same indented block structure as `each:` and `without:`. The `after:` clause MUST appear after any `each:` and `without:` blocks and before the first event handler.

```ebnf
after_clause    = "after" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
```

The keyword `after` is added to the lexer keyword set with token type `AFTER`.

#### Scenario: `after:` with single entry parsed correctly
- **WHEN** a rule body contains an `after:` block with one indented rule name
- **THEN** the parser populates `RuleNode.after_rules` with that one name

#### Scenario: `after:` with multiple entries parsed correctly
- **WHEN** a rule body contains an `after:` block with `RuleA` and `RuleB` on separate lines
- **THEN** the parser populates `RuleNode.after_rules` with `["RuleA", "RuleB"]`

#### Scenario: Empty `after:` block is a parse error
- **WHEN** a rule body contains `after:` with an empty indented block
- **THEN** the parser reports an error: "after: block must contain at least one rule name"

### Requirement: `order by:` clause parsing in rule declarations
The parser SHALL recognize an optional `order by:` block in rule declarations, positioned between the `each:`/`without:` clauses and the event handler list. The `order by:` block contains one or more sort key lines, each consisting of a dotted alias-field expression followed by an optional direction keyword.

```ebnf
rule_decl       = "rule" IDENTIFIER ":" INDENT
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
- **WHEN** a rule contains `order by:` with one indented `s.layer asc` line
- **THEN** the parser produces a `RuleNode` with `order_by = [{alias="s", field="layer", descending=false}]`

#### Scenario: order by clause with multiple keys parsed
- **WHEN** a rule contains `order by:` with `s.layer` then `p.pos.y desc`
- **THEN** the parser produces `order_by` with two entries: `{alias="s", field="layer", descending=false}` and `{alias="p", field="pos.y", descending=true}`

#### Scenario: order by with default asc direction
- **WHEN** a sort key line has no direction keyword
- **THEN** the parser produces a `SortKey` with `descending = false`

#### Scenario: rule without order by has empty order_by
- **WHEN** a rule declaration has no `order by:` block
- **THEN** `RuleNode.order_by` is an empty vector

### Requirement: Pair relation grammar in regular rules
The parser SHALL recognize contextual `pairs:` syntax in the rule-clause position and SHALL construct two ordered pair-binding nodes, each containing a binding identifier and one or more filter-style trait entries.

```ebnf
pair_clause  = "pairs" ":" NEWLINE INDENT pair_binding pair_binding DEDENT ;
pair_binding = IDENTIFIER ":" NEWLINE INDENT { filter_entry } DEDENT ;
```

The spelling `pairs` SHALL remain an ordinary identifier outside this rule-clause context.

#### Scenario: Pair bindings and aliases are preserved
- **WHEN** a rule declares `body:` with `tf.WorldTransform as transform` and `wall:` with `Collider`
- **THEN** the AST preserves binding order, qualified trait spellings, aliases, and source locations

#### Scenario: Pairs remains a contextual keyword
- **WHEN** `pairs` appears as a valid local identifier outside the rule-clause position
- **THEN** it is tokenized and parsed as an identifier

#### Scenario: Pair clause on external rule is rejected
- **WHEN** an `extern rule` body contains `pairs:`
- **THEN** parsing or semantic validation reports that pair domains are regular-rule-only

### Requirement: Parser recognizes `where:` clause
The parser SHALL accept an optional `where:` block on a regular rule, positioned after `each:`/`without:`/`pairs:`/`order by:`/`after:`/`target:` and before its `on` handlers, containing one or more indented boolean expression lines. `where` SHALL be a contextual identifier keyword recognized only in this clause position, not a reserved word — an identifier named `where` elsewhere in source SHALL continue to parse as an ordinary identifier. The parser SHALL reject `where:` on `extern rule` declarations.

#### Scenario: where: after filter: is parsed
- **WHEN** a rule declares `each:` followed by a `where:` block with one predicate line
- **THEN** the rule's parsed domain includes that predicate

#### Scenario: where: after pairs: is parsed
- **WHEN** a rule declares `pairs:` followed by a `where:` block with one predicate line
- **THEN** the rule's parsed domain includes that predicate

#### Scenario: Multiple where: lines are parsed as a list
- **WHEN** a `where:` block contains three indented expression lines
- **THEN** the parser produces three predicate expressions in source order

#### Scenario: where remains a contextual keyword
- **WHEN** source declares a local variable or trait field named `where`
- **THEN** it parses as an ordinary identifier outside clause position

#### Scenario: where: on extern rule is rejected
- **WHEN** an `extern rule` declaration includes a `where:` block
- **THEN** the parser reports that `where:` is not valid on external rules

### Requirement: Statement-level `match` parsing
The parser SHALL recognize `match expr ":"` at statement position as a statement-level match. This is distinct from the `MatchExpr` (expression-level). Each arm is a pattern followed by `=>` and an indented statement body. A pattern is a dotted name, optionally followed by `as IDENTIFIER`; an `int` or `bool` literal (a negative `int` literal included); or the wildcard `_`. Whether the statement is a trait match or a value match is decided by semantic analysis from the subject's type, not by the parser.

```ebnf
match_stmt    = "match" expression ":" NEWLINE INDENT match_stmt_arm { match_stmt_arm } DEDENT ;
match_stmt_arm = stmt_pattern "=>" NEWLINE INDENT statement { statement } DEDENT ;
stmt_pattern  = dotted_name [ "as" IDENTIFIER ] | [ "-" ] INTEGER | "true" | "false" | "_" ;
```

The `match` keyword is already in the lexer (used by `MatchExpr`). The parser distinguishes statement vs. expression context by position.

#### Scenario: Simple trait match with alias parsed
- **WHEN** `match c.other:` followed by `Boss as b =>` and a body is parsed at statement position
- **THEN** the parser produces a statement-level match with one arm whose pattern is `Boss`, alias `b`, and the parsed body

#### Scenario: Trait match with no alias parsed
- **WHEN** `Spike =>` arm appears with no `as` clause
- **THEN** the parser produces an arm whose pattern is `Spike` with no alias and the parsed body

#### Scenario: Wildcard arm parsed
- **WHEN** `_ =>` arm appears as last arm
- **THEN** the parser produces a wildcard arm with the parsed body

#### Scenario: Multiple arms parsed in order
- **WHEN** match has `Boss as b =>`, then `EnemyAI as e =>`, then `_ =>`
- **THEN** the statement contains arms in declaration order: `Boss` with alias `b`, `EnemyAI` with alias `e`, then the wildcard

#### Scenario: Value arms with enum and literal patterns parsed
- **WHEN** a statement-level match has arms `Mode.Idle =>`, `3 =>` and `true =>`, each followed by an indented body
- **THEN** the parser accepts the statement and records the patterns `Mode.Idle`, `3` and `true` in declaration order

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
- **THEN** the parser SHALL skip until finding a declaration keyword (TRAIT, RULE, FUNC, STRUCT, ENUM, MODULE, USE, CONST, EVENT, ENTITY, TEMPLATE, VIEW, INTERFACE, ASSET, INPUT)

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
The parser SHALL accept `on Trigger:` blocks inside `extern rule` declarations and parse their `after`, `reads`, `writes`, `emits`, `commands`, and `effects` clauses. It SHALL accept a leading handler `after:` block in a regular handler before executable statements.

#### Scenario: External render handler parses
- **WHEN** SpriteRenderer declares `on render` with reads and graphics effects
- **THEN** the AST contains an external render handler and its contract entries

#### Scenario: Filterless external handler parses
- **WHEN** InputSource declares only `on input` with `emits: InputSample`
- **THEN** parsing succeeds without requiring a filter clause

### Requirement: `if` / `else if` / `else` statement grammar
The parser SHALL accept a chain of zero or more `else if` clauses between the
`if` clause and an optional terminal `else` clause, each introducing an
independent condition and body at the same syntactic level as the owning
`if`. The existing single `if [else]` form remains valid as the zero-`else
if` case of the same grammar.

```ebnf
if_stmt = "if" expression ":" suite
          { "else" "if" expression ":" suite }
          [ "else" ":" suite ] ;
```

Conditions SHALL be evaluated top to bottom. Exactly the first branch whose
condition is true executes. If no condition is true and a terminal `else` is
present, its body executes. If no condition is true and no terminal `else` is
present, no branch executes.

#### Scenario: Two-branch if/else parses
- **WHEN** source contains `if x > 0:` with a body, followed by `else:` with a body
- **THEN** the parser accepts it and both bodies are recorded in declaration order

#### Scenario: Three-branch else-if chain parses
- **WHEN** source contains `if a:`, `else if b:`, `else if c:`, and a terminal `else:`, each with a body
- **THEN** the parser accepts the chain and preserves the branch order, each branch's condition, and its body

#### Scenario: Else-if chain with no terminal else parses
- **WHEN** source contains `if a:` followed by one or more `else if:` clauses with no trailing `else:`
- **THEN** the parser accepts the chain with no terminal-else body

#### Scenario: Legacy nested else+if spelling remains valid
- **WHEN** source contains `else:` whose body is a single nested `if` statement (the pre-existing spelling for chained conditions)
- **THEN** the parser accepts it unchanged and its runtime behavior matches the equivalent `else if` spelling

### Requirement: Malformed else-if chains rejected
The parser SHALL reject: an `else if` clause following a terminal `else` in
the same chain; more than one terminal `else` in the same chain; an `else if`
or terminal `else` clause whose indentation does not align with its owning
`if`; and an `else if` clause with an empty suite.

#### Scenario: else if after terminal else rejected
- **WHEN** source contains `if a:` ... `else:` ... followed by a further `else if b:` clause at the same level
- **THEN** the parser reports an error and does not silently accept the extra clause

#### Scenario: duplicate terminal else rejected
- **WHEN** source contains `if a:` ... `else:` ... followed by a second `else:` clause at the same level
- **THEN** the parser reports an error and does not silently accept the second `else`

#### Scenario: misindented else if rejected
- **WHEN** an `else if` clause's indentation does not match the indentation of its owning `if`
- **THEN** the parser reports an error rather than attaching the clause to an unrelated `if`

#### Scenario: empty else if suite rejected
- **WHEN** an `else if` clause's body contains no statements
- **THEN** the parser reports an error rather than accepting a no-op branch

### Requirement: else-if chain compiled behavior equivalent to legacy nested spelling
The compiled behavior of an `else if` chain SHALL be equivalent to the
compiled behavior of the same chain written using the legacy nested `else:` +
`if` spelling.

#### Scenario: Runtime branch selection matches legacy spelling
- **WHEN** a rule handler contains a three-branch `else if` chain, and an equivalent handler is written using nested `else:` + `if` for the same conditions and bodies
- **THEN** both compiled programs select the same branch for the same input values, for every combination of condition outcomes

### Requirement: Trait field modifier parsing
The parser SHALL parse `trait Name:` blocks containing fields with modifiers (`let`, `var`, `persist`, `pub`). Fields SHALL support default value expressions. Traits are data-only; an event handler in a trait body SHALL be a syntax error.

#### Scenario: Trait with persist field
- **WHEN** the source contains `trait Player:` with fields `persist var health: int = 100` and `var position: vec3`
- **THEN** the parser produces a TraitNode with two FieldNodes, the first having persist=true and the second having persist=false

#### Scenario: sync is not a field modifier
- **WHEN** the source contains `trait Player:` with a field `sync var position: vec3`
- **THEN** the parser reports a syntax error at `sync`

#### Scenario: Event handler in trait rejected
- **WHEN** the source contains `trait Damageable:` with an `on damage:` block
- **THEN** the parser reports "event handlers are not allowed in trait bodies; declare a rule instead"

### Requirement: Precedence-climbing expression parsing
The parser SHALL parse expressions using precedence climbing, supporting binary operators, unary operators, member access, function calls, and spawn expressions. The expression grammar SHALL NOT include lambda expressions; `=>` SHALL be accepted only inside `match` arms.

#### Scenario: Binary expression with correct precedence
- **WHEN** the source contains `a + b * c`
- **THEN** the parser produces a BinaryExpr with `+` at the root and `*` as the right child

#### Scenario: Lambda expression rejected
- **WHEN** the source contains `x => x * 2` as an expression outside a `match` arm
- **THEN** the parser reports a syntax error

#### Scenario: Match arm arrow still accepted
- **WHEN** the source contains a `match` expression with arms `Color.Red => 0` and `_ => 1`
- **THEN** the parser produces a MatchExpr with two arms

### Requirement: Named call arguments
The parser SHALL accept named arguments, `IDENTIFIER = expression`, in a call's argument list, as well as positional arguments. A trailing comma and line breaks inside the parentheses SHALL be allowed. Whether a call accepts named or positional arguments is decided by semantic analysis.

#### Scenario: Struct construction with named arguments
- **WHEN** the source contains `Squad(lead = other, size = 3)`
- **THEN** the parser produces a call expression with two named arguments, `lead` and `size`, in source order

#### Scenario: Positional arguments still parse
- **WHEN** the source contains `math.clamp(x, 0.0, 1.0)`
- **THEN** the parser produces a call expression with three positional arguments

### Requirement: If expression parsing
The parser SHALL parse an `if` expression at any expression position: `if` condition `:` expression, zero or more `else if` condition `:` expression parts, then a required `else` `:` expression. The whole expression is written on one line, or across lines inside enclosing parentheses or brackets.

```ebnf
if_expr = "if" expression ":" expression
          { "else" "if" expression ":" expression }
          "else" ":" expression ;
```

#### Scenario: Two-branch if expression parses
- **WHEN** the source contains `t.hp = if t.speed > 1.0: 1 else: 2`
- **THEN** the parser produces an assignment whose value is an if expression with one condition, a then-expression `1` and an else-expression `2`

#### Scenario: If expression with else if parses
- **WHEN** the source contains `let tier = if hp <= 0: 0 else if hp < 50: 1 else: 2`
- **THEN** the parser produces an if expression with two conditions in source order and three result expressions

#### Scenario: If expression without else is rejected
- **WHEN** the source contains `let x = if ready: 1` with no `else`
- **THEN** the parser reports that an `if` expression requires an `else`

### Requirement: State declaration parsing
The parser SHALL parse `state IDENTIFIER ":"`, optionally preceded by `pub`, at top-level declaration position as a state declaration. `state` SHALL be contextual: it starts a declaration only at top-level declaration position when an identifier follows, and stays an ordinary identifier elsewhere. The indented body SHALL hold one variant per line: an identifier, an optional contextual `final`, and an optional `:` followed by an indented block that SHALL contain only field declarations, as in a trait body.

```ebnf
state_decl    = [ "pub" ] "state" IDENTIFIER ":" NEWLINE INDENT
                state_variant { state_variant }
                DEDENT ;
state_variant = IDENTIFIER [ "final" ]
                ( ":" NEWLINE INDENT { field_decl } DEDENT | NEWLINE ) ;
```

#### Scenario: State with marker, data and final variants parses
- **WHEN** the parser reads a `state EnemyMode:` block with `Idle`, `Chasing:` holding `let target: entity_id`, and `Dying final:` holding `var elapsed: float = 0.0`
- **THEN** it produces one state declaration with three variants in source order, where only `Dying` is final and `Chasing` and `Dying` each have one field

#### Scenario: Identifier named state is not a declaration
- **WHEN** a trait body declares `var state: int` and a handler writes `ctrl.state = 1`
- **THEN** both parse as before, with `state` as an identifier

#### Scenario: Non-field line in a variant body is rejected
- **WHEN** a variant body contains a statement such as `x = 1`
- **THEN** the parser reports that a state variant body can contain only field declarations

### Requirement: Shared inline clause-list surface
Selection, pair-binding trait lists, `after:`, `before:`, `where:`, `when:`, `order by:`, and external `reads:`, `writes:`, `projects:`, `emits:`, `commands:`, and `effects:` SHALL accept nonempty comma-separated inline entries in addition to their block forms. An inline list SHALL end at NEWLINE, SHALL accept one trailing comma and a trailing comment, and SHALL reject empty entries or an indented continuation. Each clause SHALL retain its existing valid declaration positions, entry grammar, semantic checks, and evaluation behavior. An inline clause SHALL NOT make that clause valid in a previously forbidden context. Scalar clauses, payload blocks, and reducer declaration blocks SHALL retain their existing forms.

#### Scenario: Short lists accept comments and one trailing comma
- **WHEN** a rule contains `each: Position as pos, Motion, # selected state`
- **THEN** compilation accepts exactly the two selected traits and the alias

#### Scenario: A pair binding chooses its own list form
- **WHEN** the first pair binding uses an inline trait list and the second uses an indented trait list
- **THEN** compilation accepts both within the same pair declaration

#### Scenario: Empty or repeated inline entries are rejected
- **WHEN** an inline clause starts with a comma or contains two consecutive commas
- **THEN** compilation reports the empty entry at its source location

#### Scenario: Mixed continuation is rejected
- **WHEN** `each: Position` is followed by an indented `Motion` entry before the next clause
- **THEN** compilation rejects the mixed inline/block list

#### Scenario: Extern-only contracts remain extern-only
- **WHEN** a regular handler contains inline `writes: Health` as an authored access annotation
- **THEN** compilation retains the existing rejection of external contract clauses in regular handlers

### Requirement: Inline entries preserve nested expression boundaries
Inline expression lists SHALL separate entries only at the enclosing clause level. Commas and colons inside a valid call, constructor, list literal, or conditional expression SHALL remain part of that expression. Every parsed entry SHALL preserve its own source location and existing scope and type rules.

#### Scenario: Function arguments do not split a predicate list
- **WHEN** `where: touching(body, lava), body != lava` appears with a valid pure `touching` function
- **THEN** compilation parses two predicates, with both arguments belonging to the first predicate

#### Scenario: Nested conditional colon belongs to its expression
- **WHEN** a valid inline predicate uses a conditional expression containing `:`
- **THEN** the conditional expression remains one entry and does not start a payload or continuation block

#### Scenario: Second entry reports its own location
- **WHEN** the second inline predicate has an unknown name or wrong type
- **THEN** its diagnostic points at the second predicate rather than only at the clause header

### Requirement: Malformed compact clauses recover without losing later declarations
Malformed inline lists, pair bindings, and grouped-reduction headers SHALL produce source-located diagnostics and SHALL allow parsing to progress to later clauses, handlers, and top-level declarations. Recovery SHALL NOT reinterpret an invalid continuation as valid entries or loop indefinitely.

#### Scenario: Bad pair binding preserves the next rule
- **WHEN** a malformed compact binding is followed by a valid rule declaration
- **THEN** compilation diagnoses the binding and continues parsing the later rule

#### Scenario: Multiple malformed clauses report multiple errors
- **WHEN** independent inline-list errors appear in two declarations
- **THEN** compilation reports both errors with their respective source locations

### Requirement: Accepted notation preserves canonical program behavior
Changing only an accepted clause spelling or list layout SHALL preserve resolved domains, contracts, dependencies, reduction results, event and command order, structural visibility, and query acceleration decisions. Saving and reloading module artifacts SHALL preserve the same meaning without requiring notation-specific semantic records.

#### Scenario: Compact and block programs observe the same activations
- **WHEN** equivalent compact and legacy programs receive the same frame input and entity changes
- **THEN** their ordered observations, state, and derived-trait lifecycle outcomes are equivalent

#### Scenario: Module artifact round trip preserves compact rules
- **WHEN** compact rules are compiled to module artifacts and linked with dependent modules
- **THEN** their behavior and dependencies equal the corresponding block-form program

### Requirement: External list clauses are unique per handler
Each external handler SHALL declare each of reads, writes, projects, emits, commands, and effects at most once. Repeating a clause SHALL produce a diagnostic at the repeated clause, regardless of inline or block layout or whether the entries differ. Parsing SHALL continue through the repeated list and preserve later clauses, handlers, and declarations. The same clause SHALL remain valid in a different handler. Multiple entries within a single clause SHALL retain their existing validation rules.

#### Scenario: Repeated inline contract
- **WHEN** an external handler declares reads twice using inline lists
- **THEN** compilation reports a duplicate reads clause at the second declaration
- **AND** later handlers and rules are still parsed

#### Scenario: Repeated block and mixed contracts
- **WHEN** a contract clause is repeated using block lists or mixing block and inline forms
- **THEN** compilation reports the duplicate clause at the second declaration

#### Scenario: Independent handlers
- **WHEN** two external handlers each declare one effects clause
- **THEN** compilation accepts both clauses without a duplicate-clause error

#### Scenario: All contract families enforce uniqueness
- **WHEN** any of reads, writes, projects, emits, commands, or effects is repeated in one external handler
- **THEN** compilation reports a duplicate-clause error even if its entries differ

### Requirement: Bracketed trait access postfix
The parser SHALL accept a bracketed trait access postfix, `expr[TraitRef]`, after an identifier or a member expression on the same line. `TraitRef` SHALL be exactly one dotted trait name (`Match`, `tv.WorldTransform`, `EnemyLife.Dying`). Member access, calls and further postfixes MAY follow the closing bracket (`Game[Match].pos.x`). A bracketed list followed directly by `(` SHALL remain a query call with a query-filter list. A bracketed trait access SHALL also be accepted as the target of an assignment or compound assignment statement (`Game[Match].over = true`, `Game[Match].score += 1`). Whether the base may carry traits is decided by semantic analysis, not by the parser.

#### Scenario: Parse bracketed access on an entity name
- **WHEN** source contains `Game[Match].over` in expression position
- **THEN** the parser produces a trait access node with base `Game` and trait `Match`, followed by a member access `over`

#### Scenario: Parse bracketed access with a qualified trait and qualified base
- **WHEN** source contains `hud.Hud[text.ScreenLabel].visible`
- **THEN** the parser produces a trait access node with base `hud.Hud` and trait `text.ScreenLabel`, followed by a member access `visible`

#### Scenario: Query call keeps its filter list
- **WHEN** source contains `query.all[Enemy]()` and `query.count[EnemyAI, not Dead]()`
- **THEN** the parser produces query call expressions with those filter lists, not trait access nodes

#### Scenario: Assignment to a bracketed path
- **WHEN** a handler body contains `Game[Match].score += 1`
- **THEN** the parser produces a compound assignment whose target is base `Game`, trait `Match`, field path `score`

#### Scenario: More than one trait in brackets is rejected
- **WHEN** source contains `Game[Match, Score].over` or `Game[not Match].over` outside a query call
- **THEN** the parser reports that a trait access takes exactly one trait name
