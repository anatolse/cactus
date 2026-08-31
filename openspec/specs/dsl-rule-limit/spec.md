# dsl-rule-limit Specification

## Purpose

Define the `limit:` clause on rule declarations: bounding the number of handler activations globally or per pair-binding partition, its place in the filter/where/order-by/limit pipeline, its determinism guarantees, and the static proof that grants a per-binding-limited pair binding write access.

## Requirements

### Requirement: `limit:` clause on rule declarations
A regular rule with a unary (`filter:`) or pair (`pairs:`) domain SHALL support an optional `limit:` clause. `limit:` SHALL be rejected on a selectionless rule (neither `filter:` nor `pairs:`) and on `extern rule` declarations. `limit:` logically applies after `where:` and `order by:`: it bounds the already-filtered and, if present, ordered domain, without changing which rows survive `where:` or their relative order.

```ebnf
limit_clause = "limit" ":" expression [ "per" IDENTIFIER ] NEWLINE ;
```

#### Scenario: Global limit on a unary domain is accepted
- **WHEN** a `filter:` rule declares `limit: 10`
- **THEN** at most 10 entities produce a handler activation, chosen by the domain's order

#### Scenario: Global limit on a pair domain is accepted
- **WHEN** a `pairs:` rule declares `limit: 10` with no `per`
- **THEN** at most 10 tuples in total produce a handler activation

#### Scenario: Limit on a selectionless rule is rejected
- **WHEN** a rule with neither `filter:` nor `pairs:` declares `limit:`
- **THEN** semantic analysis reports that `limit:` requires a `filter:` or `pairs:` clause

#### Scenario: Limit is rejected on extern rule
- **WHEN** an `extern rule` declares `limit:`
- **THEN** semantic analysis rejects it

### Requirement: Per-binding limit requires a pair domain
The `per <binding>` form of `limit:` SHALL be accepted only on a `pairs:` rule, and `<binding>` MUST name one of that rule's two pair bindings. For every distinct value of the named binding among the tuples surviving `where:`, at most the limit's count of tuples SHALL produce a handler activation.

#### Scenario: Per-binding limit is accepted on a pairs rule
- **WHEN** a `pairs:` rule with bindings `actor` and `target` declares `limit: 1 per actor`
- **THEN** each distinct `actor` value produces at most one tuple activation

#### Scenario: Per-binding limit is rejected on a unary rule
- **WHEN** a `filter:` rule declares `limit: 1 per something`
- **THEN** semantic analysis rejects it, since a unary domain has no second binding to partition by

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
- **WHEN** a pairs rule declares `limit: tower.Tower.target_count per tower`
- **THEN** semantic analysis accepts it, evaluating the count once per distinct `tower` value

#### Scenario: Per-binding limit referencing the other binding is rejected
- **WHEN** a pairs rule with bindings `actor` and `target` declares `limit: target.Threat.priority per actor`
- **THEN** semantic analysis rejects it, since the count would depend on the binding it does not partition by

#### Scenario: Impure limit expression is rejected
- **WHEN** a `limit:` count expression emits an event or calls a function with unknown effects
- **THEN** semantic analysis rejects it

#### Scenario: Non-int limit expression is rejected
- **WHEN** a `limit:` count expression has static type `float` or `bool`
- **THEN** semantic analysis rejects it

### Requirement: Limit selection is deterministic
Row or tuple selection under `limit:` SHALL be deterministic: sort keys, when `order by:` is present, are evaluated against the rule's snapshot; ties break by stable creation order; without `order by:`, `limit` uses the domain's existing stable iteration order. A per-binding limit's partitions are each resolved independently, in the creation order of the `per` binding's own values. A conforming backend's physical implementation MUST NOT change which rows are selected relative to this logical definition.

#### Scenario: Tie-break uses creation order
- **WHEN** two candidates share an equal `order by:` key under a `limit: 1`
- **THEN** the one with the earlier creation ordinal is selected

#### Scenario: Limit without order by uses stable domain order
- **WHEN** a `pairs:` rule declares `limit: 1 per actor` with no `order by:`
- **THEN** the retained tuple for each `actor` is the first one in left-binding-major snapshot order

#### Scenario: Partitions resolve independently in per-binding creation order
- **WHEN** `limit: 1 per actor` is declared
- **THEN** each `actor`'s retained tuple is determined without regard to any other `actor`'s candidates, and partitions are processed in the creation order of their `actor` values

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

#### Scenario: A named constant equal to one is provable
- **WHEN** a rule declares `limit: MAX_ONE per actor` and `MAX_ONE` is a compile-time constant equal to `1`
- **THEN** the `actor` binding is recognized as provably limited to one

#### Scenario: A non-constant count is not provable
- **WHEN** a rule declares `limit: tower.Tower.target_count per tower`
- **THEN** the `tower` binding is not recognized as provably limited to one, regardless of `target_count`'s runtime value

#### Scenario: A constant other than one is not provable
- **WHEN** a rule declares `limit: 2 per actor`
- **THEN** the `actor` binding is not recognized as provably limited to one
