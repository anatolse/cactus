# dsl-entity-id-total-semantics Specification

## Purpose
TBD - created by archiving change dsl-entity-id-total-semantics. Update Purpose after archive.
## Requirements
### Requirement: `entity_id` is an opaque entity handle with total operation semantics
`entity_id` is an opaque value that identifies an entity instance. The language exposes no null or zero entity literal — authors never construct or manipulate null handles. Handles may become **stale** when the referenced entity is destroyed. All operations involving `entity_id` are **total**: operations on stale handles are safe no-ops or no-match at runtime. Authors are never required to check for stale handles; the generated backend takes responsibility for validity guards.

The canonical semantics of `entity_id` operations on stale/dead handles are:

| Operation | Behavior with stale handle |
|---|---|
| `add Trait to stale_id` | no-op (silently skipped) |
| `remove Trait from stale_id` | no-op |
| `emit Event to stale_id` | event dropped, not delivered |
| `destroy stale_id` | no-op |
| `match stale_id:` | no arm matches (including `_ =>`) |
| `exists(stale_id)` | returns `false` |

#### Scenario: entity_id has no null literal
- **WHEN** an author writes any literal or zero value for `entity_id`
- **THEN** the compiler SHALL report: "entity_id has no null literal; use `exists(id)` to test handle validity"

#### Scenario: entity_id compared to integer literal rejected
- **WHEN** an expression `enemy_id == 0` appears where `enemy_id` is type `entity_id`
- **THEN** the analyzer SHALL report: "entity_id has no null literal; use `exists(id)` to test handle validity or `add`/`remove` to model absent relationships via trait presence"

#### Scenario: entity_id compared to another entity_id accepted
- **WHEN** an expression `a == b` appears where both `a` and `b` are type `entity_id`
- **THEN** the type system accepts the equality comparison

#### Scenario: operation on stale handle is silent no-op
- **WHEN** `add Stunned to id` executes and the entity referenced by `id` was destroyed in a previous frame
- **THEN** no component is added; no error occurs; execution continues normally

#### Scenario: targeted event to stale handle is dropped
- **WHEN** `emit Collision(other = id) to id` executes and the entity referenced by `id` was destroyed
- **THEN** the event is not delivered to any handler; no error occurs

### Requirement: `exists(entity_id)` built-in expression
The DSL SHALL support `exists(expr)` as a built-in expression where `expr` has type `entity_id`. It returns `bool`: `true` if the referenced entity is currently alive (not yet destroyed), `false` if the entity has been destroyed. `exists()` MAY only appear inside rule event handler bodies (it requires world access).

```ebnf
exists_expr = "exists" "(" expr ")" ;
```

#### Scenario: exists returns true for live entity
- **WHEN** `exists(f.target)` is evaluated and the entity referenced by `f.target` has not been destroyed
- **THEN** the expression returns `true`

#### Scenario: exists returns false for dead entity
- **WHEN** `exists(f.target)` is evaluated and the entity referenced by `f.target` was destroyed
- **THEN** the expression returns `false`

#### Scenario: exists used in if condition
- **WHEN** `if exists(f.target):` appears in a rule handler
- **THEN** the semantic analyzer accepts it; the body executes only when the target is alive

#### Scenario: exists in pure func body is an error
- **WHEN** `exists(some_id)` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report: "`exists()` requires world access; only allowed inside rule event handlers"

#### Scenario: exists argument must be entity_id
- **WHEN** `exists(42)` appears where `42` is an integer
- **THEN** the semantic analyzer SHALL report: "`exists()` argument must be of type `entity_id`"

### Requirement: `match stale_id:` yields no match
When a `match entity_id:` statement is evaluated and the referenced entity is dead (stale handle), no arm executes — not even the `_ =>` wildcard arm. The `_ =>` wildcard means "entity is live but no listed trait matched," not "entity is dead or no trait matched." Authors who need to handle the dead-entity case use `if exists(id): match id: ...`

#### Scenario: match on dead entity — no arms fire
- **WHEN** `match c.other:` executes and `c.other` is a stale handle
- **THEN** no arm body executes; execution continues after the match block

#### Scenario: match on dead entity — wildcard does not fire
- **WHEN** `match c.other:` with `_ => do_something()` executes and `c.other` is stale
- **THEN** `do_something()` is NOT called; execution continues after the match block

#### Scenario: dead-entity case handled explicitly
- **WHEN** an author writes `if exists(c.other): match c.other: Boss as b => ...`
- **THEN** the match is only attempted when the entity is live; dead handle case is handled by the `if` guard


### Requirement: A stale handle stays stale for the life of its world
An `entity_id` SHALL name at most one entity in a world, from the world's load or restore until the world is replaced. Once that entity is destroyed, the handle SHALL stay stale for the rest of the world's life, no matter how many entities are created or destroyed later. A stale handle SHALL never compare equal to the handle of an entity created after it, and every operation on it SHALL keep the stale-handle behavior defined by the total operation semantics. If the runtime can't create an entity without giving out a handle that was already used, it SHALL stop with an error instead.

#### Scenario: Stale handle survives heavy slot reuse
- **WHEN** a trait field keeps the handle of a destroyed entity, and the program then spawns and destroys more than 5000 entities, one at a time
- **THEN** `exists` on the kept handle returns `false` every time, `match` on it matches no arm, commands and targeted events through it are no-ops, and it equals no live entity's handle

#### Scenario: Deferred spawn handle is never reissued
- **WHEN** a handler spawns an entity, keeps its handle, the entity is destroyed in a later activation, and many more entities are spawned afterwards
- **THEN** no later spawn returns a handle equal to the kept one

#### Scenario: Unset handle field is stale
- **WHEN** a trait, struct or event field of type `entity_id` has no initializer and is never assigned
- **THEN** `exists` on it returns `false`, and it equals no entity's handle, including the first entity the world creates

#### Scenario: Handle space exhaustion is an error
- **WHEN** a world has created so many entities that every remaining handle value was already used
- **THEN** the next entity creation stops the program with an error that names the exhausted handle space, and no handle is given out twice
