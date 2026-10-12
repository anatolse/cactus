# archetype-reachability Specification

## Purpose
Derive, from the whole linked program, which trait sets entities can actually have, and use that fact to reject declarations that can never take effect and spawn orders that a program could observe without defining them.
## Requirements
### Requirement: Reachable archetypes are a least fixpoint over trait sets
The compiler SHALL compute, for every linked program, the set of reachable archetypes: the trait sets that some entity can carry at some point while the program runs. It SHALL be the least set that:

- contains the trait set of every named entity and every load-time entity created from a module, scene or data file, including every descendant of a hierarchical declaration with its generated `Parent` relation;
- contains the flattened trait set of every template named by a reachable `spawn`, including its descendants;
- contains every entity a stdlib function creates natively when a reachable handler calls it (the editor's `spawn_template` can spawn any `pub template`);
- is closed under every `add`, `remove`, state variant swap, state removal and `project` that a reachable handler can apply to an entity of that archetype.

A handler is reachable when its rule domain matches a reachable archetype (or it has no domain, or it is a global `reduce:`, which runs once even over an empty domain) and its trigger has a producer. `destroy` fires no lifecycle trigger, so it produces no `on removed`. Projected traits SHALL count as members of the archetype they overlay, because rule filters match them.

The computation SHALL terminate for every program, because the traits of a program are finite. The result SHALL depend only on resolved canonical identities, never on source spelling, import aliases, file order or module link order.

#### Scenario: Template and add make two archetypes
- **WHEN** a program spawns template `Enemy` with traits `{Position, Health}` and a rule `each: Health` runs `add Dying`
- **THEN** the reachable set contains `{Position, Health}` and `{Position, Health, Dying}`

#### Scenario: State swap replaces the variant
- **WHEN** a template starts in `Door.Closed` and a rule `each: Door.Closed` runs `add Door.Open`
- **THEN** the reachable set contains the archetype with `Door.Open` and without `Door.Closed`, and no archetype carries both variants

#### Scenario: Entities created by stdlib native code are reachable
- **WHEN** a reachable handler calls the editor's `spawn_template`
- **THEN** the reachable set contains the trait set of every `pub template` of the program

#### Scenario: Result is independent of declaration and link order
- **WHEN** the same program is linked with its modules in a different order, or its rules reordered within a file
- **THEN** the reachable set is identical

### Requirement: Unknown targets are approximated soundly
Where program text does not determine which entities a structural operation reaches, the analysis SHALL assume it can reach every reachable archetype, so the reachable set is never smaller than what the program can create at runtime. This SHALL apply to:

- `add`, `remove` or `project` whose target is an `entity_id` value that is not the rule's own entity, a pair binding, a named entity, or the result of a `spawn` in the same handler;
- commands declared by an `extern rule` contract, whose targets are chosen by host code.

When the target is the rule's own entity, a pair binding, or a named entity, the operation SHALL apply only to the archetypes that target can have.

#### Scenario: Add to an unknown entity reaches every archetype
- **WHEN** a handler runs `add Frozen to f.target` and `f.target` is an `entity_id` field
- **THEN** every reachable archetype also appears with `Frozen` added

#### Scenario: Add to a pair binding stays within that binding's domain
- **WHEN** a pair rule binds `b: Crate` and runs `add Broken to b`
- **THEN** `Broken` is added only to reachable archetypes that carry `Crate`

#### Scenario: Extern commands are approximated
- **WHEN** an `extern rule` contract declares `add Selected`
- **THEN** every reachable archetype also appears with `Selected` added

### Requirement: A rule domain that matches nothing is a compile error
The compiler SHALL report an error when a rule's domain matches no reachable archetype. A unary domain matches an archetype that contains every `each:` trait and no `without:` trait. A pair domain matches when each binding's traits match some reachable archetype. The diagnostic SHALL name the rule, the domain, and the reason: either the traits never appear together on any reachable archetype, or the domain contradicts itself.

A domain that contradicts itself SHALL be an error in every module: it requires and excludes the same trait, or it requires two variants of one state. A domain that is only unreached in this program SHALL be an error in non-stdlib modules. In stdlib modules it SHALL NOT be an error; the rule is recorded as unreachable instead.

#### Scenario: Traits that never meet
- **WHEN** a user rule declares `each: Player, Bullet` and no reachable archetype carries both
- **THEN** compilation fails with an error at the rule naming `Player` and `Bullet`

#### Scenario: Self-contradictory domain is rejected anywhere
- **WHEN** a rule, in any module, declares `each: Door.Open, Door.Closed`
- **THEN** compilation fails because one entity can hold only one variant of `Door`

#### Scenario: Unused stdlib rule is not an error
- **WHEN** a program imports `std.physics` and no reachable archetype carries `RigidBody`
- **THEN** compilation succeeds and the stdlib rule over `RigidBody` is recorded as unreachable

#### Scenario: A reached domain compiles
- **WHEN** a rule declares `each: Health` and a reachable archetype carries `Health`
- **THEN** no reachability error is reported for that rule

### Requirement: A `without:` trait that can never be present is a compile error
The compiler SHALL report an error, in non-stdlib modules, when a `without:` trait appears on no reachable archetype that also matches the rule's `each:` traits, since the filter can never exclude anything. The diagnostic SHALL name the rule and the trait.

#### Scenario: Dead exclusion
- **WHEN** a user rule declares `each: Enemy` and `without: Frozen`, and no reachable archetype carries both `Enemy` and `Frozen`
- **THEN** compilation fails with an error naming `Frozen` as a filter that excludes nothing

#### Scenario: Live exclusion
- **WHEN** some reachable archetype carries both `Enemy` and `Frozen`
- **THEN** the `without: Frozen` filter compiles

### Requirement: Named access to a trait the entity can never carry is a compile error
The compiler SHALL report an error, in non-stdlib modules, when a named field access `Name[Trait].field`, in a `when:` clause or a handler body, names a trait that no reachable archetype of entity `Name` carries. The diagnostic SHALL name the entity and the trait.

#### Scenario: Named entity never gets the trait
- **WHEN** a rule declares `when: Game[Match].over` and entity `Game` never carries `Match`
- **THEN** compilation fails with an error naming `Game` and `Match`

### Requirement: Observable spawn order must be defined
When two handlers can run in the same activation, have no schedule or event-flow path between them, neither runs in a later commit round than the other (a lifecycle handler runs after every phase or host-event handler of its activation), and both spawn entities, the compiler SHALL check whether their creation order is observable. It is observable when some reachable archetype descended from each spawner's templates matches the domain of one rule, and that rule's result changes when its rows are permuted, as the correctness definition states. When the order is observable, compilation SHALL fail. The diagnostic SHALL name both spawners, the rule that observes their order, and suggest an `after:` in either direction. When the order is not observable, the program SHALL compile and the backend MAY create the entities in any order.

#### Scenario: Two spawners feed a limit
- **WHEN** two unordered handlers of one activation spawn `Coin` and `Gem`, both carrying `Pickup`, and a rule `each: Pickup` uses `limit: 1`
- **THEN** compilation fails naming both spawners and the `limit:` rule

#### Scenario: Order fixed by after
- **WHEN** the same program adds `after:` from one spawner to the other
- **THEN** the program compiles

#### Scenario: Order not observable
- **WHEN** two unordered handlers spawn `Coin` and `Gem`, and every rule that matches both only uses commuting writes on each row
- **THEN** the program compiles

### Requirement: The reachable set is part of the compiled program
The linked program SHALL record the reachable archetypes and the set of unreachable stdlib rules, so a backend can read them without recomputing. A backend MAY use them for storage layout, view specialization and dropping unreachable rules; doing so SHALL NOT change any observable behavior.

#### Scenario: Backend reads the reachable set
- **WHEN** a program is linked
- **THEN** the linked program lists every reachable archetype by canonical trait identities

#### Scenario: Unreachable stdlib rule is dropped
- **WHEN** a stdlib rule is recorded as unreachable
- **THEN** the backend may omit it, and the program behaves the same as if it were emitted

