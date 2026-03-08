## ADDED Requirements

### Requirement: Primitive type support
The type system SHALL support the following primitive types: `int` (32-bit signed), `float` (64-bit), `bool`, `string` (UTF-8 immutable), `vec2`, `vec3`, `quat`, `color`, and `entity_id`.

#### Scenario: Primitive type in field declaration
- **WHEN** a field is declared as `var health: int`
- **THEN** the type system resolves it to TypeInfo with kind=Int

#### Scenario: Vec3 type
- **WHEN** a field is declared as `var position: vec3`
- **THEN** the type system resolves it to TypeInfo with kind=Vec3, representing `{ x: float, y: float, z: float }`

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
The type system SHALL track field modifiers as boolean flags on TypeInfo: `is_let` (immutable, set once), `is_persist` (serialization marker), `is_sync` (network replication marker), and `is_pub` (public visibility).

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
The `string` type SHALL be rvalue-only — it can appear in `const` blocks or as computed expressions, but never as inline literals in logic blocks.

#### Scenario: String const reference in logic
- **WHEN** a system handler references a const identifier `SHOP_TITLE` that was declared as a string in a `const` block
- **THEN** the type system resolves the reference type as `string` (via const pool ID)
