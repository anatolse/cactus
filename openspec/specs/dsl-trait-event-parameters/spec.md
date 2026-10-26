# dsl-trait-event-parameters Specification

## Purpose
Lets a pure `func` take a read-only reference to a trait or an event, so predicates and handlers pass one name instead of unpacking many fields, while every world read stays declared and exact.

## Requirements
### Requirement: Func parameters may name a trait or an event
A user `func` parameter SHALL accept a trait type or an event type, local or reached through an import alias (`camera: cam.CameraView`, `hit: Hit`). A state variant (`EnemyMode.Chase`) SHALL count as a trait. The parameter SHALL be a read-only reference to the caller's trait or event, not a value.

The following SHALL be compile errors that name the parameter and the type:
- a marker trait, which has no fields;
- a state slot, a relation, a phase, or any other non-trait, non-event name;
- a trait or event type on an `extern func` parameter;
- a trait or event type as a `func` return type, a local's type annotation, a struct field type or a trait field type.

#### Scenario: Trait parameter accepted
- **WHEN** a module declares `func is_low(h: Health) bool:` returning `h.current < 30.0`
- **THEN** the module compiles

#### Scenario: Imported trait parameter accepted
- **WHEN** a module with `use std.transform.volume as tv` declares `func height(t: tv.WorldTransform) float:` returning `t.position.y`
- **THEN** the module compiles

#### Scenario: Event parameter accepted
- **WHEN** a module declares `func is_critical(hit: Hit) bool:` returning `hit.damage > 50`
- **THEN** the module compiles

#### Scenario: Marker trait parameter rejected
- **WHEN** a `func` declares a parameter `s: Spike` and `Spike` has no fields
- **THEN** compilation fails with an error that names `s` and `Spike`

#### Scenario: Extern func trait parameter rejected
- **WHEN** a module declares `extern func clamp_view(camera: CameraView) vec2`
- **THEN** compilation fails with an error that names `camera` and says an `extern func` can't take a trait or event parameter

#### Scenario: Trait return type rejected
- **WHEN** a `func` declares the return type `Health`
- **THEN** compilation fails with an error that names `Health` and says a trait is not a value type

### Requirement: A trait or event parameter is only read
Inside a `func`, a trait or event parameter SHALL be used only in two ways: reading a field, including a nested member (`camera.position`, `camera.position.x`), and passing it as an argument to another `func` parameter of the same trait or event type. Every other use SHALL be a compile error that names the parameter: assignment to it or to any of its fields or members, a compound assignment, use as a local's initializer, a `return` of it, comparison, use as an operand, a `match` subject, or passing it where a different type is expected.

#### Scenario: Field read accepted
- **WHEN** a `func` with parameter `camera: CameraView` returns `camera.rect_size.x > 0.0`
- **THEN** the module compiles

#### Scenario: Passing on accepted
- **WHEN** `func to_screen(camera: CameraView, point: vec3) Projected:` calls `has_area(camera)` and `has_area` takes `camera: CameraView`
- **THEN** the module compiles

#### Scenario: Field assignment rejected
- **WHEN** a `func` with parameter `h: Health` contains `h.current = 0.0`
- **THEN** compilation fails with an error that names `h` and says a trait parameter is read-only

#### Scenario: Storing in a local rejected
- **WHEN** a `func` with parameter `h: Health` contains `let copy = h`
- **THEN** compilation fails with an error that names `h`

#### Scenario: Wrong trait type rejected
- **WHEN** a `func` with parameter `h: Health` calls `g(h)` and `g` takes `a: Armor`
- **THEN** compilation fails with an error that names `Health` and `Armor`

### Requirement: Trait arguments are legal where their fields are readable
At a call to a `func` with a trait parameter, the argument SHALL be one of:
- an alias of an `each:` trait, or of a pair binding's trait;
- a bracketed access `binding[T]` on a pair binding;
- a named read `Entity[T]`;
- a trait-match arm alias;
- a trait parameter of the enclosing `func`.

The argument SHALL name the parameter's trait. The call SHALL be legal exactly where reading a field of that trait through the same expression would be legal at the call site. Every restriction on such a read SHALL apply unchanged, including `when:`'s game-wide-only reads, a binding removed by `reduce per`, and frame-local projected traits. The callee SHALL read the values that an inline field read at the call site would read at that point of the handler, including the handler's own earlier writes.

Any other argument expression SHALL be a compile error that names the parameter, including an `entity_id` value, a struct value and a call result. An `each:` trait without an alias is passed by giving it one (`each: Health as hp`).

#### Scenario: Alias argument in where
- **WHEN** a rule with `each: Health as hp` has `where: is_low(hp)`
- **THEN** the rule keeps exactly the entities whose `Health.current` is below 30.0

#### Scenario: Pair bracket argument
- **WHEN** a pair rule with bindings `camera` and `unit` has `where: cam.to_screen(camera[cam.CameraView], unit[tv.WorldTransform].position).in_front`
- **THEN** the module compiles, and the predicate reads `CameraView` of the `camera` binding

#### Scenario: Named read argument
- **WHEN** a handler calls `fits(vp.Screen[vp.ScreenSize])`
- **THEN** the module compiles, and the call reads `ScreenSize` of the named entity `Screen`

#### Scenario: Binding argument in when rejected
- **WHEN** a rule with `each: Health as hp` has `when: is_low(hp)`
- **THEN** compilation fails with the same error an inline `hp.current` read in `when:` reports

#### Scenario: entity_id argument rejected
- **WHEN** a handler calls `is_low(target)` where `target` is an `entity_id`
- **THEN** compilation fails with an error that names the parameter and says the argument must be a trait the rule binds

#### Scenario: Callee sees an earlier write
- **WHEN** a unary handler with `each: Health as hp` runs `hp.current = 10.0` and then `if is_low(hp): emit Warn`
- **THEN** `Warn` is emitted

### Requirement: Event arguments are the handler's event
At a call to a `func` with an event parameter, the argument SHALL be the alias of the event that triggered the enclosing handler (`on Hit as h` passes `h`), or an event parameter of the enclosing `func`. It SHALL name the parameter's event. Any other argument, including a call from `when:`, a keep field or a rule clause that can't read the event, SHALL be a compile error that names the parameter.

#### Scenario: Event alias argument
- **WHEN** a rule handles `on Hit as h:` and runs `if is_critical(h): emit Crit` for a `Hit` with `damage = 80`
- **THEN** `Crit` is emitted

#### Scenario: Different event rejected
- **WHEN** a handler `on Heal as h:` calls `is_critical(h)` and `is_critical` takes `hit: Hit`
- **THEN** compilation fails with an error that names `Hit` and `Heal`

### Requirement: Render-pass stage funcs take no trait or event parameters
A render-pass stage handler SHALL NOT call a `func` that has a trait or event parameter, directly or through another `func` or a constant. The call SHALL be rejected with an error that names the `func`.

#### Scenario: Stage handler call rejected
- **WHEN** a vertex-stage handler calls `func shade(t: Tint) color`
- **THEN** compilation fails with an error that names `shade`

### Requirement: Trait and event arguments are passed without a copy
Generated code SHALL pass trait and event arguments as read-only references, with no copy of the trait or event. Behavior SHALL be the same as if the read fields were written inline at the call site.

#### Scenario: Large trait in a pair loop
- **WHEN** a pair rule over 1,000 by 1,000 entities calls a `func` with a 9-field trait parameter in `where:`
- **THEN** the generated C++ passes the trait by `const` reference and copies no trait value per tuple
