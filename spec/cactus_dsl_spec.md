# Cactus DSL Language Specification

**Version:** 0.1.0
**Status:** Draft

## 1. Overview

Cactus DSL is a declarative, data-oriented language designed for game development. It targets kids (grades 1-5) with simple indentation-based syntax, but is powerful enough for complex 3D simulations. The language follows ECS (Entity Component System) architecture: data is defined in **traits** and **structs**, logic lives in **systems**, and entities are composed as **units**.

## 2. Lexical Structure

### 2.1 Character Set

Source files are UTF-8 encoded. Only ASCII characters are valid in identifiers and keywords. Non-ASCII characters are allowed only inside string literals.

### 2.2 Indentation

Cactus uses significant indentation (spaces only). Tabs are rejected with an error. Each indentation level is 4 spaces. The lexer maintains an indent stack and emits explicit `INDENT` and `DEDENT` tokens.

```
trait Player:        # TRAIT IDENTIFIER COLON NEWLINE INDENT
    var health: int  # VAR IDENTIFIER COLON IDENTIFIER NEWLINE DEDENT
```

### 2.3 Comments

Single-line comments start with `#` (when not followed by hex digits forming a color literal) and extend to end of line.

```
# This is a comment
var x: int  # Inline comment
```

### 2.4 Keywords

```
module  use     const   struct  enum    trait   unit    system
view    event   func    interface
let     var     persist sync    pub
on      emit    if      else    match   return
apply   config  child   filter  exclude target
map     reduce  true    false   as      and     or      not
template  spawn   destroy load    unload
enable  disable  disabled
```

### 2.5 Operators and Punctuation

| Token | Symbol | Token | Symbol |
|-------|--------|-------|--------|
| COLON | `:` | COMMA | `,` |
| DOT | `.` | ARROW | `->` |
| FAT_ARROW | `=>` | ASSIGN | `=` |
| LPAREN | `(` | RPAREN | `)` |
| LBRACKET | `[` | RBRACKET | `]` |
| LBRACE | `{` | RBRACE | `}` |
| PLUS | `+` | MINUS | `-` |
| STAR | `*` | SLASH | `/` |
| PERCENT | `%` | AMPERSAND | `&` |
| PIPE | `\|` | CARET | `^` |
| TILDE | `~` | EQUALS | `==` |
| NOT_EQUALS | `!=` | LESS | `<` |
| GREATER | `>` | LESS_EQ | `<=` |
| GREATER_EQ | `>=` | PLUS_ASSIGN | `+=` |
| MINUS_ASSIGN | `-=` | | |

### 2.6 Literals

#### Integer Literals
Sequence of decimal digits: `0`, `42`, `1000`

#### Float Literals
Decimal digits with a decimal point: `3.14`, `0.5`, `100.0`

#### String Literals
Double-quoted UTF-8 strings: `"Hello World"`. Only valid inside `const` blocks.

#### Hex Color Literals
`#` followed by 6 (RGB) or 8 (RGBA) hex digits: `#FF0000`, `#FF000080`

#### Boolean Literals
`true`, `false`

## 3. Grammar (EBNF)

### 3.1 Program Structure

```ebnf
program         = { declaration } EOF ;
declaration     = module_decl | use_decl | const_block | struct_decl
                | enum_decl | trait_decl | unit_decl | template_decl | system_decl
                | view_decl | event_decl | func_decl | interface_decl ;
```

### 3.2 Module and Imports

```ebnf
module_decl     = "module" dotted_name NEWLINE ;
use_decl        = "use" dotted_name [ "as" IDENTIFIER ] NEWLINE ;
dotted_name     = IDENTIFIER { "." IDENTIFIER } ;
```

Module names use dot notation mapping to folder structure:

```
module enemies.walker        # file must be enemies/walker.cactus
use phys.body                # imports all pub symbols from phys/body.cactus
use phys.body as b           # alias: access as b.RigidBody instead of phys.body.RigidBody
use player                   # simple (single file): player.cactus
```

### 3.3 Const Block

```ebnf
const_block     = "const" ":" NEWLINE INDENT
                  { const_assign }
                  DEDENT ;
const_assign    = IDENTIFIER "=" const_value NEWLINE ;
const_value     = STRING_LITERAL | INT_LITERAL | FLOAT_LITERAL | HEX_COLOR ;
```

### 3.4 Struct

```ebnf
struct_decl     = "struct" IDENTIFIER ":" NEWLINE INDENT
                  { field_decl }
                  DEDENT ;
```

### 3.5 Enum

```ebnf
enum_decl       = "enum" IDENTIFIER ":" NEWLINE INDENT
                  { enum_variant }
                  DEDENT ;
enum_variant    = IDENTIFIER [ "=" INT_LITERAL ] NEWLINE ;
```

### 3.6 Trait

Traits may have a body (data traits) or no body (marker traits). Marker traits are zero-cost tag components used purely for filtering.

```ebnf
trait_decl      = [ "pub" ] "trait" IDENTIFIER
                  [ ":" NEWLINE INDENT
                    { field_decl | event_handler | func_decl }
                    DEDENT ] ;
```

**Marker traits** (no colon, no body):
```cactus
trait Persistent        # no fields — zero-cost marker
trait Frozen
pub trait Dead
```

**Data traits** (colon + body):
```cactus
trait Health:
    var health: int = 100
    let max_health: int = 100
```

### 3.7 Fields

```ebnf
field_decl      = field_modifiers ( "let" | "var" ) IDENTIFIER ":" type_ref
                  [ "=" expression ] NEWLINE ;
field_modifiers = { "persist" | "sync" | "pub" } ;
```

### 3.8 Unit

A `unit` declares a singleton entity archetype — exactly one instance, instantiated at program start (root module) or when the module is `load`ed. Each trait in `apply:` may carry a `: disabled` annotation to start inactive.

```ebnf
unit_decl       = [ "pub" ] "unit" IDENTIFIER ":" NEWLINE INDENT
                  apply_block
                  [ config_block ]
                  [ child_block ]
                  DEDENT ;
apply_block     = "apply" ":" NEWLINE INDENT
                  { apply_entry }
                  DEDENT ;
apply_entry     = IDENTIFIER [ ":" "disabled" ] NEWLINE ;
config_block    = "config" ":" NEWLINE INDENT
                  { config_assign }
                  DEDENT ;
config_assign   = IDENTIFIER "=" expression NEWLINE ;
child_block     = "child" ":" NEWLINE INDENT
                  { child_entry }
                  DEDENT ;
child_entry     = IDENTIFIER IDENTIFIER NEWLINE ;
```

### 3.8a Template

A `template` is a reusable multi-instance entity blueprint — **not** auto-instantiated. Instances are created at runtime with the `spawn` statement. Template syntax is identical to `unit` except it uses the `template` keyword.

```ebnf
template_decl   = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                  apply_block
                  [ config_block ]
                  [ child_block ]
                  DEDENT ;
```

```cactus
template WalkerEnemy:
    apply:
        Position
        EnemyAI
        Frozen: disabled     # present but starts inactive
    config:
        patrol_speed = PATROL_SPEED
        direction = 1.0
```

### 3.9 System

Both `filter:` and `exclude:` are optional. A system with no `filter:` matches **all entities** (filter_mask = 0). Trait field access in handler bodies is only valid for traits declared in `filter:`.

```ebnf
system_decl     = "system" IDENTIFIER ":" NEWLINE INDENT
                  [ filter_clause ]
                  [ exclude_clause ]
                  [ target_clause ]
                  { event_handler }
                  DEDENT ;
filter_clause   = "filter" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
exclude_clause  = "exclude" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
target_clause   = "target" ":" ( "cpu" | "gpu" ) NEWLINE ;
```

```cactus
# System with filter and exclude
system PatrolSystem:
    filter:
        Position
        EnemyAI
    exclude:
        Frozen
        Dead

    on tick(dt: float):
        pos = pos + vec2(patrol_speed * direction * dt, 0.0)

# System with no filter (matches all entities, no field access)
system SceneCleanup:
    exclude:
        Persistent

    on unload():
        destroy
```

**Note**: The old bracket-list syntax `filter: [A, B, C]` is no longer valid.

### 3.10 Event Handler

Event handlers respond to named events or one of the four built-in lifecycle events. Lifecycle handlers have empty parameter lists.

```ebnf
event_handler   = "on" event_name "(" [ param_list ] ")" ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;
event_name      = IDENTIFIER | "spawn" | "destroy" | "load" | "unload" ;
param_list      = param { "," param } ;
param           = IDENTIFIER ":" type_ref ;
```

Lifecycle handlers:
- `on spawn()` — fires after a new entity matching the filter is created
- `on destroy()` — fires before a matching entity is removed
- `on unload()` — fires **before** new entities are created during a `load` transition (Phase 1)
- `on load()` — fires **after** all new entities are created during a `load` transition (Phase 3)

### 3.11 Event Declaration

```ebnf
event_decl      = "event" IDENTIFIER ":" NEWLINE INDENT
                  { field_decl }
                  DEDENT ;
```

### 3.12 Func

```ebnf
func_decl       = [ "pub" ] "func" IDENTIFIER "(" [ param_list ] ")"
                  [ "->" type_ref ] ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;
```

### 3.13 View

```ebnf
view_decl       = "view" IDENTIFIER "(" [ param_list ] ")" ":" NEWLINE INDENT
                  { view_element }
                  DEDENT ;
view_element    = IDENTIFIER ":" NEWLINE INDENT
                  { view_prop | view_element }
                  DEDENT ;
view_prop       = IDENTIFIER "=" expression NEWLINE ;
```

### 3.14 Interface

```ebnf
interface_decl  = "interface" IDENTIFIER ":" NEWLINE INDENT
                  { func_signature }
                  DEDENT ;
func_signature  = "func" IDENTIFIER "(" [ param_list ] ")"
                  [ "->" type_ref ] NEWLINE ;
```

### 3.15 Types

```ebnf
type_ref        = IDENTIFIER [ "[" type_ref "]" ] ;
```

Built-in type names: `int`, `float`, `bool`, `string`, `vec2`, `vec3`, `quat`, `color`, `entity_id`.
Parameterized: `list[T]` where `T` is any type.

### 3.16 Expressions

```ebnf
expression      = or_expr ;
or_expr         = and_expr { "or" and_expr } ;
and_expr        = equality_expr { "and" equality_expr } ;
equality_expr   = comparison_expr { ( "==" | "!=" ) comparison_expr } ;
comparison_expr = additive_expr { ( "<" | ">" | "<=" | ">=" ) additive_expr } ;
additive_expr   = multiplicative_expr { ( "+" | "-" ) multiplicative_expr } ;
multiplicative_expr = unary_expr { ( "*" | "/" | "%" ) unary_expr } ;
unary_expr      = ( "not" | "-" ) unary_expr | postfix_expr ;
postfix_expr    = primary_expr { "." IDENTIFIER [ "(" [ arg_list ] ")" ] } ;
primary_expr    = INT_LITERAL | FLOAT_LITERAL | STRING_LITERAL | HEX_COLOR
                | BOOL_LITERAL | IDENTIFIER | "(" expression ")"
                | lambda_expr | match_expr | if_expr | list_literal ;
lambda_expr     = IDENTIFIER "=>" expression ;
match_expr      = "match" expression ":" NEWLINE INDENT
                  { match_arm }
                  DEDENT ;
match_arm       = pattern "=>" expression NEWLINE ;
pattern         = IDENTIFIER | INT_LITERAL | "_" ;
if_expr         = "if" expression ":" expression "else" ":" expression ;
list_literal    = "[" [ expression { "," expression } ] "]" ;
arg_list        = expression { "," expression } ;
```

### 3.17 Statements

```ebnf
statement       = var_assign | emit_stmt | spawn_stmt | destroy_stmt | load_stmt
                | enable_stmt | disable_stmt | return_stmt | expr_stmt | if_stmt ;
var_assign      = IDENTIFIER ( "=" | "+=" | "-=" ) expression NEWLINE ;
emit_stmt       = "emit" IDENTIFIER "(" [ arg_list ] ")" NEWLINE ;
spawn_stmt      = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" NEWLINE ;
spawn_arg_list  = spawn_arg { "," spawn_arg } ;
spawn_arg       = IDENTIFIER "=" expression ;
destroy_stmt    = "destroy" NEWLINE ;
load_stmt       = "load" dotted_name NEWLINE ;
enable_stmt     = "enable" IDENTIFIER NEWLINE ;
disable_stmt    = "disable" IDENTIFIER NEWLINE ;
return_stmt     = "return" [ expression ] NEWLINE ;
expr_stmt       = expression NEWLINE ;
if_stmt         = "if" expression ":" NEWLINE INDENT
                  { statement }
                  DEDENT
                  [ "else" ":" NEWLINE INDENT
                    { statement }
                    DEDENT ] ;
```

**`spawn`** creates a new entity from a template, optionally overriding any config fields:
```cactus
spawn WalkerEnemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0)
```

**`destroy`** removes the current entity (queued for removal at end of handler):
```cactus
if health <= 0:
    destroy
```

**`load`** transitions to a new module-as-scene (deferred to end of frame):
```cactus
load levels.level2
```

**`enable`** / **`disable`** toggle a trait's active state on the current entity:
```cactus
enable Frozen
disable EnemyAI
```

## 4. Operator Precedence

From lowest to highest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 (lowest) | `or` | Left |
| 2 | `and` | Left |
| 3 | `==`, `!=` | Left |
| 4 | `<`, `>`, `<=`, `>=` | Left |
| 5 | `+`, `-` | Left |
| 6 | `*`, `/`, `%` | Left |
| 7 (highest) | `not`, `-` (unary) | Right (prefix) |
| 8 | `.` (member access), `()` (call) | Left (postfix) |

## 5. Type System

### 5.1 Primitive Types

| Type | Description | Size |
|------|-------------|------|
| `int` | 32-bit signed integer | 4 bytes |
| `float` | 64-bit floating point | 8 bytes |
| `bool` | Boolean | 1 byte |
| `string` | UTF-8 immutable string (rvalue only) | pool ID |
| `vec2` | `{ x: float, y: float }` | 16 bytes |
| `vec3` | `{ x: float, y: float, z: float }` | 24 bytes |
| `quat` | `{ x: float, y: float, z: float, w: float }` | 32 bytes |
| `color` | RGBA color from hex literal | 4 bytes |
| `entity_id` | Opaque handle to a unit instance | 8 bytes |

### 5.2 Composite Types

- **`struct Name:`** — Value object. Fields only. Passed by value. No identity. No methods.
- **`enum Name:`** — Named set of integer constants. Used for state machines.
- **`list[T]`** — Ordered collection supporting `map`, `filter`, `reduce`. Functional stream.

### 5.3 Field Modifiers

| Modifier | Meaning | Constraint |
|----------|---------|------------|
| `let` | Immutable. Set once at creation. | Cannot be reassigned. |
| `var` | Mutable. Can be changed by systems. | Default mutability. |
| `persist` | Marks field for serialization. | Only on `var` fields. |
| `sync` | Marks field for network replication. | Only on `var` fields. |
| `pub` | Public visibility outside module. | On fields, traits, units, funcs. |

### 5.4 Type Inference

- Lambda parameters are inferred from context (e.g., `items.map(i => i.price)` infers `i: Item`)
- Binary operation results follow standard promotion rules
- Function call return types are resolved from declarations

## 6. Semantic Constraints

### 6.1 Const-String Rule

String literals (`"..."`) are **forbidden** outside `const` blocks. All string references in logic must go through `const` identifiers. This ensures compile-time string interning and prevents accidental allocations in hot loops.

```
# VALID
const:
    SHOP_TITLE = "Cactus Shop"

# INVALID — string literal in system body
system UI:
    on tick(dt: float):
        set_title("Bad")  # ERROR: string literal outside const block
```

### 6.2 Func Purity

Functions declared with `func` are **pure**:
- No `emit` statements allowed
- No mutation of external state
- No `world` access
- Return value only
- No side effects

```
# VALID
func distance(a: vec3, b: vec3) -> float:
    return ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z))

# INVALID
func bad() -> int:
    emit SomeEvent()  # ERROR: emit in func
    return 0
```

### 6.3 No Recursion

Recursive function calls (direct or indirect) are forbidden. This is required for GPU safety and deterministic execution.

### 6.4 No Imperative Loops

There are no `for`, `while`, or `do` loops. All iteration is via functional collection operations:
- `collection.map(f)` — Transform each element
- `collection.filter(pred)` — Keep elements matching predicate
- `collection.reduce(init, accumulator)` — Fold to single value

### 6.5 Persist/Sync Validation

- `persist` modifier is only valid on `var` fields (not `let`)
- `sync` modifier is only valid on `var` fields (not `let`)
- Both `persist` and `sync` can be combined on the same field
- The semantic analyzer validates these constraints and reports errors with source locations

```
# VALID
trait Player:
    persist sync var health: int = 100
    persist var score: int = 0
    let max_health: int = 100  # let is fine without persist/sync

# INVALID
trait Bad:
    persist let name: string  # ERROR: persist on let field
    sync let id: int          # ERROR: sync on let field
```

### 6.6 System Filter Validation

- All trait names in a system's `filter` clause must reference declared traits
- Filter traits must be compatible (no conflicting field names)

### 6.7 Event Validation

- `emit` statements must reference declared events
- Event handler parameter signatures must match event field declarations

## 7. Execution Model

### 7.1 ECS Architecture

- **Traits** define data schemas (components in ECS terminology)
- **Units** compose traits into entity archetypes
- **Systems** contain logic that operates on entities matching trait filters
- **Events** enable decoupled communication between systems

### 7.2 System Execution

Systems execute their event handlers each frame. The `on tick(dt: float)` handler runs every frame. Other handlers run when their corresponding events are emitted.

### 7.3 Presentation

Raylib is the default rendering/presentation API. Generated code uses Raylib for window management, rendering, input, and audio. Alternative libraries (SDL, etc.) can be supported via backend configuration.

## 8. Module System

### 8.1 One Module = One File

Each `.cactus` source file is exactly one module. The module's identity is derived from its filesystem path relative to the project root, using dot notation:

| File path | Module name |
|-----------|-------------|
| `player.cactus` | `player` |
| `enemies/walker.cactus` | `enemies.walker` |
| `lib/physics.cactus` | `lib.physics` |

The optional `module` declaration, if present, must match the filesystem-derived name. Files in different folders with the same filename are distinct modules (e.g., `enemies/physics.cactus` ≠ `lib/physics.cactus`).

### 8.2 Pub Visibility

Top-level declarations can be marked `pub` to make them accessible from other modules. Without `pub`, declarations are module-private.

```cactus
pub trait Position:          # visible to other modules
    var x: float = 0.0
    var y: float = 0.0

trait PlayerPhysics:         # private to this module
    var velocity: vec2
```

The `pub` modifier is valid on: `trait`, `struct`, `enum`, `event`, `unit`, `func`, and individual fields.

### 8.3 Importing Modules

```cactus
use player                   # import player.cactus
use enemies.walker           # import enemies/walker.cactus
use phys.body as b           # import with alias
```

All `pub` symbols from the imported module become available via qualified access.

### 8.4 Qualified Access

Imported symbols are accessed through their module name or alias:

```cactus
use player
use phys.body as b

system Movement:
    filter: [player.Position, b.RigidBody]
    on tick(dt: float):
        player.Position.x += b.RigidBody.velocity_x * dt
```

### 8.5 Unqualified Shortcut

If a pub symbol name is **unique** across all imported modules and local declarations, it can be used without qualification:

```cactus
use player       # has pub trait Position (unique name)
use enemies      # has pub trait EnemyAI (unique name)

system Movement:
    filter: [Position, EnemyAI]    # unqualified — no ambiguity
    on tick(dt: float):
        Position.x += 1.0
```

If two modules export the same name, the compiler requires qualification:

```cactus
use module_a     # has pub trait Config
use module_b     # has pub trait Config

# ERROR: ambiguous reference 'Config': defined in module_a and module_b
# FIX: use module_a.Config or module_b.Config
```

### 8.6 Filter Clause Aliases

System `filter:` clauses support `as` aliases for trait references. Aliases are scoped to the system body and provide short names for field access:

```cactus
system Render:
    filter: [phys.Body as b, render.Sprite as s]
    on tick(dt: float):
        draw(b.x, b.y, s.width, s.height)
```

Aliases work with both qualified and unqualified trait names:

```cactus
system Simple:
    filter: [Position as pos, Velocity as vel]
    on tick(dt: float):
        pos.x += vel.dx * dt
```

### 8.7 Trait Field Disambiguation

When multiple filtered traits have fields with the **same name**, access must be qualified by trait name or alias. When field names are unique across all filtered traits, unqualified access is allowed:

```cactus
# Body has field x, Sprite has field x — conflict!
system Render:
    filter: [Body as b, Sprite as s]
    on tick(dt: float):
        draw(b.x, b.y, s.x, s.y)     # qualified via alias — OK
        # draw(x, y, ...)             # ERROR: ambiguous field 'x'

# Position has x,y — Velocity has dx,dy — no conflict
system Move:
    filter: [Position, Velocity]
    on tick(dt: float):
        x += dx * dt                   # unqualified — OK, no ambiguity
        y += dy * dt
```

### 8.8 Module Resolution Order

When resolving `use` declarations, the compiler searches for `.cactus` files in this order:

1. Root file's directory
2. Directories from `--module-path` flags (left to right)

Dotted module names map to folder paths: `use enemies.walker` → searches for `enemies/walker.cactus`.

### 8.9 Circular Dependencies

Circular module dependencies are forbidden. The compiler detects cycles and reports the full path:

```
ERROR: circular dependency: A → B → C → A
```

### 8.10 Compilation Model

Modules are compiled in topological order (dependencies first). Each compiled module produces a binary `.cmod` artifact in the `build/` folder containing its `DecoratedProgram` and public symbol table. Dependent modules load only the public symbols from `.cmod` files — not the full AST — keeping memory usage bounded. After all modules compile, the linker merges all artifacts into a single `DecoratedProgram` for code generation.

### 8.11 Module Data File (`_data.bin`)

Each compiled module also produces a `<module_name>_data.bin` flat binary file alongside the generated `.cpp`. This file contains all `unit` instance configurations — field values and initial trait active states — serialized for fast runtime loading.

**Format:**
```
[magic: 4 bytes "CDAT"] [version: uint16] [entity_count: uint32]
[entity_0: name_len(uint16) + name_bytes + field_values(packed) + trait_mask(uint64)]
[entity_1: ...]
...
```

- Field values are packed in declaration order with no padding between fields
- `trait_mask` is a `uint64` bitmask where each bit corresponds to a trait's compile-time index (1 = active, 0 = inactive/disabled)
- No offset table — the file is loaded as a single sequential read
- `template` declarations produce **no** entries in `_data.bin`
- At runtime, `load module.name` reads `module_name_data.bin` to instantiate the module's entities

**Version mismatch**: if the file's version header does not match the compiler's current format version, the runtime rejects the file with an error.

## 9. Dynamic ECS

### 9.1 Templates and Spawn

`template` is symmetric with `unit` but defines a **multi-instance blueprint**. Instances are created at runtime with `spawn`, which is symmetric with `emit`:

| Declaration | Instantiation |
|-------------|---------------|
| `event Foo:` | `emit Foo(...)` |
| `template Foo:` | `spawn Foo(...)` |
| `unit Foo:` | (auto-instantiated on load) |

```cactus
template Enemy:
    apply:
        Position
        EnemyAI
        Frozen: disabled
    config:
        patrol_speed = PATROL_SPEED

# In a system handler:
spawn Enemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0, patrol_max_x = 550.0)
```

**`spawn` field rules:**
- Any field from the template's applied traits can be overridden
- Fields not provided use the template's `config:` default
- Fields with no default and not provided at spawn → compile error

**`destroy` removes the current entity:**
```cactus
system DeathSystem:
    filter:
        Health

    on tick(dt: float):
        if health <= 0:
            destroy
```

`destroy` uses **swap-and-delete**: the last entity in the SoA arrays fills the deleted slot, keeping arrays packed. Entity ordering is not preserved.

### 9.2 Trait Enable/Disable

Each entity has a `uint64` **trait active bitmask**. Every trait is assigned a unique bit at compile time. `enable` and `disable` flip bits without changing field data:

```cactus
# Freeze an enemy (stop patrol, start frozen visual)
system FreezeSystem:
    filter:
        Position
        EnemyAI

    on FreezeEvent():
        disable EnemyAI    # PatrolSystem won't process this entity
        enable Frozen      # FrozenSystem will now process it
```

System filters are compiled to bitmask predicates:
- `filter:` → `filter_mask` — entity must have all these bits set
- `exclude:` → `exclude_mask` — entity must have none of these bits set
- Loop condition: `(trait_mask & filter_mask) == filter_mask && (trait_mask & exclude_mask) == 0`
- **No filter** (`filter_mask = 0`): condition is always true — matches all entities

### 9.3 Scene Loading

A **module is a scene**. The `load` statement transitions to a new module-as-scene, deferred to end-of-frame. The runtime performs three phases:

```
Phase 1 — UNLOAD:  emit on unload()  → teardown systems run (e.g., SceneCleanup destroys entities)
Phase 2 — INSTANTIATE: read _data.bin → spawn unit entities → emit on spawn() per entity
Phase 3 — LOAD:    emit on load()    → setup systems run (e.g., spawn template instances)
```

**Key rules:**
- Only one `load` call per frame; a second `load` in the same frame is a runtime error
- `load` is valid in any system event handler
- Root module `unit` declarations are always present (never unloaded)
- Non-root module entities are created by `load` and torn down by the next `load` or shutdown

**Complete scene transition example:**
```cactus
use std.core         # provides Persistent + SceneCleanup
use levels.level1
use levels.level2

system GameManager:
    filter:
        GameState

    on LevelComplete():
        load levels.level2    # deferred to end of frame

    on PlayerDied(lives: int):
        if lives <= 0:
            load ui.game_over
        else:
            load levels.level1

# In levels/level1.cactus:
system LevelSetup:
    filter:
        LevelState

    on load():
        spawn Enemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0, patrol_max_x = 550.0)
        spawn Enemy(pos = vec2(800.0, 568.0), patrol_min_x = 700.0, patrol_max_x = 1000.0)
```

### 9.4 Standard Library (`std.core`)

The `std.core` module ships with the compiler and must be explicitly imported:

```cactus
use std.core
```

It provides:

**`pub trait Persistent`** — marker trait; entity survives `load` transitions when `std.SceneCleanup` is active.

**`pub system SceneCleanup`** — no-filter system that destroys all non-persistent entities on scene unload:
```cactus
# std/core.cactus (conceptual — ships with compiler)
module std.core

pub trait Persistent

pub system SceneCleanup:
    exclude:
        Persistent

    on unload():
        destroy
```

**Usage:**
```cactus
use std.core

pub unit Player:
    apply:
        std.Persistent    # or just Persistent if unambiguous
        Position
        Health
    config:
        pos = vec2(100.0, 300.0)
        health = 100
```

Without `use std.core`, no automatic cleanup occurs on `load` — the developer is responsible for custom teardown.
