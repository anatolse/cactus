## ADDED Requirements

### Requirement: Inline pass gates
A regular rule SHALL accept inline `when: predicate, predicate` as equivalent to its existing block gate list. The conjunction SHALL retain game-wide-only scope, boolean typing, purity, implicit named-entity requirements, inferred reads, and evaluation once per handler pass. Both phase and event handlers SHALL obey it; selectionless rules SHALL remain supported and extern rules SHALL remain rejected.

#### Scenario: Inline conjunction gates the whole pass
- **WHEN** a rule uses `when: not Game.Match.over, Game.Match.wave > 0`
- **THEN** every handler pass runs only when both predicates hold, as for the block form

#### Scenario: Binding access remains forbidden in gates
- **WHEN** an inline gate reads a unary or pair binding
- **THEN** compilation rejects that read under the existing game-wide gate restriction

#### Scenario: Gate changes within a pass affect only later passes
- **WHEN** an invocation changes game-wide state read by its inline gate
- **THEN** the gate retains the block form's once-per-pass behavior
