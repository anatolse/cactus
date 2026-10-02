## Purpose

Define how authored code builds a struct value: a named-field construction expression that is type-checked and usable wherever an expression is.

## Requirements

### Requirement: Structs are constructed with named fields
A struct value SHALL be written `StructName(field = expression, ...)`, where `StructName` is a struct declared in the current module or reached through an import alias (`units.UnitDef(...)`). Every field of the struct SHALL be given exactly once, in any order; a trailing comma and line breaks between arguments SHALL be allowed. A missing field, an unknown field, a repeated field, or a positional argument SHALL be a compile error that names the struct and the field. Each value SHALL type-check against its field's declared type. The expression's type is the struct.

#### Scenario: Named construction type-checks
- **WHEN** `struct Squad:` declares `lead: entity_id` and `size: int`, and a handler writes `r.squad = Squad(size = 3, lead = other)`
- **THEN** compilation succeeds and the generated program stores a `Squad` with `lead = other` and `size = 3`

#### Scenario: Positional construction is rejected
- **WHEN** code writes `Squad(other, 3)`
- **THEN** compilation fails with a diagnostic stating that struct fields must be named

#### Scenario: Missing field is rejected
- **WHEN** code writes `Squad(lead = other)`
- **THEN** compilation fails with a diagnostic naming the missing field `size`

#### Scenario: Wrong field type is rejected
- **WHEN** code writes `Squad(lead = other, size = 2.5)`
- **THEN** compilation fails with a diagnostic stating that `float` does not match field `size` of type `int`

#### Scenario: Struct assigned to a non-struct place is rejected
- **WHEN** a handler assigns `r.count = Squad(lead = other, size = 3)` where `count` is an `int` field
- **THEN** compilation fails with a type-mismatch diagnostic

### Requirement: Struct construction works in every expression position
Struct construction SHALL be accepted, and SHALL produce compiling generated code, in: handler and `func` bodies (assignment, `let`/`var`, call arguments, return values, `emit` payloads), `add`, `set`, and `spawn` field blocks, template bodies and template arguments, entity bodies, trait field defaults, and `const:` values. Field values follow the rules of the position: in a `const:` value or a trait default they SHALL be const expressions.

#### Scenario: Construction in deferred command blocks
- **WHEN** a handler writes a `Squad(...)` value in an `add Roster to e:` block, a `set Roster on e:` block, and a `spawn Unit:` override block
- **THEN** compilation succeeds, the generated C++ compiles, and each command applies the constructed value at commit

#### Scenario: Construction in a template and an entity body
- **WHEN** a template body and an entity body each initialize `Roster.squad` with `Squad(lead = Boss, size = 1)` where `Boss` is a named entity
- **THEN** compilation succeeds and the generated C++ compiles

#### Scenario: Reading a field of a constructed value
- **WHEN** a handler writes `let n = Squad(lead = other, size = 3).size`
- **THEN** `n` is `3`

### Requirement: Struct values are copied
A struct value SHALL behave as a value: assigning it, passing it, storing it in a trait field, or reading it from a constant SHALL copy it. Changing one copy SHALL NOT change another.

#### Scenario: Copy from a constant
- **WHEN** a handler runs `var s = ROBOT` and then `s.speed = 1.0`
- **THEN** `ROBOT.speed` keeps its declared value
