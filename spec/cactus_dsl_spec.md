# Cactus DSL Language Specification

**Version:** 0.4.0  
**Status:** Draft

## 1. Overview

Cactus DSL is a declarative, data-oriented gameplay language that compiles to an engine backend. Its primary purpose is to express **gameplay**: entities, state, reactions, spawning, scene flow, and action-game logic.

The language is intentionally centered on a **gameplay-core profile** that is sufficient for authoring things like:

- platformers: movement, gravity, jumping, collectibles, enemies, camera follow
- shooters: fire input, projectile spawning, hit/damage flow, cleanup, enemy defeat

Cactus is **not** specified as a general-purpose engine scripting language. Rendering, audio playback plumbing, physics integration, UI rendering, and similar engine-facing concerns belong to stdlib/backend layers unless explicitly elevated into the language surface by a separate accepted change.

### 1.1 Layered Language Story

The language should be understood in layers:

```text
┌──────────────────────────────────────────────┐
│ Gameplay core                               │
│ modules, traits, units, templates, systems, │
│ events, inputs, assets, spawn, destroy,     │
│ add/remove, scene flow                      │
└──────────────────────────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────┐
│ Stdlib / backend-facing surface             │
│ std.input, rendering, camera, physics,      │
│ audio, extern funcs, extern systems         │
└──────────────────────────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────┐
│ Deferred / unsupported ideas                │
│ not part of the current normative profile   │
└──────────────────────────────────────────────┘
```

### 1.2 Current Core Commitments

The current gameplay-core profile includes:

- `module`, `use`, `const`
- `struct`, `enum`, `trait`
- `unit`, `template`
- `system`, `event`, `func`, `extern func`
- `asset`, `input`
- handlers: `on input:`, `on fixed_tick:`, `on tick:`, `on late_tick:`, `on spawn:`, `on destroy:`, `on load:`, `on unload:`
- statements: `let`, `var`, assignment, `if`, `emit`, `spawn`, `destroy`, `load`, `add`, `remove`, `return`

### 1.3 Deferred / Non-Normative Items

The following are **not part of the current normative gameplay-core profile**:

- `view`
- `interface`
- legacy `apply:` / `config:` archetype syntax
- legacy `enable` / `disable` trait toggling as the documented runtime mutation model

If older notes or examples mention them, treat those references as migration history or future work rather than active grammar.

## 2. Lexical Structure

### 2.1 Character Set

Source files are UTF-8 encoded. Identifiers and keywords are ASCII. Non-ASCII text is allowed inside string literals.

### 2.2 Indentation

Cactus uses significant indentation with spaces only. Tabs are rejected. The lexer emits explicit `INDENT` and `DEDENT` tokens.

```cactus
trait Player:
    var health: int
```

### 2.3 Comments

Single-line comments start with `#` and extend to the end of the line.

### 2.4 Keywords

```text
module  use     const   struct  enum    trait   unit    template
system  event   func    extern  asset   input
let     var     persist sync    pub
on      emit    if      else    match   return
filter  exclude order   by      after   as
spawn   destroy load    add     remove  to      from    self
true    false   and     or      not
fixed_tick late_tick
```

### 2.5 Literals

- integers: `0`, `42`
- floats: `3.14`, `0.5`
- strings: `"Hello"`
- hex colors: `#FF0000`, `#FF000080`
- booleans: `true`, `false`

## 3. Grammar

### 3.1 Program Structure

```ebnf
program         = { declaration } EOF ;
declaration     = module_decl | use_decl | const_block | struct_decl
                | enum_decl | trait_decl | unit_decl | template_decl
                | system_decl | extern_system_decl | event_decl
                | func_decl | extern_func_decl | asset_decl | input_decl ;
```

### 3.2 Module and Imports

```ebnf
module_decl     = "module" dotted_name NEWLINE ;
use_decl        = "use" dotted_name [ "as" IDENTIFIER ] NEWLINE ;
dotted_name     = IDENTIFIER { "." IDENTIFIER } ;
```

```cactus
module enemies.walker
use std.input
use std.math.vec2 as v2
use gameplay.player as player
```

### 3.3 Const Blocks

```ebnf
const_block     = "const" ":" NEWLINE INDENT
                  { const_assign }
                  DEDENT ;
const_assign    = IDENTIFIER "=" const_value NEWLINE ;
const_value     = STRING_LITERAL | INT_LITERAL | FLOAT_LITERAL | HEX_COLOR ;
```

### 3.4 Structs

```ebnf
struct_decl     = "struct" IDENTIFIER ":" NEWLINE INDENT
                  { struct_field }
                  DEDENT ;
```

Structs are value objects used for grouped data.

### 3.5 Enums

```ebnf
enum_decl       = "enum" IDENTIFIER ":" NEWLINE INDENT
                  { enum_variant }
                  DEDENT ;
```

Enums are used for named gameplay states.

### 3.6 Traits

Traits are data-only. They represent ECS-style gameplay state attached to entities.

```ebnf
trait_decl      = [ "pub" ] "trait" IDENTIFIER
                  [ ":" NEWLINE INDENT
                    { field_decl }
                    DEDENT ] ;

field_decl      = field_modifiers ( "let" | "var" ) IDENTIFIER ":" type_ref
                  [ "=" expression ] NEWLINE ;
field_modifiers = { "persist" | "sync" | "pub" } ;
```

Marker traits have no body:

```cactus
trait Frozen
pub trait PlayerTag
```

Data traits carry fields:

```cactus
trait Health:
    let max_health: int = 100
    persist sync var health: int = 100
```

### 3.7 Units and Templates

`unit` declares an entity archetype that is instantiated automatically for the owning module/scene. `template` declares a reusable blueprint that is instantiated later by `spawn`.

Both use nested trait entries.

```ebnf
unit_decl       = [ "pub" ] "unit" IDENTIFIER ":" NEWLINE INDENT
                  { archetype_trait_entry }
                  DEDENT ;

template_decl   = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                  { archetype_trait_entry }
                  DEDENT ;

archetype_trait_entry = IDENTIFIER NEWLINE
                      | IDENTIFIER ":" NEWLINE INDENT
                        { field_assignment }
                        DEDENT ;

field_assignment = IDENTIFIER "=" expression NEWLINE ;
```

```cactus
pub unit Player:
    Position:
        pos = vec2(100.0, 300.0)
        velocity = vec2(0.0, 0.0)
    Health:
        health = 3
    PlayerTag

template Bullet:
    Position:
        velocity = vec2(24.0, 0.0)
    Bullet:
        damage = 1
        lifetime = 1.0
```

### 3.8 Systems

Systems contain gameplay logic over filtered entities.

```ebnf
system_decl     = "system" IDENTIFIER ":" NEWLINE INDENT
                  [ filter_clause ]
                  [ exclude_clause ]
                  [ order_by_clause ]
                  [ after_clause ]
                  { event_handler }
                  DEDENT ;

filter_clause   = "filter" ":" NEWLINE INDENT
                  { filter_entry }
                  DEDENT ;

filter_entry    = dotted_name [ "as" IDENTIFIER ] NEWLINE ;

exclude_clause  = "exclude" ":" NEWLINE INDENT
                  { dotted_name NEWLINE }
                  DEDENT ;

order_by_clause = "order" "by" ":" NEWLINE INDENT
                  { sort_key NEWLINE }
                  DEDENT ;

after_clause    = "after" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
```

```cactus
system PatrolSystem:
    filter:
        Position as pos
        EnemyAI as ai
    exclude:
        Frozen

    on tick:
        pos.pos = pos.pos + vec2(ai.patrol_speed * ai.direction * tick.dt, 0.0)
```

### 3.9 Event Handlers

Handlers are parameter-free in the current profile. Handler-local event data is accessed through the lifecycle/event binding itself or through an explicit alias.

```ebnf
event_handler   = "on" event_name [ "as" IDENTIFIER ] ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;

event_name      = IDENTIFIER | "input" | "fixed_tick" | "tick" | "late_tick"
                | "spawn" | "destroy" | "load" | "unload" ;
```

```cactus
on tick:
    pos.pos = pos.pos + vel.value * tick.dt

on fixed_tick as ft:
    vel.value = vel.value + gravity.value * ft.dt

on PlayerDamaged:
    hp.health = hp.health - PlayerDamaged.amount

on PlayerDamaged as dmg:
    hp.health = hp.health - dmg.amount
```

### 3.10 Extern Systems

`extern system` is an advanced backend-facing declaration. It declares a filtered/ordered pass whose implementation is provided by the backend.

```ebnf
extern_system_decl = "extern" "system" IDENTIFIER ":" NEWLINE INDENT
                     [ filter_clause ]
                     [ exclude_clause ]
                     [ order_by_clause ]
                     [ after_clause ]
                     DEDENT ;
```

### 3.11 Events

Events are typed gameplay messages.

```ebnf
event_decl       = [ "pub" ] "event" IDENTIFIER
                   [ ":" NEWLINE INDENT
                     { event_field_decl }
                     DEDENT ] ;

event_field_decl = IDENTIFIER ":" type_ref NEWLINE ;
```

```cactus
event PlayerDamaged:
    amount: int

pub event spawn
```

### 3.12 Functions

Regular `func` declarations are pure. `extern func` declarations are runtime/backend-provided.

```ebnf
func_decl        = [ "pub" ] "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ]
                   ":" NEWLINE INDENT
                   { statement }
                   DEDENT ;

extern_func_decl = [ "pub" ] "extern" "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ] NEWLINE ;
```

### 3.13 Assets and Inputs

```ebnf
asset_decl  = [ "pub" ] "asset" IDENTIFIER ":" asset_type "=" STRING_LITERAL NEWLINE ;
asset_type  = "mesh" | "texture" | "sound" | "music" | "font" | "material" ;

input_decl  = [ "pub" ] "input" IDENTIFIER ":" ( "button" | "axis" ) NEWLINE INDENT
              { input_prop }
              DEDENT ;
input_prop  = IDENTIFIER "=" expression NEWLINE ;
```

```cactus
asset PlayerSprite: texture = "sprites/player.png"

input MoveX: axis
    negative = Key.A
    positive = Key.D

input Fire: button
    mouse = MouseButton.Left
```

### 3.14 Types

```ebnf
type_ref        = IDENTIFIER [ "[" type_ref "]" ] ;
```

Built-in types include:

- `int`, `float`, `bool`, `string`
- `vec2`, `vec3`, `quat`, `color`
- `entity_id`
- asset handles: `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`
- input handles: `InputButton`, `InputAxis`
- `list[T]`

### 3.15 Expressions

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
primary_expr    = literal | IDENTIFIER | "self" | "(" expression ")"
                | lambda_expr | match_expr | if_expr | list_literal | spawn_expr ;
```

`spawn` is both an expression and a statement surface:

```ebnf
spawn_expr      = "spawn" IDENTIFIER ":" NEWLINE INDENT
                  { archetype_trait_entry }
                  DEDENT ;
```

### 3.16 Statements

```ebnf
statement       = let_decl | var_decl | var_assign | emit_stmt | destroy_stmt
                | load_stmt | add_stmt | remove_stmt | return_stmt
                | expr_stmt | if_stmt | trait_match_stmt ;

let_decl        = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_decl        = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_assign      = IDENTIFIER ( "=" | "+=" | "-=" ) expression NEWLINE ;

emit_stmt       = "emit" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
                  { field_assignment }
                  DEDENT ;

destroy_stmt    = "destroy" [ expression ] NEWLINE ;
load_stmt       = "load" dotted_name NEWLINE ;

add_stmt        = "add" IDENTIFIER [ "to" expression ] NEWLINE
                | "add" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
                  { field_assignment }
                  DEDENT ;

remove_stmt     = "remove" IDENTIFIER [ "from" expression ] NEWLINE ;

return_stmt     = "return" [ expression ] NEWLINE ;
expr_stmt       = expression NEWLINE ;
```

```cactus
let speed = 5.0
var timer: float = 0.0

emit PlayerJumped:
    position = p.pos
    jumps_remaining = phys.jumps_remaining

let bullet = spawn PlayerBullet:
    Position:
        pos = p.pos
        velocity = vec2(24.0, 0.0)

add Invincible:
    duration = 1.5

remove Frozen
destroy bullet
load levels.level2
```

### 3.17 Trait Match Statements

Trait-pattern matching is an advanced gameplay statement used on `entity_id` values.

```cactus
match collision.other:
    PlayerTag =>
        emit PlayerDamaged:
            amount = 1
    Collectible as col =>
        let points = col.point_value
    _ =>
        let ignored = 0
```

## 4. Semantic Model

### 4.1 Core Data Model

- **traits** define entity data
- **units** define pre-existing entities
- **templates** define spawnable blueprints
- **systems** define logic over filtered entities
- **events** define typed gameplay messages

### 4.2 Field Access and Handler Bindings

Trait fields in systems are accessed through:

- `alias.field` if a filter alias is declared
- `TraitName.field` if no alias is declared

Lifecycle and event payloads are accessed through:

- `input`, `fixed_tick`, `tick`, `late_tick`, `spawn`, `destroy`, `load`, `unload`
- or a handler alias declared with `on ... as alias:`
- or the user event name itself / its alias

```cactus
system Move:
    filter:
        Position as pos
        Velocity as vel

    on tick:
        pos.pos = pos.pos + vel.value * tick.dt

system Damage:
    filter:
        Health as hp

    on PlayerDamaged as dmg:
        hp.health = hp.health - dmg.amount
```

Bare unqualified trait-field access is not part of the current profile.

### 4.3 `entity_id` Semantics

`entity_id` is an opaque handle. There is no null sentinel in the language surface. Operations using `entity_id` are total: stale handles produce safe no-ops or no-match behavior rather than forcing author-side null checks.

### 4.4 Runtime Trait Mutation

The canonical documented runtime trait mutation model is:

- `add TraitName`
- `add TraitName:` with block initialization
- `remove TraitName`

This is the preferred model for temporary gameplay states such as freeze, stun, invincibility, targeting, and similar state transitions.

### 4.5 Purity and Recursion

- user `func` declarations are pure
- user `func` declarations cannot recurse
- `extern func` declarations are runtime/backend-provided and are exempt from purity enforcement

### 4.6 Strings

String literals are only allowed in:

- `const:` blocks
- asset declaration paths

### 4.7 Ordering and Filtering

- `filter:` selects entities
- `exclude:` removes entities from consideration
- `after:` constrains system order within a phase
- `order by:` constrains iteration order for a system pass

## 5. Execution Model

### 5.1 Frame Phases

Each rendered frame executes in this order:

```text
input
fixed_tick (0..N times)
tick
late_tick
render
```

Events cascade between these phases according to the runtime's configured cascade depth.

### 5.2 Scene Loading

`load module.name` transitions to another module-as-scene. Conceptually:

1. unload old scene entities
2. instantiate new scene units
3. fire `on load:` handlers for the new scene

### 5.3 Structural Changes

The gameplay model relies on explicit structural changes:

- `spawn` creates entities from templates
- `destroy` removes entities
- `add` attaches gameplay state traits
- `remove` detaches gameplay state traits

## 6. Gameplay-Core Examples

### 6.1 Platformer Loop

```cactus
input MoveX: axis
input Jump: button

trait MoveIntent:
    var axis_x: float = 0.0
    var jump_pressed: bool = false

system ReadInput:
    filter:
        MoveIntent as move

    on input:
        move.axis_x = input.axis(MoveX)
        move.jump_pressed = input.pressed(Jump)

system JumpSystem:
    filter:
        Position as p
        PlayerPhysics as phys
        MoveIntent as move

    on fixed_tick:
        if move.jump_pressed and phys.jumps_remaining > 0:
            p.velocity = vec2(p.velocity.x, phys.jump_force * -1.0)
            phys.jumps_remaining = phys.jumps_remaining - 1
            emit PlayerJumped:
                position = p.pos
```

### 6.2 Shooter Loop

```cactus
input Fire: button

template Bullet:
    Position:
        velocity = vec2(24.0, 0.0)
    Bullet:
        damage = 1
        lifetime = 1.0

system FireSystem:
    filter:
        Position as p
        Shooter as shooter
        PlayerInput as input_state

    on tick:
        if input_state.fire_pressed and shooter.cooldown <= 0.0:
            let bullet = spawn Bullet:
                Position:
                    pos = p.pos
                    velocity = vec2(24.0, 0.0)
            shooter.cooldown = 0.15
            emit ShotFired:
                origin = p.pos

system BulletHitSystem:
    filter:
        Bullet as bullet

    on tick:
        if bullet.lifetime <= 0.0:
            destroy
```

The shooter loop uses the same core constructs as the platformer loop: inputs, systems, templates, spawning, events, and cleanup.

## 7. Stdlib and Backend Surface

The gameplay core is extended by stdlib modules and backend-provided declarations.

### 7.1 Common Stdlib Responsibilities

- `std.input` for logical input actions
- rendering stdlib for sprites, meshes, billboards, lights, HUD helpers
- physics stdlib for collision and movement helpers
- camera stdlib for 2D/3D camera behavior
- audio stdlib for sound/music playback surfaces

### 7.2 Extern Functions

`extern func` provides engine/runtime functionality such as math helpers, rendering calls, camera setters, collision helpers, and input helpers.

### 7.3 Extern Systems

`extern system` is used when the backend supplies the full implementation of a filtered pass, such as a renderer or a backend-driven transform/physics pass.

These surfaces are active, but they are **not the minimal gameplay-core language story**.

## 8. Deferred and Migration Notes

### 8.1 Deferred Features

The following are deferred from the current profile:

- `view`
- `interface`
- any UI-specific retained-tree syntax without an accepted capability and runtime story

### 8.2 Legacy Syntax to Migrate Away From

The following older forms are not normative in the current profile:

- `apply:` / `config:` archetype syntax
- `enable` / `disable` as the documented runtime trait mutation model
- parenthesized `emit Event(...)` as the main documented event form
- flat `spawn Foo(...)` override syntax as the main documented spawn form
- parameterized lifecycle handler forms such as `on tick(dt: float):`

Prefer:

- nested trait blocks in `unit`, `template`, and `spawn`
- `emit EventName:` with payload block syntax
- `on tick:` / `tick.dt`
- `add` / `remove`

### 8.3 Example Hygiene

Maintained examples should avoid placeholder-only syntax and stale migration comments. If an example relies on a backend helper, it should present that helper as a backend/runtime concern rather than as an unfinished core-language feature.