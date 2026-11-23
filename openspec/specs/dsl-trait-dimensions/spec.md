## Purpose

Let a trait declare dimensions so that each of its `var` fields holds one value per carrier and per cell of those dimensions, with cell access, rules over cells, and a compile-time proof that lets a rule skip cells that hold their defaults.

## Requirements

### Requirement: Dimension declaration
A trait body SHALL accept one to four lines `dim name: type`, where the type is an enum or `int in range(a, b)`. An `int` range SHALL be half-open, with the meaning of a ranged `let` field, and each bound SHALL read only literals, constants and `let` fields of the same trait. A trait with at least one dimension is a *dimensioned trait*; the entity that carries it is the *carrier*; one choice of a value for every dimension is a *cell*. `dim` SHALL be a contextual word: it starts a dimension only at the start of a line in a trait body when an identifier and `:` follow, and stays an ordinary identifier elsewhere. A dimensioned trait MAY be `single` and MAY be `pub`.

In a dimensioned trait a `let` field SHALL belong to the carrier, and every `var` field SHALL be a *cell field*: one value per cell. A cell field SHALL have type `bool`, `int`, `float`, `duration`, an enum or a vector type, and a default that is a constant expression.

The following SHALL each be a source-located error: `dim` in a state variant, a relation, a struct or an event; a fifth dimension; a dimension of another type; an `int` dimension without a range; a bound that reads a `var` field, another trait or a call; two dimensions or a dimension and a field with one name; a cell field without a default, with a non-constant default, or of another type; `persist` on a cell field; a dimensioned trait as the target of `project` or `keep`; a dimensioned trait with no cell field.

#### Scenario: An enum dimension
- **WHEN** a trait declares `dim item: Item` and `var count: int = 0`, and `Item` has three values
- **THEN** compilation accepts it and each carrier has three cells

#### Scenario: Two ranged dimensions on a single trait
- **WHEN** a `single trait Room` declares `dim x: int in range(0, 64)`, `dim y: int in range(0, 48)` and `var gas: float = 0.0`
- **THEN** compilation accepts it and the one carrier has 64 by 48 cells

#### Scenario: A duration cell field
- **WHEN** a dimensioned trait declares `dim x: int in range(0, 16)` and `var regrow: duration = 0s`
- **THEN** compilation accepts it and every cell's `regrow` starts at zero units

#### Scenario: Bounds from let fields
- **WHEN** a trait declares `let width: int = 4` and `dim x: int in range(0, width)`, and one entity is spawned with `width = 8`
- **THEN** that entity has eight cells along `x` and an entity with the default has four

#### Scenario: A cell field without a default
- **WHEN** a dimensioned trait declares `var count: int`
- **THEN** the compiler reports an error saying a cell field needs a constant default

#### Scenario: The word stays an identifier
- **WHEN** a trait declares `var dim: int = 0` and a handler reads it
- **THEN** compilation accepts it

### Requirement: Cells always hold a value
When a value of a dimensioned trait is made (a declared entity, `spawn`, `add`), every cell of every cell field SHALL hold the field's default. An entity body, template body or override block SHALL assign only `let` fields of a dimensioned trait; assigning a cell field there SHALL be an error. `add T:` on a carrier SHALL replace the trait whole, so every cell returns to its default. `remove T` and destroying the carrier SHALL drop every cell. A cell SHALL NOT be created or destroyed on its own.

Each `int` range SHALL hold at most 1048576 values. When the bounds of a made value break that limit, the operation SHALL do nothing and report a fault that names the trait and the dimension, as a failed ranged check does. A range with `a >= b` SHALL give no cells.

A save SHALL NOT record cell fields. After a restore every cell of a restored carrier SHALL hold its default.

#### Scenario: Fresh carrier
- **WHEN** an entity with `Stock` is spawned and no handler has written it
- **THEN** every read of `count` for that entity gives 0

#### Scenario: Replacing the trait
- **WHEN** a handler runs `add Stock` on an entity whose `count` for `Item.Key` is 2
- **THEN** every `count` of that entity is 0 after the commit

#### Scenario: Cell field in an entity body
- **WHEN** an entity body writes `Stock: count = 5`
- **THEN** the compiler reports an error saying cells are written by handlers

### Requirement: Cell access
`e[T][k1, ..., kn].field` SHALL name the cell field of carrier `e` at the given dimension values, in declaration order, where `e` is `self`, a pair or filter binding that requires `T`, or a named entity that declares `T`. For a `single` trait `T`, `T[k1, ..., kn].field` SHALL name the same cell of the one carrier, with the requirement and contract rules of named access. Each `ki` SHALL have the type of its dimension, and the number of values SHALL equal the number of dimensions. `e[T].name` with no second bracket SHALL read a `let` field of the carrier.

Cell access SHALL be readable in handler bodies and in every rule clause where a field of `e` may be read, and assignable in handler bodies wherever a `var` field of `e` is writable. It SHALL be an error in a `func` body, a constant, and an entity or template body. An `int` value outside its range SHALL make a read give the field's default and a write do nothing. A write SHALL be visible to later reads at once.

A read or write SHALL enter the handler's contract as a read or write of that field of `T`, with the write kind the same statement has on an ordinary field. Compound `+=` and `-=` on a `float` or vector cell field SHALL be merged exactly, as for other fields.

A cell access through `self` or a cell alias SHALL be an own-row access (`handler-execution-graph`); the short form and a cell binding SHALL NOT be. In contract audit mode every cell read and write SHALL be checked as an access of that field of `T`, with its write kind.

A write to a cell field SHALL be a write of that field for keep inputs: a keep that reads the field SHALL NOT be skipped after it, and the carrier SHALL be the changed entity. A keep that reads cells only through `self` or its own binding SHALL keep the level it would have with an ordinary field read; a keep that reads cells by the short form SHALL be full, with that `single` trait read as its reason.

#### Scenario: Taking one item
- **WHEN** a unary rule over `Stock` carriers runs `self[Stock][Item.Grenade].count -= 1`
- **THEN** only that entity's grenade count changes

#### Scenario: Short form for a single trait
- **WHEN** a unary rule over sources runs `Room[grid.cell(tf.position.x, 0.0, STEP), grid.cell(tf.position.y, 0.0, STEP)].gas += src.rate * t.dt`
- **THEN** the gas at the source's cell grows by that amount

#### Scenario: Two writers of one cell
- **WHEN** two unordered handlers each add to the same cell in one activation
- **THEN** compilation needs no order between them and the cell holds the exact sum

#### Scenario: Outside a range
- **WHEN** a handler writes `Room[-5, 0].gas = 1.0`
- **THEN** no cell changes and no fault is reported

#### Scenario: Cell read in where
- **WHEN** a unary keep rule over guards declares `where: Room[grid.cell(tf.position.x, 0.0, STEP), grid.cell(tf.position.y, 0.0, STEP)].gas > 0.3` and `keep InGas`
- **THEN** exactly the guards whose cell holds more than 0.3 carry `InGas` after the run

#### Scenario: Wrong number of values
- **WHEN** a handler reads `Room[3].gas` and `Room` has two dimensions
- **THEN** the compiler reports an error naming both dimensions

#### Scenario: Disjoint carriers write one cell field
- **WHEN** two cell rules over `Heat as h` each run `h.value += 1.0`, one with `each: Heat as h, Hot` and one with `each: Heat as h, Cold`, and no reachable archetype carries both `Hot` and `Cold`
- **THEN** compilation needs no order between the two handlers

#### Scenario: A keep reads its carrier's cells
- **WHEN** a keep rule over `Stock` carriers keeps `HasKey` with `where: self[Stock][Item.Key].count > 0`, and a handler adds a key to one carrier
- **THEN** after the next commit only that carrier gains `HasKey`, and the keep is recorded at the delta level

#### Scenario: A keep reads a single trait's cells
- **WHEN** a keep rule over guards reads `Room[i, j].gas` in its `where:`
- **THEN** the keep is recorded at the full level with the read of `Room` as its reason, and it does not run in an activation where no handler wrote `Room.gas`

#### Scenario: Audit of a cell write
- **WHEN** an audited fixture runs a handler that writes a cell field its contract does not list
- **THEN** the audited test fails and names the handler and the field

### Requirement: Unary rules over cells
A dimensioned trait in `each:` without an alias SHALL only require the trait, with one row per carrier. With an alias (`each: Room as c`) the rule SHALL be a *cell rule* with one row per carrier and cell. The alias SHALL expose each cell field for reading and writing and each dimension for reading (`c.gas`, `c.x`). `self` SHALL be the carrier, with every entity operation it has in a unary rule. Other `each:` entries and `without:` SHALL filter carriers. `each:` SHALL hold at most one aliased dimensioned trait.

Rows SHALL run carriers in creation order, and the cells of one carrier in dimension order, first dimension major, enum values in declaration order. A targeted event SHALL select the rows of its recipient.

`reduce:` on a cell rule SHALL fold all rows into one group. `reduce per self:` SHALL make one group per carrier, including a carrier with no surviving row, and its handler SHALL run once per carrier with the aggregates.

`order by:`, `limit:`, `keep`, and a lifecycle trigger on a cell rule SHALL each be a source-located error. A cell rule MAY declare `repeat: N` under the limits of `dsl-rule-repeat`; each pass SHALL take its rows, and decide which are at rest, from the world the previous pass left. An aliased dimensioned trait in the `each:` of a drawing rule (`dsl-drawing-rules`), and a cell access in a stage handler, SHALL each be a source-located error.

#### Scenario: Decay in every cell
- **WHEN** a cell rule over `Room as c` declares `where: c.gas > 0.0` and its handler lowers `c.gas`
- **THEN** each cell with gas is lowered once per activation

#### Scenario: Total per carrier
- **WHEN** a cell rule over `Stock as s` declares `reduce per self: total = sum(s.count)` and its handler stores `total` in another trait of `self`
- **THEN** each carrier gets the sum of its own counts, and a carrier with nothing gets 0

#### Scenario: Requirement without alias
- **WHEN** a rule lists `Stock` in `each:` with no alias
- **THEN** its handler runs once per carrier

#### Scenario: Two aliased dimensioned traits
- **WHEN** `each:` lists `Matter as m` and `Heat as h`, both dimensioned
- **THEN** the compiler reports an error and suggests cell access or `pairs:` for the second trait

#### Scenario: Repeated spread
- **WHEN** the spreading cell pair rule of the next requirement declares `repeat: 2`, and it is the only rule that writes `Room.gas`
- **THEN** after one activation every cell holds the value it holds after two activations of the same rule without `repeat:`

#### Scenario: Cells in a render pass
- **WHEN** a drawing rule declares `each: Room as c`
- **THEN** the compiler reports an error saying a drawing rule draws entities

#### Scenario: Cell access in a stage
- **WHEN** a fragment handler reads `Room[2, 3].gas`
- **THEN** the compiler reports an error saying a stage handler does not read cells

### Requirement: Pair rules over cells
A `pairs:` binding whose only entry is a dimensioned trait, with no alias, SHALL be a *cell binding*: it ranges over carriers and cells of that trait. It SHALL expose `c.field`, `c.<dimension>` and `c.carrier`, an `entity_id`. The first cell binding of the rule MAY, in block form, hold one `without:` line after its trait; it SHALL exclude carriers that carry a listed trait, with the meaning `without:` has in an entity binding (`dsl-pair-relations`). A `without:` line in the second cell binding SHALL be a source-located error that points to the first. A cell binding SHALL be read-only except as the retained binding of `reduce per`, whose cell fields the handler may write. Both bindings of a cell pair rule SHALL be cell bindings of the same trait.

A cell pair rule SHALL be *bounded* by top-level `where:` conjuncts in one of two ways: the built-in `adjacent(a, b)`, or the equality `b.carrier == a.carrier` together with equalities that fix every dimension of `b` from values that read nothing of `b`, where an `or` of `and` chains counts when every branch fixes all of them. So the two cells of every row SHALL have one carrier. An equality that fixes `b.carrier` to anything other than `a.carrier` SHALL be a source-located error. `adjacent(a, b)` SHALL be true when the two cells have the same carrier, differ by exactly one in exactly one `int` dimension, and are equal in every other dimension. An unbounded cell pair rule, a cell binding next to an entity or row binding, bindings of two different traits, and `adjacent` anywhere but a top-level `where:` conjunct of a cell pair rule SHALL each be a source-located error.

Aggregates SHALL be computed from one snapshot before any handler of the pass runs, as for every reduced rule. Rows SHALL run left binding major.

#### Scenario: Spreading to neighbours
- **WHEN** a cell pair rule over `here: Room` and `there: Room` declares `where: adjacent(here, there)`, `reduce per here: net = sum((there.gas - here.gas) * 0.1)` and its handler runs `here.gas += net`
- **THEN** after one activation a single cell of gas 1.0 holds 0.6 and each of its four neighbours holds 0.1

#### Scenario: Edge of a range
- **WHEN** the same rule runs for the cell at `x = 0`, `y = 0`
- **THEN** its group folds two rows

#### Scenario: Walls in the same trait
- **WHEN** `Room` also declares `var wall: bool = false` and the rule adds `where: not there.wall and not here.wall`
- **THEN** no gas moves into or out of a wall cell

#### Scenario: Cells of different carriers
- **WHEN** two entities carry `Heat` and a cell pair rule uses `adjacent`
- **THEN** no row pairs a cell of one entity with a cell of the other

#### Scenario: Unbounded pair
- **WHEN** a cell pair rule has no `adjacent` and no equalities over the dimensions
- **THEN** the compiler reports an error saying a pair of cells must be bounded

#### Scenario: Excluding frozen carriers
- **WHEN** two entities carry `Heat`, one of them also carries `Frozen`, and a cell pair rule with `where: adjacent(here, there)` declares `without: Frozen` in the `here` binding
- **THEN** heat moves only between cells of the entity without `Frozen`

#### Scenario: Without on the second binding
- **WHEN** the `there` binding of a cell pair rule declares `without: Frozen`
- **THEN** the compiler reports an error at that line that points to the first binding

#### Scenario: Cells of another carrier by equality
- **WHEN** a cell pair rule bounds `there` with `there.carrier == Furnace`, where `Furnace` is a named entity, and equalities on every dimension
- **THEN** the compiler reports an error saying both cells of a pair must have one carrier

### Requirement: A cell is not an entity
A cell alias or cell binding SHALL NOT be usable as an `entity_id`. Using one as an `emit ... to` target, a command target, a relation argument or a link value SHALL be a source-located error that says a cell is not an entity and names `self` or `.carrier`. Lifecycle triggers of a dimensioned trait SHALL fire for the carrier only.

#### Scenario: Emit to a cell
- **WHEN** a cell rule handler runs `emit Alarm to c`
- **THEN** the compiler reports an error that suggests `self`

#### Scenario: Lifecycle of the carrier
- **WHEN** an entity with `Stock` is spawned and a rule without a cell alias declares `on added Stock:`
- **THEN** the handler runs once for that entity

### Requirement: The rest law
A row of a cell rule SHALL be *at rest* when every cell field that the rule reads or writes, in every cell the row binds, holds exactly its default, bit for bit. Cell fields the rule does not name SHALL NOT matter. A cell rule SHALL *keep rest* when rows at rest change nothing. The compiler SHALL try to prove it by folding the rule with each cell field read of its cell alias or cell bindings replaced by the default, with constants evaluated as at run time and every other read unknown. The proof SHALL hold when:

- a `where:` predicate folds to `false`; or
- the rule has no `reduce:`, and on every path of the folded handler body each write to a cell field of the row stores a value that folds to that field's default, and there is no `emit`, command, `spawn`, call with effects or other write; or
- the rule has `reduce:` or `reduce per self:`, and every reducer is a `sum` whose argument folds to zero or an `any` whose argument folds to `false`; or
- the rule has `reduce per` a cell binding, every reducer passes the previous test, and the handler body folded with those aggregates at their identities passes the test for a rule without `reduce:`.

A condition that folds to a constant SHALL select its branch, and one that stays unknown SHALL require both branches to pass.

The observable result of a cell rule SHALL be the same as running it for every row of its domain, proven or not. When a rule is not proven and its trait has an `int` dimension, the compiler SHALL warn at the rule: it names the first statement or reducer that is not proven, says the rule visits every cell, and suggests a `where:` condition that is false at the defaults. A rule whose trait has only enum dimensions SHALL get no such warning.

#### Scenario: Diffusion is proven
- **WHEN** a cell pair rule reduces `sum((there.gas - here.gas) * RATE)` with a constant `RATE` and its handler runs `here.gas += net`
- **THEN** compilation reports no warning

#### Scenario: Decay by an unknown amount
- **WHEN** a cell rule over `Room as c` runs `c.gas -= t.dt * 0.1` with no `where:`
- **THEN** the compiler warns, naming that statement and suggesting `where: c.gas > 0.0`

#### Scenario: Guarded decay
- **WHEN** the same rule declares `where: c.gas > 0.0`
- **THEN** compilation reports no warning

#### Scenario: Other fields do not wake a rule
- **WHEN** a trait declares `var block: Block = Block.Air` and `var water: float = 0.0`, most cells hold `Block.Stone`, and a cell rule reads and writes only `water` under `where: c.water > 0.0`
- **THEN** compilation reports no warning, and a cell of stone without water is a row at rest for that rule

#### Scenario: Enum dimensions only
- **WHEN** a cell rule over `Stock as s` emits an event for every cell with `s.count < 5`
- **THEN** compilation reports no warning and the handler runs for every item kind of every carrier

#### Scenario: A cell that returns to rest
- **WHEN** a cell field is written back to its exact default
- **THEN** later passes behave as if that field of the cell had never been written

### Requirement: Grid helpers in the standard library
`std.grid` SHALL provide `cell(coordinate: float, origin: float, step: float) int`, the index of the cell that holds a coordinate, equal to `floor((coordinate - origin) / step)` under the total conversion, and `center(index: int, origin: float, step: float) float`, the coordinate of a cell's centre. Both SHALL be pure.

#### Scenario: Coordinate to cell and back
- **WHEN** `step` is 0.5 and `origin` is 0.0
- **THEN** `grid.cell(1.3, 0.0, 0.5)` is 2 and `grid.center(2, 0.0, 0.5)` is 1.25
