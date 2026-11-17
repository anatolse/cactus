## Purpose

Let one rule run its phase pass a constant number of times inside an activation, so a step that needs several fresh world queries needs no copied rules, and its cost is known at compile time.

## Requirements
### Requirement: Repeat clause shape
A rule SHALL accept one `repeat: N` clause, written after `reduce:` and before its handler. `repeat` SHALL be a contextual word at the rule-clause position. `N` SHALL be a constant expression of type `int` with a value of at least 2. A count that is not a constant, is not an `int`, or is below 2 SHALL be a source-located error. A keep rule and an extern rule SHALL reject the clause with a source-located error.

#### Scenario: Unary repeat
- **WHEN** a rule with `each: Settle as s` declares `repeat: 4` and one `fixed_tick` handler
- **THEN** compilation accepts it

#### Scenario: Count from a field
- **WHEN** a rule declares `repeat: s.passes` and `passes` is a field
- **THEN** the compiler reports an error at the clause saying the count must be a constant

#### Scenario: Count of one
- **WHEN** a rule declares `repeat: 1`
- **THEN** the compiler reports an error at the clause saying the count must be at least 2

#### Scenario: Keep rule
- **WHEN** a keep rule declares `repeat: 3`
- **THEN** the compiler reports an error at the clause

### Requirement: A repeating rule has one meaning per pass
A repeating rule SHALL have exactly one handler, triggered by a phase. Its handler SHALL NOT `emit`, `spawn`, `destroy`, `add`, `remove`, `set` or `project`, and SHALL NOT call a function that acts on an effect domain; observing calls are allowed. The rule SHALL NOT declare `keep`, `limit:` or `order by:`. Each violation SHALL be a source-located error that names the construct and says a repeating rule may only write fields.

#### Scenario: Emit in a repeating handler
- **WHEN** a repeating rule's handler contains `emit Bumped`
- **THEN** the compiler reports an error at the `emit`

#### Scenario: Two handlers
- **WHEN** a repeating rule declares a `fixed_tick` handler and an `on Hit` handler
- **THEN** the compiler reports an error at the second handler

#### Scenario: Limit on a repeating rule
- **WHEN** a repeating rule declares `limit: 1`
- **THEN** the compiler reports an error at the `limit:` clause

### Requirement: The pass runs exactly N times
In each activation of its phase, a rule with `repeat: N` SHALL run its pass exactly `N` times, one after another. Every pass SHALL run every row of the rule's domain, and SHALL compute `where:`, `when:` and `reduce per` groups from the world the previous pass left. Field writes of a pass SHALL be visible to the next pass. Accumulated deltas of the rule SHALL merge as any handler's do: the rule never reads a field it accumulates into, so no pass can see a difference. The whole repetition SHALL be one handler run in the schedule, with the contract of one pass.

#### Scenario: Three passes
- **WHEN** a rule with `repeat: 3` adds 1 to a field of its row in a `fixed_tick` handler
- **THEN** the field is 3 higher after one `fixed_tick` activation

#### Scenario: A pass sees the previous pass
- **WHEN** pass 1 moves a body and pass 2 sweeps from the body's position
- **THEN** pass 2's sweep starts where pass 1 left the body

#### Scenario: A filter is evaluated again
- **WHEN** pass 1 sets `s.done = true` and the rule has `where: not s.done`
- **THEN** that row does not run in pass 2

#### Scenario: One schedule node
- **WHEN** another handler writes a field the repeating rule reads
- **THEN** the schedule orders it before or after the whole repetition, never between two passes
