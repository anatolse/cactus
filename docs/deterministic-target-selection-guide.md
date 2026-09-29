# Guide: Deterministic Target Selection

Status: describes shipped behavior only — `where:`, `order by:`, and `limit:`
are normatively specified in `openspec/specs/dsl-rule-limit/spec.md` and
`openspec/specs/dsl-pair-relations/spec.md`. This guide adds no new syntax or
semantics; it is a recipe for combining existing ones.

## Problem

A rule that wants "the nearest enemy" is tempted to accumulate state across a
reset-and-scan idiom: clear an accumulator, loop candidates by hand, keep the
closest so far. That idiom is unnecessary — `order by:` plus `limit: 1 per
binding` already does the scan — and it is easy to get wrong: if the
previously-selected target disappears and nothing clears the selection flag,
a handler can go on reading a stale entity handle as if it were still valid.

## Solution

Three rules, run in this order every `fixed_tick`:

1. **Reset.** Clear the selection flag for every entity that might select a
   target this tick, before anything selects.
2. **Select.** A `pairs:` rule with `where:`, `order by:`, and `limit: 1 per
   binding` picks at most one candidate per selecting entity and writes it
   through, in the same handler that selected it.
3. **Consume.** Any handler that wants to act on the current selection reads
   the flag first and only touches the selected handle when the flag is true.

```cactus
trait Target:
    var selected: entity_id
    var has_target: bool = false

rule ResetTarget:
    filter:
        Target as slot

    on fixed_tick:
        slot.has_target = false

rule SelectTarget:
    pairs:
        tower:
            Tower
            Position
            Target
        enemy:
            Enemy
            Position
    order by:
        v2.distance(tower.Position.pos, enemy.Position.pos) asc
    after:
        ResetTarget
    where:
        not enemy.Enemy.cloaked
        v2.distance(tower.Position.pos, enemy.Position.pos) <= tower.Tower.range
    limit: 1 per tower

    on fixed_tick:
        tower.Target.selected = enemy
        tower.Target.has_target = true

rule ConsumeTarget:
    filter:
        Target as slot
    after:
        SelectTarget

    on fixed_tick:
        if slot.has_target:
            emit Damage to slot.selected
```

The three rules above are copied verbatim from
`examples/deterministic_target_selection.cactus` — keep them in sync if that
file changes. The full fixture additionally has four towers and six enemies
covering every case below, plus an `ApplyDamage` rule that reacts to `Damage`;
its runtime assertions live in
`tests/test_deterministic_target_selection_headless_behavior.cpp`.

`ResetTarget` and `SelectTarget` both write `Target`, so their relative order
matters; `after: ResetTarget` on `SelectTarget` makes that dependency explicit
instead of relying on incidental declaration order (see
`openspec/specs/handler-execution-graph/spec.md`, "Explicit handler
ordering"). `ConsumeTarget` declares `after: SelectTarget` for the same
reason.

## Why no fake null entity

`entity_id` has no null literal (`openspec/specs/dsl-entity-id-total-semantics/spec.md`).
`Target.selected` keeps whatever handle it last held even when `has_target` is
false — the recipe never invents a sentinel value to mean "no target." That
handle can go stale (the entity it pointed to may since have been destroyed),
but nothing in this recipe ever reads it without checking `has_target` first.
This is a stricter contract than the language actually requires: operating on
a stale `entity_id` is already a defined, safe no-op or no-match (same spec).
Gating on `has_target` is about writing selection logic that is obviously
correct on inspection, not about avoiding a crash the language wouldn't have
anyway.

An `entity_id` field with no default still has to be set when the entity is
created, and `self` is not allowed in an initializer. So each tower starts by
pointing `selected` at itself, by name:

```cactus
entity NearTower from Turret(at = vec2(0.0, 0.0), range = 100.0):
    Target:
        selected = NearTower
```

That starting value means nothing. It is never read, because `has_target`
starts false.

## Stable ties

When two candidates tie on every `order by:` key, `limit:` keeps the one with
the earlier creation ordinal (`dsl-rule-limit`, "Limit selection is
deterministic"). Two enemies at the exact same distance from a tower do not
produce a flip-flopping or backend-dependent choice.

## Filtered-out candidates

`where:` runs before `limit:` truncates the domain. A candidate that fails
`where:` — cloaked, dead, out of some other exclusion — can never consume the
one retained slot even if it would otherwise sort first. Put every
disqualifying condition in `where:`, not only in the sort key.

## Empty domains

If a selecting entity's partition has no surviving candidate after `where:`,
`limit: 1 per binding` produces no activation for it at all
(`dsl-rule-limit`, "An actor with no surviving tuples produces no
activation"). Nothing writes `Target` that tick, so `has_target` stays exactly
where `ResetTarget` just left it: false.

## Last-target disappearance

The same mechanism handles a target that existed last tick and is gone this
tick: `ResetTarget` clears `has_target` unconditionally, and if the vanished
target's replacement can't be found either, `SelectTarget` simply produces no
activation. There is no special "was I targeting something that just died"
branch to write — the reset-then-maybe-reselect order already covers it.

A `destroy` applies at the activation commit, after every handler has run.
An enemy destroyed during this tick can still be selected and damaged this
tick. It drops out of selection from the next tick on.

## Per-binding write permission

`SelectTarget` writes `tower.Target.selected` and `tower.Target.has_target`
directly, with no event and no second rule, because `limit: 1 per tower` is a
statically provable one (the literal `1`): the compiler can prove the handler
runs at most once for each `tower`, so the pair-relations read-only carve-out
lifts for that binding only (`dsl-rule-limit`, "A per-binding limit of
statically-provable one is recognized"; `dsl-pair-relations` for the general
read-only rule). `enemy` remains read-only in the same handler — nothing here
needs to write through it.

## When you don't need a pairs rule at all

This recipe is for selection with a per-tower predicate (`where:`) or a
per-tower sort key that reads the tower's own fields. When "nearest" has no
such predicate — any entity matching a fixed trait filter, judged only by
distance from a single point — a direct spatial query is simpler:
`query.nearest[Enemy](from = p)` from `std.physics.flat.query` (2D) or
`std.physics.volume.query` (3D) returns the nearest entity carrying the
bracketed traits, or a stale handle if none exists, in one call with no rule
of its own required. Guard its result with `exists(id)` before relying on it.
Reach for `pairs:` + `order by:` + `limit:` when the selection genuinely
depends on both sides of the pair, as `SelectTarget` above does through
`tower.Tower.range`.
