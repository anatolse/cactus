# Cactus compiler source — C++ authoring rules

Scope: `src/common`, `src/frontend`, `src/backends` (the compiler's own hand-written
source, built as C++23). Does **not** apply to the cpp-entt backend's generated
output or the 3 targets that compile it (examples, 2 test-runners) — those stay on
C++20 and have clang-tidy disabled independently.

## Hard constraints

`.clang-tidy` enables `bugprone-*`, `modernize-*`, `performance-*`, `readability-*`,
`cppcoreguidelines-*` (each with a few explicit exclusions below), plus
`readability-braces-around-statements` forced back on. `WarningsAsErrors: '*'` — every
enabled check is a build-breaking error, not a suggestion, whenever
`CACTUS_ENABLE_BUILD_CLANG_TIDY` is on. Write code that's clean by construction; don't
rely on the flag being off locally.

Watch item: `bugprone-narrowing-conversions` is enabled and not excluded. It fires
readily on `int`/`size_t`/`float` mixing in numeric and codegen code — use explicit
casts at those boundaries rather than letting an implicit conversion slip through.

Don't disable checks in `.clang-tidy`, and don't add `NOLINT`/`NOLINTNEXTLINE` to
sidestep a check you could fix instead — fix the code. The few `NOLINT`s already in
the tree mark reviewed, deliberate exceptions, not a template to reach for.

## Explicit exceptions

Deliberately disabled in `.clang-tidy` — don't hand-apply the guidance below anyway:

- `readability-magic-numbers`, `cppcoreguidelines-avoid-magic-numbers` — bare numeric
  literals, no named-constant requirement.
- `readability-identifier-length` — short names (`i`, `n`, `ok`) allowed.
- `cppcoreguidelines-pro-bounds-*` — raw pointer arithmetic/indexing, no
  `gsl::span`/`at()` requirement.
- `cppcoreguidelines-owning-memory` — raw owning pointers, no `gsl::owner<T>`
  requirement.
- `modernize-use-trailing-return-type` — ordinary `ReturnType foo()` signatures, not
  required to convert.
- `bugprone-easily-swappable-parameters` — adjacent same-type parameters allowed.

## Avoid duplication

Applies to the compiler's C++ (scope above) and to any `.cactus` source touched in
this repo (`stdlib/`, `examples/`). Before adding a new function, helper, or block of
logic, check whether equivalent behavior already exists — grep for likely names and
scan `src/common` (C++) or the relevant `stdlib` module (Cactus) for something close
before writing a fresh implementation.

- If a suitable version already exists, call it — don't re-implement it locally, even
  in a slightly different shape.
- If what exists is almost-but-not-quite right, extract the shared part into a common
  helper (`src/common` for logic used across frontend/backends; the nearest shared
  Cactus module for stdlib-level logic) and update both call sites to use it, rather
  than copy-pasting and tweaking.
- Never leave two near-identical implementations of the same behavior in the tree
  side by side — divergent copies are how one gets the next bug fix and the other
  doesn't.

## Control-flow nesting — C++ and Cactus

Applies to the compiler's C++ (scope above) and to any `.cactus` source touched in
this repo (`stdlib/`, `examples/`). Avoid step/staircase nesting: each `if` stacked
inside another `if` forces the reader to hold one more live runtime condition to
understand the branch at the bottom. Flatten instead of stacking:

- Combine short, independent conditions with `&&`/`||` (C++) or `and`/`or` (Cactus)
  into one guard instead of nesting them.
- Prefer early return over nesting the happy path: gate on the negated condition and
  `return`/`continue` immediately, one gate per line, rather than wrapping the rest of
  the function/handler body in an `if`.
- Dispatch mutually exclusive cases with `switch`/variant `visit` (C++) or value
  `match` (Cactus, where the arm form in use supports it) instead of an
  `if`/`else if` chain.
- Treat 3 levels of nested executable control flow as a warning sign and 4 as a
  rewrite trigger, in both languages. Structural/declarative nesting (C++ namespaces
  and data aggregates; Cactus `children:` blocks and declarations) isn't executable
  control flow and isn't what this rule targets.

C++ already has a backstop: `readability-*` includes
`readability-function-cognitive-complexity`, which trends toward failing the build
(`WarningsAsErrors: '*'`) as nesting piles up — this rule is the authoring discipline
that keeps you from hitting it, not a new mechanism.

Cactus has no automated nesting lint yet, so this is enforced by hand-review. One
correctness trap when flattening Cactus handlers: `return` exits the *entire* handler
invocation, not a loop iteration — there is no `continue`/`break` in bounded
`for ... in ...:` (see dsl-bounded-foreach). Converting a nested `if` guard inside a
`for` loop into an early `return` changes behavior (it abandons the remaining items)
instead of preserving it; keep the guard nested as an `if` there, or restructure the
loop body, rather than reflexively applying the early-return transform.

## C++23 idioms for this codebase

Verified to compile cleanly under both the `msvc` and `clang` presets before being
listed here (see `openspec/changes/add-cpp23-claude-rule/tasks.md` section 3). Only
reach for these where the existing pattern already invites them — this is not a
general C++23 feature survey.

- **`std::unreachable()`** — the AST is variant-heavy (`ExprNode::Variant`,
  `StmtNode::Variant`, `Declaration` in `ast.hpp`). After an exhaustive
  `std::holds_alternative`/`std::get_if`/`std::visit` dispatch over one of these, mark
  the trailing "can't happen" branch with `std::unreachable()` instead of an assert or
  a fallback `return`.
- **Deducing `this`** — the same variant-heavy AST invites recursive visitor-style
  structs. Use an explicit object parameter (`template <typename Self> auto
  visit(this Self&& self, ...)`) instead of CRTP boilerplate when writing a new
  visitor.
- **`std::expected<T, E>`** — for new, isolated fallible helpers only (a function with
  one clear failure mode, called where the caller wants to branch on success/failure
  locally). This is **not** a replacement for `error_reporter.hpp`'s `ErrorReporter`
  diagnostic-collector pattern, which accumulates multiple diagnostics across a whole
  compile pass rather than short-circuiting on the first error — keep using
  `ErrorReporter` for anything that reports to the user.

## Testing

All new code must be covered by unit tests — this applies to every change under
`src/common`, `src/frontend`, `src/backends`, not just bug fixes. Prefer writing the
test first (TDD): write a failing Catch2 test in `tests/` that captures the intended
behavior, watch it fail, then implement until it passes. Follow the existing
`test_*.cpp` naming and structure in `tests/`.

## Formatting

`.clang-tidy`'s `FormatStyle: 'file'` couples `--fix` to `.clang-format` already —
formatting is mechanical and applied for you. Don't hand-apply indent/brace/column
rules; if in doubt, match what's already in the surrounding file.
