# Cactus DSL Language Specification

**Version:** 0.6.0
**Status:** Draft

## 1. Overview

Cactus DSL is a declarative, data-oriented notation for interactive simulations that compiles to an engine backend. Games come first: entities, state, reactions, spawning, scene flow, and their timing. UI applications and tools, such as a game editor, are built from the same primitives. Language constructs describe data, relations, time and reactions, never genres or widgets; genre and UI vocabulary lives in stdlib modules.

The language is taught through a **gameplay-core profile** that is sufficient for authoring things like:

- platformers: movement, gravity, jumping, collectibles, enemies, camera follow
- shooters: fire input, projectile spawning, hit/damage flow, cleanup, enemy defeat

A **tools profile** covers UI applications and editors with the same primitives plus stdlib modules, and adds no syntax of its own.

Engine plumbing — rendering submission, audio playback, physics integration, input devices — belongs to stdlib/backend layers. Application logic does not: a UI or tool that can only be built by moving its logic into `extern` code marks a language gap.

Authored code cannot make three classes of error: memory management (no authored construct allocates or frees memory), lifetime management (entity handles are total and structural changes commit at defined boundaries), and data races (handler contracts are derived and conflicting handlers are ordered by the compiler). See `openspec/specs/language-philosophy/spec.md`.

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
- `rule` selection domains: selectionless, unary `filter:`/`exclude:`, and binary `pairs:` relations, with `order by:` and `limit:` available to either non-selectionless domain
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
let     var     persist pub
on      emit    if      else    match   return
filter  exclude order   by      after   as      every  max
reads   writes  emits   commands effects
spawn   destroy load    add     remove  project for     in
to      from    self
true    false   and     or      not
fixed_tick late_tick
```

`set` is a contextual keyword: it starts a `set_stmt` (§3.16) only at statement start when followed by a trait reference, and is an ordinary identifier everywhere else.

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
                | group_decl | func_decl | extern_func_decl | asset_decl
                | input_decl ;
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
const_assign    = IDENTIFIER [ ":" type ] "=" expression NEWLINE ;
```

A constant's value is a *const expression*: literals, enum variants, other constants, unary and binary operators, the `vec2`/`vec3`/`color`/`quat` constructors, struct construction (§3.4), list literals, and calls to functions proven pure (user `func`s and pure stdlib functions such as `std.math`). A trait or field read, an entity name, `self`, a world query, `spawn`, or a call whose effects are not known to be empty is a compile error at the offending sub-expression. A string literal may be a whole constant value or a struct field value; string operators are not const expressions. A value may span several lines inside parentheses or brackets.

```cactus
use std.math as math

const:
    HALF_PI = math.PI * 0.5
    STEER = math.radians(40.0)
    ROBOT = UnitDef(speed = 4.0, health = 3)
    WAVES: list[UnitDef] = [
        ROBOT,
        UnitDef(speed = 2.5, health = 6),
    ]
    NONE: list[UnitDef] = []
```

- **Order.** A constant may read any constant of its own module, in any declaration order; the compiler evaluates constants in dependency order. A reference cycle is a compile error naming every constant in the cycle.
- **Types.** `NAME: type = expression` declares the constant's type, and the value must match it. Without a type, the constant has its value's type. A value whose type cannot be inferred, such as an empty list, needs a type.
- **Module scope.** A constant belongs to the module that declares it. That module reads it by its bare name; other modules read it through their import alias (`math.PI`, `units.ROBOT`). Every constant is visible to importers, two modules may use the same name, and a bare name never reaches another module's constant.
- **Tables.** A constant may hold a struct or a `list[T]`. Handlers read struct fields with `.field` and iterate a list constant with bounded `for`. Constants are immutable; reading one into a local copies it.
- **Render passes.** A render-pass stage handler (§3.11.1) may read a constant only when its type is `int`, `float`, `bool`, `vec2`, `vec3`, or `color` and its value, including every constant it reads, uses only GLSL-translatable operations. Any other constant is a compile error naming it.

### 3.4 Structs

```ebnf
struct_decl     = "struct" IDENTIFIER ":" NEWLINE INDENT
                  { struct_field }
                  DEDENT ;
```

Structs are value objects used for grouped data. A struct value is built by naming every field once, in any order:

```cactus
struct Squad:
    lead: entity_id
    size: int

r.squad = Squad(size = 3, lead = other)
```

```ebnf
struct_construction = dotted_name "(" [ named_argument { "," named_argument } [ "," ] ] ")" ;
```

The struct may be local or reached through an import alias (`units.UnitDef(...)`). A missing, unknown, or repeated field, or a positional argument, is a compile error naming the struct and the field, and each value must match its field's type. Construction works anywhere an expression does: handler and `func` bodies, `add`/`set`/`spawn` field blocks, template and entity bodies, trait field defaults, and constants (where every field value must itself be a const expression). Arguments are evaluated in source order. Struct values are copied on assignment, on passing, on storing into a trait field, and on reading from a constant.

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
field_modifiers = { "persist" | "pub" } ;
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
    persist pub var health: int = 100
```

`persist` marks a field as durable game-save state; it carries no format, storage, or scene-survival meaning of its own — see §7.6 for what it makes eligible and how a save is produced.

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
entity_decl     = [ "pub" ] "entity" IDENTIFIER [ "from" template_ref ] ":" NEWLINE INDENT
                  { archetype_entry }
                  DEDENT
                | [ "pub" ] "entity" IDENTIFIER "from" template_application NEWLINE ;

template_decl   = [ "pub" ] "template" IDENTIFIER [ template_parameters ] ":" NEWLINE INDENT
                  { archetype_entry }
                  DEDENT ;

archetype_entry = template_use_entry | archetype_trait_entry ;
template_use_entry = "use" template_ref NEWLINE ;
template_ref    = dotted_name | template_application ;
template_application = dotted_name "(" [ named_argument { "," named_argument } [ "," ] ] ")" ;
named_argument  = IDENTIFIER "=" expression ;
template_parameters = "(" [ template_parameter { "," template_parameter } [ "," ] ] ")" ;
template_parameter = IDENTIFIER ":" type_ref [ "=" expression ] ;

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

Composition rule: body-level `use` composes a new shape and may add traits; `entity X from T:` places an instance of an existing shape and may only override fields.

`WalkerEnemy` is a composed blueprint: it receives `EnemyBase`'s trait initializers before applying its own `Position` block. `FirstWalker` uses a body-level `use` (compile-time composition). `SecondWalker` uses the `from` clause (template-backed entity): it starts from `WalkerEnemy`'s flattened archetype and overrides only `Position.pos`. Neither performs a runtime spawn.

Template-backed entities (`entity Name from Template:`) are the declarative load-time counterpart to runtime `spawn Template:`. Use `entity … from …` for pre-placed authored scene content; use `spawn` for entities created dynamically during gameplay.

Deferred grouped syntax (`entities from Template:` with multiple named instances in one block) is not part of this version of the language.

**An entity's name is a value.** In handler bodies, rule clauses, and archetype-body override values, the name of an `entity` declaration is an `entity_id` expression bound to that entity's current instance (§4.3): `set text.ScreenLabel on CrosshairHud:`, `emit Hit to Boss`, `target == Boss`, or an override such as `Rival: rival = Boss`. A `pub entity` is reachable from importing modules through the module qualifier (`hud.CrosshairHud`). Every entity of a module has its handle allocated before any of the module's initializers run, so override values may name entities declared later in the module, or each other. Children declared under `children:` have no name of their own. A `template` name is not a value; `spawn` it instead. Named field access through an entity's name is described in §4.2.

Templates may declare typed immutable parameters. Applications bind named arguments only; parameter names are not arbitrary trait fields. Parameter and argument lists may span lines. Explicit arguments evaluate exactly once in source order, followed by omitted defaults in parameter declaration order. Defaults are pure expressions using constants and earlier parameters; later or cyclic references are errors.

Bound values are reused by the root, composed templates, and descendants. Trait and child structure remains static. Instance override bodies win field-by-field after binding, even when an override makes an argument's value unused; every supplied argument is still evaluated once.

Spawn arguments may read handler locals, selected trait values, and event or phase data. Load-time arguments cannot capture handler state. All arguments must be pure. Parameters take precedence over module symbols within the template and cannot conflict with child roles in the same scope. Templates and template applications are not ordinary values.

Existing parameterless declarations and colon bodies remain valid. An application with parentheses may omit its override body, including an empty `()` application when all parameters have defaults. A bare parameterless `entity ... from Template` or `spawn Template` still requires its existing colon body.

#### Hierarchical children (`children:` blocks)

Archetype bodies may contain a contextual `children:` block that declares a tree of related entities. `children` is recognized only inside archetype bodies (an identifier named `children` directly followed by `:`); neither `child` nor `children` is a reserved keyword elsewhere.

```ebnf
archetype_entry = template_use_entry | archetype_trait_entry | children_block ;

children_block  = "children" ":" NEWLINE INDENT
                  { child_decl }
                  DEDENT ;

child_decl      = "entity" IDENTIFIER [ "from" template_ref ] ":" NEWLINE INDENT
                  { archetype_entry }        (* overrides when `from` is present *)
                  DEDENT
                | "entity" IDENTIFIER "from" template_application NEWLINE ;

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

Hierarchy syntax creates parent-child **relations only**. It does not by itself imply transform propagation, rendering, or physics attachment: a child follows its parent's transform only when the child carries the transform traits (`LocalTransform`/`WorldTransform`) required by the active transform propagation system. Propagation runs in `late_tick` and composes such a child onto its parent's `WorldTransform`, whether or not the parent has a `LocalTransform`. An entity with `WorldTransform` and no `LocalTransform` is a *pose root*: rules write its world pose and propagation never overwrites it. Every other `late_tick` rule that writes `WorldTransform` runs before propagation, so a child reflects a pose root moved in the same `late_tick`. Destroying a root uses the existing `Parent`-based recursive destroy, so generated descendants are destroyed with it on backends that support the cascade.

### 3.8 Rules

Rules contain gameplay logic over filtered entities. A regular rule has exactly one execution domain: **selectionless** (no `filter:`/`exclude:`/`pairs:`), **unary** (`filter:`/`exclude:`), or **binary pair** (`pairs:`). `pairs:` is mutually exclusive with `filter:` and `exclude:`. `order by:`, `limit:` and `reduce:` belong to no single domain — each may accompany either a unary `filter:` domain or a binary `pairs:` domain, and each requires one of them.

```ebnf
rule_decl       = "rule" IDENTIFIER ":" NEWLINE INDENT
                  ( unary_domain | pairs_clause )
                  [ order_by_clause ]
                  [ group_clause ]
                  [ after_clause ]
                  [ before_clause ]
                  [ where_clause ]
                  [ reduce_clause [ order_by_clause ] ]
                  [ keep_clause ]
                  [ limit_clause ]
                  [ when_clause ]
                  { event_handler }
                  DEDENT ;

unary_domain    = [ filter_clause ] [ exclude_clause ] ;

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

sort_key        = expression [ "asc" | "desc" ] ;

group_clause    = "group" ":" dotted_name NEWLINE ;

after_clause    = "after" ":" NEWLINE INDENT
                  { dotted_name NEWLINE }
                  DEDENT ;

before_clause   = "before" ":" NEWLINE INDENT
                  { dotted_name NEWLINE }
                  DEDENT ;

group_decl      = [ "pub" ] "group" IDENTIFIER ":" NEWLINE INDENT
                  "phase" ":" dotted_name NEWLINE
                  [ after_clause ]
                  DEDENT ;

limit_clause    = "limit" ":" expression [ "per" IDENTIFIER ] NEWLINE ;
```

A sort key is a pure expression of the same class as a `where:` predicate (§3.8.2) — literals and constants, reads through in-scope bindings, operators, and calls to functions proven pure — and must type-check as scalar-comparable (`int`, `float`, or `bool`). A bare `alias.field` path is simply the trivial case of that grammar. Direction defaults to `asc` when omitted; multiple sort keys order lexicographically, each breaking the preceding key's ties.

`order by:` requires a `filter:` or `pairs:` domain and may reference only that domain's bindings — a `filter:` alias for a unary rule, or either binding for a pair rule (so a pair sort key may read across both). On a pair domain it reorders the tuple pass only; it never changes which tuples run or how many.

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

A rule group names an ordering point inside one phase. `group` and `before` are contextual words: they start a group declaration or a rule clause only in those positions and stay ordinary identifiers elsewhere. `after:` and `before:` list rule names and group names; `group:` names the one group the rule joins. Their meaning is in §4.7.

```cactus
# std.physics.volume
pub group solve:
    phase: fixed_tick

rule MoveAndSlide:
    group: solve
    on fixed_tick: ...

# game
use std.physics.volume as phys

rule MovePlayer:
    before:
        phys.solve
    on fixed_tick: ...
```

#### 3.8.1 Pair Relations

`pairs:` declares a binary iteration domain over two ordered, uniquely named entity bindings, each with its own positive trait requirements. `pairs` is recognized contextually at the rule-clause position (like `children` inside archetype bodies); it is not a reserved keyword elsewhere and does not appear in the global keyword list. `pairs:` is rejected on `extern rule` declarations. It is mutually exclusive with `filter:` and `exclude:`, but may be combined with `order by:` (§3.8) to fix the tuple pass's invocation order.

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

**Pair-bound durable trait access is read-only.** A pair handler may read any trait it selected (`body.Collider.mask`, `wall.transform.position`), but direct or indirect mutation — assignment, compound assignment, or a data-bearing trait-match alias obtained from a binding — is rejected during semantic analysis. Selecting a trait does not itself count as a read. The exceptions are a binding named by a provably-one `limit: ... per` (§3.8.3) and the retained binding of `reduce: per:` (§3.8.5), which admit ordinary dotted-path assignment; a trait-match alias stays rejected even there.

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

**Recognized overlap predicates may be backend-accelerated.** A pair rule's `where:` predicate is eligible for broad-phase acceleration by a conforming backend, whatever traits its two bindings require, if it matches one of two recognized shapes. Recognition is by resolved canonical identity, so an aliased import (`use std.collision.volume as foo`) is recognized the same as an unaliased call:

- a direct, unwrapped call to a recognized overlap function: `std.collision.flat.circles_overlap(a_position, a_radius, b_position, b_radius)`, `std.collision.volume.spheres_overlap(a_position, a_radius, b_position, b_radius)`, or `std.collision.volume.sphere_box_overlap(sphere_position, sphere_radius, box_position, box_size, box_rotation)`. The arguments of one shape (the `a_*`, `b_*`, `sphere_*` or `box_*` arguments) must read exactly one pair binding between them, and the two shapes must read different bindings; either binding may supply either shape. Each argument is a pure expression: it may use that shape's binding, literals, constants, named entity reads and pure calls (`a.Actor.radius + PROBE`, `inflate(a.Actor.radius)`, or just `BULLET_RADIUS`), but not the other binding; or
- an equivalent manual expression built from the same primitives those functions use internally: a `<` or `<=` comparison whose left side is a dot product of a position delta with itself (`dot(b.position - a.position, b.position - a.position)`) and whose right side is the square of the two bindings' summed radii (`(a.radius + b.radius) * (a.radius + b.radius)`), with every position/radius operand a member-chain rooted at one of the rule's two pair bindings.

**Collider queries are eligible without any bound.** A pair rule is also broad-phase eligible when its two collider arguments are its two different pair bindings and it uses either form:

- a direct, unwrapped `where:` predicate `std.physics.volume.touching(a, b)`;
- a `reduce:` aggregate `first_hit(std.physics.volume.sweep(subject, delta, target))`, where `delta` reads no pair binding but `subject`, and every other aggregate of the clause is a `first_hit` over the same sweep; or
- a `reduce:` aggregate `sum(std.physics.volume.push_out(a, b))`, where every other aggregate of the clause is a `sum` over the same push-out.

Any other reducer counts or sums rows, so pruning would change it. The bound comes from the colliders themselves: each entity's shape and, for a sweep, the subject's motion over `delta`. A pruned row can only have been a miss or a zero push, the identities of `first_hit` and `sum`, so the aggregates don't change.

This is purely an optimization: it never changes which entities or tuples satisfy the rule, or the order in which they run, including a self-tuple `(e, e)` when an entity belongs to both bindings, `order by:`, and `limit:`. Every other shape — wrapped in `not`, combined with `or`, an argument reading both bindings, component-wise arithmetic (`dx*dx + dy*dy`) in place of the dot-product form, a check split across intermediate `let` bindings, or a call to any other function — remains fully supported as an ordinary predicate, evaluated exactly as written. No backend is required to implement this acceleration, and its absence is never a compile error or a behavior difference. Write the exact test in `where:`: when it is a recognized shape, the backend derives the broad phase from it, so authors write no separate looser bound. A looser recognized predicate paired with an exact test in the handler (for example an inflated radius) is still valid, but it is a stopgap for exact tests that are not yet recognized, not the intended form (see `language-philosophy`: acceleration bounds are derived, not written).

**An unaccelerated pair rule produces a warning.** When a pair rule has no recognized predicate in `where:` and no eligible `first_hit` sweep or `sum` push-out in `reduce:`, including a pair rule with neither clause, the compiler warns at its `pairs:` clause: `pair rule '<Rule>' is not accelerated: no recognized overlap predicate in where:, so every (<left>, <right>) tuple is checked`. Unary rules never get this warning.

**An unaccelerated linear-distance predicate produces a more specific warning.** When a pair rule's `where:` predicate calls the unaccelerated linear-distance function (`std.math.vec2.distance`, `std.math.vec3.distance`) with two pair-binding-rooted position arguments and compares the result with `<`, `<=`, `>`, or `>=` against a sum of two pair-binding-rooted radius-like reads — the same computation as the recognized shapes above, but using linear rather than squared distance — the compiler emits a warning naming the dimension-appropriate recognized alternative (`circles_overlap`/`spheres_overlap`, or the equivalent squared dot-product expression). It replaces the unaccelerated pair rule warning, so the rule gets one warning. Neither warning is an error or changes compilation output.

#### 3.8.3 Limit Clause

`limit:` bounds how many rows or tuples of an existing unary (`filter:`) or pair (`pairs:`) domain produce a handler activation. `limit` is recognized contextually at the rule-clause position (like `pairs` and `where`); it is not a reserved keyword elsewhere, and neither is `per`. `limit:` is rejected on rules that declare neither `filter:` nor `pairs:`, and on `extern rule` declarations. A rule carries at most one `limit:`.

**It applies after `where:` and `order by:`.** The logical pipeline is `filter → where → order by → limit`: `limit:` bounds the already-filtered and, if present, ordered domain. It never changes which rows survive `where:` or their relative order — only how many of them run. In particular, a row rejected by `where:` never occupies a slot; the bound counts survivors, so a rule under `limit: 1 per actor` whose top-ranked candidate fails `where:` activates on the next-ranked survivor rather than not at all.

```cactus
rule GroundActor:
    pairs:
        actor:
            KinematicActor
        surface:
            Solid
    order by:
        actor.KinematicActor.feet_y - surface.Solid.top asc
    where:
        surface.Solid.top <= actor.KinematicActor.feet_y
    limit: 1 per actor

    on fixed_tick:
        actor.KinematicActor.ground_surface = surface.Solid.top
```

**The global form bounds total count; the `per` form bounds each partition.** `limit: N` with no `per` admits at most `N` rows (unary) or `N` tuples in total (pair), and guarantees nothing about how often any single entity occurs among them. `limit: N per <binding>` is accepted only on a `pairs:` rule without `reduce:` (each group is already one row) and only when `<binding>` names one of that rule's two bindings; it admits at most `N` tuples for each distinct value of that binding. A binding value with no surviving tuples produces no activation at all.

**The count expression is pure, `int`-typed, and narrowly scoped.** It has the same purity class as a `where:` predicate (§3.8.2) and must type-check as `int`. A global `limit:`'s count may reference only constants. A `limit ... per <binding>`'s count may additionally read `<binding>`'s own trait fields — evaluated once per distinct binding value — but must not reference the rule's other binding, whose value varies across the very tuples the count bounds.

**Selection is deterministic.** With `order by:`, sort keys are evaluated against the rule's snapshot and equal keys break ties by stable creation order. Without `order by:`, `limit:` takes the domain's existing stable iteration order — for a pair domain, the left-binding-major snapshot order of §3.8.1. Each partition of a `per` limit resolves independently of every other. A conforming backend's physical strategy must not change which rows this selects.

**A provably-one `per` limit makes its binding writable.** When a `limit: <expr> per <binding>`'s count expression is the literal `1`, or a name whose `const:` initializer is literally `1`, the compiler proves that at most one tuple per `<binding>` value can run — and `<binding>` becomes an ordinary mutable assignment target inside that rule's handlers, carved out of §3.8.1's pair read-only rule. The proof is deliberately syntactic: any other expression, including one that always evaluates to `1` at runtime (`2 - 1`, a non-constant field read), is not provable. The rule's other binding stays read-only, a global `limit:` grants no writability on either binding, and trait-matching directly on a pair binding stays rejected regardless of any `limit:` — only ordinary dotted-path assignment is admitted. Such a write is inferred into the handler's contract exactly like a write through a unary `filter:` alias, so scheduling sees the same read/write conflict information.

#### 3.8.4 When Clause

`when:` switches a whole rule on or off from game-wide state. It holds one or more pure `bool` predicates that form a conjunction; when any predicate is false, a handler pass does nothing — no entity or tuple is visited, the body does not run, and no writes or commands are produced. `when` is recognized contextually at the rule-clause position, after `where:` and `limit:`; it is not a reserved keyword elsewhere. It applies to every handler of the rule, including event handlers and rules with no `filter:` or `pairs:`, and it is rejected on `extern rule` declarations.

```ebnf
when_clause     = "when" ":" NEWLINE INDENT
                  expression NEWLINE
                  { expression NEWLINE }
                  DEDENT ;
```

```cactus
rule SeekPlayer:
    filter:
        Enemy as enemy
        tv.WorldTransform as transform
    when:
        not Game.Match.over

    on fixed_tick:
        ...
```

**`when:` reads only game-wide values.** A predicate may use literals, constants, named field reads (`Game.Match.over`, §4.2), entity names as values (§4.3), operators, and calls to functions proven pure. It must not read a filter, pair, or `self` binding, an event or phase payload, or a handler local — a condition on the current entity or tuple belongs in `where:`. It has the same purity rules as `where:` (§3.8.2).

**`when:` evaluates once per pass.** A pass is one phase activation of a handler, or one delivered event occurrence. The runtime evaluates `when:` after the implicit named-entity requirement holds (§4.2) and before the first entity or tuple. It sees immediate writes made earlier in the same activation by handlers scheduled before it, and it is not re-evaluated during the pass: a write that closes the gate mid-pass affects only later passes.

**Its reads count as named field access.** Every named field read in `when:` adds the same requirement and the same contract read as a read in the handler body, for every handler of the rule.

#### 3.8.5 Reduce Clause

`reduce:` turns the rows of an existing unary (`filter:`) or pair (`pairs:`) domain into aggregate rows. `reduce` is a keyword at the rule-clause position. `reduce:` is rejected on rules that declare neither `filter:` nor `pairs:`, and on `extern rule` declarations. With `reduce:`, `order by:` is written after it, because it ranks aggregate rows.

```ebnf
reduce_clause   = "reduce" ":" NEWLINE INDENT
                  [ "per" ":" IDENTIFIER NEWLINE ]
                  reducer_decl { reducer_decl }
                  DEDENT ;

reducer_decl    = IDENTIFIER "=" reducer NEWLINE ;

reducer         = "count" "(" [ IDENTIFIER ] ")"
                | "sum" "(" expression ")"
                | "min" "(" expression "," "default" "=" expression ")"
                | "max" "(" expression "," "default" "=" expression ")"
                | "any" "(" expression ")"
                | "first_hit" "(" expression ")"
                | "best" "(" IDENTIFIER "," "by" "=" expression ")" ;
```

```cactus
rule TeamTotals:
    pairs:
        team:
            Team
        player:
            Player
    where:
        team.Team.active
        player.Player.team == team
    reduce:
        per: team
        players = count(player)
        total = sum(player.Player.points)
        best = max(player.Player.speed, default = -1.0)
        has_star = any(player.Player.star)
    order by:
        total desc
    limit: 2

    on tick:
        team.Team.total = total
```

**Reducers are typed and pure.** `count()` counts rows; `count(binding)` also counts rows (not distinct entities) and requires a pair binding. `sum` takes `int`, `float`, `vec2` or `vec3` and keeps that type; a vector sum adds per component in fold order. `min` and `max` take `int` or `float`, keep that type, and require a `default` of exactly that type. `any` takes `bool` and yields `bool`. `first_hit` takes and yields `std.physics.volume.SweepHit` (§7.7): it ignores misses and keeps the hit with the smallest `t`, and on equal `t` the row that comes first in fold order. `best(binding, by = key)` names a pair binding other than the `per` binding and an `int` or `float` key, and yields `entity_id`: that binding's entity in the row with the highest key, the earlier row in fold order on a tie; rows whose key is NaN are skipped. Reducer inputs have the purity class of a `where:` predicate (§3.8.2) and read the domain's bindings, constants and named entity fields. Reducer names are unique and must not reuse a pair binding name.

**Groups are outer.** Without `per:`, the whole domain is one group, so the handler runs exactly once, even for empty input. `per: binding` names a pair binding and creates one group for every entity in that binding's membership snapshot that passes the group filters, whether or not any row survives for it. An empty group gets identity values: `count` and `sum` are `0` (the zero vector for a vector `sum`), `min`/`max` their default, `any` is `false`, `first_hit` is the miss value `sweep` itself returns (`hit = false`, `t = 1.0`), and `best` is a stale `entity_id`. Defaults apply only to empty input.

**`where:` splits into group and row filters.** With `per: b`, a `where:` predicate whose pair-binding reads all root at `b` filters groups: an entity that fails it gets no group and no activation. Every other predicate filters rows. Without `per:`, every predicate filters rows. The split is syntactic.

**Pipeline.** The logical order is domain → `where:` → `reduce:` → `order by:` → `limit:` → handler. All aggregates are computed from one snapshot of the input before any handler of the pass runs, so a handler's write can't change an aggregate of the same pass. Rows fold in stable order (pair rows left-binding-major, unary rows by creation order); groups follow the `per` binding's creation order, and sort ties keep it. A global `limit:` bounds aggregate rows.

**Scope after reduction.** Handlers and sort keys see the aggregates as immutable values and, with `per: b`, the binding `b`. Every other binding, filter alias and implicit filter field is eliminated and can't be named; there is no `self`. The retained binding `b` is writable: the handler runs exactly once per `b` entity, so dotted-path assignment through `b` is accepted and recorded as a contract write, as for a provably-one `limit: … per` (§3.8.3). A targeted event keeps only the recipient's group; a global reduction ignores the target. Lifecycle triggers are rejected on reduced rules.

**Numbers.** Integer `count` and `sum` saturate at the signed 32-bit bounds at each fold step; this applies only to reducers. Floating `sum` keeps the fold order and is never reassociated. Floating `min`/`max` yield NaN when any input is NaN.

#### 3.8.6 Keep Clause

`keep T on b` names a trait that the compiler keeps on each group entity of a pair rule reduced `per: b`: present exactly while the group has at least one row after `where:`. Entering and leaving the relation are then ordinary lifecycle triggers, `on added T` and `on removed T as old`. `keep` is a contextual word at the rule-clause position, written after `reduce:`.

```ebnf
keep_clause     = "keep" dotted_name "on" IDENTIFIER
                  ( ":" NEWLINE INDENT { IDENTIFIER "=" expression NEWLINE } DEDENT | NEWLINE ) ;
```

```cactus
trait InLava:
    var zones: int = 0
    var dps: float = 0.0

rule TouchLava:
    pairs:
        body:
            Health
            physics.Collider
        lava:
            Lava
            physics.Collider
    group: physics.contacts
    where:
        physics.touching(body, lava)
    reduce:
        per: body
        n = count()
        damage = sum(lava.Lava.damage_per_second)
    keep InLava on body:
        zones = n
        dps = damage

rule Burn:
    filter:
        Health
    on added InLava:
        emit StartBurning to self
    on removed InLava as old:
        emit StopBurning to self
```

**Shape.** `b` is the `per` binding. `T` is a trait of the rule's own module with no `persist` field. The block assigns fields of `T` from pure expressions over the aggregates, `b`, constants and named entity fields; a field it leaves out takes its declared default, so a field without a default must be assigned. `keep` is rejected on unary, selectionless, unreduced, globally reduced and extern rules.

**Presence follows rows.** At each run, a group with rows gets `T` with this run's field values: added at the activation's commit, which fires `on added T`, or patched in place, which fires nothing. Every other carrier of `T`, including an entity that has left `b`'s membership, loses it at the commit, which fires `on removed T` with the last kept value. Changes apply in group creation order. A destroyed carrier fires nothing (§4.4). The set of carriers is the previous run's result, so a restored world reconciles at the next run.

**Phase.** A keep rule runs once per activation of its phase. Its phase handlers give the phase; a keep rule with no handler runs in its `group:`'s phase, and one with neither is rejected. Event and lifecycle handlers are rejected on a keep rule. While `when:` is false the rule doesn't run, and kept traits keep their presence and values; a frame with no activation of the phase changes nothing.

**One writer.** At most one `keep` clause names a trait. Every other change to a kept trait is a compile error in any module: field assignment, `set`, `add`, `remove`, `project`, and declaring an entity or template with it. Reading it, filtering on it and reacting to it with `on added` / `on removed` are allowed. `best` names the winning entity of a relation, for example the checkpoint a player last stands in:

```cactus
rule StandOnCheckpoint:
    pairs:
        player:
            Player
            physics.Collider
        point:
            Checkpoint
            physics.Collider
    group: physics.contacts
    where:
        physics.touching(player, point)
    reduce:
        per: player
        top = best(point, by = point.Checkpoint.order)
    keep AtCheckpoint on player:
        checkpoint = top
```

### 3.9 Event Handlers

Handlers are parameter-free in the current profile. Handler-local phase/event data is accessed through the trigger binding itself or through an explicit alias.

```ebnf
event_handler   = "on" ( lifecycle_trigger | event_name ) [ "as" IDENTIFIER ] ":" NEWLINE INDENT
                  [ handler_after_clause ]
                  { statement }
                  DEDENT ;

lifecycle_trigger = ( "added" | "removed" ) dotted_name ;

event_name      = dotted_name ;

handler_after_clause = "after" ":" NEWLINE INDENT
                       { dotted_name NEWLINE }
                       DEDENT ;
```

`added` and `removed` are trigger words only when a trait name follows them; `on added:` still names an event called `added`. `on added T` and `on removed T` react to a durable trait's arrival and departure (§4.4). `<phase>.vertex` and `<phase>.fragment` are derived triggers of render-pass phases (§3.11.1).

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
                     [ group_clause ]
                     [ after_clause ]
                     [ before_clause ]
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
                    | "remove" dotted_name
                    | "set" dotted_name ;
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

A `set Trait` command capability exposes a queue-only patch operation for that trait (the `set_stmt` semantics of §3.16). It grants no read or write access to the trait; a handler that also needs to read it must list it in `reads:` or `writes:`. For regular handlers, `set Trait` is inferred into `commands` and does not add the trait to `reads` or `writes`.

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

#### 3.11.1 Render-Pass Phases

A render pass is a phase that draws instanced quads with its own vertex and fragment stages, written in Cactus and compiled to GLSL. Detailed rules live in the `dsl-render-passes` capability spec; this section summarizes them.

**Recognition.** A phase is a render-pass phase when one of its fields has the type `std.render.passes.Pass`. The field name does not matter, and an aliased import is recognized the same way. A render-pass phase must also declare exactly one `std.render.passes.Target` field. Both field values must be compile-time constants. `Pass.Quads` and `Target.Screen` are the only values today. No new keyword or grammar is involved.

**Stage triggers.** A render-pass phase exposes two derived triggers, `<phase>.vertex` and `<phase>.fragment`, used with the ordinary `on ... as alias:` handler syntax. The alias is required, since the stage's built-in fields are reached through it. They don't exist on other phases; `on tick.vertex:` is an error. Each render-pass phase needs exactly one vertex handler and exactly one fragment handler.

**Domains.** The vertex handler is unary: it needs a `filter:` (and may use `exclude:`, `where:`, and `order by:`; `pairs:` is rejected). Each matching entity is drawn as one quad. The fragment handler is selectionless: no `filter:`, `exclude:`, `pairs:`, or `where:`, and it reads no durable trait.

**Built-in stage fields** (the `Quads` pass kind), reached through the trigger alias:

| Stage | Read | Write |
|---|---|---|
| vertex | `corner: vec2`, `uv: vec2`, `vertex_index: int` | `screen_position: vec2`, `uv_out: vec2`, `tint_out: color` |
| fragment | `uv: vec2`, `tint: color`, `frag_coord: vec2` | `frag_color: color` |

The fragment stage's `uv` and `tint` are the vertex stage's `uv_out` and `tint_out`, interpolated across the quad.

**Statement subset.** A stage handler body is translated to GLSL, so it accepts only: `let`/`var` locals; assignment and compound assignment to locals and to the stage's writable built-in fields; `if`/`else`; and calls to `func` or to an `extern func` that has a GLSL translation. `spawn`, `destroy`, `add`, `remove`, `set`, `project`, `emit`, `load`, `return`, trait `match`, world queries, and `for` are rejected. A vertex handler may read the traits its `filter:` selects but not write them. Operators and operand types are the same as in any other handler (§3.15).

**Independent instances.** Each matching entity is drawn with its own trait values. One instance's values never leak into another instance's quad in the same draw.

```cactus
use std.transform.flat as tf
use std.render.passes as passes

pub phase gradient_pass:
    after:
        render
    pipeline: passes.Pass = passes.Pass.Quads
    output: passes.Target = passes.Target.Screen

rule SquareVertex:
    filter:
        tf.WorldTransform as xf

    on gradient_pass.vertex as v:
        let half = 100.0
        v.screen_position = xf.position + v.corner * half
        v.uv_out = v.uv
        if v.corner.x < 0.0:
            v.tint_out = #FF0000FF
        else:
            v.tint_out = #0000FFFF

rule SquareFragment:
    on gradient_pass.fragment as f:
        f.frag_color = f.tint
```

`examples/gradient-square` and `examples/particle-burst` are the maintained examples; the second shades a round particle in the fragment stage with `passes.with_alpha`.

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
postfix_expr    = primary_expr { "." IDENTIFIER | call_args } ;
call_args       = "(" [ call_argument { "," call_argument } [ "," ] ] ")" ;
call_argument   = [ IDENTIFIER "=" ] expression ;
primary_expr    = literal | IDENTIFIER | "self" | "(" expression ")"
                | match_expr | if_expr | list_literal | spawn_expr ;
```

The operands of `and`, `or` and `not` must be `bool`. There is no implicit truthiness: `if t.hp:` is an error, write `if t.hp > 0:`.

The operands of `+`, `-`, `*`, `/`, `%` and unary `-` must not be `bool`. There is no conversion from `bool` to a number: `1 + true` and `-ready` are errors. `==` and `!=` still compare `bool` values.

Two expressions choose a value:

```ebnf
if_expr        = "if" expression ":" expression
                 { "else" "if" expression ":" expression }
                 "else" ":" expression ;
match_expr     = "match" expression ":" NEWLINE INDENT
                 match_expr_arm { match_expr_arm }
                 DEDENT ;
match_expr_arm = value_pattern "=>" expression NEWLINE ;
value_pattern  = dotted_name | [ "-" ] INTEGER | "true" | "false" | "_" ;
```

```cactus
let tier = if hp <= 0: 0 else if hp < 50: 1 else: 2

let speed = match ai.mode:
    Mode.Idle => 0.0
    Mode.Chase => 4.0
    _ => 1.5
body.velocity = body.forward * speed
```

An `if` expression is written on one line. `else` is required, and every condition must be `bool`. A `match` expression's subject must be an enum, `int` or `bool`. Its arm block ends the expression, so the next line starts a new statement. Patterns follow the value match rules in §3.17.

All branches of an `if` expression, and all arms of a `match` expression, must have the same type; that is the expression's type. There is no conversion between branches, so `if ready: 1 else: 2.0` is an error. The condition or subject is evaluated once, and only the chosen branch is evaluated. A conditional expression whose parts are all pure is pure, so it may appear in `const` values, clause predicates and template arguments.

A call's argument list may span lines and end with a comma. Arguments are positional for function calls and named for struct construction (§3.4); using the other form is a compile error.

`spawn` is both an expression and a statement surface:

```ebnf
spawn_expr      = "spawn" template_ref ":" NEWLINE INDENT
                  { archetype_trait_entry }
                  DEDENT
                | "spawn" template_application ;
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
                | load_stmt | add_stmt | remove_stmt | set_stmt | return_stmt
                 | project_stmt | foreach_stmt | expr_stmt | if_stmt | match_stmt ;

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

set_stmt        = "set" dotted_name "on" expression ":" NEWLINE INDENT
                  field_assignment { field_assignment }
                  DEDENT ;

project_stmt    = "project" IDENTIFIER [ "to" expression ] NEWLINE
                | "project" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
                  { field_assignment }
                  DEDENT ;

foreach_stmt    = "for" IDENTIFIER "in" expression ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;

if_stmt         = "if" expression ":" NEWLINE suite
                  { "else" "if" expression ":" NEWLINE suite }
                  [ "else" ":" NEWLINE suite ]
                | "if" expression ":" statement ;

suite           = INDENT statement { statement } DEDENT ;

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

set Health on target:
    current = 1

remove Frozen
destroy bullet
load levels.level2

if hp.health <= 0:
    add Dying
else if hp.health < 25:
    ai.mode = Mode.Fleeing
else:
    ai.mode = Mode.Attacking

if shooter.cooldown > 0.0: return
```

An `if` chain evaluates its conditions top to bottom and runs only the first branch whose condition is true; the terminal `else` runs when none is. Every `if` and `else if` condition must be `bool`. `else if` and `else` align with their `if`. An `else if` after the terminal `else`, a second `else`, or an empty branch is a compile error. The one-line form `if condition: statement` takes no `else`. Detailed rules live in the `dsl-parser` capability spec.

`let` declares an immutable local and `var` declares a mutable one, in handler and `func` bodies alike. Reassigning a `let` local, including a compound assignment or a member write such as `v.x = 1.0`, is an error. An `entity_id` local has no trait namespace: `e.Health.hp` is an error whether read or written (§4.2). Assignment never declares a local: assigning to an undeclared name is an error, and declaring the same name twice in one block is an error. An optional type annotation must match the initializer's type.

Render-pass stage handler bodies accept only a fixed subset of these statements, because they compile to GLSL (§3.11.1).

`emit` (like `add` and `project`) may omit the `:` payload block entirely when the event has no fields to set (e.g. a zero-field `pub event StartBump`) or when every field should take its default; `to expression` is still allowed without a block for a targeted zero-field emit.

`set T on e:` queues a patch of the named fields of trait `T` on entity `e`, applied at the activation commit (§5.3). It is allowed only inside rule event handlers (including pair handlers, against either binding) of programs that use the graph-driven phase scheduler. The `on` target is required and must be an `entity_id`; `set T on self` is valid and still deferred. The block needs at least one field, `T` must declare fields, and each value must match its field type. When applied, the patch overwrites only the named fields of `e`'s existing durable `T`. It does nothing if `e` is stale or does not carry `T` durably; it never attaches `T`, fires no lifecycle notifications, and does not change the persistence construction baseline. If `T` is currently projected over a durable value, the patch updates the durable value that frame-end cleanup restores, and the visible projection is unchanged for the rest of the frame. Use `add` to attach a trait and `set` to change fields of a trait an entity may or may not carry.

Field values and the target expression of `add`, `spawn` override blocks (including `children:` overrides), and `set` are evaluated exactly once, in source order, when the statement runs — the same as `emit` payloads — even though the command applies later at the activation commit. Later writes in the same activation, and calls to extern functions that read world state, do not change the values the command applies.

Bounded foreach is allowed only inside rule event handlers. The iterable expression is evaluated once before the loop and must have type `list[T]`; the loop variable is a read-only binding scoped to the loop body. Cactus still does not support `while`, numeric/indexed `for`, `break`, or `continue`.

`project` mirrors `add` field-initialization syntax but writes to a frame-local projected trait overlay instead of durable ECS component storage. If no `to` target is provided, the target is `self`. Projected traits are coalesced by `(entity, trait)`, visible to later `filter:` / `exclude:` matching during the same rendered frame, and cleared at the frame boundary after render processing. Use:

- `emit` for occurrence-oriented messages, especially when multiple occurrences matter;
- `add` / `remove` for durable entity state;
- `set` to change fields of a trait on an entity the handler did not select;
- `project` for current-frame facts such as grounded/contact facts, interaction availability, tint overrides, damage flashes, outlines, or other render/VFX hints.

Traits with `persist` fields cannot be projected because that modifier describes durable storage behavior.

### 3.17 Match Statements

```ebnf
match_stmt     = "match" expression ":" NEWLINE INDENT
                 match_stmt_arm { match_stmt_arm }
                 DEDENT ;
match_stmt_arm = stmt_pattern "=>" NEWLINE suite ;
stmt_pattern   = dotted_name [ "as" IDENTIFIER ] | [ "-" ] INTEGER | "true" | "false" | "_" ;
```

The subject's type decides what a `match` statement does. An enum, `int` or `bool` subject makes a **value match**. An `entity_id` subject makes a **trait match**. Any other subject type is an error.

A value match runs the body of the first arm whose pattern equals the subject. It is allowed in rule handlers and in `func` bodies.

```cactus
match ai.mode:
    Mode.Idle =>
        body.velocity = vec2(0.0, 0.0)
    Mode.Chase =>
        steer_toward(target)
    Mode.Flee =>
        steer_away(target)
```

Value patterns, in both the statement and the expression form:

- A pattern is an enum variant, an `int` or `bool` literal, a constant of the subject's type, or `_`. It must have the subject's type.
- `_` matches anything. It must be the last arm.
- Two arms with the same value are an error.
- Value arms cannot declare an `as` alias.

A value match must be exhaustive. On an enum it names every variant or ends with `_`. On a `bool` it names `true` and `false` or ends with `_`. On an `int` it ends with `_`. A missing variant is a compile error that names it, so adding a variant to an enum points at every `match` that must handle it.

A trait match tests which traits the entity has. The first arm whose trait is attached runs; `as` binds that trait's data. If no arm matches and there is no `_` arm, nothing happens, and a stale handle matches no arm. A trait match is allowed only in rule event handlers.

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

Pair handlers (§3.8.1) use a third, binding-qualified form instead of a filter alias: `binding.Trait.field`, `binding.module_alias.Trait.field`, or `binding.alias.field` for a binding-local `as` alias. Pair-bound access is read-only, except through a binding a provably-one `limit: ... per` made writable (§3.8.3).

**An `entity_id` value carries no fields.** Member access through any other `entity_id` value — a `let`/`var` local, a `for` loop variable, a `func` parameter, an event field, `self`, or a call result — is an error, in handler bodies, `func` bodies and rule clauses alike. A read reports `can't read trait '<T>' through entity_id '<e>'; read another entity's traits in a pairs: rule`; a write or compound write reports `can't write trait '<T>' through entity_id '<e>'; use 'set <T> on <e>:'`. Read another entity's traits by joining it in a `pairs:` rule, and change them with `set T on e:` (§3.16).

**Named field access** reaches a declared entity's traits by name, with the same `Name.Trait.field` shape: `Game.Match.over`, `Game.Transform.position.x`, or `Hud.text.ScreenLabel.visible` for an imported trait. The trait segment is required, and the entity's archetype must declare the trait. A read returns the current value; `=` and compound assignment write immediately, like a write through `self`. Named field access is allowed in rule handlers and rule clauses, and rejected in `func` bodies, `const` blocks, and archetype bodies. This is how game-wide state works: keep it on an ordinary named entity.

```cactus
entity Game:
    Match

rule CollectCoins:
    filter:
        Coin as coin

    on tick:
        Game.Match.score += coin.value
```

**Named access is an implicit requirement.** For every `(Name, Trait)` a handler accesses by name — in its body or in any clause of its rule — the handler runs a pass only while `Name` is alive and carries `Trait`. The runtime checks this once per pass, before `when:` and before any entity or tuple; when it fails the pass does nothing, and no error is raised. The requirement covers the **whole handler**, even when the name appears only in one branch: move such a branch into its own rule if the rest of the handler must keep running. Because `destroy` and `remove` apply at the activation commit, a requirement that holds at the start of a pass holds for the whole pass. A handler that writes a named entity runs as one sequential pass over its domain; a backend never splits it per entity.

### 4.3 `entity_id` Semantics

`entity_id` is an opaque handle. There is no null sentinel in the language surface. Operations using `entity_id` are total: stale handles produce safe no-ops or no-match behavior rather than forcing author-side null checks.

An entity's name (§3.7) is an `entity_id` bound to its current instance. It binds when its module's entities are instantiated (program start for the entry module), and after a restore it binds to the restored record whose origin is that declaration. Once the instance is destroyed — including by scene cleanup — or when a restored document has no record for it, the name is stale: `set` and `emit` targeting it do nothing, and handlers that access its fields by name do not run. Using a name only as a value adds no requirement.

### 4.4 Runtime Trait Mutation

The canonical documented runtime trait mutation model is:

- `add TraitName`
- `add TraitName:` with block initialization
- `remove TraitName`
- `set TraitName on e:` to patch fields of a trait `e` already carries

This is the preferred model for temporary gameplay states such as freeze, stun, invincibility, targeting, and similar state transitions.

Rules react to these changes with lifecycle triggers:

```cactus
rule BeginDeath:
    filter:
        Enemy
    on EnemyHit:
        add Dying

rule StartDying:
    filter:
        Enemy
    on added Dying as dying:
        dying.elapsed = 0.0

rule StopBurning:
    on removed Burning as old:
        emit Extinguished:
            heat = old.intensity
```

- `on added T` fires when `T` goes from absent to present on an entity; spawning an entity counts as the arrival of each of its traits. `on removed T` fires when `T` goes from present to absent.
- Firing is by net change per commit round (§5.3): replacing a trait the entity already carries, adding and removing it in the same round, or `set` fires nothing.
- A trigger is delivered only to the entity whose trait set changed, after the round that changed it. The handler runs if that entity matches the rule's `filter:`, `exclude:` and `when:` in the committed state. `on added T` implies the entity carries `T`, and `on removed T` implies it does not; neither needs `T` in `filter:` or `exclude:`.
- `on added T as x` binds `x` to the entity's live `T`, like a filter alias. `on removed T as old` binds a read-only copy of `T`'s value just before removal. Without `as`, the trigger binds nothing.
- Destroying an entity fires no trigger for it. To react to an entity's end, add a marker trait (for example `Dying`), react with `on added Dying`, then `destroy`.
- `T` must be a durable trait: a trait some handler projects is rejected, and so is a trigger in a `pairs:` rule. World restore fires no trigger.
- A program with no lifecycle trigger does no tracking; only traits named by a trigger are watched.

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
- rule-level `after:` is shorthand for ordering the rule's handlers after the named rule's handlers with the same canonical trigger; it never creates cross-trigger edges
- rule-level `before:` is the mirror of `after:`: the rule's handlers run before the named rule's handlers with the same canonical trigger
- a `group` declaration names an ordering point inside exactly one phase and has no runtime behavior. A rule joins it with `group:`; the rule's handler for the group's phase becomes a member, and its other handlers are unaffected. Rules in the group's own module may join any of its groups; rules in other modules may join only a `pub` group. A rule joins at most one group. A group's `after:` lists groups in the same phase, and every member runs after every member of each listed group, across the linked program
- a group name in `after:` or `before:` orders the rule's handler for the group's phase after or before every member handler of the group. The rule must have a handler for that phase, and a member cannot list its own group. A group with no members gives no edges. Group names resolve like other declarations, by local name or through a module qualifier, and obey `pub`
- `order by:` constrains iteration order for a rule pass
- `filter:` and `exclude:` select entities but do not imply component reads
- regular handler contracts are inferred from their bodies; extern handler contracts are declared explicitly

Every handler has a canonical identity composed from its module, owning rule, and resolved phase/event trigger. Co-eligible handlers are serialized for write/read, read/write, write/write, and matching observable-effect conflicts. Read/read and filter overlap do not conflict. Edge direction uses explicit ordering first (`after:`, `before:`, and group edges). Next, a conflict between a handler and a member of a `pub group` from a module it imports (directly or transitively) is judged against the whole group, so all of that handler's edges to the group point the same way: if they conflict in both directions, or only through shared effects, the importing handler runs first; if only one writes what the other reads, the writer runs first. Then one-way writer-before-reader dependencies, then stable linked declaration order for remaining conflicts. The combined handler schedule must be acyclic.

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

**A periodic phase's `dt` is a constant.** Every activation of a phase with `every:` has `dt = every`, so `<phase>.dt` is a constant expression wherever a constant is accepted: `const` blocks, `where:`, `reduce:`, `order by:`. Inside that phase's handlers the name still means the trigger binding, with the same value. A phase without `every:` has no fixed `dt`: reading its `dt` outside its own handlers is a compile error.

```cactus
const:
    STEP_DISTANCE = BULLET_SPEED * fixed_tick.dt
```

### 5.2 Scene Loading and Lifecycle Events

`load module.name` transitions to another module-as-scene. Conceptually:

1. `unload` fires, and `std.core`'s `SceneCleanup` rule destroys every entity without `KeepOnLoad`
2. the new scene's entities are instantiated
3. `load` fires

`std.core.KeepOnLoad` is a marker trait: an entity that carries it survives the cleanup in step 1. It is unrelated to `persist` fields, which decide what a save records (§7.6). An explicit `destroy` still removes a `KeepOnLoad` entity.

`std.core` declares two lifecycle events. Rules handle them like any other event (`on load:`, `on unload:`):

| Event | Fires |
|---|---|
| `load` | at program start, after the entry module's entities exist, and after each scene load |
| `unload` | before a scene transition, and once at program teardown |

There are no `spawn` or `destroy` events; `on spawn:` and `on destroy:` are rejected with a diagnostic naming the replacement. React to an entity's arrival with `on added <Trait>` (§4.4). Entities declared with `entity` fire `on added` too: once they exist at program start, one arrival activation delivers `on added T` for every such entity and every watched trait it carries, in entity creation order then trait canonical order, before any `on load` handler runs.

The cpp-entt backend does not lower the `load` statement yet, so today `load` fires only at program start and `unload` only at teardown.

### 5.3 Structural Changes

`spawn`, `destroy`, `add`, `remove`, and `set` are buffered in an activation-local deterministic command list, applied in handler execution-graph order, then entity iteration order, then statement order. Each command acts on the entity state at its position in the list: `add` then `set` patches the added trait, `remove` then `set` does nothing, and when several `set`s name the same field of the same entity the last one wins. An activation runs its phase handlers, dispatches emitted events, and drains the bounded event cascade before applying structural commands. Ordinary trait writes remain visible to later scheduled handlers; structural changes do not alter entity selection midway through an activation. A spawn committed after periodic repetition N is selectable in repetition N+1.

Events emitted during an activation are delivered in deterministic queue order. Event handlers follow their own stable graph schedule and may emit further events. Feedback cycles are allowed, but cascade depth is bounded; overflow occurrences are deferred to a later activation. Commands produced by deferred delivery belong to that later activation.

With lifecycle triggers (§4.4), commit runs in rounds: apply every queued command, deliver that round's `on added` / `on removed` triggers, and drain their cascade; commands those handlers queue form the next round of the same activation. Within a round, triggers are delivered in the order the round's commands first changed each (entity, trait) pair, and for one spawn in trait canonical order. Round *r* delivers at cascade depth *r*, so commit rounds count toward the same bound as event chains; deliveries past it are deferred like any other occurrence, together with the commands their handlers issue. Effect calls happen when their handler executes and are not rolled back; matching effect domains are serialized by graph order.

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
- `std.time` for entity lifetime and one-shot gameplay delays (§7.5)

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

### 7.5 Lifetime and Timers (`std.time`)

`std.time` is an ordinary ECS capability, not core-language syntax: entity lifetime and one-shot delays are traits, targeted events, and rules, with no delayed-event keyword, coroutine, callback storage, or hidden scheduler. See `examples/stdlib-fixtures/gameplay_timers.cactus` for a complete program.

**Traits.**

- `Lifetime` — `remaining`, `paused`. The entity is destroyed when `remaining` reaches zero.
- `Timer` — `remaining`, `armed`, `paused`. One `Timer` is one countdown; it emits one targeted `TimerExpired` back to its own entity and disarms.

**Events.** `RestartTimer` (`duration`) replaces an existing `Timer`'s countdown and arms it; `CancelTimer` disarms it without an expiration; zero-payload `TimerExpired` announces the expiry. All three are ordinary targeted events (§5.4) with ordinary delivery, so a stale or non-matching target is simply a no-match.

**Fixed simulation time.** Both countdowns advance by `fixed_tick.dt` once per eligible fixed-step invocation (§5.1), never by wall-clock or frame time. A frame that performs no fixed step consumes no countdown time, and catch-up repetitions the `max:` cap drops are never simulated — so a stalled frame cannot expire a timer early or late. Non-finite (`NaN`, `±inf`) and negative durations or `remaining` values normalize to a zero-length countdown.

**Pause is local.** `Lifetime.paused` and `Timer.paused` are the only pause policy in this module; there is no global clock or pause manager. A paused countdown neither advances nor expires, and `RestartTimer` preserves `paused`, so a timer restarted while paused resumes with its replacement duration intact.

**Restart ordering.** Requests apply in event delivery order, so two restarts before the same step leave the last duration in place rather than summing, and a `RestartTimer` followed by a `CancelTimer` never expires. A timer disarms *before* queuing `TimerExpired`, and each fixed-step invocation advances a timer at most once. There is no second, hidden timer update: `AdvanceTimer` is one node in the fixed-step graph, and everything an expiry sets off drains after it, within the same step —

```text
fixed_tick step N:   ... -> AdvanceLifetime, AdvanceTimer -> cascade -> commit
                                                    |            |
                                          queues TimerExpired    delivers TimerExpired,
                                                                 then any RestartTimer it emits
fixed_tick step N+1: ... -> AdvanceTimer            <- the rearmed timer's next advance
```

So an expiry handler that rearms its own timer (even with a zero duration) fires again no earlier than step N+1, rather than looping inside step N, and a restart issued after step N's timer node has already run does not retroactively change step N.

**Structural eligibility.** `RestartTimer` operates on entities that already carry a `Timer`; it never adds the trait. A newly spawned `Timer` or `Lifetime` becomes selectable at the ordinary activation commit (§5.3) and first advances on the *next* eligible fixed step. Destroying a timer's owner cancels its countdown with no expiration.

**Projectile recipe.** Put the countdown on the projectile itself and let `std.time` destroy it:

```cactus
template BulletTemplate:
    Bullet
    time.Lifetime:
        remaining = BULLET_LIFETIME
```

**Reload recipe.** Give each independent countdown its own timer entity, and relay its expiry to the gameplay event the owner cares about — this is how one entity gets several simultaneous, independent delays without a channel field:

```cactus
rule RelayReload:
    filter:
        TimerOwner as link
        ReloadChannel

    on time.TimerExpired:
        emit ReloadReady to link.owner

rule StartReload:
    filter:
        Weapon as weapon

    on ShotFired:
        emit time.RestartTimer to weapon.reload_timer:
            duration = RELOAD_SECONDS
```

`TimerOwner.owner` and `Weapon.reload_timer` are ordinary authored `entity_id` fields — the module supplies no implicit link between a timer and whatever it serves.

### 7.6 World Persistence (`std.persistence`)

World persistence is a small gameplay request surface plus a generated, format-independent snapshot — not a keyword, a template modifier, or a serializer authored in Cactus. Saving is independent of scene control and of `std.core.KeepOnLoad` (§5.2).

**Eligibility.** An entity is captured in a world snapshot when a trait in its originating archetype's declared trait set contains a `persist` field, or a trait currently attached to it does. The first condition survives removal of that trait; the second means attaching a persistent trait at runtime can make an ephemeral entity eligible, and removing it again makes that entity ephemeral once more if its original archetype was never eligible on its own. No other entity is recorded — a particle template with many `spawn` overrides is not eligible just because its fields were overridden.

**Construction baseline vs. current value.** For an eligible entity, fields without `persist` are recorded at the value they held immediately after construction — the archetype default, or the evaluated `spawn`/template argument — never a later mutation. `persist` fields are recorded at their current value at capture time. A field only changes what a save reports at all if it carries `persist`; everything else is baseline provenance for reconstructing the same starting point.

**Runtime structure.** The compiler generates one canonical schema descriptor and typed snapshot per program: entity records in creation order, document-local entity identities (so cycles and forward references need no live handle), canonical archetype/trait identity, final trait membership with reconstruction data, and parent links. This is generated data, not something authored in Cactus, and it excludes anything not reachable from a `persist` field or construction argument — event queues, physics solver internals, and random state are not implicitly captured.

**Requesting a save.** `std.persistence` exposes an ordinary event, so a save is requested like any other gameplay event:

```cactus
use std.persistence as storage

const:
    SAVE_SLOT = "slot1"

rule SaveGame:
    on SavePressed:
        emit storage.SaveRequested:
            slot = SAVE_SLOT
            request_id = 1

rule ReportSaveResult:
    on storage.SaveCompleted:
        # storage.SaveCompleted.slot, storage.SaveCompleted.request_id
        ...
    on storage.SaveFailed:
        # storage.SaveFailed.code, storage.SaveFailed.message
        ...
```

`SaveCompleted`/`SaveFailed` are public extern events, delivered the same way `std.core.frame` is: as external activations, not synchronous return values. `request_id` is a caller-supplied correlation value, never deduplicated — two requests with the same ID still produce two outcomes. Accepted requests at one activation boundary are captured and written as a single frozen batch, in emission order, after that activation's handlers, event cascade, and structural commit have all finished; a `SaveCompleted`/`SaveFailed` handler that emits another `SaveRequested` always belongs to a later boundary, never the batch it ran inside of. `std.persistence` requires the graph-driven scheduler (a program with at least one `pub phase`); using it on the legacy frame path is a compile-time error, not a silently dropped request.

**Requesting a restore.** `RestoreRequested`/`RestoreCompleted`/`RestoreFailed` mirror `SaveRequested`/`SaveCompleted`/`SaveFailed` exactly — same ordinary-event request, same externally-delivered outcomes, same per-boundary frozen-batch ordering, same graph-driven-scheduler requirement. A save and a restore accepted at the same activation boundary share one ordered batch; each is processed in the emission order it was requested in, and a save processed after a restore in that batch captures the just-restored world, not the one that boundary started with.

```cactus
rule RestoreGame:
    on LoadPressed:
        emit storage.RestoreRequested:
            slot = SAVE_SLOT
            request_id = 1

rule RebuildCameraAfterRestore:
    on storage.RestoreCompleted:
        spawn Camera()

rule ReportRestoreResult:
    on storage.RestoreFailed:
        # storage.RestoreFailed.code, storage.RestoreFailed.message
        ...
```

A successful restore fully replaces the active world: every entity reconstructs to what capture eligibility (above) says the document ought to contain, and nothing else survives. Reconstruction follows the same baseline capture already records — an entity comes back as its archetype default, then its evaluated construction/spawn arguments, then any `persist` field's captured value overlaid on top — so restore reproduces exactly the state a fresh spawn plus every `persist`-marked mutation would have produced, never a later unmarked mutation that capture never saw. Entity identity itself is not guaranteed stable across a restore (a restored entity may reuse a numeric id/version a since-destroyed entity held); code that needs to keep referring to "the same" entity across a restore should do so through a `persist`ed reference field, not a cached `entity_id`.

Ineligible content — a camera, HUD, or any other entity with no `persist` field anywhere in reach — does not survive a restore, the same way it was never written to the document in the first place. This is gameplay's responsibility to rebuild, not the restore's: `RebuildCameraAfterRestore` above is the whole pattern, and it is the reason `RestoreCompleted` exists as a distinct event rather than restore silently leaving old ineligible entities in place. A program that requests restores but never rebuilds its camera in response gets a black screen on the next frame — a real failure mode this example is built to demonstrate, not just document.

A rejected restore leaves the world untouched — no partial replacement, and nothing is published until every record has been validated and reconstructed in a staging area — and reports one of six standard codes on `RestoreFailed.code`:

- `adapter_unavailable` — no adapter is registered to read from
- `io_failure` — the adapter's `read` reported failure (missing slot, corrupt storage, and similar)
- `invalid_data` — the document is structurally unsound (unknown archetype/trait/field, a dangling or duplicate identity, a hierarchy cycle, or a configured size/depth limit exceeded)
- `incompatible_schema` — the document's schema descriptor does not match the running program's
- `unsupported_value` — every field is structurally valid but some value cannot be represented (wrong kind, out of range, or an unknown enum variant)
- `resource_preparation_failure` — the document is otherwise valid but names an asset or input declaration this build cannot resolve to a handle

**Storage adapters.** A registered adapter receives the generated schema descriptor and an owned typed snapshot to `write`, and returns an owned typed snapshot or an explicit error from `read` — never raw registry access, spawn capabilities, or entity reconstruction. This is what makes two adapters able to encode the same snapshot completely differently (JSON, a custom binary layout, cloud storage) and still round-trip to equal documents. The generated entry point registers a documented example file adapter by default, so a program built with no host C++ can request a save and get a file; a host supplying its own entry point registers whatever adapter it needs instead. A read whose stored schema descriptor does not match the running program's is rejected as incompatible, rather than partially applied — there is no cross-version migration in this baseline, only adapter-side rewriting of a document before it is submitted.

**Writing a custom adapter.** A registered adapter is host C++, not Cactus — a struct of two functions matching the generated `PersistenceAdapter` contract, built with `CACTUS_GENERATED_NO_MAIN` so the host supplies its own `main()`:

```cpp
#define CACTUS_GENERATED_NO_MAIN
#include "generated_program.cpp"

// A toy adapter: one save slot, held in memory for the process lifetime.
// A real adapter would write to cloud storage, a save-file format of its
// own, or anywhere else — the contract never depends on where bytes end up.
cactus::persistence::Snapshot g_slot;
bool g_has_slot = false;

int main() {
    cactus::runtime::entt_backend::register_persistence_adapter(
        cactus::runtime::entt_backend::PersistenceAdapter{
            .write = [](const std::string&, const cactus::persistence::SchemaDescriptor&,
                        const cactus::persistence::Snapshot& snapshot) {
                g_slot = snapshot;
                g_has_slot = true;
                return cactus::runtime::entt_backend::PersistenceWriteResult{.ok = true};
            },
            .read = [](const std::string&, const cactus::persistence::SchemaDescriptor&) {
                if (!g_has_slot) {
                    return cactus::runtime::entt_backend::PersistenceReadResult{
                        .ok = false, .code = "io_failure", .message = "no save yet"};
                }
                return cactus::runtime::entt_backend::PersistenceReadResult{.ok = true, .snapshot = g_slot};
            }});
    // ... rest of the host's main(), as documented for CACTUS_GENERATED_NO_MAIN.
}
```

Both functions return their result explicitly; neither may let an exception escape, since the runtime never wraps an adapter call in its own handler. The registered adapter takes effect immediately and stays registered until the process replaces or clears it — there is no per-save adapter selection from Cactus.

### 7.7 Volume Colliders (`std.physics.volume`)

**Shapes.** An entity with `Collider` (`layer`, `mask`) has exactly one shape trait: `BoxCollider` (`size`), `SphereCollider` (`radius`) or `CapsuleCollider` (`radius`, `height`). Every shape is centered on `WorldTransform.position`. A box is oriented by `WorldTransform.rotation` and `size` is its full size; a capsule is vertical along world Y and `height` includes the caps. `WorldTransform.scale` changes no shape. A `template` or `entity` that applies `Collider` with no shape trait, or with more than one, counting traits gained through `use` and `from`, is a compile error. A runtime `add`/`remove` is not checked: an entity with no shape is never hit, and one with two uses the box, then the capsule, then the sphere.

**Queries.** Three pure functions test the authored shapes of two rule bindings, for any mix of shape kinds:

- `sweep(subject, delta: vec3, target) SweepHit` — where `subject` first touches `target` when it moves by `delta`.
- `touching(a, b) bool` — whether the two colliders overlap now.
- `push_out(a, b) vec3` — the shortest move that takes `a` out of `b`, zero when they don't overlap. When both are `CharacterBody` entities and each one's `mask` selects the other's `layer`, it is half that move, so the pair splits it.

`SweepHit` has `hit`, `other` (the target), `t` (the fraction of `delta` travelled, in `[0, 1]`), `point` (on the target's surface), `normal` (unit, from the target toward the subject) and `surface` (the target's surface normal at the contact; on a box edge or corner, the adjacent face that `delta` meets most directly; for a round target, equal to `normal`). A miss has `hit = false`, `t = 1.0`, a stale `other`, and zero `point`, `normal` and `surface`, so `position += delta * hit.t` moves the full `delta` either way. A sweep that starts in contact is a hit at `t = 0` only when `delta` moves the subject further in; moving out of or along the contact is not blocked by it. A zero `delta` is a hit at `t = 0` exactly when the colliders touch.

Both arguments that name colliders must be bindings of the calling pair rule; a named entity, an `entity_id` field or a local is a compile error, and so is a call outside a rule. Each binding argument adds that binding's `WorldTransform` (`position`, `rotation`), `Collider` (`layer`, `mask`) and every shape trait to the handler's reads, and `push_out` also adds `CharacterBody`; these traits are read when present and don't narrow the selection. A pair never collides when the subject's `mask` shares no bit with the target's `layer` (for `touching` and `push_out`, `a` is the subject), when both are the same entity, or when either lacks a transform, `Collider` or shape.

```cactus
rule MoveBullets:
    pairs:
        bullet:
            Bullet
            tv.WorldTransform
        target:
            physics.Collider
            physics.Solid
    reduce:
        per: bullet
        first = first_hit(physics.sweep(bullet, bullet.Bullet.velocity * fixed_tick.dt, target))

    on fixed_tick:
        if first.hit:
            emit EnemyHit to first.other
            destroy bullet
            return
        bullet.tv.WorldTransform.position += bullet.Bullet.velocity * fixed_tick.dt
```

**Solids and triggers.** `Solid` is a fieldless trait. A `Collider` with `Solid` is solid; one without it is a trigger. The character controller treats only solids as obstacles; `sweep`, `touching` and `push_out` see every collider, so a rule picks solids or triggers in its pair binding's filter. `std.physics.flat` has the same `Solid` trait, but nothing moves a 2D body. Neither module has a collision event: a contact is a rule over `touching`, usually with a `keep` clause (§3.8.6).

**Contacts.** `std.physics.volume` declares `pub group contacts` in `fixed_tick`, after `solve`. A rule that joins it with `group: physics.contacts` sees this tick's body poses and their children's.

**Character controller.** `std.physics.volume` declares `pub group solve` in `fixed_tick`. Its rules move every entity with `CharacterBody`, `Collider`, a shape and `WorldTransform` against solid colliders: gravity, a step-up lift, up to three slides along the surfaces the body hits, a snap back down onto walkable ground, and a final push out of any overlap. A game writes `CharacterBody.velocity` (a positive `velocity.y` jumps) and reads `grounded`, `ground_normal` and `time_since_grounded`. A surface is walkable when its `surface` normal is within `max_slope` degrees of world up. A rule that writes `velocity` runs before `solve` with no annotation (§4.7); `after: physics.solve` reads this tick's results. The last stage of `solve` re-derives each `LocalTransform` descendant of a body, so such a rule also sees a body's children (a hurtbox, a model) at this tick's pose. `motion`, `lift`, `drop` and `ground_offset` belong to the controller. A 2D `CharacterBody` is plain data that nothing moves.

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
- positional struct construction such as `Squad(other, 3)` (now a compile error; name each field)
- parameterized handler forms such as `on tick(dt: float):`
- handlerless extern rules or extern filters treated as implicit reads
- relying on lifecycle names or renderer names to select a runtime hook

Prefer:

- nested trait blocks in `entity`, `template`, and `spawn`
- `emit EventName:` with payload block syntax
- explicit `extern event` roots and `phase` declarations
- `on tick:` / `tick.dt` where `tick` resolves to a declared phase
- explicit extern handler contracts
- rule-level `after:` / `before:` for same-trigger ordering between rules, and handler-level `after:` for exact handler dependencies
- a `pub group` and `after:` / `before:` on the group name, rather than another module's rule names, for ordering against a module's internals
- `add` / `remove`

### 8.3 Example Hygiene

Maintained examples should avoid placeholder-only syntax and stale migration comments. If an example relies on a backend helper, it should present that helper as a backend/runtime concern rather than as an unfinished core-language feature.
