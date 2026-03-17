## ADDED Requirements

### Requirement: Marker (empty-body) traits
The language SHALL allow trait declarations with no body (no colon, no fields, no handlers). A marker trait is a zero-cost tag component used purely for system filtering. The colon and body block are optional; omitting them produces a valid marker trait declaration.

```
trait Persistent
trait Frozen
trait Grounded
trait Dead

pub trait Visible
```

#### Scenario: Marker trait declared without colon
- **WHEN** `trait Frozen` appears in source (no colon, no indented body)
- **THEN** the parser accepts it as a valid trait declaration with no fields

#### Scenario: Marker trait usable in apply
- **WHEN** a unit or template's `apply:` block lists a marker trait
- **THEN** the semantic analyzer accepts it and includes it in the entity's trait set

#### Scenario: Marker trait usable in filter and exclude
- **WHEN** a system's `filter:` or `exclude:` block lists a marker trait
- **THEN** the semantic analyzer accepts it; entities are matched/excluded based on whether the marker trait is active

#### Scenario: Marker trait has no accessible fields
- **WHEN** system handler code attempts to read a field on a marker trait (e.g., `Frozen.value`)
- **THEN** the compiler SHALL report an error: "trait 'Frozen' has no fields"

---

### Requirement: `disabled` initial state in `apply:` block
Within an `apply:` block, a trait entry MAY be followed by `: disabled` to indicate that the trait starts in an inactive state. All traits not annotated with `: disabled` start active (default). A disabled trait's data is still allocated; it is simply excluded from system filter matching until `enable`d.

```
template Enemy:
    apply:
        Position
        EnemyAI
        Frozen: disabled     # present in schema, starts inactive
    config:
        patrol_speed = PATROL_SPEED
```

#### Scenario: Trait marked disabled starts inactive
- **WHEN** an entity is created from a template/unit with `Frozen: disabled`
- **THEN** `Frozen` is inactive on that entity; systems with `filter: Frozen` do NOT process it

#### Scenario: Trait without disabled annotation starts active
- **WHEN** an entity is created from a template/unit with `Position` (no `: disabled`)
- **THEN** `Position` is active on that entity by default

#### Scenario: Disabled marker trait in config has no field assignments
- **WHEN** a marker trait is listed as `Frozen: disabled`
- **THEN** no field assignments for `Frozen` are expected in `config:` (marker traits have no fields)

---

### Requirement: `enable` and `disable` statements toggle trait activation
The language SHALL support `enable TraitName` and `disable TraitName` statements inside system event handlers. These statements activate or deactivate the named trait on the entity currently being processed. The trait MUST be in the entity's static `apply:` declaration. After `disable`, the trait is excluded from filter matching; after `enable`, it is included again. Trait field data is preserved through enable/disable cycles.

```
system FreezeSystem:
    filter:
        Position
        EnemyAI

    on FreezeEvent():
        disable EnemyAI
        enable Frozen

system ThawSystem:
    filter:
        Position
        Frozen

    on ThawEvent():
        disable Frozen
        enable EnemyAI
```

#### Scenario: Disable removes entity from matching filter
- **WHEN** `disable EnemyAI` executes on an entity
- **THEN** that entity no longer matches any system whose `filter:` includes `EnemyAI`

#### Scenario: Enable restores entity to matching filter
- **WHEN** `enable EnemyAI` executes on an entity that had `EnemyAI` disabled
- **THEN** that entity again matches systems whose `filter:` includes `EnemyAI`

#### Scenario: Field data preserved through disable/enable cycle
- **WHEN** `disable EnemyAI` then `enable EnemyAI` are called
- **THEN** all fields of `EnemyAI` (e.g., `patrol_speed`) retain their values from before the disable

#### Scenario: Enable/disable on trait not in apply (invalid)
- **WHEN** `enable Frozen` is called inside a system whose filter entities don't have `Frozen` in their `apply:` block
- **THEN** the semantic analyzer SHALL report an error: "trait 'Frozen' is not in the apply block of entities matching this system's filter"

#### Scenario: Enable/disable only affects current entity
- **WHEN** `disable Frozen` executes inside a system handler
- **THEN** only the entity currently being processed by that handler is affected; other entities are unchanged

#### Scenario: Enable/disable outside event handler (invalid)
- **WHEN** `enable` or `disable` appears outside a system event handler
- **THEN** the compiler SHALL report an error: "`enable`/`disable` only allowed inside system event handlers"

---

### Requirement: `exclude:` block on system declarations
System declarations SHALL support an optional `exclude:` block containing an indented list of trait names. Entities that have ANY of the listed traits active SHALL be excluded from the system's processing, even if they match the `filter:` block. `exclude:` uses the same indented-list syntax as `filter:` and `apply:`.

```
system PatrolSystem:
    filter:
        Position
        EnemyAI
    exclude:
        Frozen
        Dead

    on tick(dt: float):
        pos = pos + vec2(patrol_speed * direction * dt, 0.0)
```

#### Scenario: Entity with excluded trait is skipped
- **WHEN** an entity matches `filter:` but has an active trait listed in `exclude:`
- **THEN** the entity is NOT processed by that system's handlers

#### Scenario: Entity without excluded trait is processed normally
- **WHEN** an entity matches `filter:` and has NONE of the `exclude:` traits active
- **THEN** the entity IS processed normally

#### Scenario: Exclude with disabled trait — entity is included
- **WHEN** an entity has `Frozen` in its `apply:` but `Frozen` is currently disabled
- **THEN** a system with `exclude: Frozen` DOES process that entity (the trait is inactive)

#### Scenario: Exclude clause is optional
- **WHEN** a system has no `exclude:` block
- **THEN** no entities are excluded by trait state (existing behavior)

---

### Requirement: `filter:` and `exclude:` are both optional; no `filter:` means match all entities (BREAKING for old bracket syntax)
Both `filter:` and `exclude:` are optional on system declarations. When `filter:` is omitted, the system matches **all entities** (equivalent to a zero filter mask). When `exclude:` is omitted, no entities are excluded. Both clauses, when present, SHALL use indented-list syntax (one trait per line), replacing the previous bracket-list syntax `filter: [A, B, C]`. A system with neither `filter:` nor `exclude:` processes all entities in its lifecycle handlers.

Trait field access inside a handler body is only valid for traits listed in `filter:`. A system with no `filter:` block cannot read or write any trait fields — only lifecycle operations (`destroy`, `emit`, `spawn`, `load`, `enable`, `disable`) are valid.

```
# System matching specific traits (filter required for field access)
system PatrolSystem:
    filter:
        Position
        EnemyAI
    exclude:
        Frozen

    on tick(dt: float):
        pos = pos + vec2(patrol_speed * direction * dt, 0.0)

# System matching ALL entities except Persistent (no filter needed)
system SceneCleanup:
    exclude:
        Persistent

    on unload():
        destroy

# System reacting to lifecycle with no filter (all entities)
system GlobalReset:
    on load():
        emit ResetComplete()
```

#### Scenario: Old bracket filter syntax rejected
- **WHEN** a system uses `filter: [A, B]` bracket syntax
- **THEN** the compiler SHALL report an error: "bracket filter syntax removed; use indented block syntax"

#### Scenario: System with no filter matches all entities
- **WHEN** a system has no `filter:` block
- **THEN** all entities are processed by that system's handlers (filter_mask = 0)

#### Scenario: System with only exclude processed against all entities
- **WHEN** a system has `exclude: Persistent` but no `filter:`
- **THEN** all entities without `Persistent` active are processed

#### Scenario: Field access without filter rejected
- **WHEN** a system has no `filter:` block and its handler body accesses a trait field (e.g., `pos.x`)
- **THEN** the compiler SHALL report an error: "trait field 'pos' not accessible — no filter clause declares this trait"

#### Scenario: Indented filter parses correctly
- **WHEN** `filter:` is followed by an indented block of trait names (one per line)
- **THEN** the parser accepts the declaration and extracts the trait list correctly
