## Context

The current DSL represents archetype initialization through two separate mechanisms:

- `apply:` declares which traits belong to a `unit` or `template`
- `config:` assigns field values using bare keys, `Trait.field`, or `alias.field`

This split requires parser support for aliases, semantic support for ambiguous bare-field resolution, and dedicated AST structures for key-prefix-based assignment. `spawn` reuses the same flat override model with parenthesized arguments, while `emit` uses a separate parenthesized payload form. The language therefore describes three closely related initialization problems with three different syntactic shapes.

This change is cross-cutting because it alters source syntax, AST structure, semantic validation, examples, and tests. It is also explicitly breaking: old archetype aliases and `apply:`/`config:` blocks will no longer be part of the authoring model.

## Goals / Non-Goals

**Goals:**
- Replace `apply:` + `config:` with a single block-structured archetype syntax for `unit` and `template` declarations.
- Remove archetype-local `as` aliases and the associated dotted-key resolution rules.
- Use nested trait blocks to make ownership of field assignments explicit in the source.
- Extend the same initialization style to `spawn` overrides and event payload construction in `emit`.
- Keep targeted emit semantically distinct from payload fields.
- Reduce ambiguity in semantic analysis by encoding structure in syntax instead of in field-name lookup rules.

**Non-Goals:**
- Changing system `filter:` alias syntax; this proposal only removes aliases from archetype declarations.
- Changing runtime semantics of entity creation, event dispatch timing, or handler ordering.
- Introducing dynamic trait injection at spawn sites beyond overriding fields of traits already present in the template.
- Preserving backward compatibility with legacy `apply:` / `config:` source syntax.

## Decisions

### Decision 1: Archetypes become a list of trait entries, not `apply:` plus `config:`

`unit` and `template` bodies will become a sequence of trait entries:

- marker trait entry: `Persistent`
- data trait entry: `Transform:` followed by an indented field block

Example:

```cactus
template WalkerEnemy:
    Position2D:
        pos = vec2(0.0, 0.0)
        velocity = vec2(0.0, 0.0)
    Renderable2D:
        texture = EnemyTex
    Collectible
```

This removes the need to correlate a trait membership list with a separate field assignment list.

**Alternatives considered:**
- Keep `apply:` but replace `config:` with nested sub-blocks under each apply entry. Rejected because it preserves a two-phase mental model.
- Keep `config:` but ban aliases and only allow `Trait.field`. Rejected because it retains indirection and duplicates trait names.

### Decision 2: Archetype aliases are removed entirely

`ApplyEntry.alias` and all alias-based config/spawn qualification rules will be removed for units and templates. The nesting of a field block under a trait name becomes the sole way to identify field ownership.

This simplifies parsing and semantic analysis:

- no local alias map for archetypes
- no ambiguity diagnostics for bare config fields
- no trait-name vs alias-name prefix lookup for config or spawn overrides

**Alternatives considered:**
- Allow optional aliases inside nested blocks for convenience. Rejected because it reintroduces multiple naming paths for the same trait and weakens the simplification goal.

### Decision 3: `spawn` adopts trait-block overrides

`spawn` changes from parenthesized flat arguments to a nested block form:

```cactus
spawn WalkerEnemy:
    Position2D:
        pos = vec2(100.0, 200.0)
```

The override block is structurally aligned with the template body. Only traits already present in the template may be overridden. Marker traits in the template remain marker entries and do not accept field sub-blocks.

Expression-form spawn remains supported, but its syntax becomes a block-bearing primary expression. The parser and AST must therefore support a spawn expression node that carries nested override blocks instead of flat args.

**Alternatives considered:**
- Keep parenthesized spawn args while changing only archetypes. Rejected because it would preserve two initialization syntaxes for the same conceptual data.

### Decision 4: `emit` adopts payload block syntax, with `to` kept outside the payload

`emit` changes to:

```cactus
emit PlayerDamaged:
    amount = 10
    source = self_id
```

Targeted emit becomes:

```cactus
emit PlayerDamaged to enemy_id:
    amount = 10
```

The target remains outside the payload because it is transport metadata, not part of the event struct. This preserves a clean distinction between event contents and delivery routing.

**Alternatives considered:**
- Encode target as `to = enemy_id` inside the block. Rejected because it conflates payload fields with dispatch behavior and would complicate validation when an event legitimately has a field named `to`.

### Decision 5: AST initialization structures should converge on nested blocks

The current AST stores flat keyed assignments in `ConfigAssignment` and `SpawnArg`, and `EmitStmt` stores only positional args. The redesign should move toward a shared nested initialization model:

- trait entries with optional assignment lists for archetypes
- trait override entries for spawn
- field assignment lists for event payloads

The exact C++ type names can be decided during implementation, but the key architectural direction is to represent initialization structurally rather than as prefix-qualified strings.

**Alternatives considered:**
- Keep existing AST shapes and lower nested syntax into synthetic dotted keys during parsing. Rejected because it would preserve old complexity in semantic analysis and make the AST less faithful to source structure.

### Decision 6: Migration is intentionally breaking and one-way

The compiler does not need to support both legacy and new archetype syntax long-term. Specs and examples will be updated to the new form, and old qualification behavior in `dsl-unit-config-qualification` will be removed rather than deprecated indefinitely.

**Alternatives considered:**
- Temporary dual-syntax support. Rejected for now because it would increase parser complexity and delay the cleanup benefits.

## Risks / Trade-offs

- **[Block-valued `spawn` expression parsing is more complex]** → Mitigation: model spawn as a first-class expression node with explicit nested override blocks, and cover both expression and statement forms in parser tests.
- **[Breaking many examples/specs at once]** → Mitigation: stage implementation with parser/semantic changes first, then systematically update examples and tests.
- **[`emit` syntax change may ripple into codegen and semantic assumptions]** → Mitigation: update AST and semantic checks together so payload validation is based on named field assignments instead of positional args.
- **[Possible confusion between trait blocks in archetypes and field blocks in events]** → Mitigation: keep the outer keyword/context explicit (`template`, `unit`, `spawn`, `emit`) and preserve targeted emit syntax outside the payload.
- **[System filter aliases remain while archetype aliases disappear]** → Mitigation: document that aliases are retained only where they bind runtime iteration variables, not where they merely compensate for configuration syntax.

## Migration Plan

1. Land parser and AST support for nested archetype, spawn, and emit blocks.
2. Replace semantic validation paths that rely on archetype alias maps or dotted config keys.
3. Update specs to remove alias/config qualification requirements and define the new nested forms.
4. Update tests and examples to the new syntax.
5. Remove legacy parsing and diagnostics for archetype alias/config forms.

Rollback is straightforward before release because this is a compiler/source-language change without runtime data migration. If implementation stalls, the change can be abandoned by dropping the proposal without affecting persisted state.

## Open Questions

- Should empty trait blocks like `Transform:` with no assignments be accepted, or should authors use bare `Transform` instead?
- Should `spawn` allow overriding only a subset of fields on a trait block multiple times, or must each trait appear at most once per spawn site?
- Should `emit` require named payload fields exclusively, or should zero-field marker events still allow a bare `emit EventName` shorthand?