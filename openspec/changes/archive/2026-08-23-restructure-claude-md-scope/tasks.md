## 1. Universal preamble (new, top of CLAUDE.md)

- [x] 1.1 Write a short preamble (~15-20 lines, per design.md's mitigation
      cap) that applies to all tasks in the repo, not just compiler C++:
      one line anchoring the project thesis (point to README.md, don't
      restate it) plus a short statement that the doc is now layered into
      named subsections below.
- [x] 1.2 Add the DSL-philosophy gate rule as an imperative trigger
      condition: before changing DSL grammar, stdlib API shape, or language
      semantics, check `openspec/specs/language-philosophy/spec.md` (identity/
      predictability/ECS boundary) and `spec/cactus_dsl_spec.md` (grammar).
      Pointer only — no restatement of their content.
- [x] 1.3 Move the existing "Testing" section here from the C++-scoped
      section and add a second bullet alongside the existing Catch2/TDD one:
      a new DSL/stdlib feature needs a `.cactus` sample fixture plus a
      failing headless-behavior Catch2 test written first, mirroring the
      existing `test_*_headless_behavior.cpp` + `examples/*.cactus` pairing
      in `tests/`.

## 2. Re-scope the existing C++ authoring section

- [x] 2.1 Retitle the current top-level doc/scope line
      ("Cactus compiler source — C++ authoring rules" / "Scope: ...") into a
      named subsection heading, e.g. "## Compiler C++ authoring rules
      (`src/common`, `src/frontend`, `src/backends`)", so it reads as one
      section of the layered doc rather than the whole document's scope.
- [x] 2.2 Remove the old standalone "Testing" section from this subsection
      (superseded by 1.3) — do not leave a duplicate copy.
- [x] 2.3 In "Avoid duplication" and "Control-flow nesting", remove the
      `.cactus`/`stdlib`/`examples` bullets and "applies to ... `.cactus`
      source touched" clauses from this subsection; replace each with a
      one-line cross-reference to the new Cactus section (task 3) instead of
      keeping the content in both places.
- [x] 2.4 Add a short "evaluate as a compiler pass" note (redundant tree-
      walks, error recovery, lookup cost) anchored to `error_reporter.hpp`,
      `execution_graph_scheduler.hpp`, and `symbol_identity.hpp` as reference
      shapes — not a general compiler-theory checklist.

## 3. New Cactus DSL authoring section

- [x] 3.1 Create "## Cactus DSL authoring rules" scoped to `stdlib/`,
      `examples/`, positioned as a subsection parallel to the C++ one.
- [x] 3.2 Move the `.cactus`-applicable dedup guidance here (relocated from
      2.3, not duplicated): check `stdlib/` for an existing equivalent
      before adding new Cactus logic, extract shared behavior into the
      nearest shared stdlib module rather than copy-pasting.
- [x] 3.3 Move the `.cactus`-applicable nesting guidance here (relocated
      from 2.3, not duplicated), including the existing correctness note
      about `return` exiting the whole handler rather than a loop iteration
      in bounded `for ... in ...:`.
- [x] 3.4 Add the game-dev simplicity bar: new stdlib/example additions are
      evaluated against gameplay-core teachability (if it can't be explained
      to a beginner in a sentence, it likely belongs in a different layer),
      and prefer existing stdlib primitives (math/physics/transform/camera/
      render/ui) over hand-rolled logic in example or game code.

## 4. New generated-code-performance section

- [x] 4.1 Create "## Generated code performance" stating explicitly that
      generated output stays exempt from lint/style rules (unchanged from
      today), but the backend's codegen strategy is judged on the runtime
      speed of the emitted game code above all else — the priority ordering
      is generated-code runtime speed, then compiler-developer velocity,
      then the compiler's own compile time.
- [x] 4.2 Cross-reference the existing convention that shared,
      program-independent logic belongs in `cactus_runtime.hpp`/`.cpp`
      rather than emitted text.

## 5. `openspec/config.yaml` context

- [x] 5.1 Populate the `context:` field with a short (3-4 line) primer:
      tech stack (C++23 compiler + Cactus DSL + Catch2), the TDD
      requirement, and a pointer to both `language-philosophy` and
      CLAUDE.md itself as the detailed source — do not restate CLAUDE.md's
      full priority ordering here.
- [x] 5.2 Leave `rules:` untouched (commented template only) — confirmed
      out of scope per design.md.

## 6. Verification

- [x] 6.1 Re-read the full restructured CLAUDE.md top to bottom; confirm
      every rule (dedup, nesting, testing, etc.) appears in exactly one
      place, not duplicated across sections.
- [x] 6.2 Diff against the pre-change CLAUDE.md to confirm all existing
      C++-section content survived the restructure (relocated/retitled, not
      dropped).
- [x] 6.3 Confirm the preamble stays within the ~15-20 line cap from
      design.md's mitigation for section 1.
- [x] 6.4 Run `openspec validate restructure-claude-md-scope` (docs-only
      change, `skip_specs: true` already set) and resolve any reported
      issues before archiving.
