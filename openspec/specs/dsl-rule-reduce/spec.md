# dsl-rule-reduce Specification

## Purpose

Define deterministic, typed global and outer-grouped aggregation over rule domains, so a rule can answer "per entity, over many others" questions (totals, minimums, whether any match exists) without reset rules, emit/apply plumbing or query loops.
## Requirements
### Requirement: Typed reduction clauses

A regular unary or pair rule SHALL accept one `reduce:` clause. It SHALL declare immutable, uniquely named aggregate values using `count()`, `count(binding)`, `sum(expression)`, `min(expression, default = value)`, `max(expression, default = value)`, `any(expression)`, `first_hit(expression)` or `best(binding, by = expression)`. `count(binding)` SHALL count rows, not distinct entities, and SHALL require a declared pair binding; `count()` SHALL count rows in either domain form. `sum` input SHALL be `int`, `float`, `vec2` or `vec3` and SHALL preserve its type; a `float` `sum` SHALL be exact, and a vector `sum` SHALL be exact per component (see "Reduction numeric edge behavior"). `min`/`max` inputs SHALL be `int` or `float` and preserve their type, and `min`/`max` SHALL require a default of exactly that type. `any` input SHALL be `bool` and its result SHALL be `bool`. `first_hit` input SHALL be `std.physics.volume.SweepHit` and its result SHALL be `SweepHit`. `best` SHALL require a declared pair binding other than the `per` binding, its `by` key SHALL be `int` or `float`, and its result SHALL be `entity_id`: the binding's entity in the row with the highest key; among equal keys, the row first in stable fold order; rows whose key is NaN SHALL be ignored; with no eligible row, a stale `entity_id`. Reducer expressions SHALL be pure; they MAY read the rule's bindings, constants and named entity fields. Selectionless and extern rules SHALL reject `reduce:`.

#### Scenario: Count and sum
- **WHEN** three input rows have integer scores 2, 3 and 5
- **THEN** count yields 3 and sum yields integer 10

#### Scenario: Vector sum
- **WHEN** three input rows have `vec3` pushes `(1,0,0)`, `(0,2,0)` and `(1,0,-1)`
- **THEN** sum yields `vec3(2.0, 2.0, -1.0)`

#### Scenario: Any over rows
- **WHEN** a group has three rows and the `any` input is true for only the second
- **THEN** `any` yields true

#### Scenario: First hit over rows
- **WHEN** a group has three rows whose sweeps miss, hit at `t = 0.6`, and hit at `t = 0.2`
- **THEN** `first_hit` yields the hit at `t = 0.2`

#### Scenario: Best picks the highest key
- **WHEN** a body's group has zone rows with priorities 1, 5 and 3
- **THEN** `best(zone, by = zone[CameraZone].priority)` yields the zone with priority 5

#### Scenario: Best tie and empty input
- **WHEN** two rows share the highest key, or a group has no row
- **THEN** `best` yields the earlier row's entity in stable fold order, or a stale `entity_id` for the empty group

#### Scenario: Bad reducer
- **WHEN** `collect`, an unknown reducer, a mismatched default, a vector `min`/`max` input, a non-bool `any` input, a non-`SweepHit` `first_hit` input, a `best` over the `per` binding or with a non-numeric key, or an impure expression is used
- **THEN** the compiler reports a source-located error

#### Scenario: Invalid domain
- **WHEN** a selectionless rule declares `reduce:`
- **THEN** the compiler rejects it

### Requirement: Global and outer-grouped empty-input behavior

A `reduce:` clause without a `per` grouping SHALL produce exactly one global aggregate row, even for empty input. `reduce per binding:` SHALL be allowed only for a declared pair binding and SHALL produce exactly one group for every entity in that binding's membership snapshot that passes the group filter (see "`where:` splits into group and row filters"), whether or not any row survives for it. Identity values SHALL apply to empty input: `count` and `sum` zero (the zero vector for a vector `sum`), `min`/`max` their default, `any` false, `first_hit` the miss value that `sweep` itself returns (`hit = false`, `t = 1.0`, stale `other`, zero `point` and `normal`).

#### Scenario: Global empty input
- **WHEN** no enemies survive the filter
- **THEN** the global handler runs once with count 0 and sum 0

#### Scenario: Empty min
- **WHEN** global min has no inputs and default is 7.0
- **THEN** the result is 7.0

#### Scenario: Last team member leaves
- **WHEN** a team binding's entity no longer has any member row
- **THEN** that team still gets one reduction activation, with count 0 and sum 0

#### Scenario: Enemy with no wall nearby
- **WHEN** a rule over `pairs: enemy, wall` reduces `blocked = any(...)` per enemy and no wall row survives `where:` for one enemy
- **THEN** that enemy's handler runs once with `blocked` false

#### Scenario: Bullet with nothing in its path
- **WHEN** a rule over `pairs: bullet, target` reduces `first = first_hit(physics.sweep(bullet, step, target))` per bullet and no target lies in one bullet's path
- **THEN** that bullet's handler runs once with `first.hit` false

#### Scenario: Body touching nothing
- **WHEN** a rule over `pairs: body, other` reduces `push = sum(physics.push_out(body, other))` per body and one body overlaps nothing
- **THEN** that body's handler runs once with `push` equal to `vec3(0.0, 0.0, 0.0)`

#### Scenario: Group multiplicity
- **WHEN** three member rows share a team binding
- **THEN** one aggregate row is produced for that team

### Requirement: `where:` splits into group and row filters

When a rule declares `reduce per binding:`, each `where:` predicate whose pair-binding reads all root at the `per` binding SHALL filter groups: an entity failing it gets no group. Every other `where:` predicate SHALL filter rows only. Without `per`, every `where:` predicate SHALL filter rows.

#### Scenario: Group-only predicate removes the group
- **WHEN** a rule uses `reduce per team:` and its `where:` contains `team[Team].active`
- **THEN** an inactive team gets no reduction activation

#### Scenario: Row predicate keeps the group
- **WHEN** a rule uses `reduce per enemy:` and its `where:` contains a predicate reading both `enemy` and `wall`
- **THEN** an enemy for which the predicate rejects every row still gets one activation with identity values

### Requirement: Reduction scope and deterministic pipeline

The logical pipeline SHALL be domain → `where:` → `reduce:` → `order by:` → `limit:` → handler. All participating field values SHALL be captured in one input snapshot before any reduced handler runs. Rows SHALL fold in stable domain order; groups SHALL be ordered by the `per` binding's creation order. The result of `count`, `sum`, `min`, `max` and `any` SHALL NOT depend on row order, so a backend MAY split, reorder or parallelize these folds. `first_hit` and `best` SHALL break ties by stable fold order. Handlers and post-reduction sort keys SHALL see the aggregates and the retained `per` binding, but no eliminated binding. The retained `per` binding SHALL be writable in the reduced handler: direct assignment to its traits SHALL be accepted, because the handler runs exactly once per group entity. Global limits SHALL operate on aggregate rows; per-binding limits SHALL be rejected when `reduce:` is present.

#### Scenario: Rank teams
- **WHEN** teams are ordered by total descending and limit is 2
- **THEN** at most two aggregate handlers run, with creation-order ties

#### Scenario: Eliminated binding
- **WHEN** a grouped handler accesses the `player` binding eliminated by `reduce per team:`
- **THEN** compilation rejects the access

#### Scenario: Write through the retained binding
- **WHEN** a handler grouped by `reduce per enemy:` assigns `enemy[Enemy].facing_yaw = 1.0`
- **THEN** compilation accepts it and the write applies to that enemy once per activation

#### Scenario: No live-value drift
- **WHEN** an aggregate handler writes a value that is an input to a later group's aggregate
- **THEN** all aggregates retain values from the input snapshot

#### Scenario: Invalid combined limit
- **WHEN** `reduce:` is combined with `limit: 1 per team`
- **THEN** compilation rejects the redundant per-binding form

#### Scenario: Float sum is independent of row order
- **WHEN** the same float inputs `1.0e8`, `1.0` and `-1.0e8` reach a `sum` in any row order
- **THEN** the result is `1.0` every time

### Requirement: Reduction numeric edge behavior

Integer `count` and `sum` SHALL wrap modulo 2³² exactly like integer `+` (see `dsl-numeric-semantics`), so the result does not depend on fold order. A `float` `sum` SHALL be the exact real sum of its inputs rounded once to the nearest binary32 value, ties to even; the result SHALL be NaN when any input is NaN or when inputs include both `+inf` and `-inf`, `±inf` when inputs include only that infinity, and `±inf` when the exact finite sum rounds beyond the binary32 range. An exact zero sum SHALL be `-0.0` only when every input is `-0.0`, and `0.0` otherwise. A vector `sum` SHALL apply the float rule to each component. Floating `min`/`max` SHALL propagate NaN when any input is NaN. Defaults SHALL apply only to empty input, not to non-finite values.

#### Scenario: Positive integer overflow
- **WHEN** integer sum folds 2147483647 followed by 1
- **THEN** it returns -2147483648

#### Scenario: Mixed-sign saturation order
- **WHEN** integer sum folds 2147483647, 1 and -1 in any order
- **THEN** it returns 2147483647, because wrapping does not depend on fold order

#### Scenario: Exact float sum
- **WHEN** a float sum folds `0.1` ten times
- **THEN** it returns the binary32 value nearest to the exact sum of those ten binary32 inputs

#### Scenario: Opposite infinities
- **WHEN** a float sum's inputs include `+inf` and `-inf`
- **THEN** it returns NaN

#### Scenario: Non-finite minimum
- **WHEN** a non-empty floating min input contains NaN
- **THEN** its result is NaN rather than the empty-input default

### Requirement: Reduced rule placement and triggers

With `reduce:`, `order by:` SHALL be written after the `reduce:` clause; an `order by:` before it SHALL be rejected. A lifecycle trigger (`on added`/`on removed`) SHALL be rejected on a reduced rule. A targeted event delivered to a rule with `reduce per binding:` SHALL produce at most the recipient's group; a global reduction SHALL ignore the target.

#### Scenario: Sort keys after reduce
- **WHEN** a reduced rule writes `order by: total desc` after `reduce:`
- **THEN** compilation accepts it and ranks aggregate rows by `total`

#### Scenario: Sort keys before reduce
- **WHEN** a reduced rule writes `order by:` before `reduce:`
- **THEN** the parser reports that `order by:` goes after `reduce:`

#### Scenario: Lifecycle trigger
- **WHEN** a reduced rule declares `on added Enemy:`
- **THEN** compilation rejects it

#### Scenario: Targeted event
- **WHEN** an event targeted at team A reaches a rule with `reduce per team:`
- **THEN** only team A's group runs, with aggregates over all of team A's rows

### Requirement: first_hit keeps the earliest hit deterministically

`first_hit` SHALL ignore rows whose input has `hit = false`, and SHALL keep the row with the smallest `t` among the rest. Among hits with equal `t`, it SHALL keep the one that comes first in stable fold order. A row's miss SHALL never replace a kept hit.

#### Scenario: Misses are ignored
- **WHEN** a group's rows are a hit at `t = 0.9` followed by two misses
- **THEN** `first_hit` yields the hit at `t = 0.9`

#### Scenario: Equal t resolves by fold order
- **WHEN** two rows hit at the same `t`
- **THEN** `first_hit` yields the row that comes first in stable domain order

### Requirement: Reduction grouping is declared in the header
A regular rule SHALL declare reduction grouping only in the clause header, as `reduce per binding:` with an indented, nonempty reducer declaration block. The header's binding SHALL obey the existing declared-pair-binding restriction. The ungrouped form SHALL remain `reduce:`. A `per: binding` line inside the reducer block SHALL be rejected with a diagnostic at that line telling the author to write `reduce per binding:`; for recovery the parser SHALL use that binding as the grouping when the header has none, so the rule produces no further errors from the removed spelling alone, and SHALL continue with the remaining reducers and later declarations. `per = count()` SHALL remain a reducer named `per`. Reducer names, typing, purity, and clause placement SHALL obey their existing rules.

#### Scenario: Header grouping
- **WHEN** a pair rule uses `reduce per team:` with reducers `players = count(player)` and `total = sum(player[Player].points)`
- **THEN** it produces one aggregate row per retained team, with the team binding writable and the player binding eliminated from handler scope

#### Scenario: Empty groups retain identities
- **WHEN** a retained team passes its group predicates but has no matching player rows
- **THEN** header grouping produces the aggregate row with existing reducer identities

#### Scenario: Body-level per names its replacement
- **WHEN** a `reduce:` block contains `per: team` followed by reducers
- **THEN** compilation reports one error at the `per:` line telling the author to use `reduce per team:`
- **AND** the remaining reducers and later declarations are still parsed

#### Scenario: Body-level per under a grouped header
- **WHEN** `reduce per team:` also contains `per: team` or `per: other` in its body
- **THEN** compilation reports the body `per:` line as a removed spelling and keeps the header grouping

#### Scenario: Reducer named per
- **WHEN** a `reduce:` block declares `per = count()`
- **THEN** compilation accepts it as a reducer named `per`

#### Scenario: Invalid grouped header is rejected
- **WHEN** the header omits its binding, names an unknown binding, uses grouping on a unary rule, or contains no reducers
- **THEN** compilation reports the corresponding grammar or grouping error without abandoning later declarations
