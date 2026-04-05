# dsl-trait-pattern-match Specification

## Purpose
TBD - created by archiving change dsl-trait-pattern-match. Update Purpose after archive.
## Requirements
### Requirement: `match entity_id:` statement with trait pattern arms
The DSL SHALL support a statement-level `match` construct. When the subject expression has type `entity_id`, the `match` performs **trait pattern matching**: each arm tests whether the referenced entity currently has the named trait attached. Arms execute in declaration order; the first matching arm fires and subsequent arms are skipped. If no arm matches and no wildcard is present, execution continues silently (no error, no-op).

When the subject handle is stale, no arm fires — see dsl-entity-id-total-semantics.

```ebnf
trait_match_stmt = "match" expr ":" INDENT trait_match_arm+ DEDENT ;
trait_match_arm  = trait_arm | wildcard_arm ;
trait_arm        = IDENTIFIER ["as" IDENTIFIER] "=>" stmt+ ;
wildcard_arm     = "_" "=>" stmt+ ;
```

#### Scenario: Trait match arm with alias fires when entity has trait
- **WHEN** `match c.other:` is executed and the entity referenced by `c.other` has the `Boss` component attached
- **THEN** the `Boss as b =>` arm executes with `b` bound to the `Boss` component data

#### Scenario: Trait match arm without alias fires for marker trait
- **WHEN** `match c.other:` is executed and the entity has `Spike` attached (marker trait, no fields)
- **THEN** the `Spike =>` arm executes; no alias binding is needed

#### Scenario: First matching arm wins
- **WHEN** an entity has both `Boss` and `EnemyAI` and the match has `Boss as b =>` before `EnemyAI as e =>`
- **THEN** only the `Boss as b =>` arm executes; `EnemyAI as e =>` is skipped

#### Scenario: No-match with no wildcard is silent no-op
- **WHEN** none of the listed trait arms match and there is no `_ =>` arm
- **THEN** execution continues after the match block with no error

#### Scenario: Wildcard arm fires when no trait arm matched
- **WHEN** `_ =>` is the last arm and no trait arm matched the entity
- **THEN** the wildcard arm body executes

#### Scenario: match on non-entity_id subject is a type error
- **WHEN** `match some_int:` appears at statement level and `some_int` is type `int`
- **THEN** the semantic analyzer SHALL report: "statement-level `match` subject must be of type `entity_id`; use expression-level match for value dispatch"

### Requirement: Trait arm alias scope and naming
The alias introduced by `TraitName as alias =>` SHALL be in scope for the duration of that arm's body. The alias provides read/write access to the matched trait's fields on the target entity. The alias name MUST NOT conflict with any in-scope name including filter aliases, event alias, and local variables. Marker trait arms (no fields) MUST NOT declare an alias.

#### Scenario: Alias provides read/write access to trait fields
- **WHEN** `Boss as b =>` arm body contains `b.phase += 1`
- **THEN** the semantic analyzer accepts it; `b.phase` resolves to the `Boss.phase` field on the matched entity

#### Scenario: Alias conflicts with filter alias is an error
- **WHEN** the system has `filter: Position as p` and a match arm uses `Boss as p =>`
- **THEN** the semantic analyzer SHALL report: "match arm alias 'p' conflicts with filter alias 'p'"

#### Scenario: Marker trait arm with alias is an error
- **WHEN** `Spike as s =>` appears and `Spike` is a marker trait with no fields
- **THEN** the semantic analyzer SHALL report: "marker trait 'Spike' has no fields; alias 'as s' is not allowed"

### Requirement: Wildcard arm is optional and must be last
The wildcard arm `_ =>` is optional. If present, it MUST be the final arm in the match block. Multiple wildcard arms or a wildcard arm before a trait arm SHALL be a compile-time error.

#### Scenario: Wildcard arm accepted at end of match
- **WHEN** `_ =>` appears as the last arm after all trait arms
- **THEN** the semantic analyzer accepts it

#### Scenario: Wildcard arm before trait arm is an error
- **WHEN** `_ =>` appears before a `TraitName =>` arm
- **THEN** the semantic analyzer SHALL report: "wildcard arm `_ =>` must be the last arm in a trait match"

### Requirement: `match entity_id:` only valid inside system event handlers
Statement-level `match` on `entity_id` SHALL only appear inside system event handler bodies. Using it inside a `func` body SHALL be a compile-time error.

#### Scenario: trait match in event handler is valid
- **WHEN** `match c.other:` appears inside `on collision as c:` in a system
- **THEN** the semantic analyzer accepts it

#### Scenario: trait match outside event handler is invalid
- **WHEN** `match some_id:` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report: "statement-level `match entity_id` only allowed inside system event handlers"

