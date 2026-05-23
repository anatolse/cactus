## Purpose
Define the required behavior of the `std.text` standard library module, which provides compiler-recognized text formatting via a C++20-style `format` intrinsic.

## Requirements

### Requirement: `std.text` provides C++20-style text formatting
The stdlib SHALL provide a `std.text` module containing a compiler-recognized `format` operation. `format` SHALL accept a string-literal format string followed by zero or more formatting values and SHALL return `string`.

`format` SHALL be pure from the Cactus language perspective and SHALL be callable from both pure `func` declarations and system event handlers. The operation SHALL be recognized through normal `std.text` import usage, including unaliased import calls such as `format(...)` and aliased member calls such as `text.format(...)` after `use std.text as text`.

`format` is a stdlib intrinsic rather than a normal fixed-arity `extern func`; the first version does not require or introduce general variadic function declarations.

#### Scenario: Format with automatic placeholders
- **WHEN** authored code imports `std.text` and calls `format("HP: {}/{}", hp, max_hp)` with supported argument types
- **THEN** the expression is accepted and has type `string`

#### Scenario: Format through alias
- **WHEN** authored code imports `std.text as text` and calls `text.format("Score: {}", score)`
- **THEN** the expression is accepted and has type `string`

#### Scenario: Format with no replacement fields
- **WHEN** authored code calls `text.format("Ready")`
- **THEN** the expression is accepted and produces the literal text as a `string`

### Requirement: Format strings support automatic and manual positional replacement fields
`std.text.format` format strings SHALL support C++20-style automatic replacement fields (`{}` and `{:spec}`) and manual positional replacement fields (`{0}`, `{1}`, `{0:spec}`). Escaped braces SHALL be written as `{{` and `}}`. A single format string SHALL NOT mix automatic and manual positional replacement fields.

#### Scenario: Manual positional fields can reorder arguments
- **WHEN** authored code calls `text.format("{1} / {0}", first, second)`
- **THEN** the expression is accepted and the backend formats `second` before `first`

#### Scenario: Escaped braces are not placeholders
- **WHEN** authored code calls `text.format("{{{} }}", value)`
- **THEN** only the unescaped replacement field consumes an argument

#### Scenario: Mixed placeholder modes rejected
- **WHEN** authored code calls `text.format("{} {1}", a, b)`
- **THEN** semantic analysis reports that automatic and manual positional placeholders cannot be mixed

### Requirement: Format argument types are restricted to supported scalar values in v1
The first version of `std.text.format` SHALL accept formatting arguments whose analyzed type is directly backend-format-ready: `int`, `float`, `bool`, `string`, `entity_id`, and backend scalar handle types for assets/input where applicable. It SHALL reject aggregate or user-defined values without agreed formatters, including `vec2`, `vec3`, `quat`, `color`, `list[...]`, structs, and enum classes.

#### Scenario: Scalar arguments accepted
- **WHEN** authored code calls `text.format("{} {} {}", name, score, alive)` where the arguments are `string`, `int`, and `bool`
- **THEN** semantic analysis accepts the call

#### Scenario: Vector argument rejected
- **WHEN** authored code calls `text.format("pos={}", position)` where `position` has type `vec2`
- **THEN** semantic analysis reports that `vec2` is not supported by `std.text.format` in v1

### Requirement: Text formatting does not generate localization resources
`std.text.format` SHALL perform formatting only. It SHALL NOT generate translation resources, JSON catalogs, plural-form entries, or locale lookup metadata.

#### Scenario: Format call has no localization side effect
- **WHEN** authored code calls `text.format("Score: {}", score)`
- **THEN** compilation does not emit a translation-resource entry for that format string
