# Cactus DSL Language Specification

**Version:** 0.2.0
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
event   func
let     var     persist sync    pub
on      emit    if      else    match   return
apply   config  filter  exclude
map     reduce  true    false   as      and     or      not
template  spawn   destroy load    unload
enable  disable  disabled
asset   input   fixed_tick  late_tick
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
Double-quoted UTF-8 strings: `"Hello World"`. Only valid inside `const` blocks (see §6.1).

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
                | event_decl | func_decl | asset_decl | input_decl ;
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
                  DEDENT ;
apply_block     = "apply" ":" NEWLINE INDENT
                  { apply_entry }
                  DEDENT ;
apply_entry     = IDENTIFIER [ ":" "disabled" ] NEWLINE ;
config_block    = "config" ":" NEWLINE INDENT
                  { config_assign }
                  DEDENT ;
config_assign   = IDENTIFIER "=" expression NEWLINE ;
```

> **Note:** The `child:` block was removed in v0.2. To compose entities with sub-entities, use `spawn` in `on spawn()` handlers and `destroy entity_id` in `on destroy()` handlers. See §9.1 for the pattern.

### 3.8a Template

A `template` is a reusable multi-instance entity blueprint — **not** auto-instantiated. Instances are created at runtime with the `spawn` statement/expression. Template syntax is identical to `unit` except it uses the `template` keyword.

```ebnf
template_decl   = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                  apply_block
                  [ config_block ]
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

Both `filter:` and `exclude:` are optional. A system with no `filter:` matches **all entities** (filter_mask = 0). Each `filter:` entry may declare an optional `as` alias for field access. The trait name is the implicit alias when no `as` is given.

```ebnf
system_decl     = "system" IDENTIFIER ":" NEWLINE INDENT
                  [ filter_clause ]
                  [ exclude_clause ]
                  { event_handler }
                  DEDENT ;
filter_clause   = "filter" ":" NEWLINE INDENT
                  { filter_entry }
                  DEDENT ;
filter_entry    = dotted_name [ "as" IDENTIFIER ] NEWLINE ;
exclude_clause  = "exclude" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
```

```cactus
# System with filter, exclude, and aliases
system PatrolSystem:
    filter:
        Position as pos
        EnemyAI as ai
    exclude:
        Frozen
        Dead

    on tick(dt: float):
        pos.pos = pos.pos + vec2(ai.patrol_speed * ai.direction * dt, 0.0)

# System with no filter (matches all entities, no field access)
system SceneCleanup:
    exclude:
        Persistent

    on unload():
        destroy
```

### 3.10 Event Handler

Event handlers respond to named events or one of the built-in lifecycle events. Lifecycle handlers have fixed parameter signatures. **User-defined event handlers have empty parameter lists** — the event payload is accessed via the implicit `event` object (see §6.8).

```ebnf
event_handler   = "on" event_name "(" [ param_list ] ")" ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;
event_name      = IDENTIFIER | "spawn" | "destroy" | "load" | "unload"
                | "tick" | "fixed_tick" | "late_tick" | "input" ;
param_list      = param { "," param } ;
param           = IDENTIFIER ":" type_ref ;
```

**Built-in lifecycle handlers and their required signatures:**

| Handler | Parameters | Phase | Description |
|---------|-----------|-------|-------------|
| `on input()` | none | Input | Input snapshot ready. Write intent traits. |
| `on fixed_tick(dt: float)` | `dt: float` | Physics | Fixed timestep. May run 0–N times per frame. |
| `on tick(dt: float)` | `dt: float` | General | Runs once per rendered frame. |
| `on late_tick(dt: float)` | `dt: float` | Post | After tick. Camera follow, dependent transforms. |
| `on spawn()` | none | Entity | Fires after a new entity matching the filter is created. |
| `on destroy()` | none | Entity | Fires before a matching entity is removed. |
| `on unload()` | none | Scene | Fires **before** new entities are created (Phase 1). |
| `on load()` | none | Scene | Fires **after** all new entities are created (Phase 3). |

**User event handlers** use the implicit `event` object:
```cactus
event PlayerDamaged:
    var amount: int

system HealthSystem:
    filter:
        Health as h

    on PlayerDamaged():              # empty param list
        h.health -= event.amount     # event.field access
        if h.health <= 0:
            destroy
```

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

### 3.13 Types

```ebnf
type_ref        = IDENTIFIER [ "[" type_ref "]" ] ;
```

Built-in type names: `int`, `float`, `bool`, `string`, `vec2`, `vec3`, `quat`, `color`, `entity_id`.
Parameterized: `list[T]` where `T` is any type.

### 3.14 Expressions

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
                | lambda_expr | match_expr | if_expr | list_literal | spawn_expr ;
lambda_expr     = IDENTIFIER "=>" expression ;
match_expr      = "match" expression ":" NEWLINE INDENT
                  { match_arm }
                  DEDENT ;
match_arm       = pattern "=>" expression NEWLINE ;
pattern         = IDENTIFIER | INT_LITERAL | "_" ;
if_expr         = "if" expression ":" expression "else" ":" expression ;
list_literal    = "[" [ expression { "," expression } ] "]" ;
arg_list        = expression { "," expression } ;
spawn_expr      = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" ;
```

`spawn_expr` is an expression that creates a new entity and returns its `entity_id`. See §9.1.

### 3.15 Statements

```ebnf
statement       = let_decl | var_decl | var_assign
                | emit_stmt | spawn_expr NEWLINE | destroy_stmt | load_stmt
                | enable_stmt | disable_stmt | return_stmt | expr_stmt | if_stmt ;
let_decl        = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_decl        = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_assign      = IDENTIFIER ( "=" | "+=" | "-=" ) expression NEWLINE ;
emit_stmt       = "emit" IDENTIFIER "(" [ arg_list ] ")" [ "to" expression ] NEWLINE ;
spawn_arg_list  = spawn_arg { "," spawn_arg } ;
spawn_arg       = IDENTIFIER "=" expression ;
destroy_stmt    = "destroy" [ expression ] NEWLINE ;
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

**`let` / `var`** declare local variables scoped to the current handler body:
```cactus
let speed = math.length(pos.velocity)    # immutable local
var count: int = 0                        # mutable local with explicit type
count += 1
```

**`spawn`** creates a new entity from a template and returns its `entity_id`. When used as a statement, the return value is discarded:
```cactus
spawn WalkerEnemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0)  # discard id
let enemy = spawn WalkerEnemy(pos = vec2(400.0, 568.0))             # capture id
```

**`destroy`** removes an entity. With no argument, removes the current entity. With an `entity_id` expression, removes that specific entity:
```cactus
if Health.health <= 0:
    destroy                          # remove current entity

destroy PlayerComposition.gun        # remove a stored child entity
```

**`load`** transitions to a new module-as-scene (deferred to end of frame):
```cactus
load levels.level2
```

**`emit`** dispatches an event. Without `to`, it is broadcast. With `to entity_id`, it is targeted to one entity:
```cactus
emit PlayerJumped()                          # broadcast
emit Damage(amount = 10) to EnemyAI.target  # targeted
```

**`enable`** / **`disable`** toggle a trait's active state on the current entity:
```cactus
enable Frozen
disable EnemyAI
```

### 3.16 Asset Declarations

Asset declarations bind a compile-time identifier to a typed external resource file path. They are only valid at module top level. The declared name resolves to a built-in opaque ID type (see §5.1).

```ebnf
asset_decl  = [ "pub" ] "asset" IDENTIFIER ":" asset_type "=" STRING_LITERAL NEWLINE ;
asset_type  = "mesh" | "texture" | "sound" | "music" | "font" | "material" ;
```

Each asset type maps to a built-in opaque handle:

| Declaration type | Resolved type |
|-----------------|---------------|
| `mesh`          | `mesh_id`     |
| `texture`       | `texture_id`  |
| `sound`         | `sound_id`    |
| `music`         | `music_id`    |
| `font`          | `font_id`     |
| `material`      | `material_id` |

String literals in `asset` declarations are the **only** exception to the const-string rule (see §6.1).

```cactus
asset PlayerMesh:  mesh    = "models/player.glb"
asset ShotSound:   sound   = "audio/shot.wav"
asset MainTheme:   music   = "audio/theme.ogg"
asset HudFont:     font    = "fonts/hud.ttf"

pub asset SharedTex: texture = "sprites/shared.png"   # visible to importers

trait MeshRenderer:
    let mesh: mesh_id
    var visible: bool = true

unit Player:
    apply:
        Transform
        MeshRenderer
    config:
        mesh = PlayerMesh
```

### 3.17 Input Declarations

Input declarations bind a compile-time identifier to a logical input action of kind `button` or `axis`. They are only valid at module top level. Properties reference enum constants from `std.input`.

```ebnf
input_decl = [ "pub" ] "input" IDENTIFIER ":" ( "button" | "axis" ) NEWLINE INDENT
             { input_prop }
             DEDENT ;
input_prop = IDENTIFIER "=" expression NEWLINE ;
```

**Valid property keys:**

| Kind | Valid keys |
|------|-----------|
| `button` | `key`, `mouse`, `gamepad` |
| `axis` | `negative`, `positive`, `gamepad`, `mouse_delta_x`, `mouse_delta_y`, `invert` |

Property values reference `std.input` enum constants: `Key.A`, `Key.Space`, `MouseButton.Left`, `GamepadButton.South`, `GamepadAxis.LeftX`, etc.

A `button` input declaration name resolves to type `InputButton`. An `axis` declaration name resolves to type `InputAxis`.

Query functions defined in `std.input` (must `use std.input`):

```cactus
pub func pressed(b: InputButton) -> bool    # true on first press frame
pub func down(b: InputButton) -> bool       # true while held
pub func released(b: InputButton) -> bool   # true on first release frame
pub func axis(a: InputAxis) -> float        # -1.0 to 1.0
pub func axis2(x: InputAxis, y: InputAxis) -> vec2
```

```cactus
use std.input

input MoveX: axis
    negative = Key.A
    positive = Key.D
    gamepad  = GamepadAxis.LeftX

input Jump: button
    key     = Key.Space
    gamepad = GamepadButton.South

trait MoveIntent:
    var axis: vec2 = vec2(0.0, 0.0)
    var jump_pressed: bool = false

system ReadPlayerInput:
    filter:
        MoveIntent as move
        PlayerTag

    on input():
        move.axis         = input.axis2(MoveX, MoveY)
        move.jump_pressed = input.pressed(Jump)
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
| `entity_id` | Opaque handle to a live entity. Always valid when held in an active trait field — there is no null sentinel. The "no relationship" state is modeled via trait presence/absence (enable/disable). Stale handles (referencing destroyed entities) are runtime-invalid; the EnTT backend's generation IDs detect them transparently. | 8 bytes |
| `mesh_id` | Opaque handle to a loaded mesh resource. Only obtained from an `asset ... : mesh` declaration. | opaque |
| `texture_id` | Opaque handle to a loaded texture resource. Only obtained from an `asset ... : texture` declaration. | opaque |
| `sound_id` | Opaque handle to a loaded sound (short, one-shot) resource. Only obtained from an `asset ... : sound` declaration. | opaque |
| `music_id` | Opaque handle to a loaded music (streaming) resource. Only obtained from an `asset ... : music` declaration. | opaque |
| `font_id` | Opaque handle to a loaded font resource. Only obtained from an `asset ... : font` declaration. | opaque |
| `material_id` | Opaque handle to a loaded material resource. Only obtained from an `asset ... : material` declaration. | opaque |
| `InputButton` | Named button action handle. Only obtained from an `input ... : button` declaration. | opaque |
| `InputAxis` | Named axis action handle. Only obtained from an `input ... : axis` declaration. | opaque |

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
- `let` / `var` local declarations infer type from the initializer expression when no explicit type annotation is given
- `spawn_expr` always has type `entity_id`

## 6. Semantic Constraints

### 6.1 Const-String Rule

String literals (`"..."`) are **forbidden** outside `const` blocks. All string references in logic must go through `const` identifiers. This ensures compile-time string interning and prevents accidental allocations in hot loops.

**Exception:** String literals are permitted as the resource path value inside `asset` declarations (see §3.16). They are not permitted in any other position outside `const` blocks.

```
# VALID
const:
    SHOP_TITLE = "Cactus Shop"

asset PlayerMesh: mesh = "models/player.glb"   # VALID — asset path exception

# INVALID — string literal in system body
system UI:
    on tick(dt: float):
        set_title("Bad")  # ERROR: string literal outside const block or asset declaration
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

```
# VALID
trait Player:
    persist sync var health: int = 100
    persist var score: int = 0
    let max_health: int = 100

# INVALID
trait Bad:
    persist let name: string  # ERROR: persist on let field
    sync let id: int          # ERROR: sync on let field
```

### 6.6 System Filter Validation

- All trait names in a system's `filter:` clause must reference declared traits
- All trait names in a system's `exclude:` clause must reference declared traits

### 6.7 Event Validation

- `emit` statements must reference declared events
- User event handler parameter lists must be empty; access event fields via the implicit `event` object
- The `to` expression in a targeted `emit ... to expr` must evaluate to type `entity_id`

### 6.8 Field Access Rules in System Handlers

Inside a system event handler body, identifier resolution follows this order:

1. **Handler parameters** — lifecycle handlers declare explicit parameters (e.g., `dt` in `on tick(dt: float):`). These are always in scope.

2. **`event` object** — in user-defined event handlers only, `event.fieldname` accesses the event payload. The `event` identifier is reserved in this context. It is read-only; `event.x = ...` is an error.

3. **Local variables** — declared with `let` (immutable) or `var` (mutable) in the current handler body. Re-declaring an existing local in the same scope is an error.

4. **Trait fields via alias** — accessed as `alias.field` or `TraitName.field`. The alias is either:
   - The explicit `as` name declared in the `filter:` entry, OR
   - The trait name itself (implicit alias when no `as` is given)

**Bare unqualified identifiers that resolve to trait fields are forbidden.** Always use `alias.field` or `TraitName.field`.

```cactus
system Movement:
    filter:
        Position as pos      # explicit alias
        Velocity             # no alias; use Velocity.dx, Velocity.dy

    on tick(dt: float):
        let step = dt * 60.0             # local binding
        pos.x += Velocity.dx * step      # alias.field and TraitName.field — both OK
        pos.y += Velocity.dy * step
        # dx += 1.0  # ERROR: bare 'dx' is not allowed
```

```cactus
system DamageSystem:
    filter:
        Health as h

    on PlayerDamaged():         # user event handler
        h.health -= event.amount   # event.field is valid here
        if h.health <= 0:
            destroy
```

### 6.9 Event Dispatch Semantics

**Dispatch timing:** Events are dispatched same-frame (within the frame they are emitted), up to a configurable cascade depth.

**Max cascade depth:** Each frame, events are processed in rounds. Round 0 is all tick-phase handlers. Events emitted in round N are processed in round N+1. If `N+1 > max_cascade_depth`, those events are deferred to the next frame. The default `max_cascade_depth` is **1** (configurable per-project).

```
Frame execution with max_cascade_depth = 1:

  Round 0:  on input(), on fixed_tick(), on tick(), on late_tick()
            → emits go into depth-1 queue
  Round 1:  process depth-1 events
            → emits at this level deferred to next frame
```

**Handler ordering:**
- Within a module: systems execute in declaration order
- Across modules: systems execute in import order (of the root module)
- Multiple systems handling the same event: each runs in system declaration order

**Queue semantics:** Multiple instances of the same event type are queued FIFO and each processed in turn.

**Targeted events:** `emit Event(...) to entity_id` delivers the event only to the specific entity. The system's `filter:` clause still applies — the entity must match the filter for the handler to fire.

**No event object in lifecycle handlers:** The `event` implicit object is only available in user-defined event handlers, not in `on tick()`, `on input()`, etc.

## 7. Execution Model

### 7.1 ECS Architecture

- **Traits** define data schemas (components in ECS terminology)
- **Units** compose traits into entity archetypes
- **Systems** contain logic that operates on entities matching trait filters
- **Events** enable decoupled communication between systems

### 7.2 Frame Execution Model

Each rendered frame executes in four ordered phases. Systems participate in a phase by declaring the corresponding handler. A system may declare handlers for multiple phases.

```
┌─────────────────────────────────────────────────────────────────┐
│  on input()                                                     │
│    Input device snapshot is fresh. Intent-writing systems run.  │
│    No dt parameter.                                             │
│  → Event phase (cascade up to max_cascade_depth)               │
├─────────────────────────────────────────────────────────────────┤
│  on fixed_tick(dt: float)                                       │
│    Fixed timestep accumulator model. May run 0, 1, or N times  │
│    per rendered frame depending on elapsed time vs step size.   │
│    Use for: physics, collision, deterministic simulation.        │
│  → Event phase (cascade up to max_cascade_depth, per step)     │
├─────────────────────────────────────────────────────────────────┤
│  on tick(dt: float)                                             │
│    Runs once per rendered frame. dt varies with frame time.     │
│    Use for: AI decisions, animation, timers, general gameplay.  │
│  → Event phase (cascade up to max_cascade_depth)               │
├─────────────────────────────────────────────────────────────────┤
│  on late_tick(dt: float)                                        │
│    Runs after tick. Player/entity positions are settled.        │
│    Use for: camera follow, dependent transforms, trail updates. │
│  → Events emitted here deferred to next frame                  │
├─────────────────────────────────────────────────────────────────┤
│  RENDER  (backend — not user code)                              │
└─────────────────────────────────────────────────────────────────┘
```

**fixed_tick accumulator:**
```
accumulator += frame_delta_time
while accumulator >= FIXED_STEP:
    run on fixed_tick(FIXED_STEP) for all systems
    process fixed_tick event phase
    accumulator -= FIXED_STEP
```

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

The optional `module` declaration, if present, must match the filesystem-derived name. Files in different folders with the same filename are distinct modules.

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

All `pub` symbols from the imported module become available.

### 8.4 Qualified Access

Imported symbols are accessed through their module name or alias. In system `filter:` clauses, use block form with optional `as` aliases:

```cactus
use player
use phys.body as b

system Movement:
    filter:
        player.Position as pos
        b.RigidBody as rb
    on tick(dt: float):
        pos.x += rb.velocity_x * dt
```

### 8.5 Field Access — Always Qualified

Inside system handler bodies, trait fields are **always** accessed via `alias.field` or `TraitName.field`. Unqualified field access is not allowed (see §6.8).

When you import a module and use its traits in a filter clause, declare an alias:

```cactus
use player

system Movement:
    filter:
        player.Position as pos    # alias shortens qualified access
        Velocity                  # no alias; use Velocity.dx etc.

    on tick(dt: float):
        pos.x += Velocity.dx * dt
        pos.y += Velocity.dy * dt
```

### 8.6 Filter Clause Aliases

System `filter:` clauses use block form. Aliases declared with `as` shorten field access. When omitted, the trait name (or qualified name's last segment) is the implicit access path:

```cactus
system Render:
    filter:
        phys.Body as b
        render.Sprite as s
    on tick(dt: float):
        draw(b.x, b.y, s.width, s.height)
```

```cactus
system Simple:
    filter:
        Position as pos
        Velocity as vel
    on tick(dt: float):
        pos.x += vel.dx * dt
```

### 8.7 Trait Field Access via Alias

With the mandatory alias.field model (§6.8), field access is always explicit through the alias:

```cactus
# Both Body and Sprite may have 'x' — but with aliases, always clear
system Render:
    filter:
        Body as b
        Sprite as s
    on tick(dt: float):
        draw(b.x, b.y, s.x, s.y)     # explicit via alias — no ambiguity

# Position and Velocity have distinct fields — still use alias.field
system Move:
    filter:
        Position as pos
        Velocity as vel
    on tick(dt: float):
        pos.x += vel.dx * dt
        pos.y += vel.dy * dt
```

If two modules export symbols with the same name, use qualified access or aliases in `use` declarations:

```cactus
use module_a as ma
use module_b as mb

system Example:
    filter:
        ma.Config as cfg_a
        mb.Config as cfg_b
    on tick(dt: float):
        cfg_a.value = cfg_b.value
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

`template` is symmetric with `unit` but defines a **multi-instance blueprint**. Instances are created at runtime with `spawn`, which is an expression returning `entity_id`:

| Declaration | Instantiation |
|-------------|---------------|
| `event Foo:` | `emit Foo(...)` |
| `template Foo:` | `let id = spawn Foo(...)` |
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
let enemy = spawn Enemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0)
emit AssignZone(min_x = 300.0, max_x = 500.0) to enemy
```

**`spawn` field rules:**
- Any field from the template's applied traits can be overridden
- Fields not provided use the template's `config:` default
- Fields with no default and not provided at spawn → compile error
- `spawn` always returns `entity_id`

**`destroy` removes an entity:**
```cactus
system DeathSystem:
    filter:
        Health as h

    on tick(dt: float):
        if h.health <= 0:
            destroy
```

With an `entity_id` expression, removes a specific entity:
```cactus
trait PlayerComposition:
    var gun: entity_id
    var camera: entity_id

unit Player:
    apply:
        Transform
        PlayerTag
        PlayerComposition

    on spawn():
        PlayerComposition.gun    = spawn Weapon()
        PlayerComposition.camera = spawn CameraRig()

    on destroy():
        destroy PlayerComposition.gun
        destroy PlayerComposition.camera
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

**Modeling absent relationships without null:**
Rather than storing a `entity_id` field that might be "null," use trait presence:

```cactus
# Instead of:  var target: entity_id = 0  (no null!)
# Do this:
trait Targeting:
    var target: entity_id     # always valid when trait is active

# enable Targeting and set target when the enemy acquires one
# disable Targeting when it loses its target
```

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
use std.core
use levels.level1
use levels.level2

system GameManager:
    filter:
        GameState as gs

    on LevelComplete():
        load levels.level2

    on PlayerDied():
        if gs.lives <= 0:
            load ui.game_over
        else:
            load levels.level1

# In levels/level1.cactus:
system LevelSetup:
    filter:
        LevelState as ls

    on load():
        let e1 = spawn Enemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0, patrol_max_x = 550.0)
        let e2 = spawn Enemy(pos = vec2(800.0, 568.0), patrol_min_x = 700.0, patrol_max_x = 1000.0)
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
        Persistent
        Position
        Health
    config:
        health = 100
```

Without `use std.core`, no automatic cleanup occurs on `load` — the developer is responsible for custom teardown.
