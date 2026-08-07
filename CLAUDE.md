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
