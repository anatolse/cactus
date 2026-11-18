## Purpose

Show the shape of each step's cost as terms in entity populations, so authors can see which handlers grow with which part of the world without running the game.

## Requirements

### Requirement: The CLI reports cost terms per root
The CLI SHALL accept `--report-cost`. With it, the CLI SHALL print, for every phase and host-event root of the linked program (an event no handler emits), each handler the root can activate, in that root's canonical schedule order, with its canonical identity and its cost term. The canonical schedule order SHALL follow schedule edges and immediate deliveries and break ties by canonical identity. The report SHALL depend only on the linked program and SHALL NOT change the generated output. Without the option, nothing SHALL be printed.

#### Scenario: Report for a small game
- **WHEN** the user runs `cactus game.cactus --report-cost` on a program with one `tick` rule over `each: Unit`
- **THEN** the output lists the `tick` root and that handler with the term `N(Unit)`

#### Scenario: Output is stable
- **WHEN** the same program is compiled twice with `--report-cost`, once with its rules in another file order
- **THEN** both reports are identical

### Requirement: Cost terms name populations and factors
A handler's term SHALL be the product of its pass's rows and the world scans one row runs: scans in sequence add, and a scan inside a loop over another scan multiplies. The rows SHALL be `N(T)` for a unary domain driven by trait `T`, `N(*)` for a unary domain that requires no trait, `N(A)·k` for an accelerated pair join, `N(A)·N(B)` for a pair product, `1` for a selectionless or lifecycle handler, `cells(T)` per carrier for a rule over the cells of `T`, and `live(T: f)` per carrier when the rule keeps rest over the cell fields `f`; a pair of cells adds `·k`. A rule with `repeat: N` SHALL carry the factor `×N`. An event handler's term SHALL start with `per event:` and a lifecycle handler's with `per delivery:`. A population SHALL be shown as the number of named carriers when only named entities carry the trait, as `1` for a `single` trait, and SHALL be marked `never destroyed` when the compiler warned that a template carrying the trait is spawned repeatedly and never destroyed. The report SHALL NOT call a population unbounded.

#### Scenario: Accelerated join
- **WHEN** a `fixed_tick` pair rule joins `Unit` to `Tile` through a key lookup
- **THEN** its term is `N(Unit)·k`

#### Scenario: Per-row scan
- **WHEN** a `tick` rule over `each: Unit` calls `query.count[Unit]()` in its body
- **THEN** its term is `N(Unit)·N(Unit)`

#### Scenario: A rule that requires no trait
- **WHEN** an `unload` rule selects with only `without: KeepOnLoad`
- **THEN** its term is `per event: N(*)`

#### Scenario: A population that is never destroyed
- **WHEN** a recurring rule spawns `Spark` with no destroyer and another rule has `each: Spark`
- **THEN** that rule's term shows `N(Spark)` marked `never destroyed`

#### Scenario: A repeating rule
- **WHEN** a pair rule with an accelerated join declares `repeat: 3`
- **THEN** its term is `N(A)·k ×3`

#### Scenario: Rules over cells
- **WHEN** one rule over the cells of a `single` trait `Room` keeps rest over `gas` and another visits every cell
- **THEN** their terms are `live(Room: gas)` and `cells(Room)`

### Requirement: Keep rules show their evaluation level
The line of a keep rule SHALL show its evaluation level. A rule evaluated by changes SHALL show `keep delta` and a term in changed entities, written `Δ(T)`. A rule evaluated over its whole domain SHALL show `keep full`, its term in `N`, and the construct that forces full evaluation: a read beyond the row's own bindings (a single trait, a named entity, a link or a relation end), a relation test, a derived relation, a row binding, or a pair plan with no lookup from one end. The report SHALL say once that any keep rule is skipped when none of its inputs changed.

#### Scenario: A delta keep
- **WHEN** a unary keep rule reads only fields of its own row
- **THEN** its line shows `keep delta` and the term `Δ(Unit)`

#### Scenario: A full keep names its reason
- **WHEN** a keep rule's `where:` reads the named entity field `Door[Gate].open`
- **THEN** its line shows `keep full` and names that named entity read

#### Scenario: One added term changes the level
- **WHEN** a delta keep rule gains a `where:` term that reads a derived relation
- **THEN** the next report shows it as `keep full` and names the derived relation

### Requirement: Each root lists its depth and delayed cycles
For each root the report SHALL print its static cascade depth and each delayed cycle group it reaches, by the canonical identities of the group's handlers.

#### Scenario: A delayed cycle
- **WHEN** a `fixed_tick` root reaches two handlers that emit events to each other
- **THEN** the report lists one delayed cycle group with both handlers under `fixed_tick`
