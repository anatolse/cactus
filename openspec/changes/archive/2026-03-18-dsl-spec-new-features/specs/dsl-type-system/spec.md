## ADDED Requirements

### Requirement: Asset opaque ID types
The type system SHALL define six built-in opaque handle types for external resources. These types are value types with no accessible fields and cannot be constructed by user code — they are only obtained via `asset` declarations.

| Type | Corresponding asset declaration type |
|------|--------------------------------------|
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
|------|--------------------------------------|
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

## MODIFIED Requirements

### Requirement: Primitive type support
The type system SHALL support the following primitive types: `int` (32-bit signed), `float` (64-bit), `bool`, `string` (UTF-8 immutable), `vec2`, `vec3`, `quat`, `color`, `entity_id`, `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`, `InputButton`, and `InputAxis`.

#### Scenario: Primitive type in field declaration
- **WHEN** a field is declared as `var health: int`
- **THEN** the type system resolves it to TypeInfo with kind=Int

#### Scenario: Vec3 type
- **WHEN** a field is declared as `var position: vec3`
- **THEN** the type system resolves it to TypeInfo with kind=Vec3, representing `{ x: float, y: float, z: float }`

#### Scenario: mesh_id primitive type in field declaration
- **WHEN** a field is declared as `let mesh: mesh_id`
- **THEN** the type system resolves it to TypeInfo with kind=MeshId

#### Scenario: InputButton primitive type in field declaration
- **WHEN** a field is declared as `let jump: InputButton`
- **THEN** the type system resolves it to TypeInfo with kind=InputButton

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
