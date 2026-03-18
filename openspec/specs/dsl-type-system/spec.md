## Requirements

### Requirement: Primitive type support
The type system SHALL support the following primitive types: `int` (32-bit signed), `float` (64-bit), `bool`, `string` (UTF-8 immutable), `vec2`, `vec3`, `quat`, `color`, `entity_id`, `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`, `InputButton`, and `InputAxis`.

**`entity_id` semantics:** An `entity_id` value SHALL always refer to a live entity. There is no null, zero, or "no entity" sentinel value. The absence of a relationship is modeled by trait presence/absence (enable/disable). The semantic analyzer SHALL reject `entity_id` comparisons against integer literals (e.g., `target == 0`).

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
- **THEN** the type system accepts the equality comparison

#### Scenario: mesh_id primitive type in field declaration
- **WHEN** a field is declared as `let mesh: mesh_id`
- **THEN** the type system resolves it to TypeInfo with kind=MeshId

#### Scenario: InputButton primitive type in field declaration
- **WHEN** a field is declared as `let jump: InputButton`
- **THEN** the type system resolves it to TypeInfo with kind=InputButton

### Requirement: Composite type support
The type system SHALL support user-defined `struct` (value objects, fields only), `enum` (named integer constants), and `list[T]` (parameterized ordered collection).

#### Scenario: Struct type declaration
- **WHEN** the source declares `struct Item:` with fields `name: string` and `price: int`
- **THEN** the type system registers a Struct type named "Item" with two fields

#### Scenario: List parameterized type
- **WHEN** a field is declared as `var items: list[Item]`
- **THEN** the type system resolves it to TypeInfo with kind=List and element type pointing to the Item struct

#### Scenario: Enum type
- **WHEN** the source declares `enum State:` with variants `Idle`, `Walking`, `Running`
- **THEN** the type system registers an Enum type named "State" with three integer-valued variants

### Requirement: Field modifier flags
The type system SHALL track field modifiers as boolean flags on TypeInfo: `is_let`, `is_persist`, `is_sync`, and `is_pub`.

#### Scenario: Combined modifiers
- **WHEN** a field is declared as `persist sync pub var score: int`
- **THEN** the TypeInfo has is_persist=true, is_sync=true, is_pub=true, is_let=false

#### Scenario: Let field immutability
- **WHEN** a field is declared as `let max_health: int = 100`
- **THEN** the TypeInfo has is_let=true and the field cannot be reassigned after creation

### Requirement: Type inference for expressions
The type system SHALL infer types for expressions, including lambda parameters inferred from context, binary operation result types, and function call return types.

#### Scenario: Lambda parameter inference
- **WHEN** `items.map(i => i.price)` is used where `items` is `list[Item]` and `Item` has field `price: int`
- **THEN** the type system infers parameter `i` as type `Item` and the map result as `list[int]`

#### Scenario: Binary operation type
- **WHEN** the expression `health - damage` is evaluated where both are `int`
- **THEN** the type system infers the result type as `int`

### Requirement: String type rvalue constraint
The `string` type SHALL be rvalue-only — it can appear in `const` blocks or as computed expressions, but never as inline literals in logic blocks. String literals ARE permitted in `asset` declarations as resource path values; this is the sole exception to this rule.

#### Scenario: String const reference in logic
- **WHEN** a system handler references a const identifier `SHOP_TITLE` that was declared as a string in a `const` block
- **THEN** the type system resolves the reference type as `string` (via const pool ID)

#### Scenario: String literal in asset declaration accepted
- **WHEN** `asset Theme: music = "audio/theme.ogg"` appears in a source file
- **THEN** the semantic analyzer accepts the string literal as an asset path without error

#### Scenario: Inline string literal in system logic rejected
- **WHEN** a system handler assigns `let name = "player"` (an inline string literal in logic)
- **THEN** the semantic analyzer reports an error: string literals are not permitted outside `const` blocks or `asset` declarations

### Requirement: Spawn expression return type
The type system SHALL resolve the type of a `spawn_expr` as `entity_id`.

#### Scenario: Spawn expression type is entity_id
- **WHEN** `let enemy = spawn Enemy(pos = vec2(400.0, 200.0))` appears
- **THEN** the type system resolves `enemy` as type `entity_id`

#### Scenario: Spawn expression used directly in emit target
- **WHEN** `emit Configure(value = 5) to spawn Enemy()` appears
- **THEN** the type system accepts `spawn Enemy()` as a valid `entity_id` expression for the `to` clause

### Requirement: Asset opaque ID types
The type system SHALL define six built-in opaque handle types for external resources. These types are value types with no accessible fields and cannot be constructed by user code — they are only obtained via `asset` declarations.

| Type | Corresponding asset declaration type |
|---|---|
| `mesh_id` | `asset ... : mesh` |
| `texture_id` | `asset ... : texture` |
| `sound_id` | `asset ... : sound` |
| `music_id` | `asset ... : music` |
| `font_id` | `asset ... : font` |
| `material_id` | `asset ... : material` |

#### Scenario: mesh_id used as trait field type
- **WHEN** a trait declares `let mesh: mesh_id`
- **THEN** the type system resolves it to TypeInfo with kind=MeshId

#### Scenario: Opaque ID type cannot be constructed directly
- **WHEN** code attempts `let m: mesh_id = mesh_id()` or any literal construction
- **THEN** the semantic analyzer reports an error: `mesh_id` cannot be constructed directly; use an `asset` declaration

#### Scenario: All opaque ID types registered as built-ins
- **WHEN** the type system is initialized
- **THEN** `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, and `material_id` are all pre-registered as built-in opaque types

### Requirement: InputButton and InputAxis built-in types
The type system SHALL define two built-in handle types for input action declarations. These types are resolved from `input` declarations and cannot be constructed by user code.

| Type | Corresponding input declaration kind |
|---|---|
| `InputButton` | `input ... : button` |
| `InputAxis` | `input ... : axis` |

#### Scenario: InputButton resolved from button input declaration
- **WHEN** `input Jump: button` is declared and `Jump` is referenced in an expression
- **THEN** the type system resolves `Jump` to type `InputButton`

#### Scenario: InputAxis resolved from axis input declaration
- **WHEN** `input MoveX: axis` is declared and `MoveX` is referenced in an expression
- **THEN** the type system resolves `MoveX` to type `InputAxis`

#### Scenario: InputButton and InputAxis registered as built-ins
- **WHEN** the type system is initialized
- **THEN** `InputButton` and `InputAxis` are pre-registered as built-in opaque types
