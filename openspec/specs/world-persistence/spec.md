## Purpose

Define format-independent capture of live archetype instances, persistent values, construction data, and entity relationships into a typed snapshot document.

## Requirements

### Requirement: Explicit fields determine entity eligibility
A live entity SHALL be captured if a trait in its originating archetype's declared trait set declares at least one persist field, or if a currently attached trait declares one. Construction overrides, marker traits, `sync` fields, and `std.core.Persistent` SHALL NOT grant eligibility. Archetype eligibility SHALL survive removal of its persistence-bearing traits. No record SHALL be emitted for an ineligible entity.

#### Scenario: Customized particles remain ephemeral
- **WHEN** a particle has spawn position, color, and lifetime overrides but neither its archetype's traits nor its attached traits has a persist field
- **THEN** capture emits no record for it

#### Scenario: Removed persistent trait preserves archetype eligibility
- **WHEN** a boss archetype includes a trait with persist health and gameplay removes that Health trait
- **THEN** capture includes the boss and records Health as absent

#### Scenario: Dynamic persistent trait changes eligibility
- **WHEN** an originally ephemeral entity gains a trait with a persist field
- **THEN** it becomes eligible while that trait is attached
- **AND** removing the trait makes it ephemeral again if no other eligibility condition holds

#### Scenario: Scene survivor is not automatically eligible
- **WHEN** an entity carries `std.core.Persistent` but no persistence-bearing trait
- **THEN** capture emits no record for it

### Requirement: Records carry archetype identity and evaluated construction data
Each captured entity record SHALL identify its resolved originating archetype node and carry the evaluated original construction parameters and overrides needed to reconstruct its unmarked state, alongside its current persist values. Construction values SHALL be the values evaluated once at creation, not re-evaluated expressions and not later unmarked mutations. Authored entity declaration overrides SHALL belong to that entity's archetype baseline rather than its construction data.

#### Scenario: Boss records its archetype rather than trait defaults
- **WHEN** a Health trait defaults maximum to 100, a Boss archetype sets it to 500, and current health is persistent
- **THEN** the record identifies the Boss archetype node, whose baseline supplies maximum 500, and carries the saved current health

#### Scenario: Override records initial rather than latest unmarked value
- **WHEN** an eligible enemy is spawned with unmarked maximum 700 and later maximum becomes 900
- **THEN** the record's construction data carries 700
- **AND** an explicitly persistent field records its latest value instead

#### Scenario: Parameter expression is evaluated once
- **WHEN** a constructor argument is evaluated from runtime state at spawn
- **THEN** the record carries the evaluated argument and capture does not evaluate that expression again

### Requirement: Provenance tracking is bounded by whole-program analysis
Construction provenance SHALL be retained for every entity that the program can make eligible, including currently ephemeral entities whose archetype can gain a persistence-bearing trait. When no path in the linked program attaches a persistence-bearing trait to an entity, entities of never-eligible archetypes SHALL carry no construction metadata. Native and authored creation and structural paths SHALL maintain the same provenance. Capture SHALL fail with a diagnostic rather than emit an eligible record with missing provenance.

#### Scenario: Particle-only program pays no provenance cost
- **WHEN** a program never adds a persistence-bearing trait at runtime
- **THEN** its ephemeral particle archetypes carry no construction metadata

#### Scenario: Later-eligible entity keeps its provenance
- **WHEN** a program can add a persistence-bearing trait and an ephemeral entity later gains it
- **THEN** its original construction data is still available to capture

#### Scenario: Native spawn participates in provenance
- **WHEN** a contracted extern handler creates an eligible entity through its generated spawn capability
- **THEN** capture retains its archetype and evaluated construction overrides just as for an authored spawn

### Requirement: Runtime structure and trait incarnations are recorded
Records SHALL carry the entity's final attached traits, including marker traits, and SHALL record baseline traits that were removed as absent. Each currently attached trait incarnation SHALL record its own initialization baseline and persist values. Removed traits SHALL contribute no field payloads. Frame-local projected traits SHALL NOT be captured as durable structure.

#### Scenario: Added marker and removed component are recorded
- **WHEN** an eligible enemy gains Poisoned and loses Shield
- **THEN** the record includes Poisoned and marks Shield absent

#### Scenario: Removed and re-added baseline trait records new initialization
- **WHEN** a baseline trait is removed and later added again
- **THEN** the record carries the current incarnation's initialization values and persist state rather than the removed incarnation's

### Requirement: Snapshots use stable canonical identities
Snapshots SHALL identify archetypes, nested child roles, traits, fields, enums, and declared assets and inputs by stable canonical module-qualified identities, independent of generated indices, allocation order, and source line numbers. Entities SHALL be referenced by document-local identities, including references nested in structs, lists, and construction values. Cycles and forward references among included records SHALL be representable. References to ineligible, excluded, or previously destroyed entities SHALL be recorded as a distinguished absent value that preserves equality of repeated references to the same absent target, without capturing the target.

#### Scenario: Mutual targets are recorded
- **WHEN** two included enemies persist references to one another
- **THEN** each record references the other's document identity

#### Scenario: Ephemeral target stays absent
- **WHEN** an included enemy persists a reference to an excluded particle
- **THEN** the reference is recorded as absent
- **AND** the particle is not implicitly captured

#### Scenario: Resource identity is independent of allocation order
- **WHEN** declared assets receive runtime handles in a given order
- **THEN** records reference canonical asset declarations rather than numeric runtime handles

### Requirement: Hierarchy and creation order are recorded
Records SHALL carry parent links between included entities and SHALL preserve relative creation order among included entities. A record whose parent is not itself included SHALL be recorded as a root, without adding a record for the parent. Destroyed and excluded archetype children SHALL NOT be recorded. Named authored entity bindings SHALL be recorded by canonical identity.

#### Scenario: Deleted child is not recorded
- **WHEN** a hierarchical archetype child was destroyed before capture
- **THEN** capturing the parent emits no record for that child

#### Scenario: Saved child has an ephemeral parent
- **WHEN** a saved child's parent has no persistence eligibility
- **THEN** the child is recorded as a root and no parent record is added

#### Scenario: Reparenting is recorded
- **WHEN** an included entity was reparented at runtime to another included entity
- **THEN** the record carries the current parent, not the archetype's parent

### Requirement: The typed schema exposes exact backend representation
Capture SHALL produce a canonical schema descriptor and a typed snapshot without imposing a byte format. The descriptor SHALL cover reconstructible archetypes, baseline values, field identities and exact value kinds including integer and floating widths and vector lanes, persistence masks, enum identities, and nested structs and lists. It SHALL exclude function bodies unrelated to construction. Ordering SHALL be canonicalized before fingerprinting so that declaration reordering and import aliasing produce an identical descriptor. Scalars, nested structs and lists, enums, strings, entity references, and declared asset and input references SHALL be representable without silent narrowing or raw native handle encoding. Raw resource pointers, registry handles, and padding SHALL NEVER be portable payloads. Unsupported reconstruction or persistent field types SHALL produce actionable field-path diagnostics rather than silently omitting values. Configured collection and depth limits SHALL be enforced during capture.

#### Scenario: Nested values are representable
- **WHEN** a persistent field contains a list of structs with enums, strings, and entity references
- **THEN** the snapshot retains their types and records references by document identity

#### Scenario: Declaration reordering does not change the descriptor
- **WHEN** trait declarations are reordered or a module is imported under a different alias
- **THEN** the canonical descriptor and its fingerprint are unchanged

#### Scenario: Unsupported field type is diagnosed
- **WHEN** a persist field has a type the schema cannot represent
- **THEN** compilation reports the field path and the unsupported type instead of omitting the value

### Requirement: Capture observes a committed, consistent world
Capture SHALL run outside any activation, after the originating activation's complete handler and event cascade and its structural and lifecycle commit drain. It SHALL produce an owned snapshot that no later gameplay mutation can alter, and SHALL NOT run ordinary gameplay handlers, mutate the live world, or create entities.

#### Scenario: Save observes committed spawn
- **WHEN** an activation queues an eligible spawn and a save request
- **THEN** the captured snapshot includes the committed entity even though the request was emitted before the spawn command applied

#### Scenario: Capture does not disturb the world
- **WHEN** a snapshot is captured
- **THEN** the live world's entities, traits, and field values are unchanged afterwards
