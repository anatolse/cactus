## ADDED Requirements

### Requirement: Semantic validation of `TraitMatchStmt`
The semantic analyzer SHALL validate `TraitMatchStmt` nodes as follows:
1. The subject expression MUST have type `entity_id`
2. Each `TraitMatchArm.trait_name` MUST resolve to a declared trait in scope
3. If the trait has fields, an alias is optional; if declared, it MUST not conflict with any in-scope name
4. If the trait is a marker (no fields), an alias MUST NOT be declared
5. The wildcard arm `_ =>` is optional and, if present, MUST be the last arm
6. `TraitMatchStmt` MUST only appear inside system event handler bodies
7. Aliases introduced in one arm are NOT in scope in other arms

#### Scenario: Valid entity_id subject accepted
- **WHEN** `match c.other:` and `c.other` is type `entity_id`
- **THEN** the semantic analyzer proceeds with trait pattern matching mode

#### Scenario: Non-entity_id subject at statement level rejected
- **WHEN** `match some_int:` at statement position and `some_int` is type `int`
- **THEN** the semantic analyzer SHALL report: "statement-level `match` subject must be of type `entity_id`"

#### Scenario: Trait arm with unknown trait rejected
- **WHEN** arm `Phantom as p =>` and `Phantom` is not declared
- **THEN** the semantic analyzer SHALL report: "undeclared trait 'Phantom'"

#### Scenario: Alias conflicts with filter binding rejected
- **WHEN** system has `filter: Position as p` and arm is `Boss as p =>`
- **THEN** the semantic analyzer SHALL report: "match arm alias 'p' conflicts with filter alias 'p'"

#### Scenario: Marker trait with alias rejected
- **WHEN** arm `Invincible as inv =>` and `Invincible` has no fields
- **THEN** the semantic analyzer SHALL report: "marker trait 'Invincible' has no fields; alias 'as inv' is not allowed"

#### Scenario: Wildcard before trait arm rejected
- **WHEN** `_ =>` arm appears before a trait arm
- **THEN** the semantic analyzer SHALL report: "wildcard arm `_ =>` must be the last arm"

#### Scenario: Arm alias in scope only within its arm body
- **WHEN** `Boss as b =>` arm ends and subsequent arm `EnemyAI as e =>` begins
- **THEN** `b` is no longer in scope; `e` is in scope only within its arm body
