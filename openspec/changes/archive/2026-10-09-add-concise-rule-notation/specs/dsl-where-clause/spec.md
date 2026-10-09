## ADDED Requirements

### Requirement: Inline row predicates
A regular rule SHALL accept inline `where: predicate, predicate` as equivalent to its existing block predicate list. `each:` SHALL qualify as the same positive unary selection as `filter:`. Boolean typing, purity, predicate evaluation rules, group/row classification during reduction, and spatial recognition SHALL remain unchanged. Inline predicates SHALL remain rejected in selectionless and extern rules under the existing domain restrictions.

#### Scenario: Inline predicates constrain the same rows
- **WHEN** a pair rule uses `where: touching(body, lava), body != lava` with a valid pure geometric function
- **THEN** it retains exactly the rows of the equivalent two-entry block, with the same acceleration decision

#### Scenario: Canonical unary selection supports where
- **WHEN** a rule uses `each: Motion as motion` and `where: motion.speed > 0.0`
- **THEN** it selects the same invocations as the corresponding `filter:` rule

#### Scenario: Inline predicates retain purity and type errors
- **WHEN** an inline predicate is non-boolean or calls an effectful function
- **THEN** compilation rejects that predicate at its own source location
