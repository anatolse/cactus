## Requirements

### Requirement: `self` keyword denotes the current entity in system handlers
The language SHALL provide `self` as a reserved keyword expression. Inside a unary entity-selected system event handler, `self` SHALL evaluate to the `entity_id` currently being processed. `self` SHALL be unavailable in selectionless and pair handlers because neither domain has one unique current entity.

#### Scenario: `self` used as destroy target
- **WHEN** a unary entity system handler contains `destroy self`
- **THEN** the compiler accepts `self` as an `entity_id` expression targeting the current entity

#### Scenario: `self` used in trait assignment
- **WHEN** a unary entity system handler assigns `Parent.parent = self`
- **THEN** the compiler accepts the assignment and types `self` as `entity_id`

#### Scenario: `self` rejected for pair relation
- **WHEN** a pair handler uses `self`
- **THEN** the compiler reports that an explicit pair binding is required

#### Scenario: `self` rejected without entity selection
- **WHEN** a selectionless handler uses `self`
- **THEN** the compiler reports that the handler has no current entity

### Requirement: `self` is restricted to world-aware handler contexts
The language SHALL reject `self` outside system event handlers. `self` is not available in top-level declarations, unit/template initialization, or pure `func` bodies.

#### Scenario: `self` in pure function rejected
- **WHEN** a `func` body returns `self`
- **THEN** the compiler reports an error indicating that `self` is only allowed inside system event handlers

#### Scenario: `self` in trait default rejected
- **WHEN** a trait field default value is written as `self`
- **THEN** the compiler reports an error indicating that `self` requires a current entity context

### Requirement: `self` cannot be shadowed by user declarations
Because `self` is a reserved keyword, authors SHALL NOT declare locals, traits, funcs, events, or aliases named `self`.

#### Scenario: local named `self` rejected
- **WHEN** a handler contains `let self = enemy_id`
- **THEN** the parser rejects the declaration because `self` is a reserved keyword