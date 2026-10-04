# dsl-rule-reduce Specification

## Purpose

Define deterministic, typed global and outer-grouped aggregation over rule domains, so a rule can answer "per entity, over many others" questions (totals, minimums, whether any match exists) without reset rules, emit/apply plumbing or query loops.

## Requirements

### Requirement: Typed reduction clauses

A regular unary or pair rule SHALL accept one `reduce:` clause. It SHALL declare immutable, uniquely named aggregate values using `count()`, `count(binding)`, `sum(expression)`, `min(expression, default = value)`, `max(expression, default = value)`, `any(expression)` or `first_hit(expression)`. `count(binding)` SHALL count rows, not distinct entities, and SHALL require a declared pair binding; `count()` SHALL count rows in either domain form. `sum`/`min`/`max` inputs SHALL be `int` or `float` and preserve their type, and `min`/`max` SHALL require a default of exactly that type. `any` input SHALL be `bool` and its result SHALL be `bool`. `first_hit` input SHALL be `std.physics.volume.SweepHit` and its result SHALL be `SweepHit`. Reducer expressions SHALL be pure; they MAY read the rule's bindings, constants and named entity fields. Selectionless and extern rules SHALL reject `reduce:`.

#### Scenario: Count and sum
- **WHEN** three input rows have integer scores 2, 3 and 5
- **THEN** count yields 3 and sum yields integer 10

#### Scenario: Any over rows
- **WHEN** a group has three rows and the `any` input is true for only the second
- **THEN** `any` yields true

#### Scenario: First hit over rows
- **WHEN** a group has three rows whose sweeps miss, hit at `t = 0.6`, and hit at `t = 0.2`
- **THEN** `first_hit` yields the hit at `t = 0.2`

#### Scenario: Bad reducer
- **WHEN** `collect`, an unknown reducer, a mismatched default, a non-bool `any` input, a non-`SweepHit` `first_hit` input or an impure expression is used
- **THEN** the compiler reports a source-located error

#### Scenario: Invalid domain
- **WHEN** a selectionless rule declares `reduce:`
- **THEN** the compiler rejects it

### Requirement: Global and outer-grouped empty-input behavior

Omitting `per:` SHALL produce exactly one global aggregate row, even for empty input. `per: binding` SHALL be allowed only for a declared pair binding and SHALL produce exactly one group for every entity in that binding's membership snapshot that passes the group filter (see "`where:` splits into group and row filters"), whether or not any row survives for it. Identity values SHALL apply to empty input: `count` and `sum` zero, `min`/`max` their default, `any` false, `first_hit` the miss value that `sweep` itself returns (`hit = false`, `t = 1.0`, stale `other`, zero `point` and `normal`).

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

#### Scenario: Group multiplicity
- **WHEN** three member rows share a team binding
- **THEN** one aggregate row is produced for that team

### Requirement: `where:` splits into group and row filters

When a rule declares `reduce:` with `per: binding`, each `where:` predicate whose pair-binding reads all root at the `per` binding SHALL filter groups: an entity failing it gets no group. Every other `where:` predicate SHALL filter rows only. Without `per:`, every `where:` predicate SHALL filter rows.

#### Scenario: Group-only predicate removes the group
- **WHEN** a rule reduces `per: team` and its `where:` contains `team.Team.active`
- **THEN** an inactive team gets no reduction activation

#### Scenario: Row predicate keeps the group
- **WHEN** a rule reduces `per: enemy` and its `where:` contains a predicate reading both `enemy` and `wall`
- **THEN** an enemy for which the predicate rejects every row still gets one activation with identity values

### Requirement: Reduction scope and deterministic pipeline

The logical pipeline SHALL be domain → `where:` → `reduce:` → `order by:` → `limit:` → handler. All participating field values SHALL be captured in one input snapshot before any reduced handler runs. Rows SHALL fold in stable domain order; groups SHALL be ordered by the `per` binding's creation order. Floating reductions SHALL preserve that fold order without reassociation. Handlers and post-reduction sort keys SHALL see the aggregates and the retained `per` binding, but no eliminated binding. The retained `per` binding SHALL be writable in the reduced handler: direct assignment to its traits SHALL be accepted, because the handler runs exactly once per group entity. Global limits SHALL operate on aggregate rows; per-binding limits SHALL be rejected when `reduce:` is present.

#### Scenario: Rank teams
- **WHEN** teams are ordered by total descending and limit is 2
- **THEN** at most two aggregate handlers run, with creation-order ties

#### Scenario: Eliminated binding
- **WHEN** a grouped handler accesses the `player` binding eliminated by `per: team`
- **THEN** compilation rejects the access

#### Scenario: Write through the retained binding
- **WHEN** a handler grouped `per: enemy` assigns `enemy.Enemy.facing_yaw = 1.0`
- **THEN** compilation accepts it and the write applies to that enemy once per activation

#### Scenario: No live-value drift
- **WHEN** an aggregate handler writes a value that is an input to a later group's aggregate
- **THEN** all aggregates retain values from the input snapshot

#### Scenario: Invalid combined limit
- **WHEN** `reduce:` is combined with `limit: 1 per team`
- **THEN** compilation rejects the redundant per-binding form

### Requirement: Reduction numeric edge behavior

Integer `count` and `sum` SHALL saturate at the signed 32-bit bounds, clamping each fold step, as a reducer-only policy that SHALL NOT change ordinary integer-expression semantics. Floating `sum` SHALL follow the existing float representation and stable fold order; floating `min`/`max` SHALL propagate NaN when any input is NaN. Defaults SHALL apply only to empty input, not to non-finite values.

#### Scenario: Positive integer overflow
- **WHEN** integer sum folds 2147483647 followed by 1
- **THEN** it returns 2147483647 without native signed overflow

#### Scenario: Mixed-sign saturation order
- **WHEN** integer sum folds 2147483647, 1, then -1
- **THEN** it returns 2147483646, preserving the defined per-step saturation order

#### Scenario: Non-finite minimum
- **WHEN** a non-empty floating min input contains NaN
- **THEN** its result is NaN rather than the empty-input default

### Requirement: Reduced rule placement and triggers

With `reduce:`, `order by:` SHALL be written after the `reduce:` clause; an `order by:` before it SHALL be rejected. A lifecycle trigger (`on added`/`on removed`) SHALL be rejected on a reduced rule. A targeted event delivered to a reduced rule with `per: binding` SHALL produce at most the recipient's group; a global reduction SHALL ignore the target.

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
- **WHEN** an event targeted at team A reaches a rule reduced `per: team`
- **THEN** only team A's group runs, with aggregates over all of team A's rows

### Requirement: first_hit keeps the earliest hit deterministically

`first_hit` SHALL ignore rows whose input has `hit = false`, and SHALL keep the row with the smallest `t` among the rest. Among hits with equal `t`, it SHALL keep the one that comes first in stable fold order. A row's miss SHALL never replace a kept hit.

#### Scenario: Misses are ignored
- **WHEN** a group's rows are a hit at `t = 0.9` followed by two misses
- **THEN** `first_hit` yields the hit at `t = 0.9`

#### Scenario: Equal t resolves by fold order
- **WHEN** two rows hit at the same `t`
- **THEN** `first_hit` yields the row that comes first in stable domain order
