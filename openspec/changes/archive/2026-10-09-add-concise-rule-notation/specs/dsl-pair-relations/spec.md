## ADDED Requirements

### Requirement: Compact pair binding trait lists
Each binding inside `pairs:` SHALL accept `binding: TraitA, TraitB as alias` as an equivalent spelling of its existing indented positive trait list. Inline and block bindings SHALL preserve qualified references, binding-local aliases, and source order, and SHALL coexist in the same pair declaration. Each binding SHALL contain at least one trait; the domain SHALL still contain exactly two ordered, uniquely named bindings. An inline binding SHALL NOT have a block continuation.

#### Scenario: Compact aliases retain their binding namespace
- **WHEN** a binding declares `body: Health as health, tf.WorldTransform as pose`
- **THEN** `body.health.current` and `body.pose.position` resolve as they do for the equivalent block declaration

#### Scenario: Inline product retains deterministic behavior
- **WHEN** a compact pair domain selects the same entities as its block-form equivalent
- **THEN** both retain directed self/reverse tuples, stable membership snapshots, tuple order, access restrictions, and acceleration eligibility

#### Scenario: Compact cardinality errors remain errors
- **WHEN** a compact `pairs:` declaration has one or three bindings, duplicate binding names, or an empty binding
- **THEN** compilation rejects it under the existing pair-domain rules

#### Scenario: Extern compact pairs remain rejected
- **WHEN** an extern rule declares compact pair bindings
- **THEN** compilation rejects the pair domain and continues reporting later diagnostics
