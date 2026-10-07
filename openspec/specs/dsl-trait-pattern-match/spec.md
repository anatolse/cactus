# dsl-trait-pattern-match Specification

## Purpose
TBD - created by archiving change dsl-trait-pattern-match. Update Purpose after archive.
## Requirements
### Requirement: `match entity_id:` statement with trait pattern arms
The DSL SHALL support a statement-level `match` construct. When the subject expression has type `entity_id`, the `match` performs **trait pattern matching**: each arm tests whether the referenced entity currently has the named trait attached. Arms execute in declaration order; the first matching arm fires and subsequent arms are skipped. If no arm matches and no wildcard is present, execution continues silently (no error, no-op).

When the subject has type `enum`, `int` or `bool`, the statement is a **value match** instead (see "Statement-level value match"). When the subject is a state slot, the statement is a **state match** (see dsl-exclusive-states). Any other subject type is a compile error.

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

#### Scenario: Slot subject selects a state match
- **WHEN** a handler with `filter: EnemyMode as m` runs `match m:` with variant arms
- **THEN** the semantic analyzer treats it as a state match, not a trait or value match

#### Scenario: match on non-entity_id subject is a type error
- **WHEN** `match some_float:` appears at statement level and `some_float` has a type that is not `entity_id`, `enum`, `int`, `bool` or a state slot
- **THEN** the semantic analyzer SHALL report: "statement-level `match` subject must be `entity_id`, an enum, `int`, `bool` or a state slot, got `<type>`"

### Requirement: Trait arm alias scope and naming
The alias introduced by `TraitName as alias =>` SHALL be in scope for the duration of that arm's body. The alias provides read/write access to the matched trait's fields on the target entity. The alias name MUST NOT conflict with any in-scope name including filter aliases, event alias, and local variables. Marker trait arms (no fields) MUST NOT declare an alias.

#### Scenario: Alias provides read/write access to trait fields
- **WHEN** `Boss as b =>` arm body contains `b.phase += 1`
- **THEN** the semantic analyzer accepts it; `b.phase` resolves to the `Boss.phase` field on the matched entity

#### Scenario: Alias conflicts with filter alias is an error
- **WHEN** the rule has `filter: Position as p` and a match arm uses `Boss as p =>`
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

### Requirement: `match entity_id:` only valid inside rule event handlers
Statement-level `match` on `entity_id` SHALL only appear inside rule event handler bodies. Using it inside a `func` body SHALL be a compile-time error, reported as exactly one diagnostic. A value match has no such restriction: it is allowed in rule handler bodies and in `func` bodies.

#### Scenario: trait match in event handler is valid
- **WHEN** `match c.other:` appears inside `on collision as c:` in a rule
- **THEN** the semantic analyzer accepts it

#### Scenario: trait match outside event handler is invalid
- **WHEN** `match some_id:` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report: "statement-level `match entity_id` only allowed inside rule event handlers"
- **AND** it SHALL report no other diagnostic for that `match` statement

#### Scenario: value match in a func body is valid
- **WHEN** `match mode:` with enum value arms appears inside a `func` body and `mode` is an enum parameter
- **THEN** the semantic analyzer accepts it

### Requirement: Statement-level value match
A statement-level `match` whose subject has type `enum`, `int` or `bool` SHALL be a value match. The subject is evaluated exactly once. Arms are tested in declaration order, and the body of the first arm whose pattern equals the subject runs; the wildcard `_` matches any value. Exactly one arm body runs, because the arms are exhaustive (see "Value match patterns are typed and exhaustive").

Value arms SHALL NOT declare an `as` alias.

#### Scenario: Enum value match runs the matching arm
- **WHEN** a handler runs `match ai.mode:` with arms `Mode.Idle =>`, `Mode.Chase =>` and `Mode.Flee =>`, and `ai.mode` is `Mode.Chase`
- **THEN** only the `Mode.Chase` arm body runs

#### Scenario: Wildcard arm catches remaining values
- **WHEN** a value match on an `int` subject has arms `0 =>` and `_ =>`, and the subject is `7`
- **THEN** the `_` arm body runs

#### Scenario: Alias on a value arm is rejected
- **WHEN** a value match arm is written `Mode.Idle as m =>`
- **THEN** the semantic analyzer reports that value match arms cannot declare an alias

### Requirement: Value match patterns are typed and exhaustive
In a value match, statement or expression, every pattern SHALL have the subject's type. An enum pattern names a variant of the subject's enum. An `int` pattern is an `int` literal or an `int` constant. A `bool` pattern is `true`, `false` or a `bool` constant. The wildcard `_` SHALL be the last arm and appear at most once. Two arms with the same pattern value SHALL be a compile error.

A value match SHALL be exhaustive:
- on an enum subject, the arms name every variant, or the last arm is `_`;
- on a `bool` subject, the arms name both `true` and `false`, or the last arm is `_`;
- on an `int` subject, the last arm is `_`.

A non-exhaustive value match SHALL be a compile error that names the missing enum variants or `bool` values. An enum value match that names every variant and also has `_` is accepted.

#### Scenario: New enum variant breaks an incomplete match
- **WHEN** enum `Mode` gains a variant `Stunned`, and a value match on a `Mode` subject names `Idle`, `Chase` and `Flee` with no `_`
- **THEN** compilation fails with an error at that `match` naming the missing variant `Stunned`

#### Scenario: Pattern of the wrong type is rejected
- **WHEN** a value match on a `Mode` subject has an arm `3 =>`
- **THEN** the semantic analyzer reports that pattern `3` is an `int` but the subject is `Mode`

#### Scenario: Duplicate pattern is rejected
- **WHEN** a value match has two arms with pattern `Mode.Idle`
- **THEN** the semantic analyzer reports the duplicate pattern at the second arm

#### Scenario: Int match without wildcard is rejected
- **WHEN** a value match on an `int` subject has arms `0 =>` and `1 =>` and no `_`
- **THEN** compilation fails with an error stating that a match on `int` needs a `_` arm
