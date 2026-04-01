## MODIFIED Requirements

### Requirement: Registry-based system generation
The backend SHALL generate system functions that use `entt::registry::view<Components...>()` for entity iteration. Filter clauses SHALL map to view template parameters. For each event handler the backend SHALL emit a second parameter `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (e.g., `tick.dt`).

#### Scenario: System with filter
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and an `on tick:` handler
- **THEN** the backend generates a function `void SystemName_tick(entt::registry& registry, const TickEvent& tick)` using `registry.view<Position, Velocity>().each([](auto& pos, auto& vel) { ... })`

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
