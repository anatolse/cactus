# Cactus DSL Language Specification

**Version:** 0.6.0
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
│ modules, traits, entities, templates, rules,   │
│ events, inputs, assets, spawn, destroy,     │
│ add/remove, scene flow                      │
└──────────────────────────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────┐
│ Stdlib / backend-facing surface             │
│ std.input, rendering, camera, physics,      │
│ audio, extern funcs, extern rules            │
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
- `rule`, `extern rule`, `event`, `extern event`, `phase`, `func`, `extern func`
- `asset`, `input`
- `rule` selection domains: selectionless, unary `filter:`/`exclude:`/`order by:`, and binary `pairs:` relations
- handlers triggered by declared phases or ordinary events, such as `on input:`, `on fixed_tick:`, `on tick:`, and `on PlayerDamaged:`
- statements: `let`, `var`, assignment, `if`, bounded `for ... in ...:`, `emit` (broadcast or targeted with `to`), `spawn`, `destroy`, `load`, `add`, `remove`, `project`, `return`

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
rule    event   phase   func    extern  asset   input
let     var     persist sync    pub
on      emit    if      else    match   return
filter  exclude order   by      after   as      every  max
reads   writes  emits   commands effects
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
                | rule_decl | extern_rule_decl | event_decl | phase_decl
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
- Hierarchical archetypes are pure syntactic sugar for the equivalent hand-written flat archetypes plus `Parent` traits plus sequential creation. They do not synthesize lifecycle events or whole-tree deferral.

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

### 3.8 Rules

Rules contain gameplay logic over filtered entities. A regular rule has exactly one execution domain: **selectionless** (no `filter:`/`exclude:`/`pairs:`), **unary** (`filter:`/`exclude:`/`order by:`), or **binary pair** (`pairs:`). `pairs:` is mutually exclusive with `filter:`, `exclude:`, and `order by:`.

```ebnf
rule_decl       = "rule" IDENTIFIER ":" NEWLINE INDENT
                  ( unary_domain | pairs_clause )
                  [ after_clause ]
                  [ where_clause ]
                  { event_handler }
                  DEDENT ;

unary_domain    = [ filter_clause ] [ exclude_clause ] [ order_by_clause ] ;

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
rule Patrol:
    filter:
        Position as pos
        EnemyAI as ai
    exclude:
        Frozen

    on tick:
        pos.pos = pos.pos + vec2(ai.patrol_speed * ai.direction * tick.dt, 0.0)
```

#### 3.8.1 Pair Relations

`pairs:` declares a binary iteration domain over two ordered, uniquely named entity bindings, each with its own positive trait requirements. `pairs` is recognized contextually at the rule-clause position (like `children` inside archetype bodies); it is not a reserved keyword elsewhere and does not appear in the global keyword list. `pairs:` is rejected on `extern rule` declarations.

```ebnf
pairs_clause    = "pairs" ":" NEWLINE INDENT
                  pair_binding pair_binding
                  DEDENT ;

pair_binding    = IDENTIFIER ":" NEWLINE INDENT
                  { filter_entry }
                  DEDENT ;
```

Each binding requires at least one positive trait entry; a `pairs:` block always has exactly two bindings.

```cactus
rule DetectContacts:
    pairs:
        body:
            DynamicBody
            Transform
            Collider

        wall:
            Solid
            tf.WorldTransform as transform
            Collider

    on fixed_tick:
        if body != wall and body.Collider.mask == wall.Collider.layer:
            emit Contact to body:
                other = wall
```

**Bindings are entity identifiers and trait namespaces.** Each binding name has type `entity_id` and also namespaces the traits selected for that entity:

- `body` — the binding itself, usable as an `entity_id` (comparison, event target, `to`/`from` argument)
- `body.Collider.mask` — an unaliased local trait, reached as `binding.Trait.field`
- `body.tf.WorldTransform.position` — an imported trait, reached as `binding.module_alias.Trait.field` (the authored `use ... as` qualification is preserved under the binding)
- `wall.transform.position` — a binding-local alias declared with `as` inside that binding's block, reached as `binding.alias.field`

Binding names and their aliases must be unambiguous within every handler scope on that rule.

**The relation is a directed Cartesian product.** For bindings A and B, the handler executes once per pair `(a, b)` where `a` satisfies every trait A requires and `b` satisfies every trait B requires. The product is directed and finite: self-pairs (`a == b`, when one entity satisfies both bindings) and reverse-role tuples are included whenever membership permits, unless excluded by a `where:` clause (§3.8.2) or by an ordinary `if`/`return` in the handler body — both are equally valid authored mechanisms for rejecting tuples, as with `if body != wall:` above or `where: body != wall`.

**Passes snapshot membership, not values.** Before executing any tuple body, the runtime records both bindings' live membership in stable, creation-order-sorted snapshots (a monotonic per-entity creation ordinal, assigned at load time and at spawn commit, defines this order independently of backend storage layout) and lazily iterates their product left-binding-major: for `left = [a, b]` and `right = [x, y]`, tuple order is `(a,x)`, `(a,y)`, `(b,x)`, `(b,y)`. Membership is fixed for the whole pass; component values are read live from storage when each tuple executes. Projected traits and buffered structural commands issued mid-pass cannot add or remove tuples from the pass already in progress — they become visible only in a later pass or at the next activation commit.

**Pair-bound durable trait access is read-only.** A pair handler may read any trait it selected (`body.Collider.mask`, `wall.transform.position`), but direct or indirect mutation — assignment, compound assignment, or a data-bearing trait-match alias obtained from a binding — is rejected during semantic analysis. Selecting a trait does not itself count as a read.

**There is no implicit current entity.** `self` and any statement form that defaults to `self` (bare `destroy`, bare `remove`, `add`/`project` with no `to`) are rejected in pair handlers. Every entity-targeting operation must name a binding explicitly:

```cactus
emit Contact to body:
    other = wall
project GroundContact to body
add PendingDestroy to wall
remove Triggered from body
destroy wall
```

Untargeted `emit` remains valid and is a broadcast occurrence (one per tuple, not privileged to any binding). `spawn` remains valid because it creates a new entity rather than acting on an implicit one.

**One pair handler is one execution-graph node.** `DetectContacts.fixed_tick` is a single node in the handler execution graph regardless of how many tuples it processes at runtime; tuples are invocations inside that node, not graph nodes. The complete tuple pass finishes before the dispatcher advances to another node or drains events the pass emitted. Handler contracts record binding-qualified reads precisely (for diagnostics and future relation-aware scheduling) while still contributing to the same conservative canonical-trait conflict analysis used by unary handlers, so pair and unary/selectionless handlers touching the same traits are still ordered safely.

#### 3.8.2 Where Clause

`where:` declares a pure boolean predicate list that restricts an existing unary (`filter:`) or pair (`pairs:`) domain, independent of any particular execution strategy. `where` is recognized contextually at the rule-clause position (like `pairs`); it is not a reserved keyword elsewhere. `where:` is rejected on rules that declare neither `filter:` nor `pairs:`, and on `extern rule` declarations.

```ebnf
where_clause    = "where" ":" NEWLINE INDENT
                  expression NEWLINE
                  { expression NEWLINE }
                  DEDENT ;
```

At least one predicate line is required. Multiple lines form an unordered logical conjunction: every line must evaluate to `true` for the entity or tuple to remain in the domain.

```cactus
rule DetectBallContact:
    pairs:
        a:
            Ball
            SphereCollider
            tv.WorldTransform
        b:
            Ball
            SphereCollider
            tv.WorldTransform
    where:
        a != b
        collision.spheres_overlap(a.tv.WorldTransform.position, a.SphereCollider.radius, b.tv.WorldTransform.position, b.SphereCollider.radius)

    on fixed_tick:
        # only overlapping, distinct pairs reach the handler body
        ...
```

**Predicates must be pure and type-check as `bool`.** A `where:` predicate may contain literals and constants, filter/pair-binding reads, entity identity comparisons, arithmetic and boolean operators, and calls to functions whose complete call graph is proven pure (the same purity analysis applied to `func` bodies). It must not mutate traits, emit events, spawn or destroy entities, add, remove, or project traits, execute world queries, or call a function whose effects are opaque or unknown. Each predicate expression must have static type `bool`.

**Evaluation order is unspecified.** Because every predicate is pure, the compiler is free to reorder, combine, inline, or otherwise replace the predicate list with an equivalent restriction; no observable short-circuit behavior is guaranteed.

**`where:` evaluates once per pass, against the already-selected domain.** It runs once per entity or tuple, at the start of that entity's or tuple's handler invocation, against the membership `filter:`/`exclude:`/`pairs:` already snapshotted for the pass. It can only shrink that membership — never add to it — and is not re-evaluated mid-pass as a result of mutations, projections, or buffered structural commands from earlier invocations in the same pass. On a pair rule, rejected tuples never begin their handler body invocation, and the tuples that do remain keep the same left-binding-major relative order described in §3.8.1.

`where:` and a leading `if`/`return` in the handler body are equally valid, freely interchangeable ways to reject an entity or tuple: `where: body != wall` and `if body == wall: return` (§3.8.1) admit the same tuples. `where:` exists to make that rejection declarative and analyzable — the reads it touches fold into the handler's contract with the same precision as an equivalent body read.

### 3.9 Event Handlers

Handlers are parameter-free in the current profile. Handler-local phase/event data is accessed through the trigger binding itself or through an explicit alias.

```ebnf
event_handler   = "on" event_name [ "as" IDENTIFIER ] ":" NEWLINE INDENT
                  [ handler_after_clause ]
                  { statement }
                  DEDENT ;

event_name      = dotted_name ;

handler_after_clause = "after" ":" NEWLINE INDENT
                       { dotted_name NEWLINE }
                       DEDENT ;
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

### 3.10 Extern Rules

`extern rule` is an advanced backend-facing declaration. Its implementation is provided by a compiler-owned adapter or by a user library, but every extern rule still declares one or more triggered handlers. Handler contracts are mandatory and shape both scheduling and the generated callback ABI.

```ebnf
extern_rule_decl   = "extern" "rule" IDENTIFIER ":" NEWLINE INDENT
                     [ filter_clause ]
                     [ exclude_clause ]
                     [ order_by_clause ]
                     { extern_handler }
                     DEDENT ;

extern_handler      = "on" event_name ":" NEWLINE INDENT
                      [ handler_after_clause ]
                      { contract_clause }
                      DEDENT ;

contract_clause     = ( "reads" | "writes" | "emits" | "effects" | "projects" ) ":"
                      NEWLINE INDENT { dotted_name NEWLINE } DEDENT
                    | "commands" ":" NEWLINE INDENT
                      { command_capability NEWLINE } DEDENT ;

command_capability  = "spawn" dotted_name
                    | "destroy"
                    | "add" dotted_name
                    | "remove" dotted_name ;
```

```cactus
extern rule NativeMovement:
    filter:
        Position
        Velocity
    on fixed_tick:
        reads:
            Velocity
        writes:
            Position
        effects:
            physics

extern rule InputSource:
    on input:
        writes:
            PlayerInput
        effects:
            input
```

An extern handler is **selectionless** when its owner has neither `filter:` nor `exclude:` and therefore runs once per trigger occurrence. Any filter or exclude clause creates an entity-selection pass. Selection does not itself grant read access: every component access by an extern handler must appear in `reads:` or `writes:`. `writes:` includes read access to the same trait.

`projects:` declares, per trait, that the handler's generated callback capability object exposes a target-safe frame-local projection call for that trait — the same `project` overlay semantics `project_stmt` (§3.16) gives authored Cactus code, but reachable from a native/compiler-owned callback instead. A trait entry cannot appear in both `writes:` and `projects:` on the same handler, and duplicate entries within `projects:` are rejected. This is a generic capability for external producers (e.g. a future pointer/render-adjacent native adapter); Standard UI's own `MeasureUi`/`ArrangeUi` project `DesiredSize`/`ComputedLayout` through ordinary authored `project` statements and do not need it.

### 3.11 Events and Phases

Events are typed gameplay messages.

```ebnf
event_decl       = [ "pub" ] [ "extern" ] "event" IDENTIFIER
                   [ ":" NEWLINE INDENT
                     { event_field_decl }
                     DEDENT ] ;

event_field_decl = IDENTIFIER ":" type_ref NEWLINE ;

phase_decl       = [ "pub" ] "phase" IDENTIFIER ":" NEWLINE INDENT
                   ( from_clause | phase_after_clause )
                   [ every_clause ] [ max_clause ]
                   { phase_field_decl }
                   DEDENT ;

from_clause      = "from" ":" NEWLINE INDENT { dotted_name NEWLINE } DEDENT ;
phase_after_clause = "after" ":" NEWLINE INDENT { dotted_name NEWLINE } DEDENT ;
every_clause     = "every" ":" constant_expression NEWLINE ;
max_clause       = "max" ":" INTEGER_LITERAL NEWLINE ;
phase_field_decl = IDENTIFIER ":" type_ref "=" expression NEWLINE ;
```

```cactus
event PlayerDamaged:
    amount: int

pub extern event frame:
    dt: float

pub phase fixed_tick:
    from:
        frame
    every: 1.0 / 60.0
    max: 8

pub phase render:
    after:
        fixed_tick
    alpha: float = fixed_tick.alpha
```

Ordinary events may be emitted by handlers. External events are injected only by the host/runtime and cannot be authored with `emit`. A phase is a typed activation barrier, not an event. `from:` declares a runtime source lineage; `after:` declares completed upstream phases. A phase must resolve to one unambiguous external-event root lineage.

Non-periodic phase fields are initialized from the current root occurrence or completed upstream phase results. A periodic phase synthesizes `dt` equal to its interval. It also produces `alpha` after its repetition barrier; `alpha` is available to downstream phases, not to the periodic phase's own handlers.

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

`spawn TemplateName:` is runtime entity creation. It creates an `entity_id` from the named template's already-composed archetype, then applies the spawn body's nested trait override blocks. Unlike body-level `use TemplateName`, `spawn` can run inside handlers and creates a new entity at the activation commit boundary.

#### 3.15.1 World and Hierarchy Queries

`std.query` exposes bounded, snapshot-returning world/hierarchy operations as query-call expressions:

```ebnf
query_call_expr = postfix_expr "." IDENTIFIER "[" [ query_filter { "," query_filter } ] "]"
                   "(" [ named_arg { "," named_arg } ] ")"
                 | postfix_expr "." IDENTIFIER "(" [ named_arg { "," named_arg } ] ")" ;
query_filter    = [ "not" ] dotted_name ;
named_arg       = IDENTIFIER "=" expression ;
```

`std.query` declares:

```cactus
pub extern func exists() bool
pub extern func count() int
pub extern func first() entity_id
pub extern func all() list[entity_id]
pub extern func parent(of: entity_id) entity_id
pub extern func children(of: entity_id) list[entity_id]
pub extern func hierarchy_preorder() list[entity_id]
pub extern func hierarchy_postorder() list[entity_id]
```

`exists`/`count`/`first`/`all` take a bracketed trait filter (positive trait names, or `not TraitName` to exclude) and no value arguments other than the filter. `parent`/`children` take a live `of: entity_id`; `children` additionally accepts a bracketed filter. `hierarchy_preorder`/`hierarchy_postorder` take a bracketed filter and no other arguments — they walk the *complete* structural forest (via generated `Parent` edges from `children:` archetypes or runtime `add`), restricted to the filter, rather than one entity's direct children.

```cactus
for item in query.hierarchy_postorder[Node]():
    ...

for child in query.children[Node](of = item):
    ...

let parent = query.parent(of = item)
if query.exists[Health, not Dead]():
    ...
```

Every query call returns an immutable, finite snapshot taken once at the call site — not a live view. `children`/`hierarchy_preorder`/`hierarchy_postorder` order matching entities by stable creation ordinal (siblings and, for preorder/postorder, roots too); a missing, stale, or non-matching structural parent makes a matching node a traversal root instead of erroring. Runtime `Parent` cycles are traversed finitely and each matching entity appears at most once in a hierarchy traversal.

### 3.16 Statements

```ebnf
statement       = let_decl | var_decl | var_assign | emit_stmt | destroy_stmt
                | load_stmt | add_stmt | remove_stmt | return_stmt
                 | project_stmt | foreach_stmt | expr_stmt | if_stmt | trait_match_stmt ;

let_decl        = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_decl        = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_assign      = IDENTIFIER ( "=" | "+=" | "-=" ) expression NEWLINE ;

emit_stmt       = "emit" IDENTIFIER [ "to" expression ] NEWLINE
                | "emit" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
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

emit Ping
emit Ping to self

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

`emit` (like `add` and `project`) may omit the `:` payload block entirely when the event has no fields to set (e.g. a zero-field `pub event StartBump`) or when every field should take its default; `to expression` is still allowed without a block for a targeted zero-field emit.

Bounded foreach is allowed only inside rule event handlers. The iterable expression is evaluated once before the loop and must have type `list[T]`; the loop variable is a read-only binding scoped to the loop body. Cactus still does not support `while`, numeric/indexed `for`, `break`, or `continue`.

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
- **rules** define behavior over filtered entities
- **events** define typed gameplay messages

Template composition is static blueprint reuse: a body-level `use TemplateName` inside an `entity` or `template` is resolved and flattened before runtime. Runtime `spawn TemplateName:` is separate; it creates an entity from the flattened template and applies spawn-site overrides.

### 4.2 Field Access and Handler Bindings

Trait fields in rules are accessed through:

- `alias.field` if a filter alias is declared
- `TraitName.field` if no alias is declared

Phase and event payloads are accessed through:

- the declared trigger name, such as `input`, `fixed_tick`, `tick`, or `PlayerDamaged`
- or a handler alias declared with `on ... as alias:`

```cactus
rule Move:
    filter:
        Position as pos
        Velocity as vel

    on tick:
        pos.pos = pos.pos + vel.value * tick.dt

rule Damage:
    filter:
        Health as hp

    on PlayerDamaged as dmg:
        hp.health = hp.health - dmg.amount
```

Bare (unqualified) trait-field access is accepted when it resolves to exactly one selected trait; `alias.field`/`TraitName.field` remain the preferred style for handlers filtering multiple substantial traits.

Pair handlers (§3.8.1) use a third, binding-qualified form instead of a filter alias: `binding.Trait.field`, `binding.module_alias.Trait.field`, or `binding.alias.field` for a binding-local `as` alias. Pair-bound access is read-only.

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
- a leading handler `after:` names canonical handler identities and constrains order for that trigger
- legacy rule-level `after:` expands only between handlers with the same canonical trigger and never creates cross-trigger edges
- `order by:` constrains iteration order for a rule pass
- `filter:` and `exclude:` select entities but do not imply component reads
- regular handler contracts are inferred from their bodies; extern handler contracts are declared explicitly

Every handler has a canonical identity composed from its module, owning rule, and resolved phase/event trigger. Co-eligible handlers are serialized for write/read, read/write, write/write, and matching observable-effect conflicts. Read/read and filter overlap do not conflict. Edge direction uses explicit `after:` first, then one-way writer-before-reader dependencies, then stable linked declaration order for remaining conflicts. The combined handler schedule must be acyclic.

## 5. Execution Model

### 5.1 Frame Phases

The standard library declares a canonical graph rooted at the external `frame` event:

```text
frame -> input -> fixed_tick -> tick -> late_tick -> render
```

The host injects exactly one typed `frame { dt = ... }` occurrence per host frame. Runtime dispatch follows phase metadata, never lifecycle spelling or renderer names.

The canonical declarations are equivalent to:

```cactus
pub extern event frame:
    dt: float

pub phase input:
    from:
        frame

pub phase fixed_tick:
    after:
        input
    every: 1.0 / 60.0
    max: 8

pub phase tick:
    after:
        fixed_tick
    dt: float = frame.dt

pub phase late_tick:
    after:
        tick
    dt: float = frame.dt

pub phase render:
    after:
        late_tick
    alpha: float = fixed_tick.alpha
```

For a periodic phase with interval `every` and catch-up cap `max`, each root occurrence performs:

```text
accumulator += root.dt
due = floor(accumulator / every)
run = min(due, max)
repeat run times:
    activate with dt = every
    drain the activation event cascade
    commit structural commands
accumulator -= due * every
alpha = accumulator / every
```

Subtracting `due` deliberately drops capped whole steps while preserving the fractional remainder, so `0 <= alpha < 1` and backlog cannot grow permanently. Each repetition is a separate activation and commit boundary.

### 5.2 Scene Loading

`load module.name` transitions to another module-as-scene. Conceptually:

1. remove old scene entities
2. instantiate new scene entities

Scene loading does not synthesize handlers by trigger spelling. Projects that need initialization work model it with an explicit runtime event or phase.

### 5.3 Structural Changes

`spawn`, `destroy`, `add`, and `remove` are buffered in an activation-local deterministic command list. An activation runs its phase handlers, dispatches emitted events, and drains the bounded event cascade before applying structural commands. Ordinary trait writes remain visible to later scheduled handlers; structural changes do not alter entity selection midway through an activation. A spawn committed after periodic repetition N is selectable in repetition N+1.

Events emitted during an activation are delivered in deterministic queue order. Event handlers follow their own stable graph schedule and may emit further events. Feedback cycles are allowed, but cascade depth is bounded; overflow occurrences are deferred to a later activation. Commands produced by deferred delivery belong to that later activation. Effect calls happen when their handler executes and are not rolled back; matching effect domains are serialized by graph order.

### 5.4 Targeted Event Delivery

`emit Event to target:` (§3.16) evaluates `target` exactly once at the emit site and stores the resulting `entity_id` with the queued occurrence. `emit Event:` without `to` queues an occurrence with no recipient (a broadcast). Recipient identity survives queueing and bounded cascade deferral unchanged — a targeted occurrence deferred past the current cascade depth is delivered with its original recipient in the later activation.

Targeted delivery obeys the same total `entity_id` semantics as the rest of the language: if the recipient is no longer live when delivery begins, the occurrence is silently dropped before any consumer executes. No handler, command, or effect runs for a dropped occurrence.

For a live targeted occurrence, delivery is routed per consumer domain rather than broadcast to every matching entity:

- a **selectionless** consumer runs once, exactly as it would for a broadcast occurrence — targeting is routing, not privacy, and does not grant it an implicit current entity;
- a **unary** consumer runs at most once, for the recipient only, and only if the recipient currently satisfies that consumer's `filter:`/`exclude:` selection — a live recipient that fails the filter causes the consumer not to run at all;
- a **pair** consumer runs only the snapshotted tuples where at least one binding equals the recipient (a tuple where both bindings equal the recipient still runs once, since the tuple itself occurs once).

An untargeted occurrence always uses each consumer's full ordinary domain: every unary match, every pair tuple, one selectionless run. Targeted occurrences use the same stable consumer graph order, bounded cascade rules, and activation command buffer as broadcast occurrences — a targeted delivery is routing over the ordinary schedule, never an immediate out-of-band call at the emit site.

## 6. Gameplay-Core Examples

### 6.1 Platformer Loop

```cactus
input MoveX: axis
input Jump: button

trait MoveIntent:
    var axis_x: float = 0.0
    var jump_pressed: bool = false

rule ReadInput:
    filter:
        MoveIntent as move

    on input:
        move.axis_x = input.axis(MoveX)
        move.jump_pressed = input.pressed(Jump)

rule Jump:
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

rule Fire:
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

rule ExpireBullets:
    filter:
        Bullet as bullet

    on tick:
        if bullet.lifetime <= 0.0:
            destroy
```

The shooter loop uses the same core constructs as the platformer loop: inputs, rules, templates, spawning, events, and cleanup.

### 6.3 Contact Detection (Pair Relations)

```cactus
trait DynamicBody:
    var vx: float

trait Solid:
    var active: bool = true

trait Collider:
    var mask: int
    var layer: int

event Contact:
    other: entity_id

rule DetectContacts:
    pairs:
        body:
            DynamicBody
            Collider
        wall:
            Solid
            Collider

    on fixed_tick:
        if body != wall and body.Collider.mask == wall.Collider.layer:
            emit Contact to body:
                other = wall

rule ResolveContact:
    filter:
        Health as hp

    on Contact:
        hp.health = hp.health - 1
```

`DetectContacts` iterates the directed product of every `body` (`DynamicBody` + `Collider`) against every `wall` (`Solid` + `Collider`); `if body != wall:` rejects self-pairs before the mask/layer check. Each qualifying tuple emits a targeted `Contact` to its `body` binding. `ResolveContact` is an ordinary unary consumer: because the occurrence is targeted, it runs at most once — for `body` — and only if `body` currently satisfies `filter: Health`, rather than broadcasting to every entity with `Health`.

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

### 7.3 Extern Rules

`extern rule` is used when a compiler-owned adapter or user library supplies handler implementations. The generated ABI is per handler and includes its canonical trigger identity. Selected callbacks receive only the declared const read references, mutable write references, entity context, and restricted event/command/effect adapters. Selectionless callbacks receive trigger data and declared capabilities but no entity. An unrestricted registry is not part of the user callback surface.

Renderers are ordinary `on render` handlers with `effects: graphics`; input producers are typically selectionless `on input` handlers. Runtime scheduling never infers behavior from a rule name, filter shape, or lifecycle-like trigger spelling.

These surfaces are active, but they are **not the minimal gameplay-core language story**.

### 7.4 Standard UI (`std.ui`, `std.pointer`)

Standard UI is an ordinary ECS capability, not core-language syntax: every widget is a regular entity carrying `std.ui.Node` plus whichever presentation/container traits it needs, composed with the same `entity`/`template`/`children:` archetype syntax as any other gameplay object (§3.7). There is no `view`/`panel`/`button` keyword.

**Traits (`std.ui`):**

- `Node` — `visible`, `enabled`, `z_index`, `clip_children`. Every widget has one.
- `Visual` — `scale`, `opacity`. Presentation only: scale never affects logical/hit bounds.
- `PreferredSize` — `min_size`, a component-wise floor applied on top of measured intrinsic/content size (never a maximum/cap).
- `Anchors` — `min`, `max`, `pivot`, `offset`, `margin_min`, `margin_max`. Equal `min`/`max` on an axis is a **fixed** axis (sized from the item's own `DesiredSize`); unequal is a **stretched** axis (sized purely from the parent slot and margins, ignoring `DesiredSize`).
- `Panel`, `Text`, `Image`, `Button` — presentation traits with their own color/value/font/fit/label fields; see `stdlib/std/ui.cactus` for the exhaustive field list.
- `Stack` (`axis`, `gap`, `align`, `padding`), `Grid` (`columns`, `cell_size`, `gap`, `padding`), `GridItem` (`column`, `row`, `column_span`, `row_span`), `Overlay` (`padding`) — container traits. If more than one is present on the same entity, precedence is Stack, then Grid, then Overlay; a Node with none of them behaves as Overlay.
- `FrameAnimation` (`frame_count`, `fps`, `frame`, `elapsed`, `playing`) and `BumpAnimation` (`from_scale`, `to_scale`, `duration`, `elapsed`, `playing`) plus the zero-field targeted `pub event StartBump` that (re)starts a `BumpAnimation` on its recipient only.
- `DesiredSize` (`size`) and `ComputedLayout` (`position`, `size`, `effective_visible`, `effective_enabled`, `effective_opacity`, `clip_min`, `clip_max`, `draw_order`) are **projected** traits (§3.16): authored `MeasureUi`/`ArrangeUi` rules `project` them each frame; they hold no meaningful value outside the phases that project and consume them (see §7.4.2).

**Layout.** `MeasureUi` reduces bottom-up over `query.hierarchy_postorder[Node]()`: leaf intrinsic size (text/image/button metrics) combines with descendant `DesiredSize` per the active container's policy, then `PreferredSize.min_size` raises the result as a floor. `ArrangeUi` allocates top-down over `query.hierarchy_preorder[Node]()`: a Node whose parent is absent/stale or a live non-Node entity is a root and receives the full window rect; otherwise its container-typed parent allocates it a slot (Stack: sequential with gap/align; Grid: cell-indexed, explicit `GridItem` overriding automatic placement; Overlay/none: the full parent content rect), and `Anchors`, if present, resolves inside that slot. `effective_visible`/`effective_enabled`/`effective_opacity` inherit down the tree (ANDed/multiplied with the node's own `Node`/`Visual` fields) and `clip_children` intersects `clip_min`/`clip_max` into descendants.

**Stacking and painter order.** `draw_order` is a single recursive, sibling-local stacking-context traversal: each parent's direct children are sorted by `(z_index, creation_ordinal)`, and each child's whole subtree is emitted atomically before the next sibling — a high-`z_index` descendant cannot escape its parent's subtree and overlap an unrelated later sibling. `RenderUi` (one unified painter, not one renderer per trait) submits primitives in ascending `draw_order`; pointer window-candidate collection (below) consumes the same order descending.

**Pointer interaction (`std.pointer`).** Generic, not UI-specific: `PointerTarget` (`enabled`, `blocks_lower`, `priority`) opts any entity — a widget, a flat/volume-world entity, an editor handle — into pointer interaction, and `PointerState` (`hovered`, `pressed`) is its presentation-facing hover/press state. `top_target()` merges window (`ComputedLayout`-bounds), flat-world (2D camera + collider), and volume-world (3D camera ray + collider) candidates window-before-world, front-to-back, honoring `blocks_lower`/`priority`. `RoutePointer` (declared in `std.ui` because it must run after `ArrangeUi`'s `project ComputedLayout` within the same input-phase batch, so routing always sees the current frame's layout) tracks singleton hover with deterministic Leave-before-Enter transitions, drives primary capture across press/hold/release, validates `Click` only on release over the still-current captured target, and consumes the primary logical pointer action on any accepted hit. `PointerEnter`, `PointerLeave`, `PointerPress`, `PointerRelease`, and `Click` are ordinary targeted events (`position: vec2`) delivered to the target — a ClickButton reacting to `on pointer.Click:` is regular gameplay code (§5.4), including reading the recipient through `self`.

#### 7.4.1 Standard UI Phase Order

Within the canonical phase graph (§5.1), Standard UI's rules run in this fixed order:

```text
input:      MeasureUi -> ArrangeUi -> RoutePointer
tick:       AnimateUiFrames, AnimateUiBump
late_tick:  MeasureUi -> ArrangeUi
render:     RenderUi
```

`MeasureUi`/`ArrangeUi` run twice — once in `input` (so `RoutePointer` and gameplay code see current-frame layout) and once in `late_tick` (so `tick`-phase presentation changes, e.g. a `BumpAnimation` scale update, render in the same frame without having affected this frame's logical hit-testing, since `Visual.scale` never changes hit bounds).

#### 7.4.2 Frame-Local Projected Layout

Because `DesiredSize`/`ComputedLayout` are projected traits, they are visible to `filter:`/`exclude:` matching and direct reads only until the end-of-frame projected-trait cleanup that follows `render` (§3.16) — never across a frame boundary. Code (including tests) that needs a value computed by an earlier phase in the *same* frame can read it normally; code that runs after a full frame has completed must not assume it survived, and should instead re-derive it (Standard UI recomputes both every frame regardless) rather than caching a stale read.

#### 7.4.3 Deferred UI Features

Not part of the current Standard UI surface: keyboard/gamepad focus, text entry/editing, scrolling and virtualized lists, themes, data binding, accessibility, and general style inheritance. Layout containers use symmetric `vec2` padding only (no per-edge padding) in the initial surface. These remain candidates for a later change, not core-language syntax (§8.1 still applies to `view`/`panel`/`button`-style retained-tree keywords).

## 8. Deferred and Migration Notes

### 8.1 Deferred Features

The following are deferred from the current profile:

- `view`
- `interface`
- core-language `view`/`panel`/`button`/recursive-layout/reduction/`Top(1)` retained-tree syntax — Standard UI (§7.4) covers this need as an ordinary stdlib capability (entities, traits, `std.query` hierarchy queries) instead, so this now specifically excludes new *core-language keywords* for UI, not UI as a capability
- keyboard/gamepad focus, text entry, scrolling/virtualized lists, themes, data binding, accessibility, and general style inheritance for Standard UI (§7.4.3)

### 8.2 Legacy Syntax to Migrate Away From

The following older forms are not normative in the current profile:

- `apply:` / `config:` archetype syntax
- `enable` / `disable` as the documented runtime trait mutation model
- parenthesized `emit Event(...)` as the main documented event form
- flat `spawn Foo(...)` override syntax as the main documented spawn form
- parameterized handler forms such as `on tick(dt: float):`
- handlerless extern rules or extern filters treated as implicit reads
- relying on lifecycle names or renderer names to select a runtime hook

Prefer:

- nested trait blocks in `entity`, `template`, and `spawn`
- `emit EventName:` with payload block syntax
- explicit `extern event` roots and `phase` declarations
- `on tick:` / `tick.dt` where `tick` resolves to a declared phase
- explicit extern handler contracts and handler-level `after:`
- `add` / `remove`

### 8.3 Example Hygiene

Maintained examples should avoid placeholder-only syntax and stale migration comments. If an example relies on a backend helper, it should present that helper as a backend/runtime concern rather than as an unfinished core-language feature.