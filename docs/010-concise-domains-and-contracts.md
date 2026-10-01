# Proposal 010: Concise Unary Domains and Handler Contracts

Status: draft  
Kind: terminology and parser sugar  
Semantic change: none  
Dependency: proposal 001

## Summary

Make unary rule domains read as iteration domains and allow short lists on one
line. Preserve block forms for long or commented declarations and preserve
explicit external contracts for graph construction and callback ABI generation.

## Domain terminology

Introduce author-facing aliases:

- `each:` for unary positive selection, replacing `filter:` in canonical docs;
- `without:` for unary exclusion, replacing `exclude:` in canonical docs.

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
```

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

Allow comma-separated entries after these clauses:

- `each:`;
- `without:`;
- `after:`;
- `reads:`;
- `writes:`;
- `emits:`;
- `commands:`;
- `effects:`.

The existing indented form remains valid for every clause. Do not allow both an
inline list and an indented continuation for the same clause.

Commands retain their structured spelling:

```cactus
commands: spawn Bullet, destroy, add Frozen, remove Burning
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

- Continue accepting `filter:` and `exclude:` for at least one transition
  period.
- The formatter may offer a mode that rewrites them to `each:`/`without:`.
- Block and inline forms produce identical AST lists.
- Serialization uses canonical domain structures, never source keywords.

If minimizing keyword growth is preferred, `filter:`/`exclude:` may remain as
canonical names while only the inline-list portion of this proposal is adopted.
The two decisions are implementation-separable.

## Diagnostics

- Reject duplicate clauses of the same kind.
- Reject an empty inline list.
- Report a trailing comma consistently with function argument lists.
- Preserve precise source locations for each inline item.
- Warn when deprecated terminology is used only after a documented migration
  decision.

## Implementation work

- Lexer: contextual `each` and `without` keywords if adopted.
- Parser: shared inline-or-block list parser.
- AST: reuse existing selection and contract lists.
- Formatter: choose inline form below a configurable width, otherwise block.
- Documentation: teach selectionless/each/pairs as the three domains.
- Tests: aliases, comments, qualification, commands, malformed mixed forms.

## Acceptance criteria

- Compact and block spellings produce identical decorated handler contracts.
- External contracts remain complete and ABI-limiting.
- `each` and `without` do not change selection timing or ordering.
- Existing `filter`/`exclude` programs continue to compile during migration.
- Module artifacts and CIR contain no distinction between source spellings.

## CIR impact

None. All spellings lower to the existing selectionless or unary domain and
the existing explicit handler contract sets.

