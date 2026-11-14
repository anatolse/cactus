# schedule-independence-fuzzing Specification

## Purpose
Search automatically for accepted programs whose result depends on the order of handlers the schedule leaves unordered, and for programs that crash the compiler or produce uncompilable code.

## Requirements
### Requirement: Programs are generated deterministically from a seed
The repository SHALL provide a generator that, given an integer seed, writes one well-typed Cactus program in the fragment of `spec/activation_semantics.md` §1, including the entities that start its world. The same seed SHALL always give the same program text.

#### Scenario: Same seed, same program
- **WHEN** the generator runs twice with seed 17
- **THEN** both runs write identical program text

### Requirement: Each generated program is checked for order independence
For each generated program, the check SHALL compile it. When compilation rejects the program, the check SHALL pass if the compiler exits normally with at least one diagnostic. When compilation accepts it, the check SHALL build it with the linearization harness and, for every pair of handlers that an activation's schedule leaves unordered, SHALL run a fixed number of frames twice: in an allowed order where the two handlers are adjacent, and in the same order with only those two swapped. The check SHALL pass only when the two runs of every pair give the same world digest in every frame. A compiler crash, generated code that does not compile, or a digest difference SHALL fail the check and SHALL keep the seed, the program text and, for a digest difference, the canonical identities of the two handlers and the first differing frame.

#### Scenario: Accepted program with matching digests
- **WHEN** a generated program compiles and both orders of every unordered pair give the same digests
- **THEN** the check passes

#### Scenario: Rejected program
- **WHEN** a generated program has an unorientable conflict and the compiler reports it
- **THEN** the check passes

#### Scenario: A digest difference
- **WHEN** swapping handlers `A` and `B` gives a different digest in frame 3
- **THEN** the check fails and keeps the seed, the program, the identities of `A` and `B`, and frame 3

#### Scenario: An accepted program with no unordered pair
- **WHEN** a generated program compiles and its schedules order every two handlers
- **THEN** the check passes and reports zero pairs

### Requirement: The linearization harness can swap one pair
In a build with the linearization harness, the generated program SHALL list, for each activation root, every pair of handlers its schedule leaves unordered, by canonical identity. The harness SHALL be able to run with one listed pair adjacent, in either order, in an order the schedule allows, with every other handler in the same position in both runs. The existing seeded random orders SHALL keep working.

#### Scenario: A pair with a schedule edge is not listed
- **WHEN** handler `A` writes a field that handler `B` reads and the schedule orders `A` before `B`
- **THEN** the pair is not in the list

#### Scenario: Two runs differ only in the pair
- **WHEN** the harness runs a listed pair in both orders
- **THEN** every other handler of that activation runs at the same position in both runs

### Requirement: The test suite runs a small fixed seed set
The normal test suite SHALL run the check for a fixed list of seeds, each taking seconds in a Debug build. A long-run mode SHALL take a seed range and check one program at a time.

#### Scenario: Default suite
- **WHEN** `ctest` runs after building `cactus_tests`
- **THEN** the fixed seed set is checked

#### Scenario: Long run
- **WHEN** a developer runs the long-run mode with seeds 1000 to 1999
- **THEN** each of the 1,000 programs is generated and checked in turn, and failures are kept

### Requirement: Failing programs are shrunk
The repository SHALL provide a shrinker that takes a failing program and removes one rule, handler, statement or entity at a time, keeping each removal only when the check still fails in the same way (for a digest difference, with the same pair of handlers), until no single removal keeps the failure.

#### Scenario: Shrinking a counterexample
- **WHEN** a 7-rule program fails with a digest difference caused by two of its rules
- **THEN** the shrunk program still fails with a digest difference and has no rule whose removal keeps the failure
