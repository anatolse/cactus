## MODIFIED Requirements

### Requirement: EnTT component struct generation
The backend SHALL generate EnTT-compatible component structs for each resolved trait. Empty traits SHALL generate empty tag structs. The generated C++ type name SHALL be derived from the trait's resolved symbol identity and SHALL be deterministic and collision-free for distinct trait symbols. Each struct field SHALL be initialized with its propagated default value from `ResolvedField.default_cpp_literal` when present; fields with no propagated default SHALL be zero-initialized (`{}`). The backend SHALL NOT use hardcoded per-trait default tables.

#### Scenario: Trait becomes component
- **WHEN** the resolved semantic program contains trait symbol `game.components.Position` with fields `var x: float` and `var y: float`
- **THEN** the backend generates a collision-free C++ component struct derived from `game.components.Position`, such as `game_components__Position`, with fields `float x{}; float y{};`

#### Scenario: Empty trait becomes tag
- **WHEN** the resolved semantic program contains marker trait symbol `game.state.Alive`
- **THEN** the backend generates an empty tag component struct using the C++ name derived from `game.state.Alive`

#### Scenario: Same local trait name from different modules produces different C++ types
- **WHEN** traits `std.transform.flat.WorldTransform` and `std.transform.volume.WorldTransform` are both present
- **THEN** the backend generates two distinct C++ component type names

#### Scenario: Float field with non-zero default uses propagated initializer
- **WHEN** `ResolvedField` for `std.render.models.ModelAnimator.speed` carries `default_cpp_literal = "1.0F"`
- **THEN** the generated struct field is `float speed{1.0F};`

#### Scenario: Bool field with true default uses propagated initializer
- **WHEN** `ResolvedField` for a trait field `var playing: bool = true` carries `default_cpp_literal = "true"`
- **THEN** the generated struct field is `bool playing{true};`

#### Scenario: Field with no default is zero-initialized
- **WHEN** `ResolvedField` for a trait field `var x: float` has no `default_cpp_literal`
- **THEN** the generated struct field is `float x{};`

## REMOVED Requirements

### Requirement: Hardcoded stdlib field default table
**Reason**: Replaced by propagated `ResolvedField.default_cpp_literal`. The `stdlib_trait_default()` function is a fragile workaround that duplicates information already present in the stdlib DSL sources.
**Migration**: Ensure every stdlib trait field that previously relied on `stdlib_trait_default()` has its default declared in the corresponding `stdlib/**/*.cactus` source file before removing the function.
