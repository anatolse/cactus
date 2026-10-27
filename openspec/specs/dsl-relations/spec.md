# dsl-relations Specification

## Purpose

Define named binary relations between entities: their declaration, row identity and lifetime, kept and stored relations, `one` ends and the compile-time proof that keeps them, row bindings, and relation tests in `where:`.

## Requirements

### Requirement: Relation declaration

A module SHALL accept a top-level declaration `[pub] relation R:` whose body declares, in this order, a `from:` end, a `to:` end, and zero or more fields. Each end SHALL name exactly one durable trait, optionally followed by the contextual word `one`. A field SHALL use the trait field grammar with `let` only. A `var` field SHALL be rejected with help that says to replace the row with `add R(a, b):`. A `persist` modifier on a relation field SHALL be rejected. A missing end, a second `from:` or `to:`, an end that names a state variant, a projected trait or more than one trait, and a relation name that clashes with another declaration in the module SHALL be source-located compile errors. `relation` SHALL be a keyword. `one` SHALL be contextual at end positions only.

```ebnf
relation_decl = [ "pub" ] "relation" IDENTIFIER ":" NEWLINE INDENT
                "from" ":" dotted_name [ "one" ] NEWLINE
                "to" ":" dotted_name [ "one" ] NEWLINE
                { field_decl }
                DEDENT ;
```

#### Scenario: Many-to-many relation with a field
- **WHEN** a module declares `relation Walk:` with `from: Tile`, `to: Tile` and `let cost: float = 1.0`
- **THEN** compilation accepts it

#### Scenario: One-to-one relation
- **WHEN** a module declares `relation Claim:` with `from: Robot one` and `to: DigTask one`
- **THEN** compilation accepts it

#### Scenario: Var field is rejected
- **WHEN** a relation declares `var cost: float = 1.0`
- **THEN** the compiler reports an error at the field whose help says to replace the row with `add R(a, b):`

#### Scenario: Missing end
- **WHEN** a relation declares `from: Tile` and no `to:`
- **THEN** the compiler reports a source-located error naming the missing `to:` end

#### Scenario: Imported end trait
- **WHEN** a relation declares `from: tf.WorldTransform` through an import alias
- **THEN** the end resolves to the canonical imported trait

### Requirement: A row is identified by its pair of ends

A relation SHALL be a set of rows. A row SHALL be identified by its ordered pair `(from, to)` of entities, so a relation holds at most one row per pair. A row SHALL have no entity identity of its own: it carries no traits, receives no events and cannot be a `spawn`, `destroy` or `emit` target. A row with `from == to` SHALL be allowed. Rows SHALL be ordered by row creation: the commit position of the command or keep run that created the row.

#### Scenario: A second add of the same pair replaces the row
- **WHEN** one round adds `Walk(a, b):` with `cost = 1.0` and a later round adds `Walk(a, b):` with `cost = 3.0`
- **THEN** `Walk` holds one row `(a, b)` with `cost = 3.0`, and its creation position is unchanged

#### Scenario: Reverse pair is a different row
- **WHEN** a program adds `Walk(a, b)` and `Walk(b, a)`
- **THEN** `Walk` holds two rows

#### Scenario: Self row
- **WHEN** a program adds `Walk(a, a)`
- **THEN** `Walk` holds the row `(a, a)`

### Requirement: A row lives only while both ends are valid

A row SHALL be removed when its `from` or `to` entity is destroyed, or loses the trait its end names. The removal SHALL happen at the commit, at the position of the command that destroyed the entity or removed the trait. No relation row SHALL ever refer to a destroyed entity, or to an entity without its end trait.

#### Scenario: Destroying an end removes its rows
- **WHEN** a commit destroys tile `b` while `Walk` holds `(a, b)` and `(b, c)`
- **THEN** after that commit `Walk` holds neither row

#### Scenario: Losing the end trait removes the row
- **WHEN** a commit removes `DigTask` from `t` while `Claim` holds `(r, t)`
- **THEN** after that commit `Claim` holds no row for `t`, and `r` can claim again

#### Scenario: Earlier tuples see the old rows
- **WHEN** a handler destroys tile `b` and a later handler in the same activation tests `Walk(a, b)`
- **THEN** the later handler's test is true, because the destroy applies at the commit

### Requirement: A relation is kept or stored

A relation that a `keep R(a, b)` clause names SHALL be a kept relation. Every other relation SHALL be a stored relation. A program SHALL have at most one `keep` clause per relation; a second one SHALL be an error that names both rules. `add R` and `remove R` of a kept relation SHALL be compile errors that name its keep rule. A kept relation SHALL NOT mark an end `one`; the error's help SHALL say to store the relation, or to keep a trait with `reduce per` and `best(...)`.

#### Scenario: Add of a kept relation
- **WHEN** `WalkLinks` keeps `Walk` and a handler writes `add Walk(a, b)`
- **THEN** the compiler reports that `Walk` is kept by `WalkLinks`

#### Scenario: Two keepers
- **WHEN** two rules both declare `keep Walk(a, b)`
- **THEN** the compiler reports an error naming both rules

#### Scenario: One end on a kept relation
- **WHEN** a relation with `from: Robot one` is named by a `keep` clause
- **THEN** the compiler reports an error at `one` whose help suggests storing the relation

### Requirement: Kept relation rows follow the rule's tuples

`keep R(a, b)` SHALL be accepted on a `pairs:` rule without `reduce:`, where `a` and `b` are the rule's two entity bindings in either order. The binding passed for `from` SHALL select `R`'s `from` end trait, and the binding passed for `to` SHALL select its `to` end trait. The clause body SHALL assign zero or more fields of `R` from pure expressions over both bindings, constants and named entity fields. A field it leaves out SHALL take its declared default, and a field with no default SHALL be assigned. At each run, `R` SHALL hold exactly one row per tuple that passes `where:`, with that tuple's field values. Rows SHALL be added, updated and removed at the run's commit. Kept relations SHALL follow the same run, `when:`, `group:` and no-handler rules as kept traits (`dsl-rule-keep`).

#### Scenario: Rows track the where: clause
- **WHEN** `WalkLinks` keeps `Walk(a, b)` where `a` and `b` are adjacent floor tiles, and a tile next to `a` becomes floor
- **THEN** after the next run `Walk` holds a row from `a` to the new floor tile and from it to `a`

#### Scenario: Field from both bindings
- **WHEN** `keep Walk(a, b):` assigns `cost = a[Tile].height + b[Tile].height`
- **THEN** compilation accepts it, and each row's `cost` comes from its own tuple

#### Scenario: Binding lacks the end trait
- **WHEN** `Walk` has `from: Tile` and the rule's binding `a` selects only `Floor`
- **THEN** the compiler reports that `a` must select `Tile` to be the `from` end of `Walk`

#### Scenario: Paused keeper
- **WHEN** a kept relation's rule has `when:` false for a run
- **THEN** every row and its field values stay as they were

### Requirement: Stored relation commands

A handler SHALL accept `add R(x, y)`, optionally with a field block, and `remove R(x, y)` on a stored relation. `x` and `y` SHALL be `entity_id` expressions. In `remove`, either argument MAY be `_`, which matches any entity. These SHALL be structural commands that apply at the activation commit in command order, like `add T` and `remove T`. At its position in the commit:
- `add` SHALL create the row, or replace the field values of an existing row. Fields the block leaves out SHALL take their defaults.
- `add` SHALL do nothing when either end is stale or does not carry its end trait.
- `remove` SHALL delete every matching row, and do nothing when none match.

#### Scenario: Add then remove in one round
- **WHEN** one handler adds `Claim(r, t)` and a later handler in the same activation removes `Claim(r, t)`
- **THEN** after the commit `Claim` holds no row `(r, t)`

#### Scenario: Wildcard remove
- **WHEN** a handler writes `remove Claim(robot, _)`
- **THEN** every `Claim` row with `from == robot` is removed at the commit

#### Scenario: Add with a stale end
- **WHEN** a handler adds `Claim(r, t)` and `t` is destroyed earlier in the same commit
- **THEN** the add does nothing

#### Scenario: Wildcard in add
- **WHEN** a handler writes `add Claim(robot, _)`
- **THEN** the compiler reports a source-located error that `_` is allowed only in `remove` and in tests

### Requirement: `one` ends are proved at compile time

An end marked `one` SHALL hold at most one row per entity on that end, in every committed state. The compiler SHALL accept an `add R(x, y)` of a relation with a `one` end only when it can prove that the add keeps the limit. For each `one` end, with `e` the argument at that end, all of the following SHALL hold:
- `e` is `self` of a unary rule, or a pair binding that the rule's `limit:` provably limits to one (`dsl-rule-limit`).
- The other argument is a pair binding, `self`, a named entity, or a constant expression.
- The handler's trigger is a phase, so the handler runs once per activation.
- The rule's `where:` contains, as a top-level conjunct, the test that `e` has no row on that end: `not R(e, _)` for `from`, `not R(_, e)` for `to`.
- No other handler in the program that runs in the same phase contains an `add R`.

Any other `add R` of such a relation SHALL be a source-located compile error that names the end and the first condition that fails, with help that shows the proven form. Restore SHALL reject a document that breaks a `one` end (`world-persistence`).

#### Scenario: Proven claim
- **WHEN** a `pairs:` rule over `robot` and `task` declares `limit: 1 per robot, 1 per task`, `where: not Claim(robot, _)` and `not Claim(_, task)`, and an `on fixed_tick:` handler that adds `Claim(robot, task)`
- **THEN** compilation accepts it

#### Scenario: Missing limit
- **WHEN** the same rule declares only `limit: 1 per robot`
- **THEN** the compiler reports an error that the `to` end of `Claim` is `one` and `task` is not limited to one

#### Scenario: Missing existence test
- **WHEN** the rule omits `not Claim(robot, _)`
- **THEN** the compiler reports an error that names `not Claim(robot, _)` as the missing `where:` test

#### Scenario: Event-triggered add
- **WHEN** an `on OrderGiven:` handler adds `Claim(robot, task)`
- **THEN** the compiler reports an error that the handler can run more than once per activation, with help that suggests recording the order as a trait and claiming in a phase rule

#### Scenario: Two adders in one phase
- **WHEN** two rules in the `fixed_tick` phase both add `Claim`
- **THEN** the compiler reports an error naming both rules

#### Scenario: Unary owner
- **WHEN** relation `Owns` has `to: Room one`, and a unary `fixed_tick` rule over rooms with `where: not Owns(_, self)` adds `Owns(Player, self)`
- **THEN** compilation accepts it

### Requirement: Relation rows as rule bindings

A rule SHALL bind relation rows in two places:
- A `pairs:` binding whose only entry is a relation name, `r: R`.
- A unary `each:` with exactly one relation entry and an alias, `each: R as r`, and no `without:`.

A row binding SHALL expose `r.from` and `r.to` as `entity_id` values and `r.<field>` for each field. It SHALL be read-only, and it SHALL NOT be used with brackets, as a command or `emit` target, or in `reduce:` or `keep`. A rule SHALL have at most one row binding. Its membership snapshot SHALL list rows in row creation order. A unary row domain SHALL accept `where:`, `order by:`, a global `limit:` and `when:`.

#### Scenario: Row joined to an entity
- **WHEN** a pair rule binds `w: Walker` and `r: Route` with `where: r.from == w[Walker].tile`
- **THEN** compilation accepts it, and each handler run sees one walker and one matching row

#### Scenario: Unary row domain
- **WHEN** a rule declares `each: Claim as claim` and its handler emits `Done to claim.from`
- **THEN** the handler runs once per `Claim` row in row creation order

#### Scenario: Write to a row field
- **WHEN** a handler assigns `r.cost = 2.0`
- **THEN** the compiler reports that relation rows are read-only, with help that suggests `add R(a, b):`

#### Scenario: Two row bindings
- **WHEN** a pair rule binds `r1: Walk` and `r2: Door`
- **THEN** the compiler reports that a rule may bind at most one relation

#### Scenario: Row as a trait namespace
- **WHEN** a handler reads `r[Tile].x`
- **THEN** the compiler reports that `r` is a relation row, with help that suggests joining a `Tile` binding on `r.from`

### Requirement: Relation tests in `where:`

`R(x, y)` SHALL be a pure `bool` expression that is true when `R` holds the row `(x, y)` in the snapshot of the current pass. Either argument MAY be `_`, which matches any entity. `R(_, _)` SHALL be rejected. A relation test SHALL be allowed in `where:` and `when:` only. Elsewhere it SHALL be a source-located error that suggests moving it into `where:`. Tests SHALL see rows as of the start of the pass; commands queued during the pass SHALL NOT change them. `not R(...)`, `and` and `or` SHALL compose relation tests as union, intersection and difference.

#### Scenario: Union of two relations
- **WHEN** a keep rule for `Passable(a, b)` declares `where: Walk(a, b) or Bridge(a, b)`
- **THEN** `Passable` holds a row for each pair linked by either relation

#### Scenario: Inverse
- **WHEN** a rule tests `Walk(b, a)` for bindings `a` and `b`
- **THEN** the test is true when `Walk` holds the row `(b, a)`

#### Scenario: Test in a handler body
- **WHEN** a handler writes `if Claim(robot, task):`
- **THEN** the compiler reports an error that suggests moving the test into `where:`

#### Scenario: Both wildcards
- **WHEN** a predicate tests `Claim(_, _)`
- **THEN** the compiler reports a source-located error

#### Scenario: Test sees the pass snapshot
- **WHEN** an early tuple in a pass adds `Claim(r, t)` and a later tuple tests `Claim(r, _)`
- **THEN** the later test is false

### Requirement: Derived relation declarations
A module SHALL accept `[pub] relation N = closure(R)`, `[pub] relation N = shortest(R, by = f)` and `[pub] relation N = components(R)`, where `R` is a relation visible in the module. `R`'s `from` and `to` ends SHALL name the same trait. Otherwise the declaration is a compile error that names both traits. `N` SHALL be a derived relation whose ends both name that trait. A derived relation SHALL be read-only: `keep N`, `add N` and `remove N` SHALL be compile errors. It SHALL take part in tests and row bindings like any relation, except as limited below. `R` MAY itself be a derived relation.

```ebnf
derived_relation = [ "pub" ] "relation" IDENTIFIER "=" derived_form NEWLINE ;
derived_form     = "closure" "(" dotted_name ")"
                 | "shortest" "(" dotted_name "," "by" "=" IDENTIFIER ")"
                 | "components" "(" dotted_name ")" ;
```

#### Scenario: Closure of a kept relation
- **WHEN** a module declares `relation Reach = closure(Walk)` and `Walk` has `from: Tile` and `to: Tile`
- **THEN** compilation accepts it, and `Reach` has ends `Tile` and `Tile`

#### Scenario: Ends differ
- **WHEN** `Claim` has `from: Robot` and `to: DigTask`, and a module declares `relation C = closure(Claim)`
- **THEN** the compiler reports that `closure` needs one trait on both ends, naming `Robot` and `DigTask`

#### Scenario: Writing a derived relation
- **WHEN** a handler writes `add Reach(a, b)`
- **THEN** the compiler reports that `Reach` is derived from `Walk` and is read-only

### Requirement: Closure rows are reachability
`closure(R)` SHALL hold the row `(a, b)` exactly when there are rows `(a, x1), (x1, x2), …, (xk, b)` of `R` with `k ≥ 0`. A row `(a, a)` SHALL exist only when such a chain leads from `a` back to `a`.

#### Scenario: Chain
- **WHEN** `Walk` holds `(a, b)` and `(b, c)`
- **THEN** `Reach` holds `(a, b)`, `(b, c)` and `(a, c)`, and not `(c, a)`

#### Scenario: Not reflexive
- **WHEN** `a` has no `Walk` rows
- **THEN** `Reach(a, a)` is false

#### Scenario: Territory connected to the heart
- **WHEN** a keep rule over `heart` and `t` declares `where: Reach(heart, t) or t == heart` and `keep Connected on t`
- **THEN** exactly the tiles linked to the heart by `Walk` chains carry `Connected`

### Requirement: Shortest rows carry the first step and the cost
In `shortest(R, by = f)`, `f` SHALL be an `int` field of `R`. A row's cost SHALL be `f`, or `0` when `f` is negative. The cost of a chain SHALL be the exact sum of its rows' costs. `shortest(R, by = f)` SHALL hold the row `(a, b)` for every `b ≠ a` that a chain leads to whose cost is at most `2147483647`, with fields:
- `distance: int`, the least cost of such a chain;
- `next: entity_id`, the second entity of a least-cost chain. When several least-cost chains have different second entities, `next` SHALL be the one with the lowest creation order.

#### Scenario: Cheapest step
- **WHEN** `Walk` links `a→b` (cost 1), `b→d` (cost 1), `a→c` (cost 1) and `c→d` (cost 5)
- **THEN** `Route` holds `(a, d)` with `distance = 2` and `next = b`

#### Scenario: Tie by creation order
- **WHEN** two chains from `a` to `d` cost the same, one through `b` and one through `c`, and `b` was created before `c`
- **THEN** `Route(a, d).next` is `b`, as seen through a row binding

#### Scenario: Unreachable goal
- **WHEN** no chain leads from `a` to `d`
- **THEN** `Route` has no row `(a, d)`, and a rule joining a walker at `a` with goal `d` runs no tuple for it

#### Scenario: Float cost field
- **WHEN** a module declares `shortest(Walk, by = cost)` and `cost` is a `float`
- **THEN** the compiler reports that `by` needs an `int` field, with help that says to scale costs to integers

### Requirement: Components map every member to its oldest member
`components(R)` SHALL treat `R`'s rows as two-way links over the entities that carry `R`'s end trait. It SHALL hold one row `(m, root)` for every such entity `m`, where `root` is the member of `m`'s connected group with the lowest creation order. An entity with no rows SHALL be its own group. The `from` end SHALL be `one`.

#### Scenario: Two rooms
- **WHEN** lair tiles form two groups with no link between them
- **THEN** every tile's `Room` row points to the oldest tile of its own group

#### Scenario: Merge keeps the older name
- **WHEN** a new link joins a group whose oldest tile is `t3` with a group whose oldest tile is `t9`, and `t3` is older
- **THEN** every tile of both groups maps to `t3`

#### Scenario: Split
- **WHEN** removing a tile splits a group with root `t3` into a part that holds `t3` and a part whose oldest tile is `t7`
- **THEN** the first part maps to `t3` and the second to `t7`

#### Scenario: Lone tile
- **WHEN** a lair tile has no `SameRoom` rows
- **THEN** `Room` holds `(tile, tile)`

### Requirement: Derived relations are current, unsaved and bounded
A derived relation SHALL be read as a function of `R`'s rows in the same snapshot that the reading pass sees for `R`. It SHALL NOT be saved, and SHALL be correct immediately after restore. A row binding of a `closure` or `shortest` relation SHALL require a top-level `where:` conjunct that equates `r.from` or `r.to` with an expression that does not read the row binding. Otherwise the compiler SHALL report an error, with help that says to fix an end. `components` row bindings and every relation test SHALL have no such requirement. Derived rows SHALL be ordered by the creation order of their `from` end, then of their `to` end.

#### Scenario: Seen in the same pass as its source
- **WHEN** a keep run adds `Walk(b, c)` and a later activation's rule tests `Reach(a, c)` while `Walk` holds `(a, b)`
- **THEN** the test is true in that later activation

#### Scenario: Unbounded row binding
- **WHEN** a rule declares `each: Reach as r` with no equality on `r.from` or `r.to`
- **THEN** the compiler reports an error with help that says to fix one end by equality

#### Scenario: After restore
- **WHEN** a world with stored links is restored
- **THEN** the first pass that reads their closure sees the restored links' reachability
