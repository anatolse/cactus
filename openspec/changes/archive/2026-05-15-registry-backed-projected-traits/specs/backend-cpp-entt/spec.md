## MODIFIED Requirements

### Requirement: Registry-based system generation
The backend SHALL generate system functions that iterate over the EnTT registry and apply declared filter clauses. For ordinary generated systems with non-empty filters, the backend SHALL use `entt::registry::view<Components...>()`-style iteration so filtering is performed by EnTT rather than by scanning every entity with early-exit guards. When `exclude:` clauses are present, the backend SHALL use native EnTT exclusion where possible. For each event handler the backend SHALL emit a second parameter `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (e.g., `tick.dt`).

Projected traits SHALL participate in this same registry-based filtering by being materialized as registry components during the current frame.

#### Scenario: System with filter
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and an `on tick:` handler
- **THEN** the backend generates a function `void SystemName_tick(entt::registry& registry, const TickEvent& tick)` that iterates matching entities through an EnTT view and binds `Position` and `Velocity` component references for the handler body

#### Scenario: Non-matching entity does not terminate handler
- **WHEN** multiple live entities exist and the first live entity does not satisfy a system filter
- **THEN** generated system filtering continues considering later entities rather than returning from the entire handler

#### Scenario: on tick handler body accesses tick.dt
- **WHEN** a handler body references `tick.dt`
- **THEN** the generated C++ handler function receives `const TickEvent& tick` as a parameter and accesses `tick.dt` directly (no translation to bare `dt`)

#### Scenario: on tick with alias uses alias name in generated code
- **WHEN** `on tick as t:` is declared
- **THEN** the generated handler function receives `const TickEvent& t` as a parameter and body references use `t.dt`

#### Scenario: User event handler uses event name as variable
- **WHEN** `on PlayerDamaged:` handler body contains `h.health -= PlayerDamaged.amount`
- **THEN** the generated handler function receives `const PlayerDamagedEvent& PlayerDamaged` and the expression accesses `.amount`

#### Scenario: Marker lifecycle event handler receives empty-struct parameter
- **WHEN** a system has `on spawn:` handler (no fields)
- **THEN** the generated handler function signature is `void SystemName_spawn(entt::registry& registry, const SpawnEvent& spawn)` with an empty `SpawnEvent` struct

### Requirement: cpp-entt backend stores projected traits as transient registry components
The cpp-entt backend SHALL materialize projected traits as normal EnTT registry components during the current frame. The backend SHALL track projected component writes so that frame cleanup can remove components that were added only by projection and restore durable components that existed before being temporarily projected over. Newly generated helper identifiers for this tracking SHALL NOT use the `cactus` prefix.

#### Scenario: Project writes registry component
- **WHEN** generated code executes `project DamageFlash to target`
- **THEN** the backend writes or patches the `DamageFlash` component in the registry for `target` for the duration of the frame

#### Scenario: Project to stale target is no-op
- **WHEN** generated code executes `project DamageFlash to target` and `target` is stale/non-live
- **THEN** the operation is a safe no-op consistent with total `entity_id` semantics

#### Scenario: Projected-trait helpers avoid cactus prefix
- **WHEN** the backend emits helper storage or functions for registry-backed projected traits
- **THEN** the generated helper identifiers do not use the `cactus` prefix

#### Scenario: Project over durable component restores durable value
- **WHEN** an entity has durable `DamageFlash` before the frame and generated code projects `DamageFlash` to that entity during the frame
- **THEN** systems during the frame observe the projected value
- **AND** frame cleanup restores the pre-existing durable `DamageFlash` value

#### Scenario: Project-only component is removed at cleanup
- **WHEN** an entity did not have durable `DamageFlash` before the frame and generated code projects `DamageFlash` to that entity
- **THEN** systems during the frame observe `DamageFlash`
- **AND** frame cleanup removes `DamageFlash` from that entity

### Requirement: cpp-entt backend coalesces projected registry components
The cpp-entt backend SHALL maintain at most one projected value per `(entity, trait)` during a frame. Repeated projections to the same key SHALL patch or replace the current registry component while preserving the original pre-frame durable snapshot for cleanup.

#### Scenario: Repeated projection coalesces
- **WHEN** `DamageFlash` is projected twice to the same entity in one frame
- **THEN** later filter matching observes one `DamageFlash` value for that entity
- **AND** cleanup restores the value that existed before the first projection, if any

### Requirement: cpp-entt system filters include projected registry components
For generated system handlers, the cpp-entt backend SHALL match entities that satisfy filter traits through registry components, including components materialized by projection earlier in the frame. Exclude traits SHALL also consider projected registry components.

#### Scenario: Filter matches projected trait through registry view
- **WHEN** an entity has durable `Health` and projected `DamageFlash`
- **AND** a later system filters `Health as hp` and `DamageFlash as flash`
- **THEN** the generated handler processes that entity through normal registry-based filtering and binds `flash` to the current projected component value

#### Scenario: Exclude skips projected trait through registry exclusion
- **WHEN** an entity has projected `Suppressed`
- **AND** a later system excludes `Suppressed`
- **THEN** the generated handler skips that entity for the current frame

### Requirement: cpp-entt backend clears projected registry components at frame boundary
The cpp-entt backend SHALL clear projected-trait state at the deterministic frame boundary after render processing completes by removing projected-only registry components and restoring any pre-existing durable component values that were temporarily replaced.

#### Scenario: Projected component not visible next frame
- **WHEN** an entity has projected `Highlighted` during one frame and did not have durable `Highlighted` before projection
- **THEN** the next frame does not match `Highlighted` unless it is projected again

#### Scenario: Durable component survives projection cleanup
- **WHEN** an entity had durable `Highlighted` before projection and receives a projected `Highlighted` value during the frame
- **THEN** the next frame observes the original durable `Highlighted` value unless authored code changed or removed it durably
