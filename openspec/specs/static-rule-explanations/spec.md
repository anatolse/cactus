## Purpose

Explain one rule from the compiler's own facts: what it selects, what it reads and writes, why it is ordered where it is, and what shape its cost has, without running the program.

## Requirements

### Requirement: The CLI explains one rule
The CLI SHALL accept `--explain-rule <qualified-rule>` for a linked program that passes analysis. It SHALL print, for each handler of that rule, its source location, selection domain, phase activation, the reads and writes of its contract, each schedule edge to or from it with the other handler's canonical identity and the edge's reason, its cost term as `--report-cost` prints it, and the evaluation level for a keep rule. It SHALL exit successfully after printing, with no code generation. An unknown rule name SHALL be a diagnostic and exit code 1. The output SHALL depend only on the linked program.

#### Scenario: An inferred edge
- **WHEN** the requested rule runs after a handler that writes a field it reads
- **THEN** the explanation names both handlers, the trait and field, and the edge's reason

#### Scenario: An explicit edge
- **WHEN** the requested rule declares `after: Lift`
- **THEN** the explanation lists the edge from `Lift` with the reason that the rule declares it

#### Scenario: Unknown rule
- **WHEN** the requested qualified name does not exist
- **THEN** the compiler reports it and exits with code 1

#### Scenario: No code is generated
- **WHEN** the user runs `cactus game.cactus --explain-rule game.Fire`
- **THEN** no output file is written

### Requirement: Explanations state only static facts
An explanation SHALL NOT print a number of matched entities, a predicate's value, or any other fact that depends on a run. Where a selection depends on run-time values, it SHALL say so. The cost term and the keep level SHALL be the same text `--report-cost` prints for that handler.

#### Scenario: A run-time filter
- **WHEN** the requested rule has `where: unit.health < 10.0`
- **THEN** the explanation shows the filter as a run-time condition and prints no match count

#### Scenario: The two views agree
- **WHEN** a program is compiled once with `--report-cost` and once with `--explain-rule` for one of its rules
- **THEN** the handler's cost term is the same text in both outputs
