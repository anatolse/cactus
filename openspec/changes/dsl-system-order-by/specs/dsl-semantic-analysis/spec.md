## ADDED Requirements

### Requirement: `order by:` semantic validation
The semantic analyzer SHALL validate `order by:` clauses in system declarations with the following rules:
1. Each sort key alias MUST be declared in the system's `filter:` block
2. Each sort key field MUST exist on the trait bound to that alias
3. The resolved field type MUST be a scalar-comparable type: `int`, `float`, or `bool`
4. For `vec2`/`vec3` field members (e.g., `p.pos.y`): the member name must be a valid component (`x`, `y`, `z`) and resolves to `float`
5. A system with `order by:` MUST have a `filter:` clause

#### Scenario: Valid single-key order by
- **WHEN** `order by: s.layer asc` and `Sprite as s` in filter and `Sprite.layer` is `int`
- **THEN** the semantic analyzer accepts it

#### Scenario: Sort key alias not in filter
- **WHEN** `order by: h.value asc` and `Health as h` is not in `filter:`
- **THEN** the semantic analyzer SHALL report: "sort key alias 'h' is not declared in filter:"

#### Scenario: Sort key field does not exist on trait
- **WHEN** `order by: s.missing_field asc` and `Sprite` has no field `missing_field`
- **THEN** the semantic analyzer SHALL report: "sort key: trait 'Sprite' has no field 'missing_field'"

#### Scenario: vec2 direct sort key rejected
- **WHEN** `order by: p.pos asc` and `Position.pos` is type `vec2`
- **THEN** the semantic analyzer SHALL report: "sort key 'p.pos' has type 'vec2' which is not orderable; use a scalar field or member"

#### Scenario: vec2 member sort key accepted
- **WHEN** `order by: p.pos.y desc` and `Position.pos` is type `vec2`
- **THEN** the semantic analyzer accepts it, resolving the type as `float`

#### Scenario: order by on filterless system rejected
- **WHEN** a system has `order by:` but no `filter:` block
- **THEN** the semantic analyzer SHALL report: "`order by:` requires a `filter:` clause"
