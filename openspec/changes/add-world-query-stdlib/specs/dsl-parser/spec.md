## ADDED Requirements

### Requirement: Query expression grammar supports bracketed trait filters on member calls
The parser SHALL accept query expressions formed from a normal member expression followed by a bracketed query-filter list and a call suffix. The bracketed filter list SHALL support comma-separated positive traits and `not TraitName` negative predicates.

#### Scenario: Parse module-qualified world query
- **WHEN** source contains `std.query.exists[Boss]()` in expression position
- **THEN** the parser treats `std.query.exists` as the member target and parses the bracketed query filter and call suffix on that target

#### Scenario: Parse world query with one trait filter
- **WHEN** source contains `query.exists[Boss]()` in expression position and `query` is an imported alias
- **THEN** the parser produces an expression node representing the member target `query.exists`, the filter list `[Boss]`, and an empty call argument list

#### Scenario: Parse query with negative trait filter
- **WHEN** source contains `query.count[EnemyAI, not Dead]()`
- **THEN** the parser accepts `not Dead` as a negative query-filter predicate in the bracket list

### Requirement: Query call grammar supports named arguments
The parser SHALL accept named arguments in query call expressions using `IDENTIFIER = expr` syntax inside the call parentheses.

#### Scenario: Parse nearest query with named argument
- **WHEN** source contains `query.nearest[Transform, Enemy](from = player_pos)`
- **THEN** the parser produces a query call expression with a named argument `from`

#### Scenario: Parse overlap query with multiple named arguments
- **WHEN** source contains `query.overlap_box[Pickup](center = p, size = s)`
- **THEN** the parser accepts both named arguments and preserves their names in the AST