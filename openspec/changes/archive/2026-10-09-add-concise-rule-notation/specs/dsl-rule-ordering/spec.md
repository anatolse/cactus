## ADDED Requirements

### Requirement: Inline ordering references at existing positions
Every existing `after:` list position and rule-level `before:` position SHALL accept a comma-separated inline list alongside its block form. These positions SHALL include rule declarations, leading regular/external handler dependencies, group dependencies, and phase dependencies where each clause is already valid. Entries SHALL use the reference grammar and resolution rules of their position. Inline syntax SHALL NOT introduce `before:` to handlers, groups, or phases. Declaration order, trigger matching, group visibility, cycle diagnostics, and phase barriers SHALL retain their existing meanings.

#### Scenario: Rule ordering accepts several inline references
- **WHEN** a rule declares `after: ReadInput, physics.solve` with valid references
- **THEN** the same ordering constraints are applied as for two block entries

#### Scenario: Leading handler dependency remains a handler dependency
- **WHEN** a regular handler starts with `after: Other/on tick` before its statements
- **THEN** that exact handler reference has the same dependency meaning as its block form

#### Scenario: Group and phase dependencies retain their distinct meanings
- **WHEN** valid group or phase declarations replace their `after:` blocks with equivalent inline lists
- **THEN** group ordering and phase completion barriers retain their respective existing meanings

#### Scenario: Compact order does not bypass cycles or visibility
- **WHEN** inline dependencies introduce a cycle or name an inaccessible group
- **THEN** compilation reports the existing scheduling or visibility error
