## 1. Backend projected-trait storage model

- [x] 1.1 Replace generated projected overlay maps with generated tracking for projected registry components.
- [x] 1.2 Track first projection per `(entity, trait)` per frame, including whether a durable component existed before projection.
- [x] 1.3 Implement cleanup semantics that remove projected-only components and restore pre-existing durable components.
- [x] 1.4 Ensure new generated helper identifiers for projected-trait tracking do not use the `cactus` prefix.

## 2. Project statement code generation

- [x] 2.1 Emit `project` statements as guarded registry component writes for targeted projections.
- [x] 2.2 Preserve field patch/coalescing behavior for repeated projections to the same `(entity, trait)`.
- [x] 2.3 Define and implement interaction between `remove Trait` and pending projected-component cleanup.

## 3. System filtering code generation

- [x] 3.1 Restore ordinary system filtering to `registry.view<...>()` for non-empty filters.
- [x] 3.2 Emit native EnTT exclusion for `exclude:` clauses where supported.
- [x] 3.3 Ensure no generated per-entity filter guard can `return` from the whole handler when it should skip one entity.
- [x] 3.4 Keep no-filter systems correct for match-all iteration.

## 4. Trait access and matching

- [x] 4.1 Update filter binding emission to bind directly from view parameters or registry access as appropriate.
- [x] 4.2 Update trait-match lowering to rely on registry-backed projected components through normal `try_get`/`all_of`.
- [x] 4.3 Review backend-owned stdlib extern systems for projected trait visibility through normal views.

## 5. Tests and verification

- [x] 5.1 Update codegen tests that currently expect overlay maps and whole-registry scans.
- [x] 5.2 Add regression coverage for multiple entities where the first entity does not match a system filter.
- [x] 5.3 Add behavior/codegen coverage for projected traits matching `filter:` and `exclude:` through registry views.
- [x] 5.4 Add cleanup coverage for projected-only traits and projected-over-durable traits.
- [x] 5.5 Run affected parser, semantic, codegen, and example compilation tests.
