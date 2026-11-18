# rule-execution-traces Specification

## Purpose

Record what happened in a run, per handler and per commit, so authors can connect event cascades and commit timing to a surprising result, with bounded cost and no change in behavior.

## Requirements

### Requirement: Bounded opt-in runtime trace

The compiler SHALL accept `--trace-rules` when generating cpp-entt output. That program SHALL expose a host-configured trace sink and capacity and, in its standalone runner, `--trace-rules-output <path>` and `--trace-rules-capacity <positive-int>`. Traces SHALL use versioned JSON Lines and name handlers by the canonical identities that `--report-cost` prints. Records SHALL distinguish phase, repetition and cascade activation, candidate and invoked counts, target delivery or no-match, queued structural commands and actual commit barriers. Trace capture SHALL NOT change any result of the program, and SHALL never write trait payloads by default.

#### Scenario: Deferred spawn
- **WHEN** `Fire` queues a `Bullet` spawn and `MoveBullet` runs before that activation commits
- **THEN** the trace records the queued command, `MoveBullet`'s actual invocation count and the later commit, and does not claim the bullet was visible earlier

#### Scenario: Repeated fixed steps
- **WHEN** a frame runs two fixed repetitions
- **THEN** records of the two repetitions carry different activation ids and their own commit barriers

#### Scenario: Target no-match
- **WHEN** an event targets a stale entity
- **THEN** the trace records a no-match with that handle's identity

#### Scenario: Traced and untraced runs agree
- **WHEN** one program runs the same inputs with and without `--trace-rules`
- **THEN** the world digest after every frame is the same

### Requirement: Trace cost and overflow are explicit

Builds without `--trace-rules` SHALL generate no per-invocation tracing branch and no trace buffer. An instrumented program SHALL stop recording after the configured per-frame capacity, write a final overflow summary with the number of dropped records, and continue gameplay normally. Capacity SHALL bound the number of buffered records. A sink error SHALL disable further tracing with a diagnostic and SHALL NOT stop gameplay. An invalid capacity on the command line SHALL be rejected. Tracing with no sink SHALL NOT keep unbounded history.

#### Scenario: Overflow
- **WHEN** a frame produces more records than the capacity
- **THEN** the output ends that frame with an overflow summary, and the world state matches an untraced run

#### Scenario: Disabled output
- **WHEN** the same program is compiled without `--trace-rules`
- **THEN** its generated gameplay paths contain no tracing call

#### Scenario: Output failure
- **WHEN** the sink becomes unwritable
- **THEN** tracing is disabled with a diagnostic and gameplay continues

#### Scenario: Deterministic trace
- **WHEN** two runs use the same inputs and starting state
- **THEN** their trace records are identical
