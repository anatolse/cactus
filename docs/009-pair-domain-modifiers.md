# Proposal 009: Structural Pair-Domain Modifiers

Status: draft  
Kind: relation-domain semantics  
Semantic change: tuple membership options

## Summary

Add `distinct` and `unordered` modifiers to `pairs` so authors can describe
common structural cardinality without repeating identity guards or evaluating
symmetric contacts twice.

This proposal does not add a general `where:` predicate. Gameplay-dependent
conditions remain ordinary `if` statements inside the handler.

## Existing default

Unmodified `pairs:` keeps its current semantics:

- directed Cartesian product;
- self-pairs included when one entity belongs to both domains;
- reverse tuples included;
- stable left-major iteration over creation-order membership snapshots.

```cactus
pairs:
    body:
        DynamicBody
    wall:
        Solid
```

## `pairs distinct`

```cactus
rule DetectBubbleContacts:
    pairs distinct:
        a:
            Bubble
            tf.WorldTransform
        b:
            Bubble
            tf.WorldTransform
```

Semantics:

- enumerate the ordinary directed product;
- omit a tuple when both bindings reference the same entity;
- retain both `(a, b)` and `(b, a)` when both satisfy membership.

This is equivalent to the current product plus a structural `a != b` guard,
but the excluded self-tuples are never invoked.

`distinct` is valid for different domains as well as identical domains.

## `pairs unordered`

```cactus
rule DetectBubbleContacts:
    pairs unordered:
        a:
            Bubble
            tf.WorldTransform
        b:
            Bubble
            tf.WorldTransform
```

Semantics:

- both bindings must resolve to the same membership domain;
- self-pairs are omitted;
- for every two members, exactly one tuple is produced;
- the tuple satisfies `creation_ordinal(a) < creation_ordinal(b)`;
- iteration order is the lexicographic order of those ordinals.

For members `[a, b, c]`, tuples are:

```text
(a, b), (a, c), (b, c)
```

An unordered collision rule normally computes both outcomes and emits a
targeted event to each participant:

```cactus
emit BubbleBounce to a:
    new_velocity = resolved_a

emit BubbleBounce to b:
    new_velocity = resolved_b
```

## Domain equivalence for `unordered`

The two bindings are equivalent when their resolved canonical required trait
sets and any future exclusion sets are identical. Binding names and local
aliases do not affect equivalence.

Reject `unordered` when the domains differ:

```cactus
pairs unordered:
    projectile:
        Projectile
    enemy:
        Enemy
```

That is a directed cross-domain relationship and must use ordinary `pairs` or
`pairs distinct`.

## Snapshot and mutation semantics

Modifiers do not change existing pair guarantees:

- membership is snapshotted once per pass;
- values are read live according to current pair rules;
- durable pair-bound trait access remains read-only;
- structural commands are buffered;
- projected traits cannot add tuples to the pass already executing;
- the complete tuple pass finishes before emitted events are drained.

## Execution graph

Every modified pair handler remains exactly one execution-graph node. Runtime
tuples never become graph nodes.

Its contract retains:

- two named bindings;
- binding-qualified reads;
- conservative canonical trait accesses;
- the new domain cardinality mode: `directed`, `distinct`, or `unordered`.

## Diagnostics

- Reject more than one modifier.
- Reject `unordered` for non-equivalent domains and show both resolved trait
  sets.
- Preserve errors for implicit `self` and durable pair-bound writes.
- A lint may suggest `pairs distinct` when the first executable statement is
  a pure `if a != b:` wrapper around the entire handler.

## Compatibility

Plain `pairs:` is unchanged. No existing rule is silently reinterpreted.

## Implementation work

- Lexer/parser: contextual modifier after `pairs`.
- AST and module artifacts: pair mode enum; increment artifact version.
- Semantic analysis: equivalent-domain check for `unordered`.
- Scheduler/CIR: preserve pair mode on the relation domain.
- C++ backend: filtered nested iteration using stable creation ordinals.
- Tests: tuple lists, different domains, spawn ordering, snapshot behavior,
  module round-trip, event delivery after complete pass.

## Acceptance criteria

- `distinct` never invokes a self-tuple.
- `distinct` remains directed.
- `unordered` invokes exactly `n * (n - 1) / 2` tuples.
- Unordered tuple order is backend-independent.
- A modified pair handler is still one graph node.
- Existing plain-pair tests remain unchanged.

## CIR impact

Extend the binary relation domain with a small cardinality enum. Do not lower
the modifier into an arbitrary predicate node; it is structural domain data.

