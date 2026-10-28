# dsl-rule-limit Specification

## Purpose

Define the `limit:` clause on rule declarations: bounding the number of handler activations globally or per pair-binding partition, its place in the filter/where/order-by/limit pipeline, its determinism guarantees, and the static proof that grants a per-binding-limited pair binding write access.

## Requirements

### Requirement: `limit:` clause on rule declarations
A regular rule with a unary (`each:`) or pair (`pairs:`) domain SHALL support an optional `limit:` clause. `limit:` SHALL be rejected on a selectionless rule (neither `each:` nor `pairs:`) and on `extern rule` declarations. `limit:` logically applies after `where:` and `order by:`: it bounds the already-filtered and, if present, ordered domain, without changing which rows survive `where:` or their relative order. A `limit:` clause SHALL be global (one count, no `per`), one-sided (one `per` part) or two-sided (two `per` parts naming different partition keys).

```ebnf
limit_clause = "limit" ":" ( expression | limit_part [ "," limit_part ] ) NEWLINE ;
limit_part   = expression "per" limit_key ;
limit_key    = IDENTIFIER [ "." ( "from" | "to" ) ] ;
```

#### Scenario: Global limit on a unary domain is accepted
- **WHEN** an `each:` rule declares `limit: 10`
- **THEN** at most 10 entities produce a handler activation, chosen by the domain's order

#### Scenario: Global limit on a pair domain is accepted
- **WHEN** a `pairs:` rule declares `limit: 10` with no `per`
- **THEN** at most 10 tuples in total produce a handler activation

#### Scenario: Limit on a selectionless rule is rejected
- **WHEN** a rule with neither `each:` nor `pairs:` declares `limit:`
- **THEN** semantic analysis reports that `limit:` requires an `each:` or `pairs:` clause

#### Scenario: Limit is rejected on extern rule
- **WHEN** an `extern rule` declares `limit:`
- **THEN** semantic analysis rejects it

#### Scenario: Two-sided limit is parsed
- **WHEN** a `pairs:` rule declares `limit: 1 per robot, 1 per task`
- **THEN** the clause has two `per` parts, for `robot` and for `task`

#### Scenario: Two parts on one binding
- **WHEN** a `pairs:` rule declares `limit: 1 per robot, 2 per robot`
- **THEN** semantic analysis reports that the two parts must name different partition keys

A partition key SHALL be an entity pair binding or a direct `from` or `to` endpoint of a row binding, as defined by "Relation endpoints partition allocation limits". Endpoint partitions SHALL also be supported on unary row domains.

### Requirement: Per-binding limit requires a pair domain
The bare `per <binding>` form of `limit:` SHALL be accepted only on a `pairs:` rule, and `<binding>` MUST name one of that rule's entity pair bindings. A direct row endpoint partition SHALL instead follow "Relation endpoints partition allocation limits", including on unary row domains. For every distinct value of the named binding among the tuples surviving `where:`, at most the limit's count of tuples SHALL produce a handler activation.

#### Scenario: Per-binding limit is accepted on a pairs rule
- **WHEN** a `pairs:` rule with bindings `actor` and `target` declares `limit: 1 per actor`
- **THEN** each distinct `actor` value produces at most one tuple activation

#### Scenario: Per-binding limit is rejected on a unary rule
- **WHEN** an `each:` rule declares `limit: 1 per something`
- **THEN** semantic analysis rejects it, since a unary entity domain has no pair binding to partition by

#### Scenario: Per-binding limit naming an undeclared binding is rejected
- **WHEN** a pairs rule binds `actor` and `target` and declares `limit: 1 per enemy`
- **THEN** semantic analysis reports that `enemy` is not a declared pair binding

#### Scenario: An actor with no surviving tuples produces no activation
- **WHEN** `limit: 1 per actor` is declared and a particular `actor` has zero tuples surviving `where:`
- **THEN** no handler activation occurs for that `actor`

### Requirement: Limit count expressions are pure and scoped to available bindings
A `limit:` count expression MUST be pure — the same purity class as a `where:` predicate or `order by:` sort key — and MUST have static type `int`. A global `limit:`'s count expression MAY reference only constants. A per-binding `limit ... per <binding>`'s count expression MAY additionally reference `<binding>`'s own trait fields, but MUST NOT reference the rule's other pair binding.

#### Scenario: Constant global limit is valid
- **WHEN** a rule declares `limit: 10`
- **THEN** semantic analysis accepts it

#### Scenario: Per-binding limit computed from the per binding's own trait is valid
- **WHEN** a pairs rule declares `limit: tower[Tower].target_count per tower`
- **THEN** semantic analysis accepts it, evaluating the count once per distinct `tower` value

#### Scenario: Per-binding limit referencing the other binding is rejected
- **WHEN** a pairs rule with bindings `actor` and `target` declares `limit: target[Threat].priority per actor`
- **THEN** semantic analysis rejects it, since the count would depend on the binding it does not partition by

#### Scenario: Impure limit expression is rejected
- **WHEN** a `limit:` count expression emits an event or calls a function with unknown effects
- **THEN** semantic analysis rejects it

#### Scenario: Non-int limit expression is rejected
- **WHEN** a `limit:` count expression has static type `float` or `bool`
- **THEN** semantic analysis rejects it

For a direct row endpoint key, the count SHALL reference only constants; row fields, entity binding fields and endpoint trait reads SHALL NOT be accepted. The count SHALL have the same purity and static `int` requirements. A count at or below zero SHALL retain no candidate for that partition. Existing entity-key count scopes SHALL remain unchanged.

#### Scenario: Endpoint capacity is constant
- **WHEN** a row rule declares `limit: SEATS per offer.to`, where `SEATS` is a constant `int` equal to 3
- **THEN** at most three retained candidates share an `offer.to` value

#### Scenario: Row-dependent endpoint capacity is rejected
- **WHEN** the endpoint limit count reads `offer.capacity`
- **THEN** compilation reports that endpoint capacity counts may reference only constants

### Requirement: Limit selection is deterministic
Row or tuple selection under `limit:` SHALL be deterministic: sort keys, when `order by:` is present, are evaluated against the rule's snapshot; ties break by stable domain order; without `order by:`, `limit` uses the domain's existing stable iteration order. An entity-key-only one-sided limit's partitions are each resolved independently, in the creation order of the `per` binding's own values. A two-sided limit SHALL be resolved as one greedy pass over the whole ordered domain (see "Two-sided limit selects a greedy matching"). A conforming backend's physical implementation MUST NOT change which rows are selected relative to this logical definition.

#### Scenario: Tie-break uses creation order
- **WHEN** two candidates share an equal `order by:` key under a `limit: 1`
- **THEN** the one with the earlier creation ordinal is selected

#### Scenario: Limit without order by uses stable domain order
- **WHEN** a `pairs:` rule declares `limit: 1 per actor` with no `order by:`
- **THEN** the retained tuple for each `actor` is the first one in left-binding-major snapshot order

#### Scenario: Partitions resolve independently in per-binding creation order
- **WHEN** `limit: 1 per actor` is declared
- **THEN** each `actor`'s retained tuple is determined without regard to any other `actor`'s candidates, and partitions are processed in the creation order of their `actor` values

An endpoint-bearing limited domain SHALL retain and invoke candidates in filtered/sorted domain order, using existing row or left-binding-major order for ties. It SHALL NOT introduce a new ordering by endpoint handles or by endpoint creation order. Every candidate tuple SHALL count separately; tuples with equal endpoint keys SHALL NOT be implicitly deduplicated. Queued commands SHALL NOT change the pass snapshot or its selection counters.

#### Scenario: Row creation order resolves endpoint ties
- **WHEN** two stored candidate rows compete for one endpoint with equal sort keys, and row creation order differs from endpoint creation order
- **THEN** the earlier candidate row is retained

#### Scenario: Distinct witnesses remain candidates
- **WHEN** two distinct joined tuples have the same endpoint keys under endpoint capacity two
- **THEN** both can be retained, consuming two units of each capacity

### Requirement: A global limit grants no pair-binding write access
A global `limit: N` (no `per`) bounds total row or tuple count only. It SHALL NOT make any pair binding writable, since it does not guarantee any single binding value occurs at most once among the retained rows.

#### Scenario: Global limit on a pairs rule leaves both bindings read-only
- **WHEN** a `pairs:` rule declares `limit: 10` with no `per`
- **THEN** mutating either pair binding is rejected exactly as it would be without any `limit:` clause

### Requirement: A per-binding limit of statically-provable one is recognized
A per-binding `limit: <expr> per <binding>` SHALL be recognized as provable to admit at most one tuple per `<binding>` value when, and only when, `<expr>` constant-folds to the literal `1`. This proof SHALL be purely syntactic/constant-evaluated: a non-constant expression, or a constant other than `1`, SHALL NOT be provable, even if it happens to always evaluate to `1` at runtime. Provability determines the `<binding>`'s write access under the pair-relations read-only carve-out.

#### Scenario: Literal one is provable
- **WHEN** a rule declares `limit: 1 per actor`
- **THEN** the `actor` binding is recognized as provably limited to one

#### Scenario: An arithmetic constant equal to one is provable
- **WHEN** an entity or direct-endpoint partition uses a compile-time integer expression such as `2 - 1` that evaluates to one under Cactus integer semantics
- **THEN** that exact key is recognized as statically one and runtime selection uses the same capacity value
#### Scenario: A named constant equal to one is provable
- **WHEN** a rule declares `limit: MAX_ONE per actor` and `MAX_ONE` is a compile-time constant equal to `1`
- **THEN** the `actor` binding is recognized as provably limited to one

#### Scenario: A non-constant count is not provable
- **WHEN** a rule declares `limit: tower[Tower].target_count per tower`
- **THEN** the `tower` binding is not recognized as provably limited to one, regardless of `target_count`'s runtime value

#### Scenario: A constant other than one is not provable
- **WHEN** a rule declares `limit: 2 per actor`
- **THEN** the `actor` binding is not recognized as provably limited to one

The same static-one test SHALL recognize direct row endpoint partition keys. That proof SHALL apply only to the exact resolved endpoint expression and SHALL discharge relation `one` proof obligations as specified by `dsl-relations`; it SHALL NOT grant row writes, endpoint trait reads or endpoint trait mutation. Equality predicates SHALL NOT transfer that proof to another key.

#### Scenario: Endpoint uniqueness does not imply trait access
- **WHEN** a rule has `limit: 1 per place.from` and reads or writes `place.from[Robot].speed`
- **THEN** compilation still rejects endpoint trait access

#### Scenario: Larger capacity is not a one proof
- **WHEN** a rule has `limit: 2 per place.from`
- **THEN** `place.from` is not proven unique

### Requirement: Two-sided limit selects a greedy matching
A two-sided `limit: n per a, m per b` SHALL be accepted when `a` and `b` are distinct valid partition keys: entity pair bindings or direct row endpoints. Unary row domains SHALL accept two distinct endpoint keys. Bare row bindings SHALL NOT be keys. Each part SHALL follow the count-expression rules of a one-sided part, scoped to its own key. Selection SHALL walk the tuples that pass `where:` in the rule's order: `order by:` keys first, then left-binding-major snapshot order. A tuple SHALL be retained when its `a` value has fewer than `n` retained tuples and its `b` value has fewer than `m`. Otherwise it SHALL be skipped. Each entity-key part whose count is provably `1` SHALL retain its existing binding write proof (`dsl-pair-relations`); a statically-one endpoint key SHALL supply only the endpoint uniqueness proof. A skipped candidate SHALL consume no capacity. Counter sets SHALL be independent for the two key roles, even if their runtime entity values coincide. Capacities SHALL apply to retained candidates in this pass, not to persistent occupancy; `where:` SHALL determine eligibility against existing allocations.

#### Scenario: Nearest-first matching
- **WHEN** robots `r1`, `r2` and tasks `t1`, `t2` have distances `r1-t1 = 1`, `r2-t1 = 2`, `r1-t2 = 3`, `r2-t2 = 4`, and the rule declares `order by: dist` and `limit: 1 per robot, 1 per task`
- **THEN** the retained tuples are `(r1, t1)` and `(r2, t2)`

#### Scenario: More robots than tasks
- **WHEN** three robots and one task pass `where:` under `limit: 1 per robot, 1 per task`
- **THEN** exactly one tuple is retained, the first in the rule's order

#### Scenario: Ties break by snapshot order
- **WHEN** two tuples for one task have equal sort keys under `limit: 1 per robot, 1 per task`
- **THEN** the tuple earlier in left-binding-major snapshot order is retained

#### Scenario: Two-sided limit on a row binding
- **WHEN** a pair rule with a row binding `r` declares `limit: 1 per w, 1 per r`
- **THEN** semantic analysis reports that a relation row is not a partition key and suggests choosing `r.from` or `r.to`

#### Scenario: Greedy is not maximum cardinality
- **WHEN** the only eligible candidates are `(r1,t1,1)`, `(r2,t1,2)` and `(r1,t2,3)`, ordered by ascending distance under capacity one on both ends
- **THEN** only `(r1,t1)` is retained, even though a different matching could retain two candidates

#### Scenario: A rejected candidate reserves nothing
- **WHEN** an earlier accepted candidate fills robot r1, the next candidate is `(r1,t2)`, and a later candidate is `(r2,t2)`
- **THEN** skipping `(r1,t2)` does not reserve t2, so `(r2,t2)` can be retained

#### Scenario: Repeated activations start fresh
- **WHEN** the same limited rule runs in a later activation after its candidate relation changes
- **THEN** capacities are recomputed from that pass alone and no previous selection counter is retained

### Requirement: Relation endpoints partition allocation limits
A `limit:` partition key SHALL accept a direct `r.from` or `r.to` when `r` is a declared row binding in the rule's unary row domain or pair domain. A rule SHALL accept one or two distinct partition keys, including both endpoints of one row, endpoints of different rows, and an endpoint paired with an entity binding. All existing domain, join-bound and row-read-only constraints SHALL remain in force. A key SHALL NOT be an arbitrary expression, a row field, an undeclared binding, a bare row, an endpoint of an entity binding, or a nested member path. Invalid keys SHALL produce source-located diagnostics. Repeating the same resolved syntactic key SHALL be rejected. Different keys equated by a predicate SHALL remain separate capacity roles and SHALL NOT acquire each other's uniqueness proof.

#### Scenario: Unary row allocation
- **WHEN** a rule declares `each: Candidate as candidate` and `limit: 1 per candidate.from, 1 per candidate.to`
- **THEN** compilation accepts endpoint capacity selection over its candidate rows

#### Scenario: Route-ranked robot allocation
- **WHEN** a valid bounded pair joins `place: Located` and `route: Route` through `route.from == place.to` and declares `limit: 1 per place.from, 1 per route.to`
- **THEN** capacity is keyed by the robot handle and destination handle, rather than by either row's identity

#### Scenario: Mixed domain allocation
- **WHEN** an entity/row pair declares `limit: 1 per actor, 3 per offer.to`
- **THEN** each actor appears at most once and each destination appears at most three times among retained tuples

#### Scenario: Invalid endpoint key
- **WHEN** a rule declares `limit: 1 per candidate.distance` or `limit: 1 per candidate.from.owner`
- **THEN** compilation rejects it with an endpoint-key diagnostic

#### Scenario: Duplicate endpoint key
- **WHEN** a rule declares two parts both keyed by `candidate.from`
- **THEN** compilation rejects the duplicate partition key
