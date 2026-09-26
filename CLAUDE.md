# Cactus — agent instructions

Use simple English. A concise sentence or question is better than a long, vague one.

A DSL for making games that compiles to native C++ (EnTT ECS + raylib) — see
`README.md` for the project thesis. This file is layered: this preamble applies to
every task in the repo; the named subsections below scope further guidance to their
own area (compiler C++, Cactus DSL content, generated-code performance).

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
   the artifacts or regress behavior.
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

## Diagnosing rendering issues

`CACTUS_RAYLIB_FAKE` headless tests never call `LoadShaderFromMemory` or issue a real
GL draw — a green test suite proves the generated C++ compiles, not that anything
renders. If a raylib/GLSL draw path compiles, links, and dispatches with no errors but
produces no visible output (or wrong output), don't guess at shader/uniform logic
first. Build a throwaway debug target that `#include`s the generated `.cpp` directly
(`CACTUS_GENERATED_NO_MAIN`, mirroring `tests/example_manual_generated_main.cpp`) with
a hand-written `main()` that drives a fixed number of frames against a real GL context
and calls `TakeScreenshot()`. Inspect the PNG directly instead of relying on a human to
watch the window — this turns "does it render" into a fast, repeatable loop and
isolates the failing layer (geometry vs. shader vs. draw call vs. state) by swapping
one piece at a time. Delete the harness once the bug is found; it's a diagnostic tool,
not a shipped test.

## Comments

Brevity is the sister of talent. Default to no comment. Add one only when the code's WHY
genuinely isn't obvious from reading it — a non-obvious constraint, a workaround for a
specific bug, a subtle invariant — and keep it to one short line. Don't restate WHAT
the code does; the code already says that. Don't reference a spec section, change
name, task number, or issue in the comment — that context belongs in the commit
message or change proposal, not the source, and goes stale once the codebase moves
past it.

## Cactus DSL authoring rules

Scope: `.cactus` source in `stdlib/` and `examples/`.

### Avoid duplication

Before adding new Cactus logic, check whether equivalent behavior already exists in
`stdlib/` — scan the relevant module for something close before writing a fresh
implementation.

- If a suitable version already exists, use it — don't re-implement it locally, even
  in a slightly different shape.
- If what exists is almost-but-not-quite right, extract the shared part into the
  nearest shared `stdlib` module and update both call sites, rather than copy-pasting
  and tweaking.
- Never leave two near-identical implementations of the same behavior side by side —
  divergent copies are how one gets the next bug fix and the other doesn't.

### Control-flow nesting

Avoid step/staircase nesting: each `if` stacked inside another `if` forces the reader
to hold one more live runtime condition to understand the branch at the bottom.
Flatten instead of stacking:

- Combine short, independent conditions with `and`/`or` into one guard instead of
  nesting them.
- Prefer early return over nesting the happy path: gate on the negated condition and
  `return` immediately, one gate per line, rather than wrapping the rest of the
  handler body in an `if`.
- Dispatch mutually exclusive cases with value `match` (where the arm form in use
  supports it) instead of an `if`/`else if` chain.
- Treat 3 levels of nested executable control flow as a warning sign and 4 as a
  rewrite trigger. Structural/declarative nesting (`children:` blocks and
  declarations) isn't executable control flow and isn't what this rule targets.

Cactus has no automated nesting lint yet, so this is enforced by hand-review. One
correctness trap when flattening Cactus handlers: `return` exits the *entire* handler
invocation, not a loop iteration — there is no `continue`/`break` in bounded
`for ... in ...:` (see dsl-bounded-foreach). Converting a nested `if` guard inside a
`for` loop into an early `return` changes behavior (it abandons the remaining items)
instead of preserving it; keep the guard nested as an `if` there, or restructure the
loop body, rather than reflexively applying the early-return transform.

### Game-dev simplicity bar

New stdlib/example additions are evaluated against gameplay-core teachability: if it
can't be explained to a beginner in a sentence, it likely belongs in a different
layer. Prefer existing stdlib primitives (math/physics/transform/camera/render/ui)
over hand-rolled logic in game code and showcase examples (teaching examples are the
exception — see `examples/CLAUDE.md`).

## Generated code performance

Generated output (the cpp-entt backend's emitted code and the 3 targets that compile
it) stays exempt from lint/style rules — unchanged. But the backend's codegen
*strategy* is judged on the runtime speed of the code it emits above all else.
Priority order when a change trades one against another: generated-code runtime speed
first, compiler-developer velocity second, the compiler's own compile time a distant
third.

Shared, program-independent logic (helpers, operators) belongs in
`cactus_runtime.hpp`/`.cpp` rather than emitted text — codegen should call it, not
duplicate it.
