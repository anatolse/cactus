# dsl-required-links Specification

## Purpose
Define required links: fields whose type names a trait, so a rule that follows a link always reaches a live entity carrying that trait, and every way a link can break is handled by an explicit rule.

## Requirements

### Requirement: Link fields

A field of a trait, a field of a state variant, and a template parameter SHALL accept a trait type: `let target: Unit`, `var follow: tf.WorldTransform`, `template Recruit(bed: RoomTile)`. The type SHALL name a durable trait or a state variant, local or through an import alias. Such a field or parameter SHALL be a required link to that trait, called its target trait. A link field SHALL have no default. Every place that makes a value of the holder trait SHALL assign it, and a missing assignment SHALL be a compile error that names the field.

A link type SHALL be rejected, with a source-located error, on a `persist` field, a relation field, an event field, a phase field, a `func` return type, a struct field and a `const`. A trait type on a `func` parameter is not a link: it keeps its meaning as a read-only trait parameter. A projected trait SHALL be rejected as a target trait, and a trait that holds a link field SHALL NOT be the target of `project`, because a frame-local value would outlive a target destroyed at a commit later in the same frame.

#### Scenario: A variant holds a link
- **WHEN** a state declares `Attack:` with `let target: Unit` and `Unit` is a durable trait
- **THEN** compilation accepts it, and `target` is a required link to `Unit`

#### Scenario: Missing link value
- **WHEN** a handler writes `add Order.Attack` with no field block and `Order.Attack.target` is a link
- **THEN** the compiler reports that `target` must be assigned

#### Scenario: Persisted link
- **WHEN** a trait declares `persist let target: Unit`
- **THEN** the compiler reports that a link field can't be `persist`

### Requirement: Link values come from proven sources

An expression SHALL have link type `T` only when it is one of:
- a pair binding whose required traits include `T`, or `self` in a rule whose `each:` includes `T`;
- a named entity whose archetype declares `T`;
- a read of a link field or link template parameter whose target trait is `T`;
- a `best(b, by = ...)` reducer whose binding `b` requires `T`.

A state variant `S.V` as the required trait SHALL satisfy a link type `S.V` only. Assigning any other expression to a link, including an `entity_id` local, event field or call result, SHALL be a compile error whose help says to join the target in `pairs:` with `T` required. A link value SHALL be accepted wherever an `entity_id` value is accepted, such as `emit ... to`, a relation argument, an `==` comparison or an `entity_id` field.

#### Scenario: Link from a pair binding
- **WHEN** a pair rule with binding `enemy: Unit` writes `add Order.Attack: target = enemy`
- **THEN** compilation accepts it

#### Scenario: Link from an entity_id
- **WHEN** a handler assigns an event's `entity_id` field to a link of type `Unit`
- **THEN** the compiler reports an error whose help suggests a `pairs:` binding that requires `Unit`

#### Scenario: Link used as an entity value
- **WHEN** a handler writes `emit Struck to order.target` and `order.target` is a link
- **THEN** compilation accepts it

### Requirement: A link is checked when its value is made

A link value SHALL be checked wherever its holder's value is made or the link field is written: when declared entities load, when a `spawn` or `children:` entity is created at commit, when an `add H:` applies, and when a `var` link field is assigned. The check SHALL pass when the target is live and carries the target trait at that point. A failed check SHALL make the whole operation have no effect, as for a ranged field: a spawn creates no entity, an `add` leaves the entity as it was, an assignment leaves the old value. The runtime SHALL report a fault that names the holder trait, the field and the target.

#### Scenario: Spawn with a dead target
- **WHEN** a handler spawns a template whose `bed` link names a tile destroyed earlier in the same commit round
- **THEN** no entity is created and the runtime reports a fault naming `bed`

### Requirement: A link is lost when its target stops carrying the trait

A link SHALL be lost at the commit position of the command that destroys its target, destroys an owner its target is destroyed with, removes the target trait from it, or, for a variant target trait, swaps the target to another variant. A holder destroyed at or before that position SHALL lose nothing. A link written in the same round to a new valid target before the losing position SHALL not be lost.

#### Scenario: Target destroyed
- **WHEN** a unit is in `Order.Attack` with `target` naming an enemy, and a handler destroys the enemy
- **THEN** the unit's `target` link is lost at that command's commit position

#### Scenario: Holder and target die together
- **WHEN** one commit round destroys both the holder and its target
- **THEN** no link is lost and no `on lost` trigger fires

### Requirement: The lost trigger

A handler SHALL accept the trigger `on lost H.f`, where `H` is a trait or state variant and `f` is one of its link fields. `lost` SHALL be a contextual word only directly after `on`, and SHALL remain an ordinary identifier elsewhere. The trigger SHALL be delivered to the holder after the commit round in which its link was lost, with the same timing, net-change, matching, delivery-order and cycle-delay rules as `on removed` (`dsl-trait-lifecycle-triggers`). A holder SHALL receive at most one delivery per link per round. `on lost H.f as old` SHALL bind a read-only copy of `H` as it was when the link was lost. The trigger SHALL be rejected in a `pairs:` rule.

#### Scenario: Lost target triggers its holder
- **WHEN** a rule over `each: Unit` declares `on lost Order.Attack.target:` with `add Order.Auto`, and the unit's target is destroyed
- **THEN** after the round, the handler runs once for that unit, and at its commit the unit is in `Order.Auto`

#### Scenario: lost stays an identifier
- **WHEN** a trait declares a field named `lost`
- **THEN** compilation accepts it

### Requirement: Every link the program can lose is resolved

For each link field `H.f`, the compiler SHALL decide from reachable archetypes and reachable commands (`archetype-reachability`) whether the program can lose it: a reachable `destroy` or owner destroy of an entity that may carry the target trait, a reachable `remove` of it, a reachable swap away from a variant target trait, or an `extern rule` command that may do any of these. When it can, the program SHALL be a compile error unless both hold:
1. Every reachable archetype that carries `H` matches the domain of at least one `on lost H.f` handler.
2. Every such handler, on every path through its body, issues at least one resolving command on its holder: `add` of another variant of `H`'s state, `add H:` that assigns `f`, `remove H` or `remove` of `H`'s state, or `destroy self`. An `if` without `else` and a loop body SHALL not count as every path.

The error SHALL name the link, one losing command with its rule, and the uncovered archetype or the path that does not resolve. A link that the program can't lose SHALL need no handler.

#### Scenario: Missing handler
- **WHEN** a combat rule can destroy units and no rule declares `on lost Order.Attack.target`
- **THEN** the compiler reports an error naming `Order.Attack.target` and the destroying rule

#### Scenario: Handler that may not resolve
- **WHEN** an `on lost Order.Attack.target` handler adds `Order.Auto` only inside an `if` with no `else`
- **THEN** the compiler reports that the handler may finish without resolving the link

#### Scenario: Link that can't be lost
- **WHEN** no reachable command destroys, removes or swaps away the target trait of a link
- **THEN** compilation accepts the program without an `on lost` handler

### Requirement: Lost holders are hidden from reads through the link

From the commit position where a link is lost until a resolving command for it applies, every pass whose handler, clauses or keep fields read through that link SHALL skip the holder. Passes that do not read through the link SHALL still see the holder. When the resolving commands issued for a lost link all have no effect, the runtime SHALL report a fault that names the link and the holder at the end of that commit round, and the holder SHALL stay hidden from reads through the link.

#### Scenario: A reader skips a broken holder
- **WHEN** a unit's attack target was lost in a round and its `on lost` handler has not yet committed
- **THEN** a pass that reads `order.target[Unit].health` does not visit that unit, and a pass over `each: Unit` that does not read the link does

### Requirement: Reading through a link

An expression `x.f[T].g` SHALL read field `g` of trait `T` on the target of link `f`, where `x` is an `each:` alias of the rule's own row, `x.f` is a link field of that trait, and `T` is the link's target trait. It SHALL be allowed in handler bodies and in `where:`, `order by:`, reducer inputs and keep fields of a unary rule. A read through a pair binding (`b[H].f[T]`) or a named entity (`E[H].f[T]`), any other trait in the brackets, any write through the path, and the same path in a `func` body SHALL be compile errors, because only the row's own holder can be hidden from the pass while its link is lost. The help for another trait or another holder SHALL suggest a `pairs:` binding.

#### Scenario: Read the target's health
- **WHEN** a rule over `each: Unit, Order.Attack as order` declares `where: order.target[Unit].health < 10.0`
- **THEN** compilation accepts it, and the pass sees each attacker whose target's health is below 10

#### Scenario: Read an undeclared trait
- **WHEN** a handler reads `order.target[Position].value` and the link's target trait is `Unit`
- **THEN** the compiler reports an error whose help suggests joining the target in `pairs:`

#### Scenario: Write through a link
- **WHEN** a handler writes `order.target[Unit].health -= 1.0`
- **THEN** the compiler reports an error whose help suggests `set Unit on order.target:`
