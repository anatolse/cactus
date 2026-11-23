# encoding-diagnostics Specification

## Purpose

Warn when a program encodes a sum or a link by hand in plain fields, and point the author to the language form that makes meaningless reads impossible.

## Requirements

### Requirement: Encoding warnings check plain trait fields

The encoding warnings SHALL check only fields of traits that have no dimensions. A field of a dimensioned trait, a state variant, a relation, an event, a phase or a struct SHALL get no encoding warning.

#### Scenario: Cell field behind a flag
- **WHEN** a dimensioned trait has a `bool` cell field `lit` and a cell field `level` read only inside `if c.lit:`
- **THEN** no encoding warning is reported

### Requirement: Fields guarded by a flag are warned

The compiler SHALL report a warning at a `bool` field `T.h` when there is a nonempty set `G` of other fields of `T` such that every read of every field in `G`, anywhere in the linked program, is a guarded read of `h` on the same value, at least one read exists, and a replacement form fits. The warning SHALL name `T.h`, list the fields in `G`, and help that suggests moving them into a trait whose presence means `h`, naming each form that fits: a `state` variant, a projected trait, or both.

#### Scenario: Ray result behind a flag
- **WHEN** `RayProbe` has `hit: bool`, an input-phase rule writes `hit`, `point`, `normal` and `cell` on every path, and later input-phase handlers read `point`, `normal` and `cell` only inside `if probe.hit:`
- **THEN** the compiler warns at `RayProbe.hit`, lists `point`, `normal` and `cell`, and the help suggests a projected trait

#### Scenario: A lasting flag read after the commit
- **WHEN** `Door.open` is set by an `on Opened` handler, `Door.opened_by` is read only inside `if door.open:`, and every reader runs in a later activation
- **THEN** the compiler warns at `Door.open` and the help suggests a `state` variant

#### Scenario: A lasting flag written by a phase handler
- **WHEN** `Body.grounded` is written by an `on tick` handler, `Body.ground_normal` is read only inside `if body.grounded:`, and every reader runs in a later frame
- **THEN** no flag-guarded warning is reported for `Body.grounded`

#### Scenario: A flag with no fitting form
- **WHEN** an `on Hit` handler sets `Shield.broken` and `Shield.break_point`, a handler scheduled after it in the same activation reads `break_point` inside `if shield.broken:`, and a `tick` handler of a later frame reads it the same way
- **THEN** no flag-guarded warning is reported for `Shield.broken`

#### Scenario: One unguarded read
- **WHEN** one handler reads `probe.point` without a `hit` guard
- **THEN** `point` is not in the listed set, and with no other guarded field no warning is reported

### Requirement: A guarded read has a syntactic form

A read SHALL count as guarded by `x.h` only when it is inside the true branch of an `if` whose condition is `x.h` or has `x.h` as a top-level `and` conjunct, after an `if not x.h: return` at the start of the same body, or in a rule whose `where:` or `when:` has `x.h` as a top-level conjunct. Reads through a `func` trait parameter SHALL count where the call is made.

#### Scenario: Guard through a conjunct
- **WHEN** a handler reads `probe.cell` inside `if probe.hit and input.pressed(Dig):`
- **THEN** the read counts as guarded by `probe.hit`

#### Scenario: Guard through a local
- **WHEN** a handler stores `let ok = probe.hit` and reads `probe.cell` inside `if ok:`
- **THEN** the read is not guarded, and `cell` is not in the listed set

### Requirement: The state form fits event-written flags read later

The state form SHALL fit a flag `h` with guarded set `G` when every handler that writes `h` runs on an event or a lifecycle trigger, and no handler that reads `h` or a field of `G` can run after a handler that writes `h` in the same activation. When neither this form nor the projected form fits, or `h` is a `var` field that no handler writes, no flag-guarded warning SHALL be reported.

#### Scenario: A var flag set outside the program
- **WHEN** a library trait has `var playing: bool`, its fields are read only inside `if anim.playing:`, and no handler of the program writes `playing`
- **THEN** no flag-guarded warning is reported for it

#### Scenario: Readers in the writer's activation
- **WHEN** an `on Opened` handler writes `Door.open`, and a handler scheduled after it in the same activation reads `Door.opened_by` inside `if door.open:`
- **THEN** the state form does not fit for `Door.open`

### Requirement: The projected form fits per-frame results

The projected form SHALL fit a flag `h` with guarded set `G` when every write of `h` and of the fields of `G` is in handlers of one phase that has no `every:`, one of those handlers assigns `h` on every path of its body, and every read of `h` or of a field of `G` is in a handler of that phase scheduled after those writers or in a phase that runs later in the frame.

#### Scenario: A periodic writer
- **WHEN** a `fixed_tick` handler writes `target.has_target` and `target.selected` on every path, and a later `fixed_tick` handler reads `selected` inside `if target.has_target:`
- **THEN** the projected form does not fit, because a projection would outlive the repetition

#### Scenario: A write on one path only
- **WHEN** the only input-phase writer assigns `probe.hit` inside an `if`, and readers run after it
- **THEN** the projected form does not fit for `probe.hit`

### Requirement: Flags used only as filters are warned

The compiler SHALL report a warning at a `let bool` field `T.f` when every read of it in the linked program is a top-level `where:` or `when:` conjunct `x.f` or `not x.f`; it gets values only where a value of `T` is made. The help SHALL suggest a marker trait that rules select with `each:` or `without:`. A `var` field SHALL not be warned, because a marker that changes often costs structural commands.

#### Scenario: Enabled flag
- **WHEN** `let enabled: bool` of `Probe` is read only as `where: p.enabled`
- **THEN** the compiler warns at `Probe.enabled` and suggests a marker trait

#### Scenario: A var flag
- **WHEN** the same flag is declared `var enabled: bool`
- **THEN** no filter-only warning is reported for it

### Requirement: Handles used only as joins are warned

The compiler SHALL report a warning at an `entity_id` field `H.f` when every read of it is a top-level `where:` equality between `x.f` and a pair binding whose required traits include a trait `U`, the same `U` for every read, and every value assigned to it would type as a link to `U` (`dsl-required-links`). The help SHALL suggest declaring `let f: U` or `var f: U` with an `on lost` handler. A field also used as a value, such as an `emit` target, SHALL not be warned.

#### Scenario: Order target joined by equality
- **WHEN** `Order.target` is read only in `where: target == order[Order].target` with `target: Unit`, and assigned only from bindings that require `Unit`
- **THEN** the compiler warns at `Order.target` and suggests `let target: Unit`

#### Scenario: Handle assigned from an event
- **WHEN** the same field is also assigned from an event's `entity_id` field
- **THEN** no joined-handle warning is reported

### Requirement: The standard library and examples are free of encoding warnings

Compiling any program that uses standard library modules SHALL report no encoding warning located in a standard library file. Every maintained example SHALL compile with no encoding warning. A test SHALL check both.

#### Scenario: A stdlib trait gains a guarded field
- **WHEN** a change adds a stdlib field read only under a flag, in a place where a form fits
- **THEN** the stdlib warning test fails
