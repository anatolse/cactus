## Purpose

Define the Cactus type system, including primitive and composite types, field modifier flags, expression type inference, string rvalue constraints, and built-in types such as asset opaque IDs, `InputButton`, and `InputAxis`.

## Requirements
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

### Requirement: Composite type support
The type system SHALL support user-defined `struct` (value objects, fields only), `enum` (named integer constants), and `list[T]` (parameterized ordered collection). Every user-defined type reference SHALL carry the resolved type symbol identity for the referenced struct, enum, or trait rather than an alias-qualified or simple source spelling.

#### Scenario: Struct type declaration
- **WHEN** source declares `module game.items` and `struct Item:` with fields `name: string` and `price: int`
- **THEN** the type system registers a Struct type with symbol identity `game.items.Item` and two fields

#### Scenario: List parameterized type
- **WHEN** a field is declared as `var items: list[items.Item]` through an import alias that resolves to module `game.items`
- **THEN** the type system resolves it to TypeInfo with kind=List and an element type carrying the symbol identity `game.items.Item`

#### Scenario: Enum type
- **WHEN** source declares `module game.ai` and `enum State:` with variants `Idle`, `Walking`, `Running`
- **THEN** the type system registers an Enum type with symbol identity `game.ai.State` and three integer-valued variants

### Requirement: Field modifier flags
The type system SHALL track field modifiers as boolean flags on TypeInfo: `is_let`, `is_persist`, and `is_pub`.

#### Scenario: Combined modifiers
- **WHEN** a field is declared as `persist pub var score: int`
- **THEN** the TypeInfo has is_persist=true, is_pub=true, is_let=false

#### Scenario: Let field immutability
- **WHEN** a field is declared as `let max_health: int = 100`
- **THEN** the TypeInfo has is_let=true and the field cannot be reassigned after creation

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

### Requirement: Expression type inference
The type system SHALL infer types for expressions, including binary operation result types and function call return types.

#### Scenario: Binary operation type
- **WHEN** the expression `health - damage` is evaluated where both are `int`
- **THEN** the type system infers the result type as `int`

#### Scenario: Function call return type
- **WHEN** the expression `math.abs(delta)` is evaluated where `delta` is `float`
- **THEN** the type system infers the result type as `float`

### Requirement: Conditions and logical operands are bool
The condition of an `if` statement, of each `else if`, and of each branch of an `if` expression SHALL have type `bool`. The operands of `and`, `or` and `not` SHALL have type `bool`. Any other type SHALL be a compile error at the condition or operand, naming the type found. No implicit truthiness conversion exists for numbers, vectors, handles or strings.

#### Scenario: Int condition is rejected
- **WHEN** a handler contains `if t.hp:` and `t.hp` is an `int`
- **THEN** the semantic analyzer reports that an `if` condition must be `bool` but is `int`

#### Scenario: Non-bool logical operand is rejected
- **WHEN** a handler contains `if ready and t.hp:` where `ready` is `bool` and `t.hp` is `int`
- **THEN** the semantic analyzer reports that the right operand of `and` must be `bool` but is `int`

#### Scenario: Comparison condition is accepted
- **WHEN** a handler contains `if t.hp > 0:`
- **THEN** the condition type-checks as `bool`

### Requirement: Conditional expression result types
An `if` expression SHALL have the type of its result expressions, and every result expression, the `else` one included, SHALL have the same type. A `match` expression SHALL have the type of its arm expressions, and every arm expression SHALL have the same type. Mixing types, `int` and `float` included, SHALL be a compile error naming both types; there is no implicit conversion between branches.

An `if` or `match` expression is pure when its condition or subject, its patterns and all of its result expressions are pure. A pure conditional expression is accepted wherever a pure expression is: `const` values, `where:`, `when:`, `order by:`, `limit:` counts, `reduce:` inputs, `keep` field values and template arguments. Its subject and conditions are evaluated once, and only the selected result expression is evaluated.

#### Scenario: If expression type
- **WHEN** a handler contains `let speed = if running: 4.0 else: 1.5`
- **THEN** `speed` has type `float`

#### Scenario: Mismatched if branches are rejected
- **WHEN** a handler contains `let x = if ready: 1 else: 2.0`
- **THEN** the semantic analyzer reports that the branches have types `int` and `float`

#### Scenario: Match expression type
- **WHEN** a handler contains `let s = match t.mode:` with arms `Mode.Idle => 0.0` and `Mode.Run => 2.0`
- **THEN** `s` has type `float`

#### Scenario: Conditional expression in a constant
- **WHEN** a `const:` block contains `STEP = if FAST: 2.0 else: 1.0` and `FAST` is a `bool` constant
- **THEN** the constant is accepted as a const expression with value `2.0` when `FAST` is `true`

#### Scenario: Only the selected branch is evaluated
- **WHEN** an `if` expression's condition is `false` and its then-expression calls an extern function
- **THEN** that call does not happen

### Requirement: Arithmetic operands are not bool
The operands of `+`, `-`, `*`, `/` and `%`, and the operand of unary `-`, SHALL NOT have type `bool`. A `bool` operand SHALL be a compile error at the expression, naming the operator and the operand types. There is no implicit conversion from `bool` to a number. Comparisons with `==` and `!=` on `bool` are unaffected.

#### Scenario: Int plus bool is rejected
- **WHEN** a handler contains `let y = 1 + true`
- **THEN** the semantic analyzer reports that there is no operator `+` for operand types `int` and `bool`

#### Scenario: Bool times float is rejected
- **WHEN** a handler contains `let s = alive * 2.0` and `alive` is `bool`
- **THEN** the semantic analyzer reports that there is no operator `*` for operand types `bool` and `float`

#### Scenario: Negating a bool is rejected
- **WHEN** a handler contains `let n = -ready` and `ready` is `bool`
- **THEN** the semantic analyzer reports that there is no operator `-` for operand type `bool`

#### Scenario: Bool equality is accepted
- **WHEN** a handler contains `if ready == true:`
- **THEN** the condition type-checks as `bool`
