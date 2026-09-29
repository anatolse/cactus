# Proposal: `limit` and `reduce` Clauses for Cactus Rules

Status: mixed. `limit` (both the global and `per`-binding forms below) is
implemented and normatively specified in
`openspec/specs/dsl-rule-limit/spec.md`, with runtime coverage in
`tests/test_pairs_limit_headless_behavior.cpp` and a complete authoring recipe
in `docs/deterministic-target-selection-guide.md` /
`examples/deterministic_target_selection.cactus`. `reduce` remains proposed —
the `## reduce` section, "Using `reduce` and `limit` Together", and the CIR
`Group`/`Aggregate` operations below describe a design, not shipped behavior.

## Summary

Extend Cactus rules with two complementary domain operators:

- `limit` selects a bounded subset of existing domain rows.
- `reduce` aggregates existing domain rows into grouped or global results.

They extend the existing rule model without introducing a separate query language or requiring world-query `extern func` calls.

The logical rule pipeline becomes:

```text
filter / pairs
→ exclude
→ where
→ reduce
→ order by
→ limit
→ on
```

When `reduce` is absent, `order by` and `limit` operate directly on entity or pair rows. When `reduce` is present, they operate on the resulting aggregate rows.

## `limit`

Implemented — see the Status note above for the normative spec and tests.
Clauses must appear in grammar order: `order by:`, then `after:`, `where:`,
`limit:`, `when:`.

`limit` bounds the number of handler activations without changing the shape of the domain.

### Global limit

```cactus
rule RenderTopScores:
    filter:
        Player
        Score as score

    order by:
        score.value desc
    limit: 10

    on render:
        ...
```

The handler runs for at most ten selected entities.

### Per-binding pair limit

```cactus
rule SelectTarget:
    pairs:
        actor:
            AI
            Position
            Target
        enemy:
            Enemy
            Position
    order by:
        v2.distance(actor.Position.value, enemy.Position.value) asc
    where:
        v2.distance(actor.Position.value, enemy.Position.value) <= actor.AI.range
    limit: 1 per actor

    on fixed_tick:
        actor.Target.selected = enemy
        actor.Target.has_target = true
```

The handler writes `actor` directly; see "Pair mutation" below. An actor with no
match gets no activation, so a stale selection must be cleared by an earlier
reset rule — the complete recipe is in
`docs/deterministic-target-selection-guide.md`.

For every `actor`, the runtime:

1. Finds all matching pair rows.
2. Orders them using `order by`.
3. Keeps at most one row.
4. Executes the handler for the selected pair.

If an actor has no matching target, no handler activation is produced for that actor.

Without `order by`, `limit` uses the domain's stable logical order.

The limit may depend on the partition binding:

```cactus
limit: tower.Tower.target_count per tower
```

Such an expression may read only constants and traits belonging to the `per` binding.

### Determinism

`limit` must preserve deterministic execution:

- sort keys are evaluated using the rule snapshot;
- equal keys are resolved by stable creation order;
- pair partitions are processed in the creation order of the `per` binding;
- optimization must not change the selected rows.

### Pair mutation

Pair-bound durable traits are normally read-only. However, with a statically known:

```cactus
limit: 1 per actor
```

the compiler can prove that the handler runs at most once for each `actor`. The `actor` binding may therefore become writable, while the other pair binding remains read-only.

This allows rules such as `SeekPlayer` to select one player and update the enemy without calling `query.first[Player]()`.

## `reduce`

Proposed, not implemented — no `reduce` clause exists in the compiler today.

`reduce` replaces multiple existing domain rows with aggregate rows.

```cactus
rule CalculateTeamScore:
    pairs:
        team:
            Team
        player:
            Player
            MemberOf
            Score

    where:
        player.MemberOf.team == team

    reduce:
        per: team
        player_count = count(player)
        total_score = sum(player.Score.value)

    on tick:
        emit TeamScore to team:
            players = player_count
            score = total_score
```

After reduction:

- the `team` binding remains available;
- the individual `player` binding is no longer available in the handler;
- aggregate results are immutable values available to every `on` handler;
- the handler runs once for every group that actually exists.

The `per` binding may become writable because reduction produces at most one activation for each such binding.

## No Implicit Outer-Group Semantics

Grouped reduction operates only on rows that exist after `where`.

If no `(team, player)` pair survives, no group is created for that team and no handler is executed.

Therefore, grouped reduction does not implicitly behave as a left join and does not create empty candidate groups.

This keeps its semantics consistent with the current `pairs` domain.

Queries based on the absence of a match require a separate, explicit anti-join feature. They are outside this proposal.

## Global Reduction

If `per` is omitted, the entire domain forms one global group:

```cactus
rule CountEnemies:
    filter:
        Enemy
        Alive
        Threat as threat

    reduce:
        enemy_count = count()
        total_threat = sum(threat.value)

    on late_tick:
        emit EnemyStatistics:
            count = enemy_count
            threat = total_threat
```

A global reduction produces one activation even when its input domain is empty:

- `count()` returns `0`;
- `sum(...)` returns the numeric identity;
- `collect(...)` returns an empty list;
- `min` and `max` use their declared default values.

## Core Reducers

The initial reducer set should contain only true aggregations:

```cactus
count(binding)
sum(expression)
min(expression, default = value)
max(expression, default = value)
collect(expression)
```

For a unary global domain, `count()` may omit the binding.

`exists` is not required for grouped reduction: every existing group already contains at least one input row. For global reduction, the equivalent condition is:

```cactus
count() > 0
```

The compiler may optimize this comparison as a short-circuit existence check.

## Operations Excluded from `reduce`

The following should not be reducers:

```cactus
first(binding)
take(binding, count)
argmin(binding, key)
argmax(binding, key)
```

They select rows rather than aggregate them and are already expressed by `order by` and `limit`.

Nearest target:

```cactus
order by:
    spatial.distance_squared(actor.Position, target.Position)

limit: 1 per actor
```

Three nearest targets:

```cactus
order by:
    spatial.distance_squared(tower.Position, target.Position)

limit: 3 per tower
```

A separate `top` clause is therefore unnecessary. `top N` would only be syntactic sugar for `order by` followed by `limit: N`.

## Using `reduce` and `limit` Together

The operators can be composed. For example, selecting the ten teams with the highest aggregate score:

```cactus
rule RankTeams:
    pairs:
        team:
            Team
        player:
            Player
            MemberOf
            Score

    where:
        player.MemberOf.team == team

    reduce:
        per: team
        total_score = sum(player.Score.value)
        player_count = count(player)

    order by:
        total_score desc
    limit: 10

    on tick:
        emit RankedTeam to team:
            score = total_score
            players = player_count
```

Here the rule performs:

```text
join players with teams
→ filter membership
→ aggregate per team
→ order aggregate rows
→ retain ten rows
→ execute handlers
```

A per-binding limit is intended for unreduced pair domains. After grouped reduction, each group already produces one row, so `limit: N per binding` would be redundant and should be rejected.

## First Person Arena Applications

Possible rewrites, not current code: `examples/first-person-arena` still uses
the event-and-compare form. `limit: 1 per` is implemented, so these shapes
compile today once the `collision.*` helpers — placeholders here — exist.

### Select One Bullet Hit

```cactus
rule DetectBulletEnemyContact:
    pairs:
        bullet:
            Bullet
            tv.WorldTransform
        enemy:
            Enemy
            KinematicActor
            tv.WorldTransform

    order by:
        collision.hit_distance(bullet, enemy) asc
    where:
        not bullet.Bullet.consumed
        not enemy.Enemy.dying
        collision.bullet_hits_actor(bullet, enemy)
    limit: 1 per bullet

    on fixed_tick:
        emit BulletContact to bullet:
            other = enemy
            hit_enemy = true
```

This prevents one bullet from producing multiple enemy-contact events during one pair pass.

### Select the Highest Ground Surface

```cactus
rule SelectGroundSurface:
    pairs:
        actor:
            KinematicActor
            tv.WorldTransform
        surface:
            Walkable
            physics.BoxCollider
            tv.WorldTransform

    order by:
        collision.surface_top(surface) desc
    where:
        collision.is_ground_candidate(actor, surface)
    limit: 1 per actor

    on fixed_tick:
        let top = collision.surface_top(surface)

        actor.KinematicActor.ground_surface = top
        actor.tv.WorldTransform.position.y =
            top + actor.KinematicActor.half_height
```

This replaces the emission and subsequent comparison of multiple `GroundCandidate` events.

## CIR Representation

The proposal requires the following logical CIR operations. All of them are
proposed: the compiler has no CIR layer today, and the cpp-entt backend lowers
`where:`, `order by:`, and `limit:` directly. `Group` and `Aggregate` exist
only for `reduce`.

```text
Filter
Group
Aggregate
Order
Limit
PartitionedLimit
```

Physical planning may implement them using:

- component scans;
- direct entity lookup;
- relation indexes;
- SAP;
- spatial hashes;
- AABB trees;
- streaming aggregation;
- partial sorting;
- Top-K selection.

These implementation choices must not affect logical results or deterministic ordering.

## Final Recommendation

Already shipped:

```cactus
limit: count
limit: count per binding
```

Still to add:

```cactus
reduce:
    per: optional_binding
    name = aggregate
```

Do not add:

- `top`;
- `first` or `take` reducers;
- implicit left-join behavior;
- grouped `exists`;
- specialized clauses such as `nearest`, `within radius`, or `highest`.

This keeps the rule language compact while clearly separating filtering, aggregation, ordering, and cardinality control.
