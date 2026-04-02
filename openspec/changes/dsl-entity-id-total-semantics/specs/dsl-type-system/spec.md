## MODIFIED Requirements

### Requirement: Primitive type support
The type system SHALL support the following primitive types: `int` (32-bit signed), `float` (64-bit), `bool`, `string` (UTF-8 immutable), `vec2`, `vec3`, `quat`, `color`, `entity_id`, `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`, `InputButton`, and `InputAxis`.

**`entity_id` semantics:** `entity_id` is an opaque entity handle. The language exposes no null or zero entity literal — authors never construct null handles. A stored `entity_id` value may become **stale** when the referenced entity is destroyed. All operations using `entity_id` are **total**: operations on stale handles are defined as safe no-ops or no-match at runtime. Authors are not required to guard against stale handles; the generated backend inserts validity checks. Use `exists(entity_id)` to explicitly test whether a handle refers to a live entity.

The semantic analyzer SHALL reject `entity_id` comparisons against integer literals and SHALL reject null-like constructions.

#### Scenario: Primitive type in field declaration
- **WHEN** a field is declared as `var health: int`
- **THEN** the type system resolves it to TypeInfo with kind=Int

#### Scenario: entity_id field in trait
- **WHEN** a trait declares `var target: entity_id`
- **THEN** the type system resolves it to TypeInfo with kind=EntityId; the field may hold a live or stale handle at runtime

#### Scenario: entity_id compared to integer literal rejected
- **WHEN** an expression `enemy_id == 0` appears where `enemy_id` is of type `entity_id`
- **THEN** the analyzer SHALL report: "entity_id has no null literal; use `exists(id)` to test handle validity or `add`/`remove` to model absent relationships via trait presence"

#### Scenario: entity_id compared to another entity_id accepted
- **WHEN** an expression `a == b` appears where both `a` and `b` are of type `entity_id`
- **THEN** the type system accepts the equality comparison

#### Scenario: Vec3 type
- **WHEN** a field is declared as `var position: vec3`
- **THEN** the type system resolves it to TypeInfo with kind=Vec3, representing `{ x: float, y: float, z: float }`

#### Scenario: mesh_id primitive type in field declaration
- **WHEN** a field is declared as `let mesh: mesh_id`
- **THEN** the type system resolves it to TypeInfo with kind=MeshId

#### Scenario: InputButton primitive type in field declaration
- **WHEN** a field is declared as `let jump: InputButton`
- **THEN** the type system resolves it to TypeInfo with kind=InputButton
