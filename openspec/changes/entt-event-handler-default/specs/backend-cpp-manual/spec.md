## MODIFIED Requirements

### Requirement: Event buffer generation
The backend SHALL generate event POD structs and `std::vector`-based event buffers for each declared event, including lifecycle events sourced from the std.core AST event declarations. An event dispatch function SHALL flush buffers and invoke registered handlers. For each event handler the backend SHALL bind the event data as `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (`tick.dt`).

#### Scenario: Event declaration generates struct and buffer
- **WHEN** the decorated AST contains `event Damage:` with field `amount: int`
- **THEN** the backend generates `struct DamageEvent { int amount; };` and `std::vector<DamageEvent> damage_buffer;`

#### Scenario: Lifecycle tick event generates struct with dt field
- **WHEN** the std.core `tick` event (field `dt: float`) is processed from AST event declarations
- **THEN** the backend generates `struct TickEvent { float dt; };` (sourced from AST, not a hardcoded list)

#### Scenario: on tick handler body accesses tick.dt
- **WHEN** a handler body references `tick.dt`
- **THEN** the generated C++ handler function receives `const TickEvent& tick` and accesses `tick.dt`

#### Scenario: on tick with alias uses alias name in generated code
- **WHEN** `on tick as t:` is declared
- **THEN** the generated handler function receives `const TickEvent& t` and body references use `t.dt`

#### Scenario: User event handler uses event name as variable
- **WHEN** `on PlayerDamaged:` handler body contains `h.health -= PlayerDamaged.amount`
- **THEN** the generated handler function receives `const PlayerDamagedEvent& PlayerDamaged` and the expression accesses `.amount`

#### Scenario: Marker lifecycle event generates empty struct
- **WHEN** the std.core `spawn` event (no fields) is processed from AST event declarations
- **THEN** the backend generates `struct SpawnEvent {};` and corresponding buffer
