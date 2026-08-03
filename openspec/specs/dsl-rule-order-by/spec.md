# dsl-rule-order-by Specification

## Purpose
TBD - created by archiving change rename-system-to-rule. Update Purpose after archive.
## Requirements
### Requirement: `order by:` clause on rule declarations
Rule declarations SHALL support an optional `order by:` block. When present, the rule's entity iteration order SHALL be sorted according to the specified sort keys before each handler invocation. Sort keys are `alias.field` expressions where `alias` must be declared in the rule's `filter:` block. Multiple sort keys produce lexicographic ordering: the first key is the primary sort, subsequent keys break ties.

```ebnf
order_by_clause = "order" "by" ":" INDENT sort_key+ DEDENT ;
sort_key        = IDENTIFIER "." IDENTIFIER ["asc" | "desc"] ;
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
- **THEN** entity iteration order is undefined (implementation-dependent)

### Requirement: Sort keys must reference filter aliases
Each sort key MUST use an alias that is declared in the rule's `filter:` block. Referencing an alias not in scope SHALL be a compile-time error.

#### Scenario: Sort key alias in filter is valid
- **WHEN** `filter:` declares `Sprite as s` and `order by:` uses `s.layer`
- **THEN** the semantic analyzer accepts it

#### Scenario: Sort key alias not in filter is an error
- **WHEN** `order by:` uses `h.value` but `Health as h` is not in `filter:`
- **THEN** the semantic analyzer SHALL report: "sort key alias 'h' is not declared in filter:"

#### Scenario: order by on filterless rule is an error
- **WHEN** a rule has no `filter:` block and declares `order by:`
- **THEN** the semantic analyzer SHALL report: "`order by:` requires a `filter:` clause"

### Requirement: Sort key field types must be scalar-comparable
Sort key fields MUST have a type that supports ordering: `int`, `float`, or `bool`. Fields of type `vec2`, `vec3`, `quat`, `color`, `string`, or other non-scalar types SHALL be rejected at compile time. Members of `vec2`/`vec3` fields (e.g., `p.pos.y` where `pos` is `vec2`) are valid if the member is a `float`.

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

### Requirement: Sorting applies once per handler invocation before iteration
The sort SHALL be applied before each handler's entity iteration loop. If a rule has multiple handlers (e.g., `on tick:` and `on late_tick:`), sorting is performed before each handler's loop independently. Sorting runs every frame.

#### Scenario: Sort runs before tick handler
- **WHEN** a rule has `order by: s.layer asc` and `on tick:`
- **THEN** the sort is performed once before the `on tick:` handler iterates its entities

#### Scenario: Sort runs before each handler independently
- **WHEN** a rule has `order by: s.layer asc` and both `on tick:` and `on late_tick:`
- **THEN** the sort runs before `on tick:` iteration and again before `on late_tick:` iteration
