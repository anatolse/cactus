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
│ modules, traits, entities, templates, systems, │
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
- `entity`, `template`
- `system`, `event`, `func`, `extern func`
- `asset`, `input`
- handlers: `on input:`, `on fixed_tick:`, `on tick:`, `on late_tick:`, `on spawn:`, `on destroy:`, `on load:`, `on unload:`
- statements: `let`, `var`, assignment, `if`, bounded `for ... in ...:`, `emit`, `spawn`, `destroy`, `load`, `add`, `remove`, `project`, `return`

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
module  use     const   struct  enum    trait   entity  template
system  event   func    extern  asset   input
let     var     persist sync    pub
on      emit    if      else    match   return
filter  exclude order   by      after   as
spawn   destroy load    add     remove  project for     in
to      from    self
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
                | enum_decl | trait_decl | entity_decl | template_decl
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

### 3.7 Entities and Templates

The four load-time and runtime constructs and how they differ:

| Construct | Time | Purpose |
|---|---|---|
| `entity Name:` | module/scene load | declare one pre-existing entity directly |
| `entity Name from Template:` | module/scene load | declare one pre-existing entity from a template plus overrides |
| `template Name:` | declaration-time | declare a reusable entity blueprint |
| `spawn Template:` | runtime handler execution | dynamically create an entity from a template |

`entity` declares an entity archetype that is instantiated automatically for the owning module/scene. When the optional `from TemplateName` clause is present, the entity starts from the referenced template's flattened archetype and applies the body's nested trait override entries field-by-field. `template` declares a reusable blueprint that is instantiated by `spawn` (at runtime) or by `entity … from Template:` (at load time).

Both `entity` and `template` use archetype bodies. An archetype body can contain nested trait entries and body-level `use TemplateName` entries. Body-level `use` composes another template into the current archetype at compile time; it is distinct from top-level module `use` and does not create an entity.

Legacy `unit` is no longer valid; use `entity` instead.

```ebnf
entity_decl     = [ "pub" ] "entity" IDENTIFIER [ "from" dotted_name ] ":" NEWLINE INDENT
                  { archetype_entry }
                  DEDENT ;

template_decl   = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                  { archetype_entry }
                  DEDENT ;

archetype_entry = template_use_entry | archetype_trait_entry ;
template_use_entry = "use" dotted_name NEWLINE ;

archetype_trait_entry = IDENTIFIER NEWLINE
                      | IDENTIFIER ":" NEWLINE INDENT
                        { field_assignment }
                        DEDENT ;

field_assignment = IDENTIFIER "=" expression NEWLINE ;
```

```cactus
pub entity Player:
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

template EnemyBase:
    Health:
        health = 3

template WalkerEnemy:
    use EnemyBase
    Position:
        velocity = vec2(2.0, 0.0)

# Inline entity: body-level use composes the template at compile time
entity FirstWalker:
    use WalkerEnemy
    Position:
        pos = vec2(400.0, 568.0)

# Template-backed entity: `from` clause provides the base archetype;
# the body contains per-entity override fields only
entity SecondWalker from WalkerEnemy:
    Position:
        pos = vec2(800.0, 568.0)
```

`WalkerEnemy` is a composed blueprint: it receives `EnemyBase`'s trait initializers before applying its own `Position` block. `FirstWalker` uses a body-level `use` (compile-time composition). `SecondWalker` uses the `from` clause (template-backed entity): it starts from `WalkerEnemy`'s flattened archetype and overrides only `Position.pos`. Neither performs a runtime spawn.

Template-backed entities (`entity Name from Template:`) are the declarative load-time counterpart to runtime `spawn Template:`. Use `entity … from …` for pre-placed authored scene content; use `spawn` for entities created dynamically during gameplay.

Deferred grouped syntax (`entities from Template:` with multiple named instances in one block) is not part of this version of the language.

#### Hierarchical children (`children:` blocks)

Archetype bodies may contain a contextual `children:` block that declares a tree of related entities. `children` is recognized only inside archetype bodies (an identifier named `children` directly followed by `:`); neither `child` nor `children` is a reserved keyword elsewhere.

```ebnf
archetype_entry = template_use_entry | archetype_trait_entry | children_block ;

children_block  = "children" ":" NEWLINE INDENT
                  { child_decl }
                  DEDENT ;

child_decl      = "entity" IDENTIFIER [ "from" dotted_name ] ":" NEWLINE INDENT
                  { archetype_entry }        (* overrides when `from` is present *)
                  DEDENT ;

(* In template-backed entity bodies and spawn bodies, `children:` entries are
   overrides addressing existing roles instead of declarations: *)
child_override  = IDENTIFIER ":" NEWLINE INDENT
                  { archetype_trait_entry | children_block(child_override) }
                  DEDENT ;
```

```cactus
template PlayerRig:
    LocalTransform
    WorldTransform

    children:
        entity WeaponSocket:
            LocalTransform:
                position = vec3(0.4, 1.0, 0.0)
            WorldTransform

            children:
                entity Sword from SwordTemplate:
                    LocalTransform
```

Creation semantics (all creation paths — `spawn`, `entity … from …`, and the editor template palette):

- Creating a hierarchical archetype creates one entity per node and returns/exposes the **root** entity. Descendants are implementation-owned; child role names do not introduce global entity declarations or `entity_id` constants.
- Every non-root node receives a generated `Parent` relation whose `parent` field references the entity created for its **immediate** containing node (grandchildren point at their parent, not the root).
- Creation order is deterministic parent-first preorder: the root, then each child in source order, each child's descendants before the next sibling.
- Hierarchical archetypes are pure syntactic sugar for the equivalent hand-written flat archetypes plus `Parent` traits plus sequential creation. Lifecycle events fire exactly as they would for that hand-written sequence; no whole-tree deferral is introduced.

Rules:

- Child role names are **sibling-scoped**: duplicates within one `children:` block are rejected; the same role may appear under different parents.
- A manual `Parent` trait entry inside a child declaration is rejected — the `children:` nesting itself assigns the parent relation.
- A child declared `from SomeTemplate` splices that template's fully flattened tree (traits **and** descendants) at that node, then applies the child body as overrides. Roles inherited this way are override-addressable through that child.
- Body-level `use OtherTemplate` merges the used template's traits and child declarations into the current node, merging same-role children field-by-field.
- Template dependency cycles through child `from` references (direct or indirect) are rejected, like `use` cycles.
- Hierarchical syntax requires the standard `Parent` trait (from `std.core`) to be resolvable; the compiler reports an error otherwise.

Template-backed entities and spawn sites override nested children by role, mirroring the declaration structure. Unknown roles, traits not present on the child, unknown fields, and unsatisfied required fields are semantic errors:

```cactus
entity Player1 from PlayerRig:
    LocalTransform:
        position = vec3(0.0, 0.0, 0.0)

    children:
        WeaponSocket:
            LocalTransform:
                position = vec3(0.5, 1.1, 0.0)

            children:
                Sword:
                    Renderer:
                        material = BlueSwordMaterial
```

The four composition/creation constructs at a glance:

| Construct | What it does | When |
|---|---|---|
| `use Template` (body-level) | merges another template's traits and children into **this node** — no extra entity | compile time |
| `children:` | declares **separate child entities** created with this archetype, wired via `Parent` | creation time |
| `entity Name from Template:` | one load-time instance of a template (whole tree if hierarchical) plus overrides | module/scene load |
| `spawn Template:` | one runtime instance of a template (whole tree if hierarchical) plus overrides; evaluates to the root | handler execution |

Hierarchy syntax creates parent-child **relations only**. It does not by itself imply transform propagation, rendering, or physics attachment: a child follows its parent's transform only when the child carries the transform traits (`LocalTransform`/`WorldTransform`) required by the active transform propagation system. Destroying a root uses the existing `Parent`-based recursive destroy, so generated descendants are destroyed with it on backends that support the cascade.

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

`spawn TemplateName:` is runtime entity creation. It creates an `entity_id` from the named template's already-composed archetype, then applies the spawn body's nested trait override blocks. Unlike body-level `use TemplateName`, `spawn` can run inside handlers, creates a new entity, and participates in `on spawn` lifecycle delivery.

### 3.16 Statements

```ebnf
statement       = let_decl | var_decl | var_assign | emit_stmt | destroy_stmt
                | load_stmt | add_stmt | remove_stmt | return_stmt
                 | project_stmt | foreach_stmt | expr_stmt | if_stmt | trait_match_stmt ;

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

project_stmt    = "project" IDENTIFIER [ "to" expression ] NEWLINE
                | "project" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
                  { field_assignment }
                  DEDENT ;

foreach_stmt    = "for" IDENTIFIER "in" expression ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;

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

project DamageFlash:
    color = #FF3333

remove Frozen
destroy bullet
load levels.level2
```

Bounded foreach is allowed only inside system event handlers. The iterable expression is evaluated once before the loop and must have type `list[T]`; the loop variable is a read-only binding scoped to the loop body. Cactus still does not support `while`, numeric/indexed `for`, `break`, or `continue`.

`project` mirrors `add` field-initialization syntax but writes to a frame-local projected trait overlay instead of durable ECS component storage. If no `to` target is provided, the target is `self`. Projected traits are coalesced by `(entity, trait)`, visible to later `filter:` / `exclude:` matching during the same rendered frame, and cleared at the frame boundary after render processing. Use:

- `emit` for occurrence-oriented messages, especially when multiple occurrences matter;
- `add` / `remove` for durable entity state;
- `project` for current-frame facts such as grounded/contact facts, interaction availability, tint overrides, damage flashes, outlines, or other render/VFX hints.

Traits with `persist` or `sync` fields cannot be projected because those modifiers describe durable storage behavior.

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
- **entities** define pre-existing load-time entity instances
- **templates** define spawnable blueprints
- **systems** define logic over filtered entities
- **events** define typed gameplay messages

Template composition is static blueprint reuse: a body-level `use TemplateName` inside an `entity` or `template` is resolved and flattened before runtime. Runtime `spawn TemplateName:` is separate; it creates an entity from the flattened template and applies spawn-site overrides.

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
2. instantiate new scene entities
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

- nested trait blocks in `entity`, `template`, and `spawn`
- `emit EventName:` with payload block syntax
- `on tick:` / `tick.dt`
- `add` / `remove`

### 8.3 Example Hygiene

Maintained examples should avoid placeholder-only syntax and stale migration comments. If an example relies on a backend helper, it should present that helper as a backend/runtime concern rather than as an unfinished core-language feature.