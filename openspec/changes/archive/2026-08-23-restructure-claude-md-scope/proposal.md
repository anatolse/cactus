## Why

`CLAUDE.md` is the one file Claude Code always loads, but its own first line
scopes it to `src/common`, `src/frontend`, `src/backends` only ("the compiler's
own hand-written source"). Everything else touched in this repo — `stdlib/`,
`examples/`, `tests/`, `openspec/` — gets no project framing from it. The
user needs CLAUDE.md to act as a real system prompt for all tasks in the
project, not just C++ authoring, with an explicit focus on code quality,
preserving the DSL's philosophy/architecture, evaluating Cactus code against
game-dev simplicity, evaluating backend C++ against compiler-engineering
technique, treating generated-code speed as the top priority, and TDD for
new DSL features.

The DSL philosophy itself is not missing from the project — it's already
fully specified in `openspec/specs/language-philosophy/spec.md` and
`spec/cactus_dsl_spec.md`. The gap is that CLAUDE.md neither points to it nor
gates new work against it, so nothing forces a fresh session to consult it.
Separately, `openspec/config.yaml` — which primes every `/opsx:*` artifact
generation — has empty `context:`/`rules:` fields, so proposal/design/tasks
authoring currently starts from zero project framing.

## What Changes

- Restructure `CLAUDE.md` from a single flat C++-scoped document into a
  layered doc: a short universal preamble (applies to all tasks) plus the
  existing C++ section re-scoped as one subsection, plus a new parallel
  Cactus DSL authoring subsection, plus a short generated-code-performance
  subsection.
- Add a gate rule in the new preamble: before changing DSL grammar, stdlib
  API shape, or language semantics, consult
  `openspec/specs/language-philosophy/spec.md` (and `spec/cactus_dsl_spec.md`
  for grammar) — pointer, not restatement, to keep one source of truth.
- Extend the existing "Testing" (TDD) section so it explicitly covers new
  DSL/stdlib features, not just C++ changes: a new language/stdlib feature
  needs a `.cactus` sample fixture plus a failing headless-behavior Catch2
  test written first, mirroring the existing `test_*_headless_behavior.cpp`
  + `examples/*.cactus` pairing already used in the tree.
- Add a new "Cactus DSL authoring" subsection (parallel to the C++ section,
  scoped to `stdlib/`, `examples/`): game-dev simplicity as a hard bar,
  prefer existing stdlib primitives over hand-rolled logic, and relocate the
  existing dedup/nesting rules that currently bleed into `.cactus` files as
  asides on the C++ section into this new home instead.
- Add a new short "Generated code performance" subsection: state explicitly
  that generated output stays exempt from lint/style rules (unchanged), but
  the backend's codegen *strategy* is judged on the runtime speed of the
  code it emits above all else, and that shared, program-independent logic
  belongs in `cactus_runtime.hpp`/`.cpp` rather than emitted text.
- Populate `openspec/config.yaml`'s `context:` field with a short (3-4 line)
  project primer (tech stack, TDD requirement, pointer to
  `language-philosophy`) so `/opsx:*` artifact generation starts from
  correct framing instead of empty context. This is a separate, smaller
  edit from the CLAUDE.md restructure — same motivation, different consumer
  (config.yaml primes artifact authoring; CLAUDE.md governs day-to-day
  coding).

Not in scope: rewriting or duplicating the language philosophy/grammar
content itself (it stays authoritative in `openspec/specs/language-philosophy`
and `spec/cactus_dsl_spec.md`); changing `.clang-tidy` or any enforcement
tooling; touching the ad hoc `spec/00N-*.md` drafts or `docs/` notes, which
are unrelated scratch material, not conventions this change needs to encode.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This change edits contributor/agent guidance (`CLAUDE.md`,
`openspec/config.yaml`) and does not alter any product, compiler, or
language behavior — no spec describes agent-instruction content, so no
spec-level requirement changes. `skip_specs: true` is set in this change's
`.openspec.yaml` accordingly.

## Impact

- `CLAUDE.md` — restructured (existing C++ content preserved and re-scoped,
  new preamble + two new subsections added).
- `openspec/config.yaml` — `context:` field populated.
- No source code, build config, or spec files change. No effect on compiler
  behavior, generated output, or existing tests.
