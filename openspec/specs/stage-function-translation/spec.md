# stage-function-translation Specification

## Purpose
Define which functions a drawing rule's stage handler may call and what results they give when the stage runs as a shader: any pure function works, integers are exact, floats are close.

## Requirements

### Requirement: Any pure function is callable from a stage
A stage handler SHALL be able to call a `func` of any module whose body uses only: `let` and `var` locals; assignment and compound assignment to locals; `if`/`else` and the `if` expression; value `match`; `return`, including an early one; a bounded `for` over a range with constant bounds; struct construction and field reads; and calls to functions that themselves qualify or to extern functions that have a shader translation.

#### Scenario: Function with locals and an early return
- **WHEN** a fragment handler calls a `func` that declares locals, returns early for an out-of-range input and otherwise returns a computed value
- **THEN** the program compiles and the stage gets the same value an ordinary handler would, within the float rule below

#### Scenario: Noise in a stage
- **WHEN** a fragment handler calls `noise.perlin2(f.uv * 8.0, 1)` and `noise.voronoi2(f.uv * 8.0, 1).nearest`
- **THEN** the program compiles and the quad shows the noise

#### Scenario: Easing in a stage
- **WHEN** a vertex handler of a rule with `each: Pulse as pulse` calls `ease.apply(pulse.curve, pulse.at)`
- **THEN** the program compiles

#### Scenario: Constant loop
- **WHEN** a function called from a stage sums four octaves with `for i in range(0, 4):`
- **THEN** the program compiles

### Requirement: Untranslatable calls are reported at the stage
A stage call to a function that does not qualify SHALL be a source-located error at the stage's call site that names the innermost cause. A `func` with a trait or event parameter SHALL NOT qualify.

#### Scenario: Run-time loop bound
- **WHEN** a function called from a stage loops over `range(0, n)` with `n` an argument
- **THEN** the compiler reports an error at the stage's call saying the loop bound is not constant

#### Scenario: Untranslatable callee
- **WHEN** a stage calls `f`, which calls `g`, which formats a string
- **THEN** the compiler reports an error at the stage's call that names `g` and the string operation

#### Scenario: Trait parameter
- **WHEN** a stage calls a `func` that takes a trait parameter
- **THEN** the compiler reports an error at the stage's call saying the function takes a trait or event parameter

### Requirement: Standard math is available in stages
Every scalar function of `std.math` and every function of `std.math.vec2` and `std.math.vec3` SHALL be callable from a stage handler. Where a function's `std.math` rule for an edge input differs from the shader built-in, the stage SHALL follow the `std.math` rule.

#### Scenario: Trigonometry
- **WHEN** a vertex handler computes `vec2(math.cos(a), math.sin(a))`
- **THEN** the program compiles and the quad turns by `a`

#### Scenario: Rounding to int
- **WHEN** a fragment handler evaluates `math.floor(-1.5)` and `math.round(x)` for NaN `x`
- **THEN** the results are `-2` and `0`, as in an ordinary handler

### Requirement: Stage float results are close to the CPU's
A `float` expression evaluated in a stage MAY differ from the ordinary handler's value in its last bits. A stage result SHALL NOT be readable by any rule.

#### Scenario: Float tolerance
- **WHEN** `math.sin(1.0)` and `noise.perlin2(vec2(0.5, 0.5), 0)` are evaluated in a stage and in an ordinary handler
- **THEN** each pair agrees within 1e-5

### Requirement: Fragment-only extern functions
An extern function MAY be marked as translatable for the fragment stage only. A call to it from a fragment handler, or from a `func` that only fragment handlers call, SHALL be accepted. A call from a vertex handler, an ordinary rule, or a `func` that either calls, SHALL be a source-located error at that call site that names the function and the fragment stage. `std.render.textures.sample` SHALL be such a function.

#### Scenario: Sample through a helper
- **WHEN** a fragment handler calls a `func` that calls `textures.sample`
- **THEN** the program compiles

#### Scenario: Sample from an ordinary rule
- **WHEN** a `tick` handler calls a `func` that calls `textures.sample`
- **THEN** the compiler reports an error at that call saying `sample` belongs to a fragment stage

#### Scenario: Sample from the vertex stage
- **WHEN** a vertex handler calls a `func` that calls `textures.sample`
- **THEN** the compiler reports an error at the vertex handler's call saying `sample` belongs to a fragment stage

### Requirement: Texture parameters
A `func` that a stage calls MAY take `texture_id` parameters. Inside it, such a parameter SHALL be used only as the `texture` argument of `sample` or as the argument of another `texture_id` parameter; any other use makes the function untranslatable. A `func` SHALL NOT return a `texture_id` to a stage, nor read an `asset` itself.

#### Scenario: Texture parameter
- **WHEN** a fragment handler calls `soft(sprite.texture, f.uv)` and `soft(tex: texture_id, uv: vec2) color` returns `textures.sample(tex, uv)`
- **THEN** the program compiles and each quad samples its own sprite's texture

#### Scenario: Texture returned
- **WHEN** a stage calls a `func` that returns its `texture_id` parameter
- **THEN** the compiler reports an error saying the func returns a value of type `texture_id`
