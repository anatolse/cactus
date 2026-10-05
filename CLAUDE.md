# Cactus — agent instructions

Use simple English. A concise sentence or question is better than a long, vague one.

A DSL for making games, and tools such as editors, that compiles to native C++ (EnTT ECS + raylib) — see
`README.md` for the project thesis. This file is layered: this preamble applies to
every task in the repo; the named subsections below scope further guidance to their
own area. Cactus DSL authoring rules live in `.claude/rules/cactus-dsl.md` (loaded
for `stdlib/` and `examples/`); rendering diagnosis is the `debug-rendering` skill.

Two goals decide most design questions: the language keeps definitions simple, uses
genre-neutral primitives, and rules out memory, lifetime and data-race errors by
construction; the backend derives the most efficient code from whole-program knowledge.
See `openspec/specs/language-philosophy/spec.md`.

## Change lifecycle (mandatory for substantive changes)

Applies to any change touching `src/common`, `src/frontend`, `src/backends`,
`stdlib/`, or DSL/stdlib-visible behavior — the same scope as "Testing" below.
Docs-only edits, build/config tweaks, and refactors with no observable behavior
change are exempt and may be committed directly.

A substantive change goes through, in order:

1. **Explore** (`/opsx:explore`) — think through the problem, surface risks, decide
   whether it's worth becoming a change.
2. **Propose** (`/opsx:propose`) — generate proposal, design, specs, and tasks in
   one step.
3. **Apply** (`/opsx:apply`) — implement the tasks, TDD per "Testing" below.
4. **Clean up** — run `/simplify` in subagent.
5. **Verify** (`/opsx:verify`) — confirm the cleanup pass didn't drift from
   the artifacts or regress behavior. Also run the coverage gate:
   `cmake --preset clang-coverage` then
   `cmake --build --preset clang-coverage --target coverage_check`. It must pass.
   If gated line coverage is below the threshold, add test cases for the uncovered
   lines (the HTML report it prints shows them). Never lower the threshold.
6. **Archive** (`/opsx:archive`) — sync specs and move the change into
   `openspec/changes/archive/`.

If the verify step surfaces findings, fix them (back to step 3) and re-verify
before moving on — never archive with a known gap open.

## Before changing DSL grammar, stdlib API shape, or language semantics

Check `openspec/specs/language-philosophy/spec.md` (identity, predictability, ECS
boundary, declarative/imperative tiers) and `spec/cactus_dsl_spec.md` (grammar)
first — they're the normative source. Don't restate or improvise around them here.

## Testing

All new code must be covered by unit tests — this applies to every change under
`src/common`, `src/frontend`, `src/backends`, `stdlib/`, not just bug fixes. Prefer
writing the test first (TDD): write a failing Catch2 test in `tests/` that captures
the intended behavior, watch it fail, then implement until it passes. Follow the
existing `test_*.cpp` naming and structure in `tests/`.

A new DSL/stdlib feature additionally needs a `.cactus` sample fixture plus a failing
headless-behavior Catch2 test written first, mirroring the existing
`test_*_headless_behavior.cpp` + `examples/*.cactus` pairing already in `tests/`.

Compiler C++ authoring rules live in `src/CLAUDE.md` (loaded when working under `src/`).

## Comments

Brevity is the sister of talent. Default to no comment. Add one only when the code's WHY
genuinely isn't obvious from reading it — a non-obvious constraint, a workaround for a
specific bug, a subtle invariant — and keep it to one short line. Don't restate WHAT
the code does; the code already says that. Don't reference a spec section, change
name, task number, or issue in the comment — that context belongs in the commit
message or change proposal, not the source, and goes stale once the codebase moves
past it.

