## Purpose

Measures how much of the compiler's own C++ the test suite exercises and fails a dedicated build target when global line coverage drops below a configurable threshold, so the TDD rule is enforced by tooling at every change's final verification.

## Requirements

### Requirement: Coverage instrumentation is opt-in and Clang-only
The build SHALL provide a `CACTUS_ENABLE_COVERAGE` option, OFF by default, and a `clang-coverage` configure preset that turns it on with Clang in a `build-clang-coverage/` binary directory inside the repository. Configuring with the option ON and a non-Clang C++ compiler SHALL fail at configure time with a message naming Clang as the requirement.

#### Scenario: Default builds carry no instrumentation
- **WHEN** any existing preset is configured with `CACTUS_ENABLE_COVERAGE` left at its default
- **THEN** no target's compile or link flags SHALL contain coverage instrumentation flags

#### Scenario: Non-Clang compiler is rejected
- **WHEN** the project is configured with `CACTUS_ENABLE_COVERAGE=ON` and a C++ compiler whose id is not Clang
- **THEN** configuration SHALL fail with an error stating coverage requires Clang

### Requirement: Only project-owned code is instrumented
With coverage enabled, instrumentation SHALL be applied only to the project's own compiler, runtime, CLI, and test targets. Third-party dependency targets (Catch2, EnTT, raylib) and generated example targets SHALL NOT be instrumented.

#### Scenario: Generated example targets stay uninstrumented
- **WHEN** the project is configured with coverage enabled
- **THEN** the `compile_commands.json` entries for generated example sources SHALL contain no coverage instrumentation flags, so `ExampleCppCompilationTests` clang-tidy stages behave as in a non-coverage build

#### Scenario: Dependencies stay uninstrumented
- **WHEN** the project is configured with coverage enabled
- **THEN** Catch2, EnTT, and raylib targets SHALL be compiled without coverage instrumentation flags

### Requirement: coverage_check gates on global compiler line coverage
With coverage enabled, a `coverage_check` build target SHALL build every registered test executable (including those excluded from the default build), run the full ctest suite, and compute global line coverage over the gated scope. The target SHALL fail when that coverage is below `CACTUS_COVERAGE_THRESHOLD` percent and SHALL succeed when it is at or above it. `CACTUS_COVERAGE_THRESHOLD` SHALL be a cache variable defaulting to `90`.

#### Scenario: Coverage below threshold fails the target
- **WHEN** `coverage_check` runs and measured gated line coverage is below `CACTUS_COVERAGE_THRESHOLD`
- **THEN** the target SHALL exit non-zero and print the measured percentage and the threshold

#### Scenario: Coverage at or above threshold passes
- **WHEN** `coverage_check` runs, all tests pass, and measured gated line coverage is at or above `CACTUS_COVERAGE_THRESHOLD`
- **THEN** the target SHALL exit zero and print the measured percentage and the threshold

#### Scenario: Threshold is overridable
- **WHEN** the project is configured with `-DCACTUS_COVERAGE_THRESHOLD=85`
- **THEN** `coverage_check` SHALL compare against 85 instead of 90

#### Scenario: Failing tests fail the gate
- **WHEN** any ctest test fails during `coverage_check`
- **THEN** the target SHALL still report measured coverage and SHALL exit non-zero

### Requirement: Gated scope is the compiler's C++
Gated line coverage SHALL be computed over the files under `src/common`, `src/frontend`, `src/backends/cpp-entt`, and `src/main.cpp`, excluding the runtime sources `src/backends/cpp-entt/runtime.*`, `src/backends/cpp-entt/raylib_io.hpp`, `src/backends/cpp-entt/spatial_query.hpp`, and `src/common/cactus_runtime.*`. Test sources, third-party code, and generated code SHALL never count toward it. Runtime coverage SHALL be printed separately for reference and SHALL NOT affect the gate.

#### Scenario: Runtime files do not move the gate
- **WHEN** `coverage_check` reports results
- **THEN** the gated percentage SHALL be computed without any runtime source file, and runtime line coverage SHALL be printed as a separate, non-gating figure

#### Scenario: Test and generated sources are excluded
- **WHEN** `coverage_check` reports results
- **THEN** no file under `tests/`, any dependency directory, or any generated `.cpp` SHALL appear in either the gated or the runtime figure

### Requirement: Coverage counts all test-driven execution, including subprocesses
Profiles written by any instrumented process launched during the ctest run — including `cactus` CLI subprocesses started by tests — SHALL count toward coverage. Profiles written by instrumented processes before the ctest run (such as build-time fixture generation) SHALL NOT count. No instrumented process SHALL write profile files into the source tree.

#### Scenario: CLI subprocess runs contribute coverage
- **WHEN** a test launches the instrumented `cactus` CLI as a subprocess
- **THEN** the lines that subprocess executes SHALL count toward gated coverage

#### Scenario: Build-time generation does not contribute
- **WHEN** `cactus` runs during the build to generate test fixtures before ctest starts
- **THEN** its profile data SHALL be discarded before coverage is computed

#### Scenario: Source tree stays clean
- **WHEN** a coverage build and `coverage_check` complete
- **THEN** no `.profraw` or `.profdata` file SHALL exist anywhere in the source tree outside the coverage binary directory

### Requirement: Coverage results are inspectable
`coverage_check` SHALL write a per-file HTML line-coverage report for the gated scope inside the coverage binary directory and SHALL print its location.

#### Scenario: HTML report is produced
- **WHEN** `coverage_check` finishes, whether it passes or fails the threshold
- **THEN** an HTML report SHALL exist in the coverage binary directory and its path SHALL be printed

### Requirement: The coverage gate is part of every change's final verification
The repository's change-lifecycle guidance in `CLAUDE.md` SHALL require running `coverage_check` in the Verify step of every substantive change, and `openspec/config.yaml` SHALL carry a `tasks` rule requiring each change's `tasks.md` to end with a task that runs `coverage_check`. Both SHALL state that a failing gate is fixed by adding test cases, not by lowering the threshold.

#### Scenario: Lifecycle guidance names the gate
- **WHEN** `CLAUDE.md`'s change-lifecycle Verify step is read
- **THEN** it SHALL require a passing `coverage_check` and SHALL state that falling below the threshold means adding tests

#### Scenario: Generated task lists include the gate
- **WHEN** `openspec/config.yaml` is read
- **THEN** its `rules.tasks` entries SHALL require a final `coverage_check` task in every `tasks.md`
