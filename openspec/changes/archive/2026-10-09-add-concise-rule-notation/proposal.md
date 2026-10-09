## Why

Short rule declarations currently need several nested blocks, and grouped reductions place their grouping information inside the body. Proposal 010 identifies a focused notation change that makes these declarations easier to read while preserving the existing execution model and compiler knowledge.

## What Changes

- Adopt contextual `each:` and `without:` as the canonical unary selection spellings; retain `filter:` and `exclude:` without deprecation warnings.
- Accept inline, comma-separated lists alongside existing blocks for selection, ordering, predicates, sort keys, and external handler contracts. Parse nested expressions normally, preserve entry locations, allow one trailing comma, and reject mixed inline/block continuations.
- Accept compact pair bindings such as `body: Health, physics.Collider`, including existing binding-local aliases.
- Accept `reduce per body:` as an alternative to `reduce:` with body-level `per: body`; retain global reductions and reject both grouping spellings in one clause.
- Normalize every spelling to existing domains, reductions, contracts, and scheduling metadata. Preserve phase timing, tuple order, empty groups, mutation restrictions, spatial planning, and external callback capabilities.
- Update the DSL grammar and selected teaching examples, with parser, semantic, artifact, and headless equivalence coverage.

```cactus
rule TouchLava:
    pairs:
        body: Health, physics.Collider
        lava: Lava, physics.Collider
    group: physics.contacts
    where: physics.touching(body, lava)
    reduce per body:
        zones = count()
    keep InLava on body:
        zones = zones
```

Supporting declarations and imports are omitted; this is proposed notation.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `dsl-parser`: Contextual aliases, shared inline/block list grammar, canonical parsing, and recovery.
- `dsl-pair-relations`: Inline trait lists on each existing pair binding.
- `dsl-rule-reduce`: Header-level grouping equivalent to body-level `per:`.
- `dsl-extern-rule`: Compact selection and explicit contract lists with unchanged capabilities.
- `dsl-rule-ordering`: Inline ordering references at existing ordering-clause positions.
- `dsl-where-clause`: Inline pure predicate lists equivalent to predicate blocks.
- `dsl-when-clause`: Inline pass-gate predicates with existing scope and timing.
- `dsl-rule-order-by`: Inline sort-key lists with unchanged ranking behavior.

## Impact

Work centers on `src/frontend/parser.*`, frontend diagnostics if needed, Catch2 tests, a new `examples/concise_rule_notation.cactus` fixture, `spec/cactus_dsl_spec.md`, and selected README/stdlib examples. Existing semantic and backend structures remain the integration boundary; this change does not depend on CIR.

This is genre-neutral author notation. The compiler continues to infer handler access and choose execution strategies without new author obligations or runtime overhead.

## Non-goals

No clause reordering, new query domains, pair modifiers, reducers inside `keep`, derived-state generalization, formatter implementation, legacy-spelling removal, or runtime changes.

## Verification scope extension

The user approved fixing existing runtime, linker, semantic-analyzer, artifact, scheduler, and backend lint failures exposed by the required verification gate, including the MSVC narrowing warning. These are behavior-preserving refactors, covered by the existing regression suites; language semantics and generated runtime strategy remain unchanged.

Cross-compiler verification also exposed a source-location deserialization failure under MSVC. Read the filename, line, and column sequentially before constructing SourceLocation, preserving the artifact format and intended values.

The same gate also requires smaller backend emission helpers, explicit fallible moves, and normal Catch2 registration macros during static analysis. Preserve generated output, keep all lint checks enabled, and compare generated fixtures before and after these refactors.
