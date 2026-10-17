# Activation Semantics

A small mathematical model of one Cactus activation. It explains how handlers run,
how values and membership are read, how events are delivered, and how structural
commands commit. It also states what can be argued about this model, and under which
assumptions.

## 0. Status and authority

This document is an explanatory companion. It does not define the language. The
normative sources are:

| Topic | Normative source |
|---|---|
| Correctness properties, timing guarantees, clocks | `openspec/specs/language-philosophy/spec.md` |
| Grammar and execution model | `spec/cactus_dsl_spec.md` §4.4, §4.7, §5 |
| Handler contracts, write kinds, effect use | `openspec/specs/handler-contracts/spec.md` |
| Schedule edges, conflicts, delivery graph, generations | `openspec/specs/handler-execution-graph/spec.md` |
| Phases, cadence, commit boundary | `openspec/specs/phase-activation-model/spec.md` |
| Targeted delivery | `openspec/specs/targeted-event-delivery/spec.md` |
| Lifecycle triggers and commit rounds | `openspec/specs/dsl-trait-lifecycle-triggers/spec.md` |
| `set` patches | `openspec/specs/dsl-deferred-set/spec.md` |
| Numbers, exact accumulation | `openspec/specs/dsl-numeric-semantics/spec.md` |
| Entity handles | `openspec/specs/dsl-entity-id-total-semantics/spec.md` |
| Reachable archetypes, spawn order | `openspec/specs/archetype-reachability/spec.md` |

Each rule below names its source. When this text and a normative source disagree,
the normative source wins, and the disagreement is listed in §10. When the
implementation disagrees with a normative source, both are cited in §10; this
document does not pick the implementation's meaning.

**Impact.** Syntax impact: none. Semantic impact: none. Direct authoring concision
benefit: none. The model prepares compiler-derived guarantees and optimizations; it
adds no author obligation.

**Evidence labels.** Each claim carries one of these labels.

- **Model**: argued in this document inside the abstract model. Nothing here is
  machine-checked.
- **Open**: a condition the model needs, which the compiler, the runtime or a native
  function is assumed, not shown, to provide.
- **Tested**: behavior of the cpp-entt backend checked by a named test in the tree.
- **Code**: read from the current source; no test checks it directly.
- **Probe**: a scratch program compiled and run on the current tree while writing
  this document. Probes are not committed tests; each is reproducible from the
  program text in §8 or §10.

## 1. Fragment

The propositions in §7 hold only for programs whose activations use this fragment.
Excluding a construct restricts the propositions, not the language.

**Included.**

- Rules with a selectionless domain or a unary domain (`each:`, `without:`), and their
  `when:` and `where:` guards, treated as reads evaluated per row.
- Triggers: phases, ordinary events (broadcast and targeted), host `extern` events,
  `load` and `unload`, `on added T` and `on removed T` on durable traits.
- Statements: `let`, field assignment (`=`, `+=`, `-=`, other compound forms), `if`,
  bounded `for x in list`, `return`, `emit` with or without `to`, `spawn` of a
  template without children, `destroy`, `add`, `remove`, `set`.
- Expressions: literals, locals, field reads through aliases and through entity names,
  arithmetic, comparison, logic, `exists`, calls to user `func`s, calls to `extern
  func`s (as effect calls, §3.4).

**Excluded from the propositions.** Pair rules; `reduce:`, `limit:`, `order by:`,
`keep`; projected and derived traits; states (`S.V`); hierarchy (`Parent`, children,
cascaded destroy, hierarchy queries); trait-pattern `match`; the scene `load`
statement; persistence save and restore; render-pass stage handlers; `extern rule`
handlers; native emitters and native consumers of events.

An `extern rule` handler could be added as a step whose effect is any terminating
function that respects its declared contract. That needs the assumption that native
code respects its contract, which is not modeled here.

**Phases.** Phase cadence (accumulator, catch-up, `alpha`) is outside the model. The
model receives activation requests, each with a clock (§3.3). No proposition here
covers the whole frame scheduler.

## 2. Notation

- `⟨a, b, …⟩` is a sequence, `·` concatenates, `ε` is empty.
- `⦃…⦄` is a finite multiset, `⊎` is multiset union.
- `f[x ↦ v]` is `f` with `x` mapped to `v`.
- `rnd(x)` rounds an exact value to binary32 once, with the edge rules of a float
  `sum` reduction (`dsl-numeric-semantics`: "Float accumulation into one field is
  exact").

**Static program data.** The compiler computes these; the model takes them as given.

- `𝓗` is the finite set of reachable handlers. Each `h ∈ 𝓗` has a trigger `trig(h)`,
  a domain `dom(h)` (selectionless, or required traits `P` and excluded traits `N`),
  guards, a body and a contract `C(h)` (reads, writes with write kinds, emits,
  commands, effect uses).
- A *dispatch set* is a set of handlers started together: the phase handlers of an
  activation, the consumers of one occurrence, or the lifecycle consumers of one
  delivery. For a dispatch set `D`, `≺_D` is the transitive closure of the schedule
  edges between members of `D`.
- The *delivery graph* `G_D` has an edge `p → c` when `p` may emit an event `c`
  consumes (event edge), or when `p` may add or remove a trait `c` watches with
  `on added` / `on removed` (lifecycle edge). An edge is *delayed* when `p` and `c` lie
  in one strongly connected component that contains a cycle; otherwise it is
  *immediate*.
- `ht(h)` is the height of `h` in the immediate edges: `ht(h) = 1 + max ht(c)` over
  immediate edges `h → c`, and `1` when there is none.

Schedule edges and delivery edges are different relations. Schedule edges order
handlers inside one dispatch set; they must be acyclic. Delivery edges say which
handlers an occurrence reaches and whether it reaches them now or later; they may be
cyclic. (`handler-execution-graph`: "Producer records do not alter scheduling",
"Every event cycle passes through a delay".)

## 3. State

### 3.1 World

A world is `w = ⟨Iss, L, ord, M, V⟩`.

- `Iss` is the finite set of entity identities ever issued. Identities come from an
  infinite set `Id`. An identity is never issued twice (`dsl-entity-id-total-semantics`:
  "A stale handle stays stale for the life of its world").
- `L ⊆ Iss` is the set of live entities. A handle `e` is *stale* when `e ∉ L`.
- `ord` maps each identity that ever became live to its creation ordinal. It is
  injective, and a later creation gets a larger ordinal.
- `M(e)` is the set of durable traits that live entity `e` carries.
- `V(e, T, f)` is the value of field `f` of trait `T` on `e`, defined when `T ∈ M(e)`.
  A vector field is treated as one cell per component where accumulation needs it.

A *cell* is a triple `(e, T, f)`.

### 3.2 Pending accumulation

`A` maps each cell to a finite multiset of exact deltas. Outside an activation `A` is
empty everywhere. Only Accum writes to `float`, `vec2` and `vec3` fields add to `A`
(§6.4).

### 3.3 Clocks and delayed occurrences

The clocks are the phases and one host clock. Every activation has one clock
(`language-philosophy`: "Delayed occurrences belong to the producing clock").

An *occurrence* is `o = ⟨ev, rcp, src⟩`: its payload `ev` (an event value, or a
lifecycle trigger `added T` / `removed T` with its snapshot), its recipient `rcp`
(an identity, or `⊥` for a broadcast), and its source `src` (the producing handler,
or a host, boundary or arrival source).

A *queue entry* is a pair `(o, C)` of an occurrence and the set `C ⊆ 𝓗` of consumers
it may still reach.

`Δ(κ)` is the sequence of delayed entries for clock `κ`. `R` is the sequence of host
events waiting for the next host drain. Both survive between activations.

### 3.4 Effects

`η` is abstract effect-domain state, and `τ` is the trace of effect calls. Each record
is `⟨h, d, op, args, resp⟩`: handler, domain, operation, arguments, response.

**Host contract (Open).** A call that only observes domain `d` does not change `η`,
and its response depends only on `η` and its arguments. A call that acts on `d` changes
`η` and returns a response, both determined by `η` and its arguments. Every native
call terminates. Inputs from outside the program, such as device state, enter only
through `η` between activations and through host events in `R`.

### 3.5 Activation state

During an activation the configuration also holds `Γ = ⟨κ, r, Q, K, X⟩`.

- `κ` is the clock and `r` the commit round, starting at `0`.
- `Q` is the cascade queue: a sequence of queue entries, first in first out.
- `K` is the command list: a sequence of `(command, producer)` pairs.
- `X` is the execution state: the occurrence being delivered (none for phase
  handlers), the dispatch set in progress and the handlers of it still to run; for the
  running pass, its row sequence, cursor, local bindings and the statement
  continuation.

There is no separate membership snapshot. No rule changes `L` or `M` inside a round
(§6.6), so membership read from `w` during a round is the round's snapshot. Field
values are not frozen: reads see the latest write (§6.3).

The full configuration is `⟨w, A, Δ, R, η, τ, Γ⟩`.

### 3.6 Observation and equivalence

Two configurations are *equivalent*, written `c₁ ≈ c₂`, when there is a bijection `π`
on identities that fixes every identity issued before the activation and maps the
identities issued during it onto each other, such that:

- `π(w₁) = w₂` and `π(Δ₁) = Δ₂`, with every `A` empty (both are at an activation
  boundary);
- `η₁ = η₂`;
- for each domain `d`, the records of `τ₁` and `π(τ₂)` on `d` are equal up to swapping
  adjacent records that both only observe `d`.

`≈_ord` is `≈` with the extra demand that `π` preserves `ord`. Creation order is part
of the world that later activations observe through creation-ordered passes, so
`≈_ord` is the default observation. The coarser `≈` is enough only for spawns whose
relative order no pass can observe; the compiler's spawn-order check
(`archetype-reachability`: "Observable spawn order must be defined") is the
obligation that licenses it (Open).

Raw backend handle values are never compared: only `π` relates identities across two
runs. A delayed entry is part of the observation, because it changes a later
activation.

## 4. Static assumptions used by the rules

These describe the compiler data the rules consume. Each is labeled.

- **S1 (Model).** The immediate edges of `G_D` form a DAG. Removing every edge inside a
  strongly connected component leaves only edges between components, and the
  component graph of any directed graph is acyclic. Tested: `test_delivery_cycles`
  ("an acyclic program has no delayed edge", "a two-handler event cycle delays both
  edges").
- **S2 (Open).** Contracts are sound: every field a handler's execution reads or
  writes, every event it emits, every command it issues and every effect it uses is in
  its contract with the right kind. §10 F2 is a counterexample for reads made inside
  native functions.
- **S3 (Code).** A spawn whose template the compiler cannot resolve is recorded as
  adding every watched trait, so the lifecycle edges cover it (`delivery_cycles.cpp`:
  `build_lifecycle_flows`).

## 5. Activation requests

An activation starts from one request. The request fixes the clock and the first
dispatch set `D₀`.

| Request | Clock | `D₀` and initial queue |
|---|---|---|
| Phase `φ` activation | `φ` | handlers with trigger `φ`; then the delayed entries of `φ` (§6.8) |
| Host event `e` from `R` that no phase claims | host | consumers of `e`, broadcast |
| `load` / `unload` boundary | host | consumers of `load` / `unload` |
| Startup arrival | host | `D₀ = ∅`; `Q` holds `added T` for every placed entity and watched trait it carries, in creation order, then canonical trait order |
| Host-delayed entry `(o, C)` | host | after the check of §6.5, the passes of `C` for `o` |

Sources: `phase-activation-model` ("Activation commit boundary"),
`dsl-trait-lifecycle-triggers` ("Load-time entities fire added before load"),
`language-philosophy` ("Delayed occurrences belong to the producing clock"), DSL spec
§5.2.

## 6. Transition rules

Each rule is a step of the configuration. Where the normative text leaves a choice
open, the rule says so and names the finding.

### 6.1 Dispatch

**R1. Run a dispatch set.** Given a dispatch set `D` and an occurrence `o` (or none,
for phase handlers), repeatedly pick a handler `h ∈ D` not yet run whose
`≺_D`-predecessors in `D` have all run, and run its pass (R2). The set ends when every
member has run.

The pick is a free choice. Every sequence it can produce is a *linearization* of
`(D, ≺_D)`. Source: `handler-execution-graph` ("An activation denotes a trace"). The
cpp-entt backend picks the stable topological order (Code), and a test harness can
pick seeded random linearizations (Tested: `test_linearization_sample_headless_behavior`,
`test_linearization_commuting_headless_behavior`).

### 6.2 Pass selection

**R2. Select rows.** The rows of a pass of `h` for occurrence `o` are:

- selectionless `h`: one row with no current entity;
- unary `h`, `o` absent or broadcast: every `e ∈ L` with `P ⊆ M(e)` and
  `N ∩ M(e) = ∅`, in increasing `ord`;
- unary `h`, `o` targeted at `e`: the one row `e` when `e ∈ L` and it matches, and no
  row otherwise;
- lifecycle `h` for `added T` (`removed T`) on `e`: the one row `e` when it matches and
  `T ∈ M(e)` (`T ∉ M(e)`), and no row otherwise.

`when:` and `where:` are evaluated at the start of each row and skip the row when
false. They read live values like the body.

Creation order is the model's order. The backend may use any order where the pass
cannot observe it (DSL spec §4.7). Sources: `handler-contracts` ("Selectionless
handler cardinality"), `targeted-event-delivery` ("Target routing constrains
relation consumers"), `dsl-trait-lifecycle-triggers` ("Targeted delivery after
commit").

### 6.3 Row execution and live reads

**R3. Evaluate.** Expressions evaluate by structural induction to one value (§7.1).
Reading cell `x` first applies `flush(x)` (R4) and then returns `V(x)`. A read sees the
latest value written to that cell by any earlier step, in this pass or in an earlier
one. Sources: `phase-activation-model` ("Activation commit boundary"), DSL spec §5.3.

Statements run in order:

- **Write.** For `x = e` and for every write of kind Assign, Join or General, apply
  `flush(x)`, evaluate `e`, then set `V(x)`.
- **Accumulate.** For a statement whose write kind is Accum on a `float`, `vec2` or
  `vec3` cell `x`, evaluate `e` to a binary32 value `v` and set
  `A(x) := A(x) ⊎ ⦃v⦄` for `+=`, or `⦃−v⦄` for `-=`. On `int`, set
  `V(x) := V(x) ± v` with wrapping. The write kind comes from the contract
  (`handler-contracts`: "Field writes carry a write kind"); a `+=` in a handler that
  also reads `x` is General and takes the Write case.
- **Emit.** Evaluate the payload and the target once, form `o = ⟨ev, rcp, h⟩`, and
  apply R6.
- **Spawn.** Pick a fresh identity `i ∉ Iss`, set `Iss := Iss ∪ {i}`, append
  `(spawn i Tmpl vals, h)` to `K`, and bind `i` if the statement names it. `i` is not
  live until commit: `exists(i)` is false in this round (Code: `reserve_entity`).
- **Destroy, add, remove, set.** Evaluate the target and all values once, now, and
  append the command to `K`. Later writes do not change them (`dsl-deferred-set`:
  "`set` values are evaluated when the statement runs").
- **Effect call.** Append `⟨h, d, op, args, resp⟩` to `τ` and update `η` by the host
  contract.
- **`if`, `for`, `return`, `let`.** As usual. `for` iterates a finite list value.
  `return` ends the row.

A user `func` call evaluates its body as an expression with no access to `w`, `A`,
`K`, `Q` or `τ`.

### 6.4 Accumulation flush

**R4. Flush.** `flush(x)`: when `A(x)` is not empty, set
`V(x) := rnd(V(x) + Σ A(x))`, summing exactly, and set `A(x) := ⦃⦄`.

R3 flushes a cell before each non-Accum access of that cell. R7 flushes every cell
when a cascade ends, before commands apply. So the deltas between two non-Accum
accesses of one cell, or between such an access and the start of the activation or of
a commit round, are summed exactly and rounded once. This is the normative meaning
(`dsl-numeric-semantics`: "Float accumulation into one field is exact"). The backend
settles a cell just before each non-Accum access of it in a handler body or clause, so
an access that does not run settles nothing. Some accesses have no site of their own:
an extern handler, a projection, a spatial join or reduction, a whole-trait read and a
trait-match arm. For these the backend flushes the whole field when the handler starts
(Code: `AccumulationPlan`), which can round other entities' deltas early.

### 6.5 Delivery

**R5. Deliver one entry.** When the current dispatch set has finished and `Q` is not
empty, remove the first entry `(o, C)` of `Q`.

1. If `o.rcp ≠ ⊥` and `o.rcp ∉ L`, drop the entry. No handler, command or effect runs
   for it (`targeted-event-delivery`: "Targeted delivery obeys total entity
   semantics").
2. If the entry came from a delayed queue and `o` is a lifecycle trigger, drop it
   unless the change still holds: for `added T`, `T ∈ M(o.rcp)`; for `removed T`,
   `T ∉ M(o.rcp)` (`dsl-trait-lifecycle-triggers`: "Lifecycle cycles are delayed").
   The rule's own selection is checked again by R2.
3. Otherwise run the dispatch set `C` for `o` (R1).

**R6. Split an emission.** An occurrence `o` produced while clock `κ` runs splits by
the delivery edges of its source:

```text
Imm(o) = { c consumes o.ev : o.src → c is immediate }
Del(o) = { c consumes o.ev : o.src → c is delayed }
if Imm(o) ≠ ∅:  Q    := Q    · ⟨(o, Imm(o))⟩
if Del(o) ≠ ∅:  Δ(κ) := Δ(κ) · ⟨(o, Del(o))⟩
```

Host, boundary and arrival sources have only immediate edges. One occurrence can reach
an observer off a cycle now and a consumer on the cycle later. Sources:
`handler-execution-graph` ("Every event cycle passes through a delay"), DSL spec §5.3.

**Generations (Model).** `Q` is first in, first out, and a dispatch only appends.
So if every entry of generation `d` is in `Q` before any entry of generation `d + 1`,
that stays true: entries of `d + 1` are appended behind every entry of `d`. The phase
handlers form generation 0; their emissions and the delayed entries form generation 1,
with the delayed entries first (§6.8). This is the order `handler-execution-graph`
requires ("Occurrences are delivered in generations"). Tested:
`test_generations_headless_behavior`, `test_runtime_activation_scheduler` ("delayed
occurrences enter the cascade ahead of the activation's own emissions").

### 6.6 Commit rounds

**R7. End a cascade.** When the dispatch set has finished and `Q = ε`, flush every
cell. If `K = ε`, the activation completes (R9). Otherwise run a commit round (R8).

**R8. Commit round.** Let `w⁻` be the world before the round. Apply the entries of `K`
in order; each acts on the world left by the previous one:

| Command | Effect when its target is live | Stale target |
|---|---|---|
| `spawn i Tmpl vals` | `L += {i}`, `ord(i)` := next ordinal, `M(i)` := template traits, values from defaults then `vals` | n/a |
| `destroy t` | `L −= {t}`; its traits and values go | no-op |
| `add T to t: vals` | `M(t) += {T}`; `V(t, T, ·) := vals` (replaces a carried `T`) | no-op |
| `remove T from t` | when `T ∈ M(t)`: keep the old value as the snapshot, `M(t) −= {T}` | no-op |
| `set T on t: vals` | when `T ∈ M(t)`: overwrite only the named fields | no-op |

Sources: DSL spec §4.4 and §5.3, `dsl-deferred-set`, `dsl-entity-id-total-semantics`.

Then compute the round's net lifecycle changes. For each watched trait `T` and each
`e` live after the round:

- `added T` on `e` when `T` was absent before the round (or `e` was spawned in it) and
  `T ∈ M(e)` now;
- `removed T` on `e`, with its snapshot, when `e` was live before, `T ∈ M⁻(e)` and
  `T ∉ M(e)` now.

A destroyed entity fires nothing, and neither does an entity spawned and destroyed in
the round (`dsl-trait-lifecycle-triggers`: "Net-change firing per commit round",
"Destroyed entities fire no lifecycle trigger").

Order the changes by the first command that touched each `(e, T)`, then by the first
touch of `e` inside that command, then removals before additions, then canonical
trait order ("Deterministic delivery order"). For each change form
`o = ⟨added T or removed T, e, S⟩`, where `S` is the set of its causes: the handlers
that issued a command in the round adding `T` to `e` (for an addition) or removing it
(for a removal). Split it by R6, where `S → c` is immediate when some `p ∈ S` has
`p → c` immediate, and delayed when every `p ∈ S` has `p → c` delayed
(`dsl-trait-lifecycle-triggers`: "Lifecycle cycles are delayed"). The split depends on
`S` only, not on the order of `K`. An edge `p → c` is delayed exactly when `p` and `c`
share a delivery cycle, so the backend folds the cycles of `S` into one common cycle, or
none once two differ, and gives that to the route table (Code: `CommonCycle`,
`LifecycleTracker`).

Finally set `K := ε` and `r := r + 1`. The deliveries just queued are generation 0 of
round `r`; R5 continues the cascade. Commands that round's handlers issue form the next
round (`dsl-trait-lifecycle-triggers`: "Lifecycle cycles are delayed").

Inside one round no rule changes `L` or `M`. This is the precise form of "structural
changes do not alter entity selection within one commit round" (DSL spec §5.3);
see §10 F4.

### 6.7 Completion

**R9. Complete.** When `Q = ε` and `K = ε` after the flush of R7, the activation ends.
`A` is empty. `Δ` and `R` carry over. This is an observation point for §3.6.

### 6.8 Delayed delivery

**R10. Phase clock.** In an activation of phase `φ`, after `D₀` has run and before the
first entry of `Q` is delivered, set `Q := Δ(φ) · Q` and `Δ(φ) := ε`. `D₀` itself adds
nothing to `Δ(φ)`: nothing emits a phase, so a phase handler is on no delivery cycle.
Delays produced from then on, including in commit rounds, go to the new `Δ(φ)` and
wait for the next activation of `φ`. A batch with no repetition of a periodic phase leaves `Δ(φ)`
untouched.

**R11. Host clock.** At a host drain, set `Δ' := Δ(host)` and `Δ(host) := ε`. First
process each host event of `R`, in order: an event that roots phases starts their
batches (outside the model, §1), and any other event runs as its own activation. Then
run each entry of `Δ'` as its own activation, in order (§5). Delays produced during
the drain go to the new `Δ(host)`, so one drain always ends.

**R12. Teardown.** After the `unload` activation, every `Δ(κ)` is discarded without
running a handler.

Sources: `language-philosophy` ("Delayed occurrences belong to the producing clock"),
DSL spec §5.1 and §5.3. Code: `deliver_delayed_events` (`runtime.hpp`) and
`emit_phase_batch`, `emit_host_clock_entry_points` (`cpp_entt_codegen.cpp`). Tested:
`test_delivery_clocks_headless_behavior`, `test_commit_notify_loop_headless_behavior`.

## 7. Propositions

### 7.1 Total primitive evaluation

**Domain.** Expressions and statements of the fragment.

**Assumptions.**

- (A1) Primitive numeric operations are the total functions of
  `dsl-numeric-semantics`: wrapping `int`, floor division with its zero rule, IEEE-754
  binary32 with one rounding per operation, and total float-to-int conversion.
- (A2) User `func`s are pure, and their call graph has no cycle. Code: the analyzer
  rejects any call path from a `func` back to itself inside a module
  (`check_no_recursion`). A call cycle across modules is Open.
- (A3) Native calls terminate and return a value of their declared type (host
  contract, §3.4).
- (A4) Operations on `entity_id` are total: on a stale handle they are no-ops or
  no-match (`dsl-entity-id-total-semantics`).

**Claim (Model).** Every expression of the fragment evaluates to exactly one value in
every configuration, and every row runs a finite number of statements.

**Argument.** Induction on the expression. Literals, locals and field reads have one
value; a read of a flushed cell is a value of the cell's type. Each primitive is total
by A1, and `exists` and other handle operations by A4. A user call terminates by
induction on the depth of the acyclic call graph (A2); a native call by A3. A row has
no general loop or recursion (`language-philosophy`:
"The declarative/restricted-imperative boundary"); each `for` iterates a finite list
value, so the statement count is finite by induction on nesting.

**Limits.** Storage is unbounded in the model. Running out of memory, or out of entity
handles (`dsl-entity-id-total-semantics`, scenario "Handle space exhaustion is an error"), is
outside this claim.

### 7.2 Activation termination

**Domain.** Activations of the fragment.

**Assumptions.** The claim of §7.1 and its assumptions; S1, S2, S3; the initial `L`,
`Δ(κ)` and `R` are finite.

**Claim (Model).** Every activation reaches R9 after finitely many steps.

**Argument.** Give each piece of pending work a rank in `ℕ`:

- a pass of `h` still to run in the current dispatch set: `2·ht(h)`;
- an entry `(o, C)` of `Q`: one element `2·ht(c)` for each `c ∈ C`;
- a command in `K` issued by `p`: `2·ht(p) − 1`.

Let `μ` be the multiset of all ranks. Multisets of naturals ordered by the
Dershowitz–Manna multiset order are well-founded. Group the steps into macro-steps:

1. *A pass of `h`.* It has finitely many rows (`L` is finite) and each row is finite
   (§7.1), so it ends. It removes `2·ht(h)` and adds finitely many entries and
   commands. An entry it adds to `Q` holds only consumers `c` with `h → c` immediate
   (R6), so `ht(c) < ht(h)` and `2·ht(c) < 2·ht(h)`. A command gets
   `2·ht(h) − 1 < 2·ht(h)`. Entries added to `Δ` leave the activation and get no rank.
2. *Delivering an entry.* Dropping it removes ranks. Running it replaces each
   `2·ht(c)` by the pass of `c` with the same rank; together with step 1 for those
   passes, every added rank is below a removed one.
3. *A commit round.* It removes every command. A delivery it queues for consumer `c`
   has `src = p`, the producer of the first command that touched `(e, T)`, and
   `p → c` immediate (R6). That first touch changed `T` in the direction of the net
   change: `T` was absent before a net addition and present before a net removal. So
   `p`'s contract records a command that may add or remove `T` (S2, S3), the edge
   `p → c` exists, and `2·ht(c) ≤ 2·ht(p) − 2 < 2·ht(p) − 1`.
4. *A flush* changes no rank and is finite.

Each macro-step removes at least one element and adds only finitely many elements, each
smaller than a removed one. So `μ` decreases strictly, and the activation ends. Its
first `μ` is finite: `D₀` is finite, and the delayed entries that join it are finite
by assumption.

**Corollaries (Model).** Let `H` be the largest `ht` among the handlers an activation
root reaches.

- Every cascade of one round has at most `H + 1` generations. From generation 2 on,
  each generation's consumers have a smaller height than the producers of the
  previous one; generation 1 can hold delayed entries of any height up to `H`.
- An activation has at most `H` commit rounds: a delivery in round `r + 1` goes to a
  smaller height than some handler that ran in round `r`.

`H` is the depth the compiler records per root (`handler-execution-graph`: "Activation
cascade depth is static"; Code: `record_activation_depths`; Tested: `test_delivery_cycles`
"an acyclic chain records its length").

**Limits.** Finite work per activation is not bounded work. Rows multiply work by the
number of entities, and spawns add entities. Across activations work can grow without
bound: a handler on a cycle that emits two occurrences per run doubles `Δ` every
activation, and a lifecycle cycle that spawns two entities per round doubles the
population every activation. No frame-time bound follows from this claim. There is no
configured cascade limit in the model or in the normative text.

### 7.3 Schedule independence

**Domain.** One dispatch set `D` with its relation `≺_D`, and the rest of the
activation after it.

**Local commutation.** Two handlers `h, k ∈ D` that `≺_D` does not order *commute*
when, from every reachable configuration, running the pass of `h` then the pass of `k`,
and running them in the other order, each followed by the same remaining steps of the
activation, end in `≈_ord`-equivalent results (`≈` for their spawns, see C2).

These conditions are sufficient (Model, argued per case). Each gives the same
configuration up to `π`, up to the order inside each `A(x)`, and up to the
permutations C2 to C4 allow. No rule looks at raw identities or at the order inside
`A(x)`, so the remaining steps end equivalently.

- **C1, data.** No cell is accessed by both, except when both write it only with Accum
  writes, or both only with Join writes of one operator. Float Accum writes add to
  the multiset `A(x)`, and `⊎` commutes; `int` Accum writes add with wrapping, which
  commutes. A Join of operator `op` sets
  `V(x) := op(V(x), e)` where `e` does not read `x` (`handler-contracts`); `op` is
  commutative and associative (`dsl-numeric-semantics`: "Float min and max are
  commutative"; `or` and `and` on `bool`), so two Joins give `op(op(v, a), b) =
  op(op(v, b), a)`. Every other shared access can change a value the other reads or
  leaves.
- **C2, commands.** Each command of `h` and each command of `k` commute by the command
  table of `handler-execution-graph` ("Channel conflicts"). Then the commit gives the
  same `L`, `M` and `V` in either order. Spawns commute only up to `π`; their ordinals
  swap, so this gives `≈`, and `≈_ord` needs the spawn-order check (Open).
- **C3, occurrences.** At every position (round, generation), each run reached from
  `h` and each run reached from `k` at that position commute, and so do all runs
  reached from them through delayed edges (`handler-execution-graph`: "Channel
  conflicts", occurrences). This covers the order in which their emissions and their
  commands' lifecycle deliveries enter `Q` and `Δ`.
- **C4, effects.** They share no effect domain on which either acts. Two observations
  of `d` give the same responses in either order by the host contract and are
  equivalent in `τ`.
- **C5, contracts.** S2 holds for `h` and `k`: every access they make, including inside
  native calls, is in their contracts.

*Why C3 is needed (Model, sketch).* Swapping `h` and `k` permutes the entries they add
to `Q`. Entries from one handler keep their relative order. Consider the positions in
lexicographic order; there are finitely many (§7.2). At the first position, the entries
reached from `h` and from `k` sit in one block of `Q`, and any order of the block can
be reached by adjacent swaps of an `h`-entry with a `k`-entry. By C3 each swap exchanges
commuting runs, so the configuration after the block is equivalent, except that the
entries those runs produce for the next position are permuted in the same way. The
induction continues at the next position. Delayed entries move to `Δ` in a permuted
order and are covered because C3 also demands that the delayed runs commute.

C3 assumes that the runs reached from `h` and `k` are fixed by `h` and `k` alone. When
commands of both cause one net change, R8 makes one delivery. Its route is fixed by the
set of causes, not by which command came first, and it is a run that `h` or `k` reaches
on its own, so the swap keeps it (§10 F1, resolved).

**Global step (Model).** If every two handlers of `D` that `≺_D` does not order
commute, then all linearizations of `(D, ≺_D)` from one configuration end in
equivalent results. The equivalence is `≈_ord`, weakened to `≈` only for the spawns
of unordered handlers (C2).

*Argument.* Any two linear extensions of a finite partial order are connected by a
finite sequence of swaps of adjacent elements that the order leaves unordered. (Sort the
first into the second; each swap exchanges an adjacent pair that appears in the other
order in the second sequence, and such a pair cannot be ordered, because both sequences
respect the order.) Each swap exchanges two commuting handlers after the same prefix
and before the same remaining steps, so by the definition of commuting the results stay
equivalent.

**Activation as a function (Model, conditional).** If every dispatch set of an
activation meets the global step's hypothesis, its completed result is unique up to
that equivalence. The cascade order between dispatch sets is fixed by R5, R6 and R8.
Under that hypothesis, and under §7.2, a completed activation is a function from
configurations to configurations and effect traces, both taken up to that
equivalence.

**Open obligation O1.** The compiler must place a schedule edge between every two
co-eligible handlers that do not commute (`handler-execution-graph`: "An activation
denotes a trace"). Its rules implement C1 to C4 and the spawn-order check. Showing that
they imply local commutation for every accepted program is not done here. §10 lists
one accepted class where the obligation fails (F2); F1 was another and is resolved.

### 7.4 Non-vacuity boundary

**What the structural analysis establishes (Code, Tested).** The reachable-archetype
fixpoint over-approximates the trait sets entities can carry
(`archetype-reachability`). From it the compiler rejects a rule whose domain matches no
reachable archetype, a `without:` trait that never appears, and a trigger with no
producer. Each rejected declaration can never run, so these checks are sound for
"never runs". Tested: `test_archetype_reachability`.

**What it does not establish.**

- A value guard (`when:`, `where:`, `if`) can be false in every reachable world. The
  analysis ignores values.
- A reachable archetype need not be reached. Commands to unknown targets are assumed
  to reach every archetype, so the set can be larger than what runs.
- A handler that runs can still be unobservable: its writes may be overwritten before
  any read, or read by nothing that reaches an observation.

No general semantic non-vacuity theorem is claimed here. A declaration that passes
these checks is *structurally live*: it is not proven to affect the result.

## 8. Worked traces

Each trace lists the initial state, the relevant edges, the steps and the result. Names
are short; `std.core` is imported. Values are binary32 where they are floats.

### 8.1 A writer before a reader: membership against values

```cactus
trait Speed:
    var v: float = 1.0
trait Pos:
    var x: float = 0.0
trait Fast

entity Car:
    Speed
    Pos

rule Boost:
    each:
        Speed as s
    on tick:
        s.v = s.v * 2.0
        add Fast

rule Move:
    each:
        Speed as s
        Pos as p
    without:
        Fast
    on tick:
        p.x = p.x + s.v
```

Edges: `Boost ≺ Move`, writer before reader on `Speed.v`. `without: Fast` reads
nothing, so the `add` adds no edge.

| Step | Rule | State after |
|---|---|---|
| 0 | start, tick 1 | `M(Car) = {Speed, Pos}`, `v = 1.0`, `x = 0.0`, `K = ε` |
| 1 | R2 for Boost | rows `⟨Car⟩` |
| 2 | R3 | `v = 2.0`; `K = ⟨add Fast to Car⟩` |
| 3 | R2 for Move | `Fast ∉ M(Car)`: rows `⟨Car⟩`. Membership is the round's. |
| 4 | R3 | reads `v = 2.0`, the live value: `x = 2.0` |
| 5 | R7, R8 | `M(Car) = {Speed, Pos, Fast}`; no watched trait, no delivery |
| 6 | tick 2 | Boost: `v = 4.0`, `add Fast` replaces a carried marker; Move: no row |

Result: `x = 2.0`, `v = 4.0` after two ticks. Move saw the new value of `v` in tick 1
but the old membership. Probe: this program gives `v = 2, x = 2` after one frame and
`v = 4, x = 2` after two. No committed test checks this pair (§9).

### 8.2 Independent handlers, and a rejected conflict

```cactus
trait Health:
    var hp: float = 10.0
trait Tint:
    var r: float = 0.0

rule Burn:
    each:
        Health as h
    on tick:
        h.hp -= 0.5

rule Poison:
    each:
        Health as h
    on tick:
        h.hp -= 0.25

rule Flash:
    each:
        Tint as t
    on tick:
        t.r = 1.0
```

Burn and Poison both write `Health.hp` with kind Accum, so they add no edge; Flash
touches another trait. `≺` is empty. For one entity with both traits, two of the six
linearizations:

| Order | After passes | After the end-of-cascade flush |
|---|---|---|
| Burn, Poison, Flash | `A(hp) = ⦃−0.5, −0.25⦄`, `r = 1.0` | `hp = rnd(10.0 − 0.75) = 9.25` |
| Flash, Poison, Burn | `A(hp) = ⦃−0.25, −0.5⦄`, `r = 1.0` | `hp = 9.25` |

The multisets are equal, so the flushes agree (C1). Tested:
`test_linearization_commuting_headless_behavior`, `test_exact_accumulation_headless_behavior`.

Now replace Poison's body with `h.hp = 0.0`. Burn is Accum and Poison is Assign on one
field, and no meaningful rule orients them. The compiler rejects the program with an
unorientable-conflict error naming both handlers, `Health.hp`, and the kinds Accum and
Assign (`handler-execution-graph`: "Unorientable conflicts are compile errors").
Tested: `test_semantic` "Commuting writes: Accum against Assign conflicts" and
"Unorientable conflicts: the diagnostic names both sides and both fixes".

### 8.3 A spawn is invisible until commit

```cactus
trait Coin
trait Counter:
    var seen: int = 0
template CoinT:
    Coin

entity Bank:
    Counter

rule Mint:
    on tick:
        spawn CoinT()

rule Count:
    each:
        Coin
    on tick:
        Bank[Counter].seen += 1
```

Mint and Count share no cell and no channel, so `≺` is empty.

| Step | State |
|---|---|
| tick 1, Mint | fresh `c₁ ∈ Iss`, `c₁ ∉ L`; `K = ⟨spawn c₁ CoinT⟩` |
| tick 1, Count | rows `⟨⟩`: `c₁` is not live. Either order gives this. |
| tick 1, R8 | `L += {c₁}`, `ord(c₁) = n`; `seen = 0` |
| tick 2, Mint | `K = ⟨spawn c₂⟩` |
| tick 2, Count | rows `⟨c₁⟩`: `seen = 1` |
| tick 2, R8 | `c₂` live |

Result after two ticks: `seen = 1`, two coins live. In `fixed_tick` the same holds per
repetition: a spawn of repetition N is selectable in repetition N + 1
(`phase-activation-model`: "Activation commit boundary", scenario "Repetition commits
before next repetition"). Tested:
codegen test "graph structural commands commit after cascades and between fixed
repetitions"; `test_runtime_entity_allocator` for fresh identities.

### 8.4 An immediate chain, and a cycle delayed to its clock

Immediate chain: a `tick` handler emits `A`, the `A` handler emits `B`, the `B` handler
writes. All edges are immediate. Heights are 3, 2, 1, so the recorded depth of `tick` is
3, and the chain finishes in generations 0, 1, 2 of one activation. Tested:
`test_delivery_cycles` "an acyclic chain records its length";
`test_delivery_clocks_headless_behavior` runs a 100-handler chain inside one `tick`.

Delayed cycle, from `tests/fixtures/delivery_clocks_runtime.cactus`: `StartKnock`
(on `fixed_tick`) emits `Knock to self` once; `Answer` (on `Knock`) increments `knocks`
and emits `Knock to self`; `Observe` (on `Knock`) increments `seen`. Edges:
`StartKnock → Answer` and `StartKnock → Observe` immediate; `Answer → Answer` delayed;
`Answer → Observe` immediate. One entity `Front`. One frame runs three repetitions.

| Repetition | Generation 0 | Delayed in | Cascade | After |
|---|---|---|---|---|
| 1 | StartKnock emits `o₁` | none | `(o₁, {Answer, Observe})`: knocks 1, emits `o₂`; R6: `(o₂, {Observe})` to `Q`, `(o₂, {Answer})` to `Δ(fixed_tick)`; Observe runs for `o₁`, then for `o₂` | knocks 1, seen 2, one delay pending |
| 2 | StartKnock returns | `(o₂, {Answer})` | Answer: knocks 2, emits `o₃` split again; Observe once | knocks 2, seen 3, one delay pending |
| 3 | StartKnock returns | `(o₃, {Answer})` | as in 2 | knocks 3, seen 4, one delay pending |

A frame with no repetition leaves `Δ(fixed_tick)` as it is. Tested:
`test_delivery_clocks_headless_behavior` checks knocks 3, seen 4, one pending delay,
and no change on a frame without repetition.

### 8.5 Lifecycle rounds and delayed revalidation

Immediate rounds: `BeginDeath` (on `tick`) runs `add Dying`; `StartDying` (on
`added Dying`) runs `add Fading`; `StartFade` (on `added Fading`) writes a field. No
cycle.

| Round | Applies | Deliveries |
|---|---|---|
| 0 | tick handlers run; `K = ⟨add Dying to e⟩` | none |
| 1 | `Dying` added | `added Dying` → StartDying; `K = ⟨add Fading to e⟩` |
| 2 | `Fading` added | `added Fading` → StartFade |
| 3 | nothing queued | activation completes |

Delayed, with revalidation: `OnAdded` (on `added Pulse`) removes `Pulse`, and
`OnRemoved` (on `removed Pulse`) adds it back. The two lifecycle edges form a cycle, so
both are delayed. A `tick` handler `Start` adds `Pulse` once; `Start → OnAdded` is
immediate.

| Activation | Round | Event |
|---|---|---|
| tick 1 | 1 | `added Pulse`, `src = Start`: immediate, OnAdded runs, queues `remove Pulse` |
| tick 1 | 2 | `removed Pulse`, `src = OnAdded`: delayed to `Δ(tick)`; activation ends with `Pulse` absent |
| tick 2 | gen 1 | the delayed entry: `Pulse ∉ M(e)` still holds, OnRemoved runs, queues `add Pulse` |
| tick 2 | 1 | `added Pulse`, `src = OnRemoved`: delayed |
| tick 3 | gen 1 | if `Pulse ∈ M(e)`, OnAdded runs; if another handler removed `Pulse` in tick 2 or 3, the entry is dropped by R5 step 2 |

Each activation commits one change and ends. Tested: `test_runtime_activation_scheduler`
("Lifecycle: commands from a delivery commit in the next round", "a self-feeding
add/remove commits one round per activation and queues the trigger", "a delayed added
delivery is skipped when the trait is gone", "a delayed removed delivery keeps its
snapshot"); `test_trait_lifecycle_headless_behavior`.

### 8.6 Unordered output: why disjoint fields are not enough

Each case has two handlers `A` and `B` that write no common field.

1. **Spawns seen by an order-sensitive rule.** `A` spawns `Coin`, `B` spawns `Gem`,
   both carry `Pickup`, and a rule `each: Pickup` has `limit: 1`. Their creation order
   decides which entity the limited rule takes. Rejected:
   `archetype-reachability` "Observable spawn order must be defined". Tested:
   `test_archetype_reachability` "[spawn-order]", `test_spawn_order_headless_behavior`.
2. **Emitters with conflicting consumers.** `A` emits `Hit`, `B` emits `Heal`, the
   `Hit` consumer runs `h.hp = 0` and the `Heal` consumer runs `h.hp = 10`. If the
   program were accepted, its two linearizations would give:

   | Order | `Q` after generation 0 | Generation 1 | `hp` |
   |---|---|---|---|
   | `A`, `B` | `⟨(Hit, {OnHit}), (Heal, {OnHeal})⟩` | `0`, then `10` | `10` |
   | `B`, `A` | `⟨(Heal, {OnHeal}), (Hit, {OnHit})⟩` | `10`, then `0` | `0` |

   Rejected by the occurrence channel. Tested: `test_semantic` "Channel conflicts:
   occurrences race per generation".
3. **Consumers at different generations.** If the `Hit` consumer only emits `E3` and
   the `E3` consumer assigns `hp`, that run is at generation 2, after the `Heal`
   consumer in every order. Accepted, correctly.
4. **One net change from both.** §10 F1: accepted, and its route is fixed by the set of
   causes, so the result does not depend on the order of `A` and `B`. Tested:
   `test_lifecycle_route_ping_first_headless_behavior`,
   `test_lifecycle_route_pong_first_headless_behavior`.

### 8.7 A stale recipient, and an accumulation read in between

Stale recipient inside one activation:

```cactus
trait Victim
trait Marked
trait Keeper
trait Count:
    var n: int = 0

entity Doomed:
    Victim

entity Holder:
    Keeper
    Count

event Ping

rule Kill:
    each:
        Keeper
    on tick:
        destroy Doomed
        add Marked

rule Notify:
    each:
        Keeper
    on added Marked:
        emit Ping to Doomed

rule Hear:
    each:
        Victim
    on Ping:
        Holder[Count].n += 1
```

| Step | State |
|---|---|
| round 0 | `K = ⟨destroy Doomed, add Marked to Holder⟩` |
| round 1 | `Doomed ∉ L`; `added Marked` on `Holder` → Notify queues `(Ping, Doomed)` |
| R5 | `rcp ∉ L`: dropped; Hear does not run, `n = 0` |

Probe: `n = 0` after one frame; without the `destroy` line, `n = 1`. Tested for the
same rule outside commit rounds: `test_runtime_activation_scheduler`
"drain_event_cascade dispatches valid events and drops events targeting a
no-longer-valid entity", "a delayed occurrence whose recipient is gone is dropped at
delivery"; codegen test "drain_event_cascade drops a stale-recipient occurrence before
any dispatch".

Accumulation with a read in between: two entities `First` and `Second` carry
`Pool.level = 1.0e8`. `AddOne` (each Pool) runs `p.level += 1.0`; `Look` reads
`First[Pool].level`, ordered after `AddOne` (writer before reader); `TakeLarge` (each
Pool, `after: Look`) runs `p.level -= 1.0e8`.

| Cell | Normative steps | Result |
|---|---|---|
| `First.level` | `A = ⦃1.0⦄`; Look's read flushes: `rnd(1.0e8 + 1) = 1.0e8`; then `A = ⦃−1.0e8⦄`; final flush | `0.0`; Look saw `1.0e8` |
| `Second.level` | no non-Accum access between the writes: `A = ⦃1.0, −1.0e8⦄`; one flush: `rnd(1.0e8 + 1 − 1.0e8)` | `1.0` |

The backend gives the same values (Tested: `test_accumulation_per_cell_headless_behavior`;
§10 F3).

## 9. Correspondence

| Model element | Normative requirement | Implementation | Evidence |
|---|---|---|---|
| R1 linearization | handler-execution-graph: "An activation denotes a trace", "Dependency levels are the canonical form of the trace" | `compute_handler_schedule` (`execution_graph_scheduler.cpp`); stable order in codegen; `linearization_harness.hpp` | Tested: `test_linearization_sample_headless_behavior`, `test_linearization_commuting_headless_behavior` |
| R2 selection, creation order | handler-contracts: "Selectionless handler cardinality"; DSL §4.7; philosophy: "Predictability is a first-class design goal", item 8 | `system_emitter.cpp` creation-ordered passes (`creation_ordered`, `CreationOrdinal`) | Tested: `test_spawn_order_headless_behavior`; targeted routing: codegen tests "targeted unary handler runs at most once for the recipient, gated on its selection" |
| R3 live reads, writes | phase-activation-model: "Activation commit boundary"; handler-contracts: "Field writes carry a write kind" | generated handler bodies | Tested: `test_semantic` "[write-kinds]"; values in 8.1: Code |
| R3 deferred command values | dsl-deferred-set: "`set` values are evaluated when the statement runs" | `queue_structural_command` (`runtime.hpp`) | Tested: `test_deferred_set_headless_behavior` |
| R3 fresh spawn identity | dsl-entity-id-total-semantics: "A stale handle stays stale for the life of its world" | `reserve_entity`, `create_reserved` (`runtime.hpp`) | Tested: `test_entity_handle_reuse_headless_behavior`, `test_runtime_entity_allocator` |
| R4 flush | dsl-numeric-semantics: "Float accumulation into one field is exact" | `AccumulationPlan` (`accumulation_plan.cpp`), `AccumulatedDeltas` (`cactus_runtime.hpp`), `generated_flush_accumulations` | Tested: `test_exact_accumulation_headless_behavior`, `test_accumulation_per_cell_headless_behavior`, `test_accumulation_storage_move_headless_behavior`, `test_runtime_stdlib` "accumulated deltas: …", codegen "[write-kinds]"; accesses with no site: Code (§6.4) |
| R5 stale drop | targeted-event-delivery: "Targeted delivery obeys total entity semantics" | `deliverable`, `drain_event_cascade` (`runtime.hpp`) | Tested: `test_runtime_activation_scheduler`, codegen stale-recipient test |
| R5 revalidation | dsl-trait-lifecycle-triggers: "Lifecycle cycles are delayed" | `lifecycle_change_holds` (`runtime.hpp`) | Tested: `test_runtime_activation_scheduler` "Lifecycle: a delayed added delivery is skipped when the trait is gone" |
| R6 split, delayed edges | handler-execution-graph: "Every event cycle passes through a delay" | `classify_delivery_cycles`, `mark_delayed_edges` (`delivery_cycles.cpp`); `route_admits`, `DeliveryRoute` (`runtime.hpp`); `DeliveryPlan` (`cpp_entt_codegen.cpp`) | Tested: `test_delivery_cycles`, `test_runtime_activation_scheduler` "route_admits splits consumers by the producer's cycle", codegen "[delivery-cycles]" |
| Generations | handler-execution-graph: "Occurrences are delivered in generations" | FIFO `event_queue` in `drain_event_cascade` | Tested: `test_generations_headless_behavior` |
| R7, R8 commit rounds | dsl-trait-lifecycle-triggers: "Net-change firing per commit round", "Deterministic delivery order", "Destroyed entities fire no lifecycle trigger"; DSL §5.3 | `commit_activation`, `LifecycleTrackers::deliver`, `LifecycleTracker::collect` (`runtime.hpp`) | Tested: `test_runtime_activation_scheduler` "Lifecycle: …", `test_trait_lifecycle_headless_behavior`; causes of a net change: `test_runtime_activation_scheduler` "Lifecycle: a change with several causes …", "… whose causes all share …", "only commands in the net change's direction …", `test_lifecycle_route_*_first_headless_behavior` (F1) |
| R8 command table | DSL §4.4, §5.3; dsl-deferred-set: "`set` ordering against other commands" | `StructuralCommand`, generated command lambdas | Tested: `test_deferred_set_headless_behavior`, `test_runtime_activation_scheduler` "commit_activation with no lifecycle trackers applies queued commands exactly once" |
| R10 phase clock | philosophy: "Delayed occurrences belong to the producing clock" | `deliver_delayed_events`, `emit_phase_batch` | Tested: `test_delivery_clocks_headless_behavior`, `test_runtime_activation_scheduler` "delayed occurrences keep production order and recipient" |
| R11 host clock | same | `emit_host_clock_entry_points` (`generated_drain_external_events`) | Tested: `test_delivery_clocks_headless_behavior` (load chain), `test_commit_notify_loop_headless_behavior`, codegen "a deferred internal event runs as its own activation with its target" |
| R12 teardown | same | no drain after `unload` | Tested: `test_delivery_clocks_headless_behavior` (unload step) |
| Startup arrival | dsl-trait-lifecycle-triggers: "Load-time entities fire added before load" | `LifecycleTrackers::deliver_arrivals` | Tested: `test_runtime_activation_scheduler` "Lifecycle: arrivals follow creation order, then trait order" |
| §7.2 depth `H` | handler-execution-graph: "Activation cascade depth is static" | `record_activation_depths` | Tested: `test_delivery_cycles` depth cases |
| §7.3 C1 | handler-execution-graph: "Contract conflict edges are oriented only by meaningful rules", "Commuting writes add no edge" | `pair_conflict`, `conflict_reasons` | Tested: `test_semantic` "Commuting writes: …" |
| §7.3 C2, C3, C4 | handler-execution-graph: "Channel conflicts", "Unorientable conflicts are compile errors" | `command_conflicts`, `OccurrenceReach`, `RunConflicts`, `ConflictNodes` | Tested: `test_semantic` "Channel conflicts: …", "Unorientable conflicts: …"; `test_program_linker` "a cross-module occurrence race is a link error" |
| §7.3 spawn order | archetype-reachability: "Observable spawn order must be defined" | `archetype_reachability.cpp` | Tested: `test_archetype_reachability` "[spawn-order]" |
| §7.4 structural liveness | archetype-reachability; handler-execution-graph: "A trigger with no producer is a compile error" | `archetype_reachability.cpp` | Tested: `test_archetype_reachability` |
| Trace 8.1 | as R2, R3, R8 | — | Probe. Missing test: none checks a live value read next to an unchanged membership in one activation |
| Trace 8.2 | as §7.3 C1 | — | Tested (see trace) |
| Trace 8.3 | phase-activation-model: "Activation commit boundary" | — | Tested: codegen commit test. `exists` on a reserved handle before commit: Code (`reserve_entity`); Missing test |
| Trace 8.4 | as R6, R10 | — | Tested: `test_delivery_clocks_headless_behavior` |
| Trace 8.5 | as R8, R5 | — | Tested (see trace) |
| Trace 8.6 | as §7.3 | — | Tested (see trace) |
| Trace 8.7 | as R5, R4 | — | Stale drop Tested. Stale drop after a commit round: Probe, Missing test. Accumulation scope: Tested (`test_accumulation_per_cell_headless_behavior`) |

Every test named above was run on the current tree while writing this document and
passed: `test_delivery_cycles`, `test_runtime_activation_scheduler`,
`test_delivery_clocks_headless_behavior`, `test_generations_headless_behavior`,
`test_exact_accumulation_headless_behavior`, `test_linearization_commuting_headless_behavior`,
`test_linearization_sample_headless_behavior`, `test_spawn_order_headless_behavior`,
`test_trait_lifecycle_headless_behavior`, `test_deferred_set_headless_behavior`,
`test_commit_notify_loop_headless_behavior`, `test_semantic` tags
`[handler-execution-graph]`, `[write-kinds]`, `[channels]`, `[unorientable]`,
`test_codegen_entt` tags `[delivery-cycles]`, `[trait-lifecycle]`, `[write-kinds]` and
the named codegen cases, and `test_archetype_reachability` `[spawn-order]`.
`flush-accumulation-per-cell` added `test_accumulation_per_cell_headless_behavior`,
`test_accumulation_storage_move_headless_behavior` and the `test_runtime_stdlib`
"accumulated deltas: …" cases. `route-merged-lifecycle-changes` added
`test_lifecycle_route_ping_first_headless_behavior`,
`test_lifecycle_route_pong_first_headless_behavior` and three
`test_runtime_activation_scheduler` "Lifecycle: …" cases.

## 10. Findings

Each finding states the gap, a minimal witness, the evidence, the claim it limits,
options to resolve it, and their impact. A finding that needs a behavior change needs
its own proposal. `list-activation-semantics-gaps` listed F1 and F2 as known gaps and
resolved F4 and F5. `flush-accumulation-per-cell` resolved F3.
`route-merged-lifecycle-changes` resolved F1.

### F1. One net lifecycle change from two producers takes the first producer's route

**Gap.** `dsl-trait-lifecycle-triggers` fires one delivery per net change of
`(e, T)` per round, and delays a delivery that travels along a delayed lifecycle edge.
When commands of two handlers cause one net change, the text does not say whose edge
the delivery travels along. The backend takes the producer of the first command
(Code: `LifecycleTrackers::deliver` uses the first change's `component`). The
occurrence-channel rule treats the two producers' reaches separately: one reaches the
consumer through a delayed edge, the other at round 1, so it finds no race and adds no
edge. The command order then follows the stable topological order, which is
declaration order for unordered handlers.

**Witness (Probe).**

```cactus
module lifecycle_route

use std.core

trait Mark
trait Body:
    var runs: int = 0

entity Target:
    Body
    KeepOnLoad

event Ping
event Pong

rule SendPing:
    on tick:
        emit Ping to Target

rule SendPong:
    on tick:
        emit Pong to Target

rule Loop:
    each:
        Body
    on Ping:
        add Mark

rule Other:
    each:
        Body
    on Pong:
        add Mark

rule React:
    each:
        Body as b
    on added Mark:
        b.runs += 1
        emit Ping to Target
```

`Loop` and `React` form a cycle, so `Loop → React` is delayed; `Other → React` is
immediate. Both orders compile. After one frame `runs` is `0` when `SendPing` is
declared first and `1` when `SendPong` is declared first; after the next frame both
are `1`.

**Affected claims.** Determinacy (`language-philosophy`, scenario "Source layout does
not change behavior"); §7.3 C3; "An activation denotes a trace".

**Options.**

1. Define the route by the set of producers: a delivery is immediate to a consumer
   when any causing producer has an immediate edge to it. The net change then does
   not depend on command order. Syntax: none. Behavior: changes the timing of such
   deliveries in programs like the witness. Authoring: none.
2. Treat a delayed reach and an immediate reach of one lifecycle consumer from two
   unordered producers as a race in the occurrence channel, and report an
   unorientable conflict. Syntax: none. Behavior: rejects programs like the witness;
   authors add one `after:`. Authoring: one ordering line where needed.

**Resolved.** `route-merged-lifecycle-changes` took option 1. A net change's route comes
from every handler whose command in the round moved `T` in the change's direction (R8),
so the witness gives `runs = 1` after the first frame in both orders. Tested:
`test_lifecycle_route_ping_first_headless_behavior`,
`test_lifecycle_route_pong_first_headless_behavior`, `test_runtime_activation_scheduler`
"Lifecycle: a change with several causes is immediate when any cause is off the
consumers' cycle".

### F2. Native world reads are not ordered against field writers

**Gap.** `std.transform` and `std.query` functions read trait data in native code. The
analyzer records their use as an observation of the `world` effect domain
(`known_stdlib_effect_summary`), not as field reads. A handler that writes the field is
not acting on `world`, so no data or effect conflict arises. S2 fails for these calls.

**Witness (Probe).**

```cactus
module native_read

use std.core
use std.transform.flat as transform

trait Seen:
    var x: float = 0.0

entity Mover:
    transform.WorldTransform
    KeepOnLoad

entity Watcher:
    Seen
    KeepOnLoad

rule Move:
    each:
        transform.WorldTransform as t
    on tick:
        t.position.x = t.position.x + 1.0

rule Look:
    each:
        Seen as seen
    on tick:
        seen.x = transform.world_position(Mover).x
```

Both orders compile. After one frame `Watcher`'s `seen.x` is `1.0` when `Move` is
declared first and `0.0` when `Look` is.

**Affected claims.** Determinacy; §7.3 C5 and O1. It also affects §6.4: a native read of
a buffered float field does not flush it, because only contract accesses trigger a
flush (Code: `AccumulationPlan`).

**Options.**

1. Give each such native function a field-level read summary (here
   `WorldTransform.position`) and fold it into the caller's contract, as for body reads.
   Syntax: none. Behavior: adds writer-before-reader edges, or unorientable-conflict
   errors where none orients. Authoring: none for one-way flows.
2. Make the `world` domain conflict with every write of a trait the native may read.
   Syntax: none. Behavior: coarser; more edges. Authoring: none.

**Follow-up.** A separate change. Listed in "Known correctness gaps are listed" until
that change closes it.

### F3. The backend flushes a whole field's deltas when one entity's cell is read

**Gap.** `dsl-numeric-semantics` sums the Accum writes "to one float field of one
entity" between two non-Accum accesses of that field, and rounds once. The backend
flushes the whole table of a field on entry to any handler that accesses the field
directly (Code: `AccumulationPlan::entry_flushes`). Deltas of entities that handler does
not touch are then rounded twice.

**Witness (Probe).** The program of §8.7. The backend ends with `Second.level = 0.0`;
the normative value is `1.0`. The result does not depend on order: this is a
conformance gap, not a determinacy gap.

**Affected claims.** Exact accumulation as specified; §6.4 as a description of the
backend. Determinacy and §7.3 are not affected, because the flush point is fixed by
the schedule.

**Options.**

1. Flush only the cells the entering handler can access: per row for a unary pass,
   per named cell for a named access. Syntax: none. Behavior: matches the spec; the
   witness gives `1.0`. Authoring: none.
2. Change the specification so that any non-Accum access of the field by a handler
   ends the accumulation interval for every entity. Syntax: none. Behavior: the
   current one becomes normative. Authoring: none, but the meaning is less local.

**Follow-up.** A separate change. Not a known-gaps row: no correctness property is
broken.

**Resolved.** `flush-accumulation-per-cell` took option 1, at the access site instead of
on handler entry: generated code settles one cell just before each non-Accum access of
it. Pending deltas are keyed by entity, so a handler that may move component storage
re-resolves them instead of flushing. The witness gives `1.0`
(Tested: `test_accumulation_per_cell_headless_behavior`). Accesses with no site of
their own still flush the whole field on handler entry (§6.4).

### F4. "Midway through an activation" and commit rounds

**Gap.** DSL spec §5.3 said structural changes "do not alter entity selection midway
through an activation". Lifecycle commit rounds do change selection inside one
activation: a round-1 handler sees round-0 commands. The intended meaning is "within a
commit round" (R8).

**Witness.** §8.5: `StartDying` in round 1 sees `Dying`, added inside the same
activation.

**Evidence.** Tested: `test_runtime_activation_scheduler` "Lifecycle: a delivery
observes every structural change of its round".

**Affected claims.** None, once read as R8 states it.

**Options.** Reword §5.3 to "within one commit round". Syntax: none. Behavior: none.
Authoring: none.

**Resolved.** DSL spec §5.3 now says selection does not change "within one commit
round".

### F5. "Commit" in exact accumulation and commit rounds

**Gap.** `dsl-numeric-semantics` ended an accumulation interval at "the activation's
start or commit". With commit rounds there are several commits per activation. The
backend flushes at the end of every cascade, before each round's commands apply
(Code: `generated_drain_event_cascade`), which R7 follows.

**Witness.** A `tick` handler and an `on added T` handler both `+=` one float field:
are their deltas summed into one rounding or two?

**Evidence.** Tested: codegen test "the commit flushes every buffered field first";
headless test "a commit round ends the accumulation interval".

**Affected claims.** §6.4 wording only.

**Options.** Reword to "the start of the activation or of a commit round". Syntax:
none. Behavior: none (matches the backend). Authoring: none.

**Resolved.** `dsl-numeric-semantics` and DSL spec §4.7 now end an interval at "the
start of the activation or of a commit round".

### Note on the known gaps list

`language-philosophy` ("Known correctness gaps are listed") says that an accepted class
that may break a correctness property, once found, is listed until a change closes it.
F1 and F2 were such classes (determinacy). F1 is resolved, and that requirement lists
F2.
