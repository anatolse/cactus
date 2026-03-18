## MODIFIED Requirements

### Requirement: Primitive type support
The type system SHALL support the following primitive types: `int` (32-bit signed), `float` (64-bit), `bool`, `string` (UTF-8 immutable), `vec2`, `vec3`, `quat`, `color`, and `entity_id`.

**`entity_id` semantics update:** An `entity_id` value SHALL always refer to a live entity. There is no null, zero, or "no entity" sentinel value. The absence of a relationship between entities is modeled by trait presence/absence (enable/disable), not by storing a null `entity_id`. Stale `entity_id` values (referencing entities that have been destroyed) are runtime-invalid; the EnTT backend's generation-based entity identifiers detect stale references transparently.

The semantic analyzer SHALL reject expressions that attempt to compare `entity_id` values against integer literals (e.g., `target == 0`) with an error: "entity_id has no null value; model absent relationships using trait enable/disable instead."

#### Scenario: Primitive type in field declaration
- **WHEN** a field is declared as `var health: int`
- **THEN** the type system resolves it to TypeInfo with kind=Int

#### Scenario: Vec3 type
- **WHEN** a field is declared as `var position: vec3`
- **THEN** the type system resolves it to TypeInfo with kind=Vec3, representing `{ x: float, y: float, z: float }`

#### Scenario: entity_id field always valid at runtime
- **WHEN** a trait has `var target: entity_id` and is active on an entity
- **THEN** the type system treats the field value as always referring to a valid live entity

#### Scenario: entity_id compared to integer literal rejected
- **WHEN** an expression `enemy_id == 0` appears where `enemy_id` is of type `entity_id`
- **THEN** the analyzer reports an error: "entity_id has no null value; use trait enable/disable to model absent relationships"

#### Scenario: entity_id compared to another entity_id accepted
- **WHEN** an expression `a == b` appears where both `a` and `b` are of type `entity_id`
- **THEN** the type system accepts the equality comparison (identity check between two handles)

---

### Requirement: Spawn expression return type
The type system SHALL resolve the type of a `spawn_expr` as `entity_id`. The spawn expression `spawn TemplateName(...)` SHALL have type `entity_id` regardless of the template's applied traits.

#### Scenario: Spawn expression type is entity_id
- **WHEN** `let enemy = spawn Enemy(pos = vec2(400.0, 200.0))` appears
- **THEN** the type system resolves `enemy` as type `entity_id`

#### Scenario: Spawn expression used directly in emit target
- **WHEN** `emit Configure(value = 5) to spawn Enemy()` appears
- **THEN** the type system accepts `spawn Enemy()` as a valid `entity_id` expression for the `to` clause
