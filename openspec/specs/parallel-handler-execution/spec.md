# parallel-handler-execution Specification

## Purpose
Let a build run independent handlers of one activation on worker threads, chosen by a compiler option and never in source, with the same observable result as a sequential build.

## Requirements

### Requirement: A parallel build means the same as a sequential build
For a given initial world and sequence of host inputs, a program built with `--parallel-handlers` SHALL produce the same sequence of worlds, events and effect calls as the same program built without it, on any number of worker threads. Authored source SHALL NOT say whether a build is parallel.

#### Scenario: Same digest
- **WHEN** an example runs the same scripted input in a sequential and a parallel build
- **THEN** the world digest after every frame is the same

#### Scenario: Thread count does not matter
- **WHEN** a parallel build runs with one worker and with many workers
- **THEN** the world digest after every frame is the same

#### Scenario: No parallel build, no change
- **WHEN** a program is compiled without the option
- **THEN** the generated code contains no worker pool and no thread use

### Requirement: Only eligible handlers run on workers
A handler SHALL be eligible when it is a phase handler of a Cactus rule, its contract has no effect use, no `spawn`, and no projected output, it calls no host facade such as `input.pressed` (directly or through a `func`), and it looks up no derived relation that computes rows per key on first lookup (`closure`, `shortest`). Every other handler SHALL run on the main thread. Event handlers, commit rounds and the event cascade SHALL run on the main thread.

#### Scenario: A rendering handler stays on the main thread
- **WHEN** a handler calls a function with an effect
- **THEN** it does not run on a worker

#### Scenario: A spawning handler stays on the main thread
- **WHEN** a handler contains `spawn`
- **THEN** it does not run on a worker

#### Scenario: A handler reading host input stays on the main thread
- **WHEN** a `tick` handler calls `input.mouse_position()`
- **THEN** it does not run on a worker

#### Scenario: A pure data handler is eligible
- **WHEN** a `tick` handler only reads and writes fields of its domain traits
- **THEN** it can run on a worker

#### Scenario: A handler that reads a shared index is eligible
- **WHEN** a pair handler joins through a shared binding index or reads a `components` relation
- **THEN** it can run on a worker

#### Scenario: A handler that looks up a closure stays on the main thread
- **WHEN** a handler tests `Reach(a, b)` where `Reach` is a `closure` relation
- **THEN** it does not run on a worker

### Requirement: Shared structures are ready before a parallel level
Before handlers of one level run at the same moment, the main thread SHALL bring up to date every shared index, membership and derived cache that a handler of the level reads. During the level, handlers SHALL only read them. The result SHALL equal the sequential build's, where each handler brings them up to date when it starts.

#### Scenario: An index catches up once, before the level
- **WHEN** two handlers of one parallel level read the same shared binding index, and entities joined it in the previous commit
- **THEN** the index catches up once on the main thread before the level, and both handlers see the same candidates as in a sequential build

### Requirement: The main thread runs ineligible handlers while workers run
In a parallel level, the main thread SHALL run the ineligible handlers of a batch in stable order while workers run the batch's eligible handlers. An ineligible handler SHALL run at the same moment as an eligible one only when neither reads or writes a field the other writes.

#### Scenario: A rendering handler overlaps disjoint work
- **WHEN** a level holds an effect handler and an eligible handler, and they share no written field
- **THEN** the effect handler runs on the main thread while the eligible handler runs on a worker

### Requirement: Handlers that run together touch separate data
Two handlers SHALL run at the same moment only when they are in one dependency level and neither reads or writes a field the other writes. Handlers of one level that write one field with commuting writes SHALL NOT run at the same moment.

#### Scenario: Commuting writes are not concurrent
- **WHEN** two eligible handlers in one level both run `+=` on one field
- **THEN** they do not run at the same moment, and the result equals the sequential result

#### Scenario: Disjoint handlers run together
- **WHEN** two eligible handlers of one level write different traits and read neither's writes
- **THEN** they can run at the same moment

### Requirement: Outputs merge in sequential order
Events a handler emits and structural commands it issues SHALL be delivered and applied as if the handlers had run one by one in the stable sequential order, whichever finished first.

#### Scenario: Emissions keep their order
- **WHEN** two parallel handlers each emit an event
- **THEN** the events are queued in the same order as in a sequential build

#### Scenario: Commands keep their order
- **WHEN** two parallel handlers each issue `add` commands
- **THEN** the commit applies them in the same order as in a sequential build
