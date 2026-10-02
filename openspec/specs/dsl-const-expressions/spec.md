## Purpose

Define module constants as checked compile-time values: which expressions a constant may use, how constants refer to each other across declaration order and modules, and how struct and list constants serve as data tables.

## Requirements

### Requirement: Const values are const expressions
A `const:` value SHALL be a *const expression*. A const expression is built only from: literals; enum variants; other module constants; unary and binary operators; the `vec2`, `vec3`, `color`, and `quat` constructors; struct construction; list literals; and calls to functions whose complete call graph is proven pure (the same purity analysis `where:` uses). Any other form SHALL be rejected at the offending sub-expression, including: a trait or field read, an entity name, `self`, an event or phase payload, a world query, `spawn`, and a call to a function whose effects are opaque, unknown, or not empty. A string literal SHALL be allowed only as a whole constant value or as a struct-construction field value; string operators are not const expressions.

#### Scenario: Arithmetic and pure calls are accepted
- **WHEN** a module declares `const:` with `HALF_PI = math.PI * 0.5` and `ROOT_TWO = math.sqrt(2.0)` after `use std.math as math`
- **THEN** compilation succeeds and both constants hold the computed values at runtime

#### Scenario: Pure user function is accepted
- **WHEN** a module declares a pure `func twice(v: float) float` that returns `v * 2.0`, and `const:` with `SIX = twice(3.0)`
- **THEN** compilation succeeds and `SIX` is `6.0`

#### Scenario: Trait read is rejected
- **WHEN** a `const:` value is `Ping.x` where `Ping` is a trait
- **THEN** compilation fails with a diagnostic at `Ping.x` stating that a constant cannot read a trait

#### Scenario: Impure call is rejected
- **WHEN** a `const:` value calls an `extern func` whose effects are not known to be empty
- **THEN** compilation fails with a diagnostic naming the call and stating that constants must be pure

#### Scenario: Vector constant is accepted
- **WHEN** a `const:` value is `vec2(1.0, 2.0)` and a handler reads that constant's `.x`
- **THEN** compilation succeeds and the handler gets `1.0`

### Requirement: Constants may reference each other in any order without cycles
A constant SHALL be able to reference any other constant of its own module regardless of declaration order. The compiler SHALL evaluate constants in dependency order. A reference cycle SHALL be a compile error that names every constant in the cycle.

#### Scenario: Forward reference is accepted
- **WHEN** a `const:` block declares `HALF = LATER * 0.5` followed by `LATER = 2.0`
- **THEN** compilation succeeds and `HALF` is `1.0`

#### Scenario: Cycle is rejected
- **WHEN** a `const:` block declares `A = B` and `B = A`
- **THEN** compilation fails with a diagnostic naming `A` and `B` as a constant cycle

### Requirement: Constants may declare a type
A constant SHALL accept an optional type annotation, `NAME: type = expression`. When present, the value's type SHALL match the annotation, otherwise compilation fails at the constant. When absent, the constant's type is the value's inferred type. A constant whose type cannot be inferred, such as an empty list literal, SHALL require an annotation.

#### Scenario: Matching annotation is accepted
- **WHEN** a `const:` block declares `SPEED: float = 2.0`
- **THEN** compilation succeeds and `SPEED` has type `float`

#### Scenario: Mismatched annotation is rejected
- **WHEN** a `const:` block declares `SPEED: int = 2.5`
- **THEN** compilation fails with a diagnostic stating that the value type `float` does not match the declared type `int`

#### Scenario: Empty list needs an annotation
- **WHEN** a `const:` block declares `NONE = []`
- **THEN** compilation fails with a diagnostic asking for a type annotation

#### Scenario: Annotated empty list is accepted
- **WHEN** a `const:` block declares `NONE: list[UnitDef] = []`
- **THEN** compilation succeeds and iterating `NONE` runs the loop body zero times

### Requirement: Constants are module-scoped
A constant SHALL belong to the module that declares it. Code in that module SHALL reference it by its bare name. Code in another module SHALL reference it through that module's import alias (`math.PI`). Every constant is visible to importing modules; `const:` blocks have no `pub` modifier. Two modules SHALL be able to declare constants with the same name without conflict. A bare name SHALL NOT resolve to another module's constant.

#### Scenario: Qualified stdlib constant
- **WHEN** a module imports `use std.math as math` and a handler reads `math.PI`
- **THEN** compilation succeeds and the value is approximately `3.14159265`

#### Scenario: Same name in two modules
- **WHEN** module `other` declares `SPEED = 2.0` and the main module, which imports `other`, declares `SPEED = 5.0`
- **THEN** compilation succeeds, a bare `SPEED` in the main module reads `5.0`, and `other.SPEED` reads `2.0`

#### Scenario: Bare name does not reach an imported constant
- **WHEN** the main module imports `std.math` and reads bare `PI` without declaring its own `PI`
- **THEN** compilation fails with an unknown-identifier diagnostic for `PI`

### Requirement: Struct and list constants serve as data tables
A constant SHALL be able to hold a struct value or a `list[T]` value, including a list of structs. A handler SHALL read a struct constant's fields with `.field`, and SHALL iterate a list constant with bounded `for`, in list order. Constant values are immutable; a write through a constant SHALL be rejected.

#### Scenario: Struct constant field read
- **WHEN** a `const:` block declares `ROBOT = UnitDef(speed = 4.0, health = 3, run_clip = 6)` and a handler reads `ROBOT.speed`
- **THEN** the handler gets `4.0`

#### Scenario: Iterating a list of structs
- **WHEN** a `const:` block declares `WAVES: list[UnitDef] = [ROBOT, UnitDef(speed = 2.5, health = 6, run_clip = 4)]` and a handler runs `for unit in WAVES:` and sums `unit.health`
- **THEN** the loop body runs twice, first for `ROBOT`, and the sum is `9`

#### Scenario: Writing through a constant is rejected
- **WHEN** a handler assigns `ROBOT.speed = 1.0`
- **THEN** compilation fails with a diagnostic stating that constants are immutable

### Requirement: Render-pass stage handlers read only GLSL-portable constants
A render-pass stage handler SHALL be able to read a constant whose type is `int`, `float`, `bool`, `vec2`, `vec3`, or `color` and whose value, including every constant it references, uses only GLSL-translatable operations. Reading any other constant from a stage handler SHALL be a semantic error that names the constant, reported before code generation.

#### Scenario: Derived scalar constant in a vertex handler
- **WHEN** a vertex-stage handler reads `HALF = SIZE * 0.5` where `SIZE = 200.0`
- **THEN** compilation succeeds and the shader uses the value `100.0` for `HALF`

#### Scenario: Struct constant in a stage handler is rejected
- **WHEN** a fragment-stage handler reads `ROBOT.speed` where `ROBOT` is a struct constant
- **THEN** compilation fails with a diagnostic naming `ROBOT` and the stage-handler constant restriction
