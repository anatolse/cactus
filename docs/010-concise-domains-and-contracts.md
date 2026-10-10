# Proposal 010: Concise Rule Clauses and Domains

Status: implemented
Kind: terminology and parser sugar  
Semantic change: unchanged domain/runtime behavior; duplicate clauses are errors
Dependency: current rule, pair-domain, reduction, and handler-contract semantics

Implementation: [add-concise-rule-notation](../openspec/changes/archive/2026-10-09-add-concise-rule-notation/proposal.md).
Completion follow-up: [complete-concise-rule-notation](../openspec/changes/archive/2026-10-09-complete-concise-rule-notation/proposal.md).
Verification: [acceptance audit and test evidence](../openspec/changes/archive/2026-10-09-complete-concise-rule-notation/verification.md).
The OpenSpec artifacts track implementation and verification. Legacy spellings
remain supported without deprecation warnings. Formatter work and clause reordering
are excluded from this change.

## Summary

Make rule declarations shorter with four changes: inline clauses, `each` /
`without`, compact pair bindings, and `reduce per`. Each new spelling lowers to
the existing semantic structure. Preserve block forms for long or commented
declarations and explicit external contracts for scheduling and callback ABI
generation.

This is the first notation change from the language review. Reducers directly
inside `keep`, new derived-state facilities, and changes to clause placement are
separate work.

## Philosophy fit

This change concerns author notation and applies equally to gameplay and tools.
It adds no iteration domain, access annotation, engine-specific syntax, or runtime
operation. Handler access remains inferred; the backend keeps responsibility for
query strategy and execution. Memory, lifetime, data-race, purity, and timing
guarantees remain those of the current language.

## Domain terminology

Adopt these canonical author-facing spellings:

- `each:` for unary positive selection, replacing `filter:`;
- `without:` for unary exclusion, replacing `exclude:`.

Both words are contextual at rule-clause positions. They remain ordinary
identifiers elsewhere. The old spellings `filter:` and `exclude:` were later
removed, and `filter` and `exclude` are ordinary identifiers now.

The three domain forms then read consistently:

- no domain clause: one selectionless invocation;
- `each:`: one invocation per matching entity;
- `pairs:`: one invocation per matching tuple.

## Proposed compact syntax

```cactus
rule Move:
    each: tf.WorldTransform as transform, PlayerMotion as motion
    without: Frozen
    after: ReadInput
    where: motion.move_speed > 0.0
    when: not Game.Match.paused

    on tick:
        transform.position += motion.velocity * tick.dt
```

Equivalent block syntax remains available:

```cactus
rule Move:
    each:
        tf.WorldTransform as transform
        PlayerMotion as motion
    without:
        Frozen
    after:
        ReadInput
    where:
        motion.move_speed > 0.0
    when:
        not Game.Match.paused
```

Examples show declaration fragments; supporting declarations and imports are
omitted. Aliases follow the existing scope and name-resolution rules.

## Compact pair bindings

Keep the explicit `pairs:` block and its two named entity bindings. Allow a
binding's positive trait list on the same line:

```cactus
rule TouchLava:
    pairs:
        body: Health, physics.Collider
        lava: Lava, physics.Collider
    group: physics.contacts
    where: physics.touching(body, lava)

    reduce per body:
        zones = count()
        damage = sum(lava[Lava].damage_per_second)

    keep InLava on body:
        zones = zones
        dps = damage
```

Each inline binding is equivalent to its existing nested form:

```cactus
pairs:
    body:
        Health
        physics.Collider
    lava:
        Lava
        physics.Collider
```

Inline entries support the same `as` aliases as block entries:

```cactus
pairs:
    body: Health as health, tf.WorldTransform as body_pose
    lava: Lava, tf.WorldTransform as lava_pose
```

Aliases are rule-scoped and written bare, such as `health.current` and
`lava_pose.position`. Alias validation follows the current rules. One binding
may be inline while the other is a block; one binding may not mix both forms.

The domain still has exactly two nonempty bindings. Its directed product,
self-pairs, membership snapshots, stable tuple order, mutation restrictions,
and spatial-query recognition are unchanged. `pairs` remains unavailable on
extern rules and mutually exclusive with unary domain clauses.

## Compact grouped reductions

Make the retained binding part of the clause header:

```cactus
reduce per team:
    players = count(player)
    total = sum(player.Player.points)
```

This replaces the old body-level `per: team` line, which was later removed.
The global form remains `reduce:` without `per`. Reducer declarations remain an
indented block in both forms. A body-level `per:` line is an error that names
the header form.

Outer groups, group/row predicate classification, empty-input identities,
retained-binding writability, aggregate snapshots, fold order, and ranking of
aggregate rows retain their current meaning. `keep` still requires its existing
pair reduction; this proposal does not infer it from the result clause.

## External rule example

Current contracts often require several nested one-item blocks. Proposed form:

```cactus
extern rule ShapeRenderer:
    each: tf.WorldTransform, Shape

    on render:
        reads: tf.WorldTransform, Shape
        effects: graphics
```

More complex contracts remain explicit:

```cactus
extern rule NativeMovement:
    each: Position, Velocity

    on fixed_tick:
        reads: Velocity
        writes: Position
        emits: EntityMoved
        commands: spawn Mover
        effects: physics
```

## List syntax

Allow comma-separated entries after these list clauses wherever each is already
valid:

| Clauses | Entry form |
|---|---|
| `each:` | Trait reference with optional `as` alias |
| `without:` | Trait reference |
| `after:`, `before:` | Existing ordering reference |
| `where:`, `when:` | Pure boolean expression |
| `order by:` | Pure sort expression with optional `asc` / `desc` |
| `reads:`, `writes:`, `projects:`, `emits:`, `effects:` | Existing contract entry |
| `commands:` | Existing command capability |

The same trait-list form applies after a pair binding's colon. This does not
introduce inline `pairs:` declarations containing both bindings.

The existing indented form remains valid for every clause. Do not allow both an
inline list and an indented continuation for the same clause. Inline lists end
on the same logical line, require at least one entry, and may have one trailing
comma. A trailing comment is allowed after the completed list.

Parse entries according to their grammar, not by splitting source text on
commas. Commas inside calls, constructors, and list literals belong to those
expressions. For example:

```cactus
where: physics.touching(body, lava), body != lava
order by: v2.distance(body_pose.position, lava_pose.position) asc, lava[Lava].priority desc
```

Multiple `where` or `when` entries retain the existing conjunction and
evaluation behavior. Sort keys retain their existing lexicographic order and
stable tie-breaking. The distinction between a pass gate and a row predicate
does not change.

Scalar clauses such as `group:`, `phase:`, and `limit:` keep their current forms.
Payload blocks, reducers, and handler bodies are outside this inline-list rule.
Existing clause-order restrictions remain in force, including the placement of
`order by:` after a reduction.

Commands retain their structured spelling:

```cactus
commands: spawn Bullet, destroy, add Frozen, remove Burning, set Health
```

## Why contracts remain explicit

For ordinary Cactus handlers, reads and writes continue to be inferred from the
body. External handlers have no Cactus body, so their contracts must stay
explicit.

The compiler must not infer that every selected trait is read: selection tests
membership, while an external handler may write a selected trait without
reading another selected trait. Optional reads such as `Parent` may also be
contracted without being part of the positive domain.

## Compatibility and migration

- `each`, `without`, and `reduce per` are the only forms. Use inline lists for
  short declarations and blocks for longer or commented lists.
- The old spellings were removed without a transition period, because the
  language has no external users. `filter:`, `exclude:`, and a body-level
  `per:` line are errors that name `each:`, `without:`, and `reduce per x:`.
- Block and inline forms produce equivalent AST lists, aside from source
  locations.
- Serialization uses canonical domain structures, never source keywords.

## Diagnostics

- Reject duplicate clauses of the same kind.
- Reject an empty inline list.
- Reject empty entries, repeated commas, and mixed inline/block continuations.
- Reject extra or empty pair bindings using the current domain diagnostics.
- Reject duplicate header/body grouping and unknown or invalid `per` bindings.
- Preserve purity, type, alias, scope, and clause-order diagnostics for both
  spellings.
- Preserve precise source locations for each inline item.
- Recover at clause boundaries so malformed lists do not swallow later handlers
  or declarations.

## Implementation work

- Write failing parser and semantic Catch2 tests before implementation. Cover
  every list-clause family, nested commas, comments, trailing commas, aliases,
  contextual identifiers, mixed pair-binding styles, both grouping spellings,
  and malformed-input recovery.
- Add a `.cactus` fixture and a failing headless behavior test pairing compact
  and existing forms. Compare ordered event observations, reduced results,
  empty groups, and kept-trait entry/exit across activations.
- Parser: reuse existing entry parsers behind a shared inline-or-block list
  helper; recognize contextual aliases at rule-clause positions and lower
  `reduce per` into the existing reduction node.
- AST/frontend: preserve canonical domains, contracts, symbol identities, and
  reduction plans; avoid carrying notation choices into semantic analysis.
- Documentation: update the DSL grammar and canonical examples to teach
  selectionless/each/pairs and the grouped-reduction header.
- Run focused parser, semantic, module-artifact, linker, codegen, and headless
  checks, followed by the required coverage gate. Runtime lowering must use the
  existing backend paths.

## Acceptance criteria

- Compact and block spellings produce equivalent resolved domains, handler
  contracts, reduction plans, and execution graphs, apart from source locations.
- External contracts remain complete and ABI-limiting.
- `each` and `without` do not change selection timing or ordering.
- Compact pairs retain aliases, cardinality, stable tuple order, spatial plans,
  and the current mutation restrictions.
- `reduce per` preserves empty groups, identities, aggregate snapshots,
  writability, sort/limit behavior, and `keep` lifecycle timing.
- `filter:`, `exclude:`, and body-level `per:` are rejected with a diagnostic
  that names the replacement.
- Accepted spellings produce the same runtime behavior and backend strategy;
  no new runtime helper, allocation, or dispatch is introduced by the notation.
- Module-artifact round trips preserve canonical meaning without storing which
  spelling the author used.
- Diagnostics identify the offending inline entry and recover to later clauses.

## CIR impact

No new semantic record is needed. All spellings lower to existing unary or pair
domains, reduction plans, and explicit or inferred handler contracts. CIR, when
integrated, should consume those canonical structures without a spelling flag;
this proposal has no dependency on the separate CIR implementation.
