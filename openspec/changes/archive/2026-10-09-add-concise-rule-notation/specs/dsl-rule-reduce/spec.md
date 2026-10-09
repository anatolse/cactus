## ADDED Requirements

### Requirement: Header-level reduction grouping
A regular rule SHALL accept `reduce per binding:` with an indented, nonempty reducer declaration block as equivalent to `reduce:` with a leading body-level `per: binding`. The header's binding SHALL obey the existing declared-pair-binding restriction. The ungrouped form SHALL remain `reduce:`. A clause SHALL reject simultaneous header-level and body-level grouping, even when both names are equal. Reducer names, typing, purity, and clause placement SHALL obey their existing rules.

#### Scenario: Header and body grouping are equivalent
- **WHEN** equivalent pair rules use `reduce per team:` and `reduce:` with `per: team`
- **THEN** they retain the same groups, retained writable binding, aggregates, contracts, and handler scope

#### Scenario: Empty groups retain identities
- **WHEN** a retained team passes its group predicates but has no matching player rows
- **THEN** either grouping spelling produces the same aggregate row with existing reducer identities

#### Scenario: Ranking and keep retain their timing
- **WHEN** grouped rules use header grouping with aggregate sorting/limits or a `keep` result
- **THEN** ranking, aggregate snapshots, commit visibility, and kept-trait lifecycle notifications equal the legacy spelling

#### Scenario: Duplicate grouping is rejected
- **WHEN** `reduce per team:` also contains `per: team` or `per: other` in its body
- **THEN** compilation reports duplicate grouping

#### Scenario: Invalid grouped header is rejected
- **WHEN** the header omits its binding, names an unknown binding, uses grouping on a unary rule, or contains no reducers
- **THEN** compilation reports the corresponding grammar or grouping error without abandoning later declarations
