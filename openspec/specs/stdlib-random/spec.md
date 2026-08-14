## Requirements

### Requirement: std.random provides a seeded generator type
The `std.random` module SHALL declare `Rng` as a `pub struct` with a single visible `state: int` field. The `seeded` extern func SHALL construct an `Rng` from an integer seed deterministically — the same seed always produces the same initial state. `Rng` SHALL be usable as a `var` trait field so authors can hold per-entity generator state.

#### Scenario: Rng constructed from seed
- **WHEN** `rand.seeded(42)` is called
- **THEN** it returns an `Rng` value with a deterministic `state` derived from `42`

#### Scenario: Same seed produces same initial state
- **WHEN** `rand.seeded(99)` is called twice independently
- **THEN** both calls return `Rng` values with identical `state`

#### Scenario: Rng used as var trait field
- **WHEN** a trait declares `var rng: rand.Rng = rand.seeded(0)`
- **THEN** the type system accepts the declaration and the field holds an `Rng` value

---

### Requirement: std.random provides distribution value types
The `std.random` module SHALL declare `Uniform`, `UniformInt`, and `Normal` as `pub struct` types. These are pure value types with no callable methods; they serve as configuration passed to sampling functions.

- `Uniform`: fields `lo: float`, `hi: float` — continuous uniform distribution over [lo, hi)
- `UniformInt`: fields `lo: int`, `hi: int` — discrete uniform distribution over [lo, hi]
- `Normal`: fields `mean: float`, `stddev: float` — Gaussian distribution

Constructor extern funcs SHALL exist for each: `uniform(lo, hi)`, `uniform_int(lo, hi)`, `normal(mean, stddev)`.

Distribution structs SHALL be usable as `let` trait field defaults, enabling declarative distribution configuration.

#### Scenario: Uniform struct constructed
- **WHEN** `rand.uniform(-5.0, 5.0)` is called
- **THEN** it returns a `Uniform` value with `lo = -5.0` and `hi = 5.0`

#### Scenario: UniformInt struct constructed
- **WHEN** `rand.uniform_int(1, 6)` is called
- **THEN** it returns a `UniformInt` value with `lo = 1` and `hi = 6`

#### Scenario: Normal struct constructed
- **WHEN** `rand.normal(0.0, 1.0)` is called
- **THEN** it returns a `Normal` value with `mean = 0.0` and `stddev = 1.0`

#### Scenario: Distribution used as let trait field
- **WHEN** a trait declares `let spread: rand.Uniform = rand.uniform(-2.0, 2.0)`
- **THEN** the type system accepts the declaration and the field is a constant `Uniform` value

---

### Requirement: std.random.advance is a pure extern func
The `std.random` module SHALL declare `pub extern func advance(rng: Rng) Rng`. This function SHALL be pure: given the same `Rng` input it always returns the same next `Rng` state. Authors advance the generator by explicit reassignment: `rng = rand.advance(rng)`.

#### Scenario: Advance returns new state
- **WHEN** `rand.advance(rand.seeded(1))` is called
- **THEN** it returns an `Rng` whose `state` differs from the input

#### Scenario: Advance is deterministic
- **WHEN** `rand.advance(rng_a)` and `rand.advance(rng_b)` are called and `rng_a.state == rng_b.state`
- **THEN** both calls return `Rng` values with identical `state`

#### Scenario: Advance used via explicit assignment in handler
- **WHEN** a rule handler contains `rng = rand.advance(rng)`
- **THEN** the compiler accepts this as a valid field assignment expression

---

### Requirement: std.random sampling functions are pure
The `std.random` module SHALL declare the following pure extern sampling functions. Given the same `Rng` state and the same distribution arguments, each function SHALL return the same value.

- `pub extern func sample(rng: Rng, dist: Uniform) float` — draw from continuous uniform distribution
- `pub extern func sample_int(rng: Rng, dist: UniformInt) int` — draw from discrete uniform distribution
- `pub extern func sample_normal(rng: Rng, dist: Normal) float` — draw from Gaussian distribution
- `pub extern func chance(rng: Rng, p: float) bool` — return `true` with probability `p` (clamped to [0, 1])

`sample` SHALL return a value in [lo, hi). `sample_int` SHALL return a value in [lo, hi] inclusive. `chance(rng, 0.0)` SHALL always return `false`; `chance(rng, 1.0)` SHALL always return `true`.

#### Scenario: Sample from uniform distribution
- **WHEN** `rand.sample(rng, rand.uniform(0.0, 1.0))` is called
- **THEN** it returns a `float` in [0.0, 1.0)

#### Scenario: Sample is deterministic given same state
- **WHEN** two `Rng` values have identical `state` and `rand.sample` is called on each with the same `Uniform`
- **THEN** both calls return the same `float`

#### Scenario: sample_int returns value in inclusive range
- **WHEN** `rand.sample_int(rng, rand.uniform_int(1, 6))` is called
- **THEN** it returns an `int` in {1, 2, 3, 4, 5, 6}

#### Scenario: chance with p=0 always false
- **WHEN** `rand.chance(rng, 0.0)` is called
- **THEN** it returns `false`

#### Scenario: chance with p=1 always true
- **WHEN** `rand.chance(rng, 1.0)` is called
- **THEN** it returns `true`

---

### Requirement: std.random provides a fixed-palette color lookup
The `std.random` module SHALL declare `pub extern func palette_color(index: int) color`, returning one of a fixed palette of at least 8 distinct, deterministically-ordered colors selected by `index`. The same `index` value SHALL always return the same color. An `index` outside the palette's own size SHALL be wrapped (e.g. via modulo by the palette size) rather than erroring, so any `int` — including the direct output of `sample_int`, or a plain loop counter — is a valid argument.

#### Scenario: Palette lookup is deterministic
- **WHEN** `rand.palette_color(3)` is called twice
- **THEN** both calls return the same color

#### Scenario: In-range indices give distinct colors
- **WHEN** `rand.palette_color(i)` is called for each `i` in `0..7`
- **THEN** all 8 returned colors are distinct

#### Scenario: Out-of-range index wraps rather than erroring
- **WHEN** `rand.palette_color(11)` is called against an 8-entry palette
- **THEN** it returns the same color as `rand.palette_color(3)` (`11 mod 8 == 3`), and the call does not error

### Requirement: std.random functions are backend-backed and verified
The `std.random` module extern functions SHALL have concrete implementations in the C++ backend runtime. The backend test suite SHALL include behavioral tests covering `seeded`, `advance`, `sample`, `sample_int`, `sample_normal`, `chance`, and `palette_color`.

#### Scenario: Sample extern functions are backed
- **WHEN** authored code calls `rand.advance`, `rand.sample`, `rand.sample_int`, `rand.sample_normal`, `rand.chance`, or `rand.palette_color`
- **THEN** the active backend resolves the call through a concrete runtime implementation rather than a missing symbol

#### Scenario: std.random extern coverage is behaviorally verified
- **WHEN** the backend/runtime test suite runs
- **THEN** it includes tests covering determinism of `seeded`, state progression of `advance`, range correctness of `sample`/`sample_int`, boundary correctness of `chance`, and determinism/distinctness/wraparound of `palette_color`
