# dsl-rule-order-by Specification

## Purpose
TBD - created by archiving change rename-system-to-rule. Update Purpose after archive.
## Requirements
### Requirement: `order by:` clause on rule declarations
Rule declarations SHALL support an optional `order by:` block on a unary (`filter:`) domain or on a binary `pairs:` domain. When present, the domain's entity or tuple iteration order SHALL be sorted according to the specified sort keys before each handler invocation. A sort key SHALL be a pure expression — of the same expression and purity class as a `where:` predicate (literals and constants, in-scope binding reads, arithmetic and boolean operators, and calls to functions whose complete call graph is proven pure), rather than a bare field path — and MUST type-check as scalar-comparable (`int`, `float`, or `bool`). Multiple sort keys produce lexicographic ordering: the first key is the primary sort, subsequent keys break ties.

```ebnf
order_by_clause = "order" "by" ":" INDENT sort_key+ DEDENT ;
sort_key        = expression [ "asc" | "desc" ] ;
```

Direction is optional; `asc` is the default when omitted.

#### Scenario: Rule with single sort key
- **WHEN** a rule declares `order by: s.layer asc` with `Sprite as s` in its filter
- **THEN** entities are iterated in ascending `Sprite.layer` order each frame

#### Scenario: Rule with multi-key sort
- **WHEN** a rule declares `order by: s.layer asc` then `p.pos.y desc`
- **THEN** entities are sorted by `s.layer` ascending first, then by `p.pos.y` descending as a tiebreaker

#### Scenario: Direction defaults to asc when omitted
- **WHEN** `order by: s.layer` appears with no direction keyword
- **THEN** the sort direction is ascending

#### Scenario: Rule without order by iterates in undefined order
- **WHEN** a rule has no `order by:` clause
- **THEN** entity or tuple iteration order is undefined (implementation-dependent)

#### Scenario: Computed sort key over a single binding is valid
- **WHEN** a unary rule declares `order by: math.length(pos.value - origin) desc`, where `pos` is a filter alias and `origin` is a constant
- **THEN** the semantic analyzer accepts the expression as a sort key and evaluates it once per entity before sorting

#### Scenario: Computed sort key over two pair bindings is valid
- **WHEN** a `pairs:` rule with bindings `actor` and `target` declares `order by: spatial.distance_squared(actor.Position.value, target.Position.value)`
- **THEN** the semantic analyzer accepts the expression as a sort key and evaluates it once per tuple before sorting

### Requirement: Sort keys must reference filter aliases
A sort key expression MUST reference only bindings in scope for the rule's domain: a `filter:` alias for a unary rule, or either of the two pair bindings for a `pairs:` rule. Referencing a name not in scope SHALL be a compile-time error.

#### Scenario: Sort key alias in filter is valid
- **WHEN** `filter:` declares `Sprite as s` and `order by:` uses `s.layer`
- **THEN** the semantic analyzer accepts it

#### Scenario: Sort key alias not in filter is an error
- **WHEN** `order by:` uses `h.value` but `Health as h` is not in `filter:`
- **THEN** the semantic analyzer SHALL report: "sort key alias 'h' is not declared in filter:"

#### Scenario: order by on filterless rule is an error
- **WHEN** a rule has neither a `filter:` block nor a `pairs:` block and declares `order by:`
- **THEN** the semantic analyzer SHALL report: "`order by:` requires a `filter:` or `pairs:` clause"

#### Scenario: Sort key expression spanning both pair bindings is valid
- **WHEN** a `pairs:` rule binds `actor` and `target` and its `order by:` expression reads fields through both `actor` and `target`
- **THEN** the semantic analyzer accepts it, resolving each name through that binding's trait namespace

#### Scenario: Sort key referencing an undeclared pair binding is an error
- **WHEN** a `pairs:` rule binds `actor` and `target` and `order by:` reads a field through a third name
- **THEN** the semantic analyzer SHALL report that the name is not a declared pair binding

### Requirement: Sort key field types must be scalar-comparable
A sort key expression MUST be pure — it must not mutate traits, emit events, spawn or destroy entities, add, remove, or project traits, execute world queries, or call a function whose effects are opaque or unknown — and MUST have a static type of `int`, `float`, or `bool`. Expressions of type `vec2`, `vec3`, `quat`, `color`, `string`, or other non-scalar types SHALL be rejected at compile time. Member access into a `vec2`/`vec3` result (e.g. `.y`) that resolves to `float` is valid.

#### Scenario: int field sort key is valid
- **WHEN** `order by: s.layer asc` and `Sprite.layer` is of type `int`
- **THEN** the semantic analyzer accepts it

#### Scenario: float field sort key is valid
- **WHEN** `order by: p.pos.y desc` and `Position.pos` is `vec2` and `.y` resolves to `float`
- **THEN** the semantic analyzer accepts it (member access of vec2/vec3 resolves to float)

#### Scenario: vec2 field sort key is invalid
- **WHEN** `order by: p.pos asc` and `Position.pos` is of type `vec2`
- **THEN** the semantic analyzer SHALL report: "sort key 'p.pos' has type 'vec2' which is not orderable; use a scalar field or member (e.g., 'p.pos.y')"

#### Scenario: color field sort key is invalid
- **WHEN** `order by: s.tint asc` and `Sprite.tint` is of type `color`
- **THEN** the semantic analyzer SHALL report: "sort key 's.tint' has type 'color' which is not orderable"

#### Scenario: Impure sort key expression is rejected
- **WHEN** a sort key expression emits an event, mutates a trait, or calls a function with unknown or non-empty effects
- **THEN** the semantic analyzer rejects it, using the same purity-violation diagnostic class as an impure `where:` predicate

### Requirement: Sorting applies once per handler invocation before iteration
The sort SHALL be applied before each handler's entity or tuple iteration. If a rule has multiple handlers (e.g., `on tick:` and `on late_tick:`), sorting is performed before each handler's iteration independently. Sorting runs every frame. On a `pairs:` domain, sorting reorders the existing tuple snapshot only — it SHALL NOT add, remove, or otherwise change which tuples are present or how many execute.

#### Scenario: Sort runs before tick handler
- **WHEN** a rule has `order by: s.layer asc` and `on tick:`
- **THEN** the sort is performed once before the `on tick:` handler iterates its entities

#### Scenario: Sort runs before each handler independently
- **WHEN** a rule has `order by: s.layer asc` and both `on tick:` and `on late_tick:`
- **THEN** the sort runs before `on tick:` iteration and again before `on late_tick:` iteration

#### Scenario: Pair-domain sort does not change tuple membership or count
- **WHEN** a `pairs:` rule declares both `where:` and `order by:`
- **THEN** the same tuples selected by `where:` still execute, in a reordered but otherwise unchanged sequence

### Requirement: `order by:` is valid on a pair-domain rule
A `pairs:` rule MAY declare `order by:`. This reorders the deterministic tuple invocation order already established for the pair pass (`dsl-pair-relations`); it SHALL NOT change tuple membership, tuple count, or any other pair-domain semantics such as read-only pair-bound trait access.

#### Scenario: Pairs rule with order by is accepted
- **WHEN** a `pairs:` rule declares `actor` and `target` bindings and an `order by:` block using a valid cross-binding expression
- **THEN** semantic analysis accepts the rule and the tuple pass executes in the specified order

#### Scenario: Pair-domain order by alone does not grant write access
- **WHEN** a `pairs:` rule declares `order by:` with no accompanying cardinality-bounding clause
- **THEN** both pair bindings remain read-only, unchanged from a pairs rule without `order by:`
