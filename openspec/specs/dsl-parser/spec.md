## Requirements

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `view`, `event`, `func`, `interface`, `template`.

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

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

### Requirement: Unit parsing with apply, config, and child blocks
The parser SHALL parse `[pub] unit Name:` blocks containing `apply:` (list of traits), optional `config:` (field overrides), and optional `child:` (nested unit references).

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
The parser SHALL accept `template_decl` as a new top-level declaration, structurally identical to `unit_decl` except using the `TEMPLATE` keyword. The EBNF is:

```ebnf
template_decl = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                apply_block
                [ config_block_with_trait_state ]
                [ child_block ]
                DEDENT ;
```

#### Scenario: Template with apply and config parsed
- **WHEN** source contains a complete `template` declaration with `apply:` and `config:` blocks
- **THEN** the parser produces a `TemplateDecl` AST node with `apply` and `config` children

#### Scenario: Template with pub modifier parsed
- **WHEN** `pub template Foo:` appears in source
- **THEN** the parser produces a `TemplateDecl` node with `is_pub = true`

### Requirement: `filter:` and `exclude:` both optional in system grammar (BREAKING for old bracket syntax)
Both `filter:` and `exclude:` are optional on system declarations. The parser SHALL accept systems with `filter:` only, `exclude:` only, both, or neither. The old bracket-list syntax `filter: [A, B, C]` is rejected. Updated EBNF:

```ebnf
system_decl    = "system" IDENTIFIER ":" NEWLINE INDENT
                 [ filter_clause ]
                 [ exclude_clause ]
                 [ target_clause ]
                 { event_handler }
                 DEDENT ;

filter_clause  = "filter" ":" NEWLINE INDENT
                 { IDENTIFIER NEWLINE }
                 DEDENT ;

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

### Requirement: `spawn` statement grammar
The parser SHALL accept `spawn` as a statement inside event handler bodies:

```ebnf
spawn_stmt = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" NEWLINE ;
spawn_arg_list = spawn_arg { "," spawn_arg } ;
spawn_arg = IDENTIFIER "=" expression ;
```

#### Scenario: Spawn statement with overrides parsed
- **WHEN** `spawn Enemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0)` appears in a handler
- **THEN** the parser produces a `SpawnStmt` with template name `"Enemy"` and two field overrides

#### Scenario: Spawn with no overrides parsed
- **WHEN** `spawn Foo()` appears in a handler (template has all defaults in config)
- **THEN** the parser produces a `SpawnStmt` with template name `"Foo"` and empty override list

### Requirement: `destroy` statement grammar
The parser SHALL accept `destroy` as a zero-argument statement inside event handler bodies:

```ebnf
destroy_stmt = "destroy" NEWLINE ;
```

#### Scenario: Destroy statement parsed
- **WHEN** `destroy` appears on its own line inside a handler
- **THEN** the parser produces a `DestroyStmt` node

### Requirement: `load` statement grammar
The parser SHALL accept `load` as a statement inside event handler bodies, followed by a dotted module name:

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
The parser SHALL accept `enable` and `disable` as statements inside event handler bodies:

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

### Requirement: `on spawn()`, `on destroy()`, `on load()`, `on unload()` lifecycle handler grammar
The parser SHALL accept `on spawn()`, `on destroy()`, `on load()`, and `on unload()` as event handler forms with empty parameter lists:

```ebnf
event_handler = "on" lifecycle_name "(" ")" ":" NEWLINE INDENT
                { statement }
                DEDENT ;
lifecycle_name = "spawn" | "destroy" | "load" | "unload" | IDENTIFIER ;
```

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

### Requirement: Expression parsing with precedence
The parser SHALL parse expressions using precedence climbing, supporting binary operators (`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `and`, `or`), unary operators (`not`, `-`), member access (`.`), function calls, and lambda expressions (`param => body`).

#### Scenario: Binary expression with correct precedence
- **WHEN** the source contains `a + b * c`
- **THEN** the parser produces a BinaryExpr with `+` at the root and `*` as the right child

#### Scenario: Lambda expression
- **WHEN** the source contains `x => x * 2`
- **THEN** the parser produces a LambdaExpr with parameter "x" and a BinaryExpr body

### Requirement: Pipeline expression parsing
The parser SHALL parse functional pipeline chains: `collection.map(f).filter(g).reduce(init, h)`.

#### Scenario: Map-filter-reduce chain
- **WHEN** the source contains `items.map(i => i.price).filter(p => p > 0).reduce(0, acc, p => acc + p)`
- **THEN** the parser produces a PipelineExpr with three chained operations

### Requirement: Func parsing with purity contract
The parser SHALL parse `[pub] func name(params) [-> type]:` blocks with a body of statements. The parser SHALL record that these are `func` declarations (purity enforcement is done by the semantic analyzer).

#### Scenario: Pure function with return type
- **WHEN** the source contains `func distance(a: vec3, b: vec3) -> float:`
- **THEN** the parser produces a FuncNode with two parameters and return type float

### Requirement: Const block parsing
The parser SHALL parse `const:` blocks containing name-value assignments where values are string literals, number literals, or hex color literals.

#### Scenario: Const block with strings
- **WHEN** the source contains `const:` with `SHOP_TITLE = "Cactus Shop"` and `MAX_ITEMS = 50`
- **THEN** the parser produces a ConstBlockNode with two ConstAssignment entries

### Requirement: View parsing with UI element tree
The parser SHALL parse `view Name(params):` blocks containing nested UI element declarations.

#### Scenario: View with nested elements
- **WHEN** the source contains `view ShopUI(inventory: list[Item]):` with nested `panel:` and `text:` elements
- **THEN** the parser produces a ViewNode with nested ViewElement children

### Requirement: Match expression parsing
The parser SHALL parse `match expr:` blocks with pattern arms.

#### Scenario: Match on enum
- **WHEN** the source contains `match state:` with arms for different enum variants
- **THEN** the parser produces a MatchExpr with the matched expression and a list of arms

### Requirement: Module path support in `module` and `use` declarations
The parser SHALL accept dotted identifiers in `module` and `use` declarations. The parser SHALL also support `as` aliases in `use` declarations and `as` aliases in filter clause entries.

#### Scenario: Dotted module declaration
- **WHEN** `module enemies.walker` appears at the top level
- **THEN** the parser produces a `ModuleNode` with `name = "enemies.walker"`

#### Scenario: Use with alias
- **WHEN** `use phys.body as b` appears
- **THEN** the parser produces a `UseNode` with `module_name = "phys.body"` and `alias = "b"`

#### Scenario: Filter with as alias
- **WHEN** a system has `filter: [phys.Body as b, render.Sprite as s]`
- **THEN** the parser stores each filter entry with its optional alias
