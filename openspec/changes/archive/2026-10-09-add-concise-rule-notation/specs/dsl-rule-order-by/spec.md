## ADDED Requirements

### Requirement: Inline sort-key lists
A valid `order by:` position SHALL accept comma-separated inline sort keys alongside its block form. Each entry SHALL preserve its optional direction, with `asc` the existing default. Lexicographic key order, scope, purity, comparable typing, evaluation timing, and existing stable tie-breaking SHALL remain unchanged. Inline syntax SHALL preserve the current placement of sorting after `reduce:` when reduction is present.

#### Scenario: Computed keys retain nested arguments and directions
- **WHEN** a pair rule uses `order by: distance(a.pose.position, b.pose.position) asc, b.Priority.value desc` with a valid pure distance function
- **THEN** it parses two keys and ranks tuples exactly as the corresponding block

#### Scenario: Aggregate sorting uses retained scope
- **WHEN** `reduce per team:` is followed by `order by: total desc` and a global limit
- **THEN** the aggregate groups are ranked and limited with the legacy reduction behavior

#### Scenario: Compact sorting preserves placement errors
- **WHEN** a reduced rule places its inline `order by:` before its reduction
- **THEN** compilation retains the existing clause-placement error
