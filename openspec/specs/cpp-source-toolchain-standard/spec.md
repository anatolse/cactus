# cpp-source-toolchain-standard Specification

## Purpose
TBD - Update Purpose after archive

## Requirements

### Requirement: Compiler source builds under C++23
The compiler's own hand-written source (`src/common`, `src/frontend`, `src/backends`) SHALL be compiled as C++23.

#### Scenario: Project configuration sets the C++ standard to 23
- **WHEN** the top-level `CMakeLists.txt` configures `CMAKE_CXX_STANDARD` for the compiler's own targets
- **THEN** the value SHALL be `23`

#### Scenario: Compiler source builds cleanly under both installed toolchains
- **WHEN** the `msvc` or `clang` CMake preset is configured and built after the standard bump
- **THEN** `src/common`, `src/frontend`, and `src/backends` SHALL compile with no new errors introduced by the standard change

### Requirement: Format config matches the compiler's C++ standard
`.clang-format`'s declared `Standard` SHALL track the C++ standard used to compile the source it formats. Because clang-format's `Standard` enum has no literal `c++23` value (only `Cpp03`/`Cpp11`/`Cpp14`/`Cpp17`/`Cpp20`/`Latest`/`Auto` are recognized — confirmed against the installed clang-format 22.1.6, which errors with "unknown enumerated scalar" on `c++23`), `Latest` is the correct value: it always formats for the newest standard clang-format supports, which is C++23-equivalent today.

#### Scenario: clang-format targets the latest standard
- **WHEN** `.clang-format` is inspected
- **THEN** its `Standard` key SHALL be `Latest`

### Requirement: Claude has a standing, config-derived C++23 authoring rule
A `CLAUDE.md` file SHALL exist at the repository root and SHALL describe, for `src/common`, `src/frontend`, and `src/backends`: which `.clang-tidy` check groups are enforced as build-breaking errors, which checks are explicitly disabled and therefore not required, and which C++23 idioms are recommended for patterns already present in this codebase.

#### Scenario: CLAUDE.md documents enforced checks
- **WHEN** `CLAUDE.md` is read
- **THEN** it SHALL list the `.clang-tidy` check groups that are enabled and subject to `WarningsAsErrors: '*'` (`bugprone-*`, `modernize-*`, `performance-*`, `readability-*`, `cppcoreguidelines-*`, forced `readability-braces-around-statements`)

#### Scenario: CLAUDE.md documents deliberately disabled checks
- **WHEN** `CLAUDE.md` is read
- **THEN** it SHALL list the checks disabled in `.clang-tidy` (e.g. magic-numbers, identifier-length, `pro-bounds-*`, `owning-memory`, `use-trailing-return-type`, `easily-swappable-parameters`) so Claude does not over-apply guidance the config does not enforce

#### Scenario: CLAUDE.md recommends only verified C++23 idioms
- **WHEN** `CLAUDE.md` recommends a specific C++23 idiom (e.g. `std::unreachable()`, deducing `this`, `std::expected`)
- **THEN** that idiom SHALL have been confirmed to compile under both the `msvc` and `clang` presets before being included

### Requirement: Generated-code compilation targets build as C++23
Every target that compiles generated cpp-entt backend output (the example targets, the external-handler fixture targets, and the headless test runners) SHALL target C++23, the same standard as the compiler's own source.

#### Scenario: Example targets request C++23
- **WHEN** the top-level `CMakeLists.txt` configures a target that compiles generated cpp-entt output
- **THEN** that target's C++ standard SHALL be 23 and no such target SHALL request `cxx_std_20`

#### Scenario: Tidy fallback flags use C++23
- **WHEN** the example compilation test runs clang-tidy on generated output without a compilation database
- **THEN** it SHALL pass `-std=c++23` to clang-tidy
