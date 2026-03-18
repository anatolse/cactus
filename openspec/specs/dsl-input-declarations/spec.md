## ADDED Requirements

### Requirement: Input declaration syntax
The parser SHALL accept `input` as a top-level declaration form. An `input` declaration binds a compile-time identifier to a logical input action of kind `button` or `axis`.

```ebnf
input_decl = [ "pub" ] "input" IDENTIFIER ":" ( "button" | "axis" ) NEWLINE INDENT
             { input_prop }
             DEDENT ;
input_prop = IDENTIFIER "=" expression NEWLINE ;
```

The keyword `input` SHALL be added to the keyword list and to the `declaration` production.

#### Scenario: Button input declaration parsed
- **WHEN** the following appears at the top level:
  ```
  input Jump: button
      key    = Key.Space
      gamepad = GamepadButton.South
  ```
- **THEN** the parser produces an `InputDecl` node with `name = "Jump"`, `kind = button`, and two `InputProp` entries

#### Scenario: Axis input declaration parsed
- **WHEN** the following appears at the top level:
  ```
  input MoveX: axis
      negative = Key.A
      positive = Key.D
      gamepad  = GamepadAxis.LeftX
  ```
- **THEN** the parser produces an `InputDecl` node with `name = "MoveX"`, `kind = axis`, and three `InputProp` entries

#### Scenario: Pub input declaration parsed
- **WHEN** `pub input Fire: button` with a body appears at the top level
- **THEN** the parser produces an `InputDecl` with `is_pub = true`

#### Scenario: Input declaration with no properties accepted
- **WHEN** `input Unused: button` appears with an empty indented body
- **THEN** the parser accepts it (a button with no bindings is valid)

### Requirement: Valid property keys for button and axis inputs
The semantic analyzer SHALL validate that `input_prop` keys match the expected set for the input kind.

**Valid keys for `button`:** `key`, `mouse`, `gamepad`
**Valid keys for `axis`:** `negative`, `positive`, `gamepad`, `mouse_delta_x`, `mouse_delta_y`, `invert`

#### Scenario: Valid button properties accepted
- **WHEN** a `button` input uses `key`, `mouse`, and `gamepad` properties
- **THEN** the semantic analyzer accepts all three

#### Scenario: Invalid property key rejected
- **WHEN** a `button` input uses `negative = Key.A` (an axis-only key)
- **THEN** the semantic analyzer reports an error: `negative` is not a valid property for `button` inputs

#### Scenario: Axis invert property
- **WHEN** an `axis` input includes `invert = true`
- **THEN** the semantic analyzer accepts `invert` with a `bool` value

### Requirement: Input property values reference std.input enum constants
Property values in `input` declarations SHALL reference enum constants defined in `std.input`. The recognized constant namespaces are: `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis`.

#### Scenario: Key enum constant accepted
- **WHEN** `key = Key.Space` appears in a button input declaration
- **THEN** the semantic analyzer accepts the value as a valid `Key` constant

#### Scenario: GamepadAxis constant accepted
- **WHEN** `gamepad = GamepadAxis.LeftX` appears in an axis input declaration
- **THEN** the semantic analyzer accepts the value as a valid `GamepadAxis` constant

#### Scenario: Unknown constant rejected
- **WHEN** `key = Key.Unknown999` appears in a button input declaration
- **THEN** the semantic analyzer reports an error: `Key.Unknown999` is not a known key constant

### Requirement: Input identifiers resolve to InputButton or InputAxis types
The semantic analyzer SHALL resolve `button` input declaration names to type `InputButton` and `axis` input declaration names to type `InputAxis` at all use sites.

#### Scenario: Button name used in std.input.pressed call
- **WHEN** `input.pressed(Jump)` is called and `Jump` is declared as `input Jump: button`
- **THEN** the semantic analyzer resolves `Jump` to type `InputButton` and accepts the call

#### Scenario: Axis name used in std.input.axis call
- **WHEN** `input.axis(MoveX)` is called and `MoveX` is declared as `input MoveX: axis`
- **THEN** the semantic analyzer resolves `MoveX` to type `InputAxis` and accepts the call

#### Scenario: Axis name used where InputButton expected, rejected
- **WHEN** `input.pressed(MoveX)` is called and `MoveX` is declared as `input MoveX: axis`
- **THEN** the semantic analyzer reports a type mismatch error

### Requirement: std.input query functions
The `std.input` module SHALL define the following public functions queryable from any handler:

| Function signature | Description |
|--------------------|-------------|
| `pub func pressed(b: InputButton) -> bool` | True on the first frame the action is pressed |
| `pub func down(b: InputButton) -> bool` | True every frame the action is held |
| `pub func released(b: InputButton) -> bool` | True on the first frame the action is released |
| `pub func axis(a: InputAxis) -> float` | Returns a value in [-1.0, 1.0] |
| `pub func axis2(x: InputAxis, y: InputAxis) -> vec2` | Returns a `vec2` of two axis values |

#### Scenario: pressed() returns bool
- **WHEN** the expression `input.pressed(Jump)` is type-checked
- **THEN** the type system resolves the return type as `bool`

#### Scenario: axis2() returns vec2
- **WHEN** the expression `input.axis2(MoveX, MoveY)` is type-checked and both arguments are `InputAxis`
- **THEN** the type system resolves the return type as `vec2`

#### Scenario: Calling query functions outside std.input import
- **WHEN** a system calls `input.pressed(Jump)` without `use std.input`
- **THEN** the semantic analyzer reports an undeclared identifier error for `input`

### Requirement: Input declarations participate in module visibility
An `input` declaration without `pub` SHALL be module-private. A `pub input` declaration SHALL be importable by other modules via `use`.

#### Scenario: Non-pub input not accessible to importer
- **WHEN** module A declares `input Jump: button` (no `pub`) and module B uses `A.Jump` in a query
- **THEN** the semantic analyzer reports that `Jump` is not exported from module A

#### Scenario: Pub input accessible to importer
- **WHEN** module A declares `pub input Jump: button` and module B imports A
- **THEN** `Jump` is available in module B with type `InputButton`
