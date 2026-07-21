## ADDED Requirements

### Requirement: ResolvedField carries propagated literal default
The semantic analyzer SHALL populate `ResolvedField.default_cpp_literal` from the field's `FieldNode.default_value` when the default expression is a simple literal. Supported literal kinds: `bool`, `int`, `float`, and `color` (`#RRGGBBAA`). Unsupported or absent defaults leave `default_cpp_literal` as `nullopt`. The serialized string SHALL be a valid C++ brace-initializer value (e.g. `"true"`, `"42"`, `"1.0F"`, `"{.r=255,.g=128,.b=0,.a=255}"`).

#### Scenario: Float literal default is propagated
- **WHEN** a trait field is declared `var speed: float = 1.0`
- **THEN** `ResolvedField.default_cpp_literal` is `"1.0F"`

#### Scenario: Bool literal true default is propagated
- **WHEN** a trait field is declared `var playing: bool = true`
- **THEN** `ResolvedField.default_cpp_literal` is `"true"`

#### Scenario: Bool literal false default is propagated
- **WHEN** a trait field is declared `var active: bool = false`
- **THEN** `ResolvedField.default_cpp_literal` is `"false"`

#### Scenario: Int literal default is propagated
- **WHEN** a trait field is declared `var clip: int = 0`
- **THEN** `ResolvedField.default_cpp_literal` is `"0"`

#### Scenario: Field with no default yields nullopt
- **WHEN** a trait field is declared `var x: float` with no default expression
- **THEN** `ResolvedField.default_cpp_literal` is `nullopt`

#### Scenario: Complex expression default yields nullopt
- **WHEN** a trait field has a default expression that is not a simple literal (e.g. a function call or composite expression)
- **THEN** `ResolvedField.default_cpp_literal` is `nullopt` and no error is reported
