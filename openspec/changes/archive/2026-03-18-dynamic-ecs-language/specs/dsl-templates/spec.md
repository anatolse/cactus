## ADDED Requirements

### Requirement: Template declaration syntax
The language SHALL support a `template` top-level declaration that defines a reusable entity blueprint. A `template` declaration has the same structure as a `unit` declaration — an `apply:` block listing traits, an optional `config:` block providing default field values, and an optional `child:` block — but is NOT automatically instantiated at program start. `template` declarations may be marked `pub` for cross-module access.

```
template WalkerEnemy:
    apply:
        Position
        EnemyAI
    config:
        patrol_speed = PATROL_SPEED
        direction = 1.0
        enemy_type = EnemyType.Walker
```

#### Scenario: Template declared but not auto-instantiated
- **WHEN** a module contains a `template Foo:` declaration
- **THEN** no entity for `Foo` exists at program start (unlike `unit`)

#### Scenario: Template with pub modifier
- **WHEN** a `template` is declared with `pub`
- **THEN** it is accessible from other modules via qualified name or `use` import

#### Scenario: Template with no config defaults (all fields required at spawn)
- **WHEN** a `template` declares traits with fields but provides no `config:` defaults for them
- **THEN** all such fields SHALL be provided at every `spawn` site or the compiler reports an error

---

### Requirement: `spawn` statement creates entity from template
The language SHALL support a `spawn` statement inside system event handlers. `spawn TemplateName(field = value, ...)` creates a new entity using the named template as its archetype. Field overrides provided in parentheses are merged with the template's `config:` defaults; provided values take precedence over defaults. `spawn` is syntactically symmetric with `emit`.

```
spawn WalkerEnemy(pos = vec2(400.0, 568.0), patrol_min_x = 350.0, patrol_max_x = 550.0)
```

#### Scenario: Spawn can override any field including defaulted ones
- **WHEN** `spawn Foo(field_a = value)` is called and `Foo`'s `config:` also provides a default for `field_a`
- **THEN** the spawn-site value takes precedence; `field_a = value` (not the template default)

#### Scenario: Spawn with partial overrides (remaining defaults applied)
- **WHEN** `spawn Foo(field_a = value)` is called and `Foo`'s `config:` sets `field_b = default`
- **THEN** the new entity has `field_a = value` and `field_b = default`

#### Scenario: Spawn with unknown field name
- **WHEN** `spawn Foo(unknown_field = value)` references a field not in any of `Foo`'s applied traits
- **THEN** the compiler SHALL report an error: "unknown field 'unknown_field' for template 'Foo'"

#### Scenario: Spawn with missing required field
- **WHEN** `spawn Foo()` omits a field that has no `config:` default and is `var` (not `let` with a default)
- **THEN** the compiler SHALL report an error: "required field '<name>' not provided for template 'Foo'"

#### Scenario: Spawn inside event handler
- **WHEN** `spawn` is used inside an `on tick(dt: float):` or any other event handler
- **THEN** it is valid and creates an entity at that point in execution

#### Scenario: Spawn outside event handler (invalid)
- **WHEN** `spawn` appears at module top-level or inside a `func` body
- **THEN** the compiler SHALL report an error: "`spawn` only allowed inside system event handlers"

---

### Requirement: `destroy` statement removes current entity
The language SHALL support a `destroy` statement inside system event handlers. `destroy` removes the entity currently being processed by the enclosing handler. Before removal, `on destroy()` lifecycle handlers fire on all systems whose filter matches the entity (see `dsl-scene-loading` spec for lifecycle details).

```
system DeathSystem:
    filter:
        Health
    exclude:
        Persistent

    on tick(dt: float):
        if health <= 0:
            destroy
```

#### Scenario: Destroy removes entity from world
- **WHEN** `destroy` executes inside a system handler
- **THEN** the current entity SHALL be queued for removal at the current handler's completion and SHALL no longer appear in any system's filter after that frame

#### Scenario: Destroy outside event handler (invalid)
- **WHEN** `destroy` appears outside a system event handler
- **THEN** the compiler SHALL report an error: "`destroy` only allowed inside system event handlers"

#### Scenario: Destroy on persistent entity
- **WHEN** `destroy` is called on an entity that has the `Persistent` trait active
- **THEN** the entity SHALL still be destroyed — `Persistent` only protects against `load`-triggered cleanup, not explicit `destroy`

---

### Requirement: `on spawn()` lifecycle handler on systems
Systems MAY declare an `on spawn():` handler. This handler fires once for each new entity that matches the system's `filter:` (and does not match `exclude:`), after all of the entity's fields have been initialized. Multiple systems may each have `on spawn()` handlers; they fire in system declaration order.

```
system EnemySpawnEffects:
    filter:
        Position
        EnemyAI

    on spawn():
        emit SpawnParticle(pos)
```

#### Scenario: On spawn fires after fields are initialized
- **WHEN** an entity is created via `spawn` or module `load`
- **THEN** `on spawn()` handlers fire with all trait fields already set to their initial values

#### Scenario: On spawn only for matching entities
- **WHEN** a new entity is created that does NOT match a system's `filter:`
- **THEN** that system's `on spawn()` SHALL NOT fire for that entity

#### Scenario: Multiple systems each receive on spawn
- **WHEN** two systems both have `on spawn()` handlers and a new entity matches both filters
- **THEN** both handlers fire, in the order the systems are declared in source

---

### Requirement: `on destroy()` lifecycle handler on systems
Systems MAY declare an `on destroy():` handler. This handler fires once for each entity that matches the system's filter and is about to be removed (via `destroy` statement or `load`-triggered cleanup). The handler fires before the entity is actually removed, so trait fields are still accessible.

#### Scenario: On destroy fires before entity removal
- **WHEN** `destroy` is called or a `load` cleans up non-persistent entities
- **THEN** `on destroy()` handlers fire while the entity's fields are still readable

#### Scenario: Emitting events from on destroy
- **WHEN** `on destroy()` contains an `emit` statement
- **THEN** the emitted event is dispatched after the handler returns (same as any other `emit`)
