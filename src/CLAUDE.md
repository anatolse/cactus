# Compiler C++ authoring rules (`src/common`, `src/frontend`, `src/backends`)

Built as C++23. Does **not** apply to the cpp-entt backend's generated output or the 3
targets that compile it (examples, 2 test-runners) — those stay on C++20 and have
clang-tidy disabled independently.

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

Before adding a new function, helper, or block of logic, check whether equivalent
behavior already exists — grep for likely names and scan `src/common` for something
close before writing a fresh implementation.

- If a suitable version already exists, call it — don't re-implement it locally, even
  in a slightly different shape.
- If what exists is almost-but-not-quite right, extract the shared part into a common
  helper in `src/common` (used across frontend/backends) and update both call sites to
  use it, rather than copy-pasting and tweaking.
- Never leave two near-identical implementations of the same behavior in the tree
  side by side — divergent copies are how one gets the next bug fix and the other
  doesn't.

For `.cactus` source (`stdlib/`, `examples/`), see "Avoid duplication" under Cactus
DSL authoring rules in the root `CLAUDE.md` — same stance, different module boundary.

## Control-flow nesting

Avoid step/staircase nesting: each `if` stacked inside another `if` forces the reader
to hold one more live runtime condition to understand the branch at the bottom.
Flatten instead of stacking:

- Combine short, independent conditions with `&&`/`||` into one guard instead of
  nesting them.
- Prefer early return over nesting the happy path: gate on the negated condition and
  `return`/`continue` immediately, one gate per line, rather than wrapping the rest of
  the function body in an `if`.
- Dispatch mutually exclusive cases with `switch`/variant `visit` instead of an
  `if`/`else if` chain.
- Treat 3 levels of nested executable control flow as a warning sign and 4 as a
  rewrite trigger. Structural/declarative nesting (C++ namespaces and data
  aggregates) isn't executable control flow and isn't what this rule targets.

`readability-*` includes `readability-function-cognitive-complexity`, which trends
toward failing the build (`WarningsAsErrors: '*'`) as nesting piles up — this rule is
the authoring discipline that keeps you from hitting it, not a new mechanism.

For `.cactus` source (`stdlib/`, `examples/`), see "Control-flow nesting" under Cactus
DSL authoring rules in the root `CLAUDE.md` — same principle, different syntax, plus a Cactus-specific
correctness trap around `return` in bounded loops.

## Evaluate as a compiler pass

When reviewing or writing a frontend/backend change, weigh it the way you'd weigh any
compiler pass: redundant tree walks over the same AST, lookup cost in symbol/scope
resolution, and graceful error recovery (don't abort on the first diagnostic). Use
`error_reporter.hpp` (diagnostic accumulation), `execution_graph_scheduler.hpp` (pass
ordering/dependency resolution), and `symbol_identity.hpp` (identity/lookup) as
reference shapes for these concerns rather than inventing new ones.

## C++23 idioms for this codebase

Only reach for these where the existing pattern already invites them — this is not a
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
