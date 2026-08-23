## Context

See proposal.md - Why. Relevant constraints for the approach:

- `CLAUDE.md` is a single file Claude Code always injects; there is no
  per-directory CLAUDE.md in this repo today, and the change should not
  introduce one (see Decisions).
- The DSL philosophy and grammar are already normative elsewhere:
  `openspec/specs/language-philosophy/spec.md` (identity, predictability,
  ECS boundary, declarative/imperative tiers) and `spec/cactus_dsl_spec.md`
  (grammar). `openspec/specs/dsl-spec-curation/spec.md` exists specifically
  to keep that surface singular and non-duplicated.
- CLAUDE.md's own "Avoid duplication" rule already states the project's
  stance on divergent copies at the code level; this design applies the same
  stance to the docs themselves.
- `openspec/config.yaml` currently has empty `context:` and `rules:` fields
  (template comments only) — it is consumed separately from CLAUDE.md, only
  during `/opsx:*` artifact generation.

## Goals / Non-Goals

**Goals:**
- One CLAUDE.md, layered: a short universal preamble + named subsections,
  so every task in the repo (not just `src/{common,frontend,backends}`)
  gets project framing.
- Every new rule either points at an existing source of truth or states a
  genuinely new process/quality bar — never restates spec content.
- Existing C++ section content is preserved as-is, only re-scoped/re-homed.

**Non-Goals:**
- Not restating or summarizing `language-philosophy` or the grammar spec.
- Not adding a Cactus-level automated lint/nesting checker (still hand-review,
  per the existing "Control-flow nesting" section).
- Not populating `openspec/config.yaml`'s `rules:` (per-artifact rules) —
  out of scope, a separate decision the user hasn't asked for.
- Not touching `spec/00N-*.md` or `docs/` — confirmed scratch material in
  the explore phase, not a convention this change encodes.

## Decisions

**1. Single root CLAUDE.md, not split into per-directory files.**
Claude Code supports directory-scoped CLAUDE.md discovery, which was
considered (e.g. a `stdlib/CLAUDE.md` for Cactus-specific rules). Rejected:
sessions in this repo routinely span `src/` and `stdlib/`/`examples/` in the
same task (a compiler change plus its example fixture), so directory-scoped
files would fragment guidance a single session needs simultaneously. A
single file with clear H2 sections keeps the "system prompt for all tasks"
property the user asked for.

**2. DSL philosophy: pointer + gate rule, never restated.**
The preamble adds one imperative gate — before changing DSL grammar, stdlib
API shape, or language semantics, check `language-philosophy` (and the
grammar spec for syntax) — phrased as a trigger condition, matching the
style CLAUDE.md already uses for its C++ hard constraints (e.g. the
clang-tidy `WarningsAsErrors` framing), rather than as a passive "see also"
link that's easy to skip past.

**3. Dedup/nesting rules for `.cactus`: relocated, not duplicated.**
These currently live as an aside inside the C++-scoped sections ("Applies to
the compiler's C++ ... and to any `.cactus` source touched"). Moving them
under the new Cactus authoring section and leaving a one-line cross-reference
in the C++ section (rather than repeating the bullet lists in both places)
avoids the exact two-copies problem the rule itself warns about.

**4. Compiler-engineering guidance: anchored to concrete files, not a
checklist.** Considered writing general compiler-engineering advice (avoid
redundant tree walks, watch lookup complexity, graceful error recovery).
Rejected as the primary form — too generic to be checkable and risks
becoming boilerplate nobody reads. Instead, the new note points at
`error_reporter.hpp`, `execution_graph_scheduler.hpp`, and
`symbol_identity.hpp` as reference shapes to imitate, the same pattern the
existing "C++23 idioms" section already uses successfully (each idiom tied
to a concrete AST pattern, not a general feature survey).

**5. Generated-code performance: state the priority ordering explicitly.**
README's thesis ("simple DSL, fast native code") implies an ordering —
generated-code runtime speed first, compiler-developer velocity second,
compiler's own compile time a distant third — but it was never written down
anywhere actionable. This design writes that ordering into CLAUDE.md
directly (short, no new mechanism) and cross-references the existing
`cactus_runtime.hpp`/`.cpp` convention (shared logic belongs in the runtime,
not emitted text) so the rule is enforceable, not just aspirational.

**6. TDD: one Testing section, promoted to the preamble tier, not two.**
Considered keeping "Testing" under the C++ section and adding a second,
separate testing note under the new Cactus section. Rejected: that risks the
two drifting apart (e.g. one gets updated, the other doesn't). Instead the
existing "Testing" section moves to the universal preamble and gets a second
bullet: new DSL/stdlib features need a `.cactus` sample fixture plus a
failing headless-behavior Catch2 test written first, mirroring the existing
`test_*_headless_behavior.cpp` + `examples/*.cactus` pairing already in
`tests/`. One section, two concrete instantiations of the same TDD policy.

**7. `openspec/config.yaml`: populate `context` only.**
`context` is unconditionally shown to the AI during artifact generation;
`rules` are per-artifact constraints (e.g. word limits) — a distinct,
unrelated decision. Filling in `context` closes the "zero project framing"
gap found during explore; `rules` stays untouched to avoid scope creep.
The `context` block stays short (tech stack, TDD requirement, pointer to
`language-philosophy` and to CLAUDE.md itself) rather than re-deriving the
full priority ordering — CLAUDE.md is the detailed source, config.yaml is a
primer that points at it.

## Risks / Trade-offs

- [Risk] The new preamble becomes a dumping ground for every future concern,
  eroding the "concise system prompt" property → [Mitigation] Cap it at
  roughly 15-20 lines; anything needing elaboration goes in its own named
  subsection instead of growing the preamble.
- [Risk] A pointer-only gate rule to `language-philosophy` gets skipped by a
  future session that doesn't actually open the linked file → [Mitigation]
  Phrase the gate as an imperative trigger condition ("before X, check Y"),
  not a passive reference, consistent with how CLAUDE.md's existing hard
  constraints are phrased.
- [Risk] The new "Cactus DSL authoring" section drifts into restating
  language semantics over time, recreating the duplication this change
  removes → [Mitigation] Scope it explicitly to process/quality guidance
  (simplicity bar, prefer stdlib primitives, dedup/nesting) and keep
  semantics out of it by construction.
- [Risk] `openspec/config.yaml` context drifts out of sync with CLAUDE.md's
  preamble as both evolve independently → [Mitigation] Keep config.yaml's
  context deliberately shorter than CLAUDE.md and have it point at CLAUDE.md
  rather than restate priorities in parallel prose.

## Migration Plan

Docs-only change, no build/runtime impact: edit `CLAUDE.md` and
`openspec/config.yaml` directly, no code or test changes required. Rollback
is a plain `git revert` of the commit; no data or state to migrate.

## Open Questions

- Should `openspec/config.yaml`'s `rules:` (per-artifact constraints) also
  get populated at some point? Deferred — doesn't change this change's
  approach or task breakdown, and the user hasn't asked for it.
- Should README.md gain a one-line pointer to CLAUDE.md for human
  contributors (as distinct from the AI-agent framing this change covers)?
  Deferred — orthogonal to restructuring CLAUDE.md itself.
