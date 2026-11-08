## Purpose

A `single` trait states that at most one live entity carries it. It is a domain fact the compiler cannot derive. The runtime enforces it, and the compiler uses it as a size bound.

## Requirements

### Requirement: Singleton declaration
`[pub] single trait T` SHALL declare a trait that at most one live entity carries in every committed world. `single` SHALL be recognized only directly before `trait` at declaration position, and SHALL stay an ordinary identifier elsewhere. A state or a state variant SHALL NOT be declared `single`.

```cactus
pub single trait Terrain
```

#### Scenario: Singleton is accepted
- **WHEN** a module declares `single trait Terrain`
- **THEN** compilation succeeds, and `Terrain` works like any other trait in filters, `add`, `remove` and field access

#### Scenario: An identifier named single still works
- **WHEN** a trait declares a field `var single: bool`
- **THEN** it parses as a field

#### Scenario: A single state is rejected
- **WHEN** source declares `single state Mode:`
- **THEN** the compiler reports an error at `single`

### Requirement: A second carrier is a fault
At a commit round, a `spawn` or `add` that would give `T` to an entity SHALL be checked against the carriers that stay. A carrier stays unless a command of that round destroys it, destroys an owner it is destroyed with, or removes `T` from it, wherever that command falls in the round. When a carrier stays, or an earlier command of the round already gave `T` to another entity, the command SHALL fail. A failed command SHALL have no effect, fire no lifecycle trigger, and report a deterministic fault with a diagnostic that names `T`. A failed `spawn` SHALL create no entity, including its descendants, and its handle SHALL be stale. `add T` on the entity that already carries `T` SHALL NOT be a fault. A creation that applies at once, outside a commit round, SHALL be checked against the live world the same way: a `spawn` or `add` in a program without phases, and a native creation such as `std.editor.spawn_template`.

#### Scenario: Second spawn fails
- **WHEN** one entity carries `Terrain` and a handler spawns a template that carries `Terrain`
- **THEN** no entity is created, the handle is stale, and a fault naming `Terrain` is reported

#### Scenario: Add to a second entity fails
- **WHEN** one entity carries `Terrain` and a handler runs `add Terrain to other`
- **THEN** `other` does not carry `Terrain`, no `on added Terrain` fires, and a fault is reported

#### Scenario: Replacing the carrier in one round
- **WHEN** one round destroys the carrier and spawns a new one, in either command order
- **THEN** the spawn succeeds and no fault is reported

#### Scenario: Moving the trait in one round
- **WHEN** one round runs `remove Terrain` on the carrier and `add Terrain` on another entity, in either command order
- **THEN** the other entity carries `Terrain` and no fault is reported

#### Scenario: Destroying the owner frees the trait
- **WHEN** the carrier is a child, and the same round destroys its owner and spawns a new carrier
- **THEN** no fault is reported

#### Scenario: Two rows spawn in one pass
- **WHEN** a pass spawns a `Terrain` template once per row and no carrier exists
- **THEN** the first row in creation order succeeds and every later row's spawn fails

#### Scenario: Re-adding to the carrier
- **WHEN** `add Terrain` applies to the carrier
- **THEN** its fields are replaced like any `add` and no fault is reported

#### Scenario: Without phases the check still holds
- **WHEN** a program has no phases, one entity carries `Terrain`, and a handler spawns a `Terrain` template or adds `Terrain` to another entity
- **THEN** nothing is created or added and a fault naming `Terrain` is reported

#### Scenario: The editor's spawn is checked
- **WHEN** one entity carries `Terrain` and `std.editor.spawn_template` names a `pub` template that carries `Terrain`
- **THEN** no entity is created, the call returns a stale handle, and a fault naming `Terrain` is reported

### Requirement: Two producers need an order
Two handlers of one activation that can each give `T` to an entity, through `add T`, a `spawn` whose template tree carries `T`, or a native creation that can create a carrier of `T`, SHALL conflict. A `spawn` whose template is not known, and a call to `std.editor.spawn_template` when a `pub` template tree carries `T`, SHALL count as able to give `T`. With no explicit or inferred order, this SHALL be an unorientable conflict reported as a compile error naming both handlers. A handler that destroys a carrier or removes `T` SHALL NOT conflict with a producer through this rule.

#### Scenario: Unordered producers are rejected
- **WHEN** two `tick` rules each spawn a template that carries `Terrain`, with no order between them
- **THEN** the compiler reports an unorientable conflict naming both

#### Scenario: Ordered producers compile
- **WHEN** one of those rules declares `after:` the other
- **THEN** compilation succeeds

#### Scenario: The editor's spawn is a producer
- **WHEN** a `pub` template carries `Terrain`, one `tick` rule calls `std.editor.spawn_template` and another spawns a `Terrain` template, with no order between them
- **THEN** the compiler reports an unorientable conflict naming both

#### Scenario: Destroying and spawning need no order
- **WHEN** one `tick` rule destroys the carrier and another spawns a new one, with no order between them
- **THEN** compilation succeeds

### Requirement: Singleton restrictions
The compiler SHALL report an error when two named entities carry `T`, when one template tree or named entity tree carries `T` on more than one node, and when `T` is the target of `project` or `keep`. Restoring a saved world that has two carriers of `T` SHALL be rejected before the world is replaced.

#### Scenario: Two named carriers are rejected
- **WHEN** two `entity` declarations carry `Terrain`
- **THEN** the compiler reports an error that names both

#### Scenario: One tree with two carriers is rejected
- **WHEN** a template lists `Terrain` on itself and on a child
- **THEN** the compiler reports an error at the template

#### Scenario: Projecting a singleton is rejected
- **WHEN** a handler runs `project Terrain`
- **THEN** the compiler reports an error at the statement

#### Scenario: Keeping a singleton is rejected
- **WHEN** a rule declares `keep Terrain`
- **THEN** the compiler reports an error at the clause

#### Scenario: Restore rejects two carriers
- **WHEN** a save document has two records that carry `Terrain`
- **THEN** validation rejects the document before the world is replaced

### Requirement: A singleton bounds a query
A pair binding, unary domain or query filter that requires a `single` trait SHALL count as bounded to one entity in every query cost diagnostic. A pair rule with such a binding SHALL NOT be reported as not accelerated. A `without:` entry or negated filter entry SHALL NOT bound anything.

#### Scenario: A singleton binding clears the pair warning
- **WHEN** a pair rule has a binding `terrain` that requires `Terrain`, and no recognized predicate
- **THEN** no "not accelerated" warning is reported

#### Scenario: A product of two unbounded bindings is still warned
- **WHEN** a pair rule's two bindings both require only ordinary traits that spawned entities carry
- **THEN** the "not accelerated" warning is reported

#### Scenario: Exclusion does not bound
- **WHEN** a rule has `each: Unit` and `without: Terrain`
- **THEN** the domain is not bounded by `Terrain`
