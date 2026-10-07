# dsl-exclusive-states Specification

## Purpose

Exclusive states let an entity be in exactly one of a set of named variants. Each variant is a trait, so rules select states and react to entry and exit with the existing trait machinery, and the compiler keeps the variants exclusive.

## Requirements

### Requirement: State declaration
A `state S:` declaration SHALL declare a state `S` with one or more variants, one per indented line. A variant SHALL be a name, optionally followed by `final`, optionally followed by `:` and an indented block of field declarations with the same grammar and rules as a trait body. `pub state` SHALL export the state and all its variants. Variant names within one state SHALL be unique. A state with no variants SHALL be a compile error.

```cactus
state EnemyMode:
    Idle
    Chasing:
        let target: entity_id
    Attacking:
        var windup: float = 0.3
    Dying final:
        var elapsed: float = 0.0
```

#### Scenario: State with marker and data variants is accepted
- **WHEN** a module declares the `EnemyMode` state above
- **THEN** compilation succeeds and `EnemyMode.Idle`, `EnemyMode.Chasing`, `EnemyMode.Attacking` and `EnemyMode.Dying` are traits of that module

#### Scenario: Duplicate variant is rejected
- **WHEN** a state declares the variant `Idle` twice
- **THEN** the semantic analyzer reports the duplicate variant at the second declaration

#### Scenario: Existing field named state still compiles
- **WHEN** a trait declares a field `var state: PlayerState`
- **THEN** it parses as a field, not as a state declaration

### Requirement: Variants are traits named through their state
Every variant `V` of state `S` SHALL be a trait written `S.V`, qualified like an enum variant, and through a module alias as `m.S.V`. A bare `V` SHALL NOT name the variant. `S.V` SHALL be usable wherever a trait name is accepted, unless a requirement of this capability restricts it: `filter:`, `exclude:`, `on added`, `on removed`, `set S.V on e:`, field access through an alias, trait-match arms and `query.*[S.V]` type arguments.

#### Scenario: Filter on one variant
- **WHEN** a rule has `filter: EnemyMode.Chasing as c` and a handler reads `c.target`
- **THEN** the handler visits only entities in `Chasing`, and `c.target` reads that variant's field

#### Scenario: Exclude a variant
- **WHEN** a rule has `filter: Enemy` and `exclude: EnemyMode.Dying`
- **THEN** the rule visits no entity in `Dying`

#### Scenario: Bare variant name is not a trait
- **WHEN** a rule has `filter: Chasing` and no trait named `Chasing` exists
- **THEN** the semantic analyzer reports an unknown trait `Chasing`

#### Scenario: Trait match arm on a variant
- **WHEN** a handler runs `match hit.other:` with an arm `EnemyMode.Dying =>`, and the other entity is in `Dying`
- **THEN** that arm runs

### Requirement: Exactly one variant per carrier
An entity that carries state `S` SHALL carry exactly one variant of `S` in every committed state of the world. `filter: S as m` SHALL select every entity that carries `S` and bind `m` to its state slot. `exclude: S` SHALL exclude every carrier. The slot `m` SHALL be usable only as a `match` subject.

#### Scenario: Slot filter selects every carrier
- **WHEN** three enemies are in `Idle`, `Chasing` and `Dying`, and a rule has `filter: EnemyMode as m`
- **THEN** the rule visits all three

#### Scenario: Slot used outside match is rejected
- **WHEN** a handler with `filter: EnemyMode as m` writes `let x = m`
- **THEN** the semantic analyzer reports that a state slot can only be a `match` subject

### Requirement: Entities start in a variant
An entity or template body SHALL place an archetype in a state by naming either a variant `S.V`, with an optional field block, or the bare state `S`, which means its first declared variant with that variant's field defaults. An archetype SHALL name at most one variant of each state. When an entity is declared `from` a template that names a variant of `S`, and the entity body names a variant of `S`, the entity's variant SHALL replace the template's.

#### Scenario: Bare state starts in the first variant
- **WHEN** `entity Grunt:` lists `EnemyMode`
- **THEN** `Grunt` starts in `EnemyMode.Idle`

#### Scenario: Two variants of one state are rejected
- **WHEN** an entity body lists both `EnemyMode.Idle` and `EnemyMode.Chasing`
- **THEN** the semantic analyzer reports that the entity names two variants of `EnemyMode`

#### Scenario: Entity variant replaces the template variant
- **WHEN** template `Enemy` lists `EnemyMode.Idle` and `entity Boss from Enemy:` lists `EnemyMode.Chasing: target = Player`
- **THEN** `Boss` starts in `Chasing` and does not carry `Idle`

### Requirement: Add swaps the variant
`add S.V`, with or without a field block, SHALL be a structural command like `add T` (deferred, applied in command order at the commit). When it applies to an entity in another variant `S.W`, it SHALL remove `S.W` and add `S.V` as one command. When it applies to an entity already in `S.V`, it SHALL replace the variant's field values like `add T` on a carried trait. When it applies to an entity that does not carry `S`, it SHALL give the entity `S` in variant `V`.

Lifecycle triggers SHALL follow the existing net change per commit round. A swap from `W` to `V` SHALL deliver `on removed S.W` before `on added S.V`. Within one round, several transitions on one entity SHALL net out: only the variants before and after the round are compared.

#### Scenario: Swap fires removed then added
- **WHEN** an entity in `Idle` receives `add EnemyMode.Chasing: target = p`, and rules declare `on removed EnemyMode.Idle` and `on added EnemyMode.Chasing as c`
- **THEN** after the commit the entity is in `Chasing` with `target == p`, and the `on removed` handler runs before the `on added` handler

#### Scenario: Two transitions in one round net out
- **WHEN** one round issues `add EnemyMode.Chasing` and then `add EnemyMode.Attacking` on an entity in `Idle`
- **THEN** the entity ends in `Attacking`, `on removed EnemyMode.Idle` and `on added EnemyMode.Attacking` fire, and no trigger for `Chasing` fires

#### Scenario: Round trip in one round fires nothing
- **WHEN** one round issues `add EnemyMode.Chasing` and then `add EnemyMode.Idle` on an entity in `Idle`
- **THEN** the entity is in `Idle` and no lifecycle trigger for `EnemyMode` fires

#### Scenario: Conflicting transitions resolve by command order
- **WHEN** two handlers in one activation add different variants to the same entity, and neither variant is `final`
- **THEN** the variant from the later command in the deterministic command order wins

#### Scenario: Add to an entity without the state
- **WHEN** `add EnemyMode.Idle` applies to an entity that carries no `EnemyMode`
- **THEN** the entity now carries `EnemyMode` in `Idle` and `on added EnemyMode.Idle` fires

### Requirement: Removing a state
`remove S` SHALL remove the whole slot, and with it the current variant, which fires `on removed S.V` for that variant. `remove S.V` SHALL be a compile error that names `remove S` and `add S.W` as the alternatives.

#### Scenario: Remove a variant is rejected
- **WHEN** a handler writes `remove EnemyMode.Chasing`
- **THEN** the semantic analyzer reports that a variant cannot be removed and suggests `add` of another variant or `remove EnemyMode`

#### Scenario: Remove the slot
- **WHEN** a handler issues `remove EnemyMode` on an entity in `Chasing`
- **THEN** after the commit the entity carries no `EnemyMode` variant, and `on removed EnemyMode.Chasing` fires

### Requirement: Final variants are never left
Once the committed world has an entity in a variant marked `final`, every later `add S.*` and `remove S` on that entity SHALL apply as no-ops, including `add` of the same final variant. This holds within a commit round too: once a command has put the entity in a final variant, later commands of that round SHALL apply as no-ops. `set S.V on e:`, field writes and `destroy` SHALL still apply.

#### Scenario: Death wins over a later AI transition
- **WHEN** in one round `add EnemyMode.Dying` applies first and `add EnemyMode.Attacking` applies later on the same entity
- **THEN** the entity ends in `Dying`

#### Scenario: Re-entering a final variant does not reset it
- **WHEN** an entity in `Dying` with `elapsed == 0.4` receives `add EnemyMode.Dying`
- **THEN** `elapsed` stays `0.4` and no lifecycle trigger fires

#### Scenario: Destroy still applies
- **WHEN** a handler destroys an entity in `Dying`
- **THEN** the entity is destroyed

### Requirement: State match
A statement-level `match` whose subject is a state slot SHALL be a state match. The subject is read once. Each arm pattern SHALL be a variant of that state, optionally with `as x`, which binds the variant's data with the same access rules as a filter alias. Marker variants SHALL NOT declare an alias. The wildcard `_` SHALL be the last arm and appear at most once. Duplicate variant arms SHALL be a compile error. A state match SHALL be exhaustive: its arms name every variant, or its last arm is `_`. A non-exhaustive state match SHALL be a compile error that names the missing variants. A state match is allowed only in rule handler bodies.

The `match` expression form SHALL accept a state slot subject with the same patterns, without `as`.

```cactus
match m:
    EnemyMode.Chasing as c =>
        steer_toward(c.target)
    EnemyMode.Attacking as a =>
        a.windup -= tick.dt
    _ =>
        pass_time()
```

#### Scenario: State match runs the current variant's arm
- **WHEN** an entity in `Attacking` with `windup == 0.3` runs the match above with `tick.dt == 0.1`
- **THEN** only the `Attacking` arm runs and `windup` becomes `0.2`

#### Scenario: New variant breaks an incomplete state match
- **WHEN** `EnemyMode` gains a variant `Stunned`, and a state match names `Idle`, `Chasing`, `Attacking` and `Dying` with no `_`
- **THEN** compilation fails at that `match` with an error naming the missing variant `EnemyMode.Stunned`

#### Scenario: Variant of another state is rejected
- **WHEN** a state match on an `EnemyMode` slot has an arm `JumpPhase.Rising =>`
- **THEN** the semantic analyzer reports that `JumpPhase.Rising` is not a variant of `EnemyMode`

#### Scenario: Alias on a marker variant is rejected
- **WHEN** a state match arm is written `EnemyMode.Idle as i =>` and `Idle` has no fields
- **THEN** the semantic analyzer reports that marker variant `EnemyMode.Idle` cannot declare an alias

### Requirement: State restrictions
A variant SHALL NOT declare a `persist` field. A variant or a state SHALL NOT be the target of `project` or `keep`. A trait SHALL NOT have the same name as a state in the same module. Each violation SHALL be a compile error that names the state or variant.

#### Scenario: Keep on a variant is rejected
- **WHEN** a pair rule declares `keep EnemyMode.Chasing on b`
- **THEN** the semantic analyzer reports that a state variant cannot be kept

#### Scenario: Persist field in a variant is rejected
- **WHEN** a variant declares `persist var elapsed: float`
- **THEN** the semantic analyzer reports that state variants cannot declare `persist` fields
