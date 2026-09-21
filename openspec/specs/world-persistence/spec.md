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

### Requirement: Restore reconstructs recorded nodes with defined precedence
Restore SHALL create only the nodes present in the document. For each record it SHALL apply archetype defaults, then evaluated original construction parameters and overrides, then the current trait incarnation's initialization values, then persist values. Spawn expressions and template arguments SHALL NOT be re-evaluated. Unmarked mutations made after construction SHALL NOT reappear. Restore SHALL NOT regenerate a template's other children.

#### Scenario: Boss uses its archetype rather than trait defaults
- **WHEN** a Health trait defaults maximum to 100, a Boss archetype sets it to 500, and current health was persistent
- **THEN** restore sets maximum to 500 and current health to its saved value

#### Scenario: Override restores initial rather than latest unmarked value
- **WHEN** an eligible enemy was spawned with unmarked maximum 700 and maximum later became 900
- **THEN** restore sets maximum to 700
- **AND** an explicitly persistent field restores its latest saved value

#### Scenario: Parameter expression is not replayed
- **WHEN** a record carries a constructor argument evaluated from runtime state before the save
- **THEN** restore uses the recorded value and does not evaluate that expression

#### Scenario: Removed and re-added baseline trait restores its current incarnation
- **WHEN** a record carries a baseline trait that was removed and later added again
- **THEN** restore applies the recorded incarnation's initialization and persist state, not the removed incarnation's

### Requirement: References resolve to fresh handles
Restore SHALL allocate runtime handles for all records before applying any reference, so forward references and cycles among included records resolve. Document identities SHALL map to those handles, including references nested in structs, lists, and construction values. Recorded absent references SHALL restore as stale values on which total entity operations remain safe, preserving equality of repeated references to the same absent target. A reference naming an identity that the document does not include SHALL be rejected as malformed. Named authored entity bindings SHALL resolve to restored instances, or resolve stale when absent.

#### Scenario: Mutual targets restore
- **WHEN** two included enemies reference one another
- **THEN** each restored target resolves to the other restored entity

#### Scenario: Absent target stays stale
- **WHEN** an included enemy references an excluded particle
- **THEN** the restored reference is stale, total entity operations remain safe, and no particle is created

#### Scenario: Dangling document reference is malformed
- **WHEN** a reference names a document identity with no matching record
- **THEN** validation rejects the document before the world is replaced

### Requirement: Hierarchy and creation order are restored
Restore SHALL reproduce included parent relationships, including runtime reparenting, and SHALL preserve the recorded relative creation order among restored entities. A record whose parent is absent SHALL become a root; no record SHALL be added for the missing parent, and the record SHALL NOT be discarded. Destroyed and excluded archetype children SHALL NOT be recreated.

#### Scenario: Deleted child does not return
- **WHEN** a hierarchical archetype child was destroyed before capture
- **THEN** restoring the parent does not recreate that child

#### Scenario: Saved child has an ephemeral parent
- **WHEN** a saved child's parent had no persistence eligibility
- **THEN** the child restores as a root

#### Scenario: Ordering survives container changes
- **WHEN** the restored backend assigns different native handles or storage positions
- **THEN** creation-order-based selection among restored entities retains its previous relative order

### Requirement: Documents are fully validated before the world changes
Restore SHALL validate a decoded document before mutating the active world: canonical schema descriptor compatibility, known archetypes, traits, and fields, value kinds and ranges, construction completeness, unique document identities, legal trait combinations, absence of hierarchy cycles, resolvable references, and configured size and depth limits. Diagnostics SHALL accumulate and identify field paths rather than stopping at the first problem. Changed archetype defaults, renames, and field type changes SHALL fail compatibility unless an adapter explicitly migrated the document to the current descriptor.

#### Scenario: Incompatible baseline is rejected
- **WHEN** a document was captured against a different archetype default descriptor
- **THEN** restore reports incompatible schema before changing the active world

#### Scenario: Malformed document is diagnosed by field path
- **WHEN** a document carries a value of the wrong kind and a duplicate identity
- **THEN** restore reports both with their field paths and leaves the world unchanged

### Requirement: Restore publishes an atomic replacement without gameplay creation effects
Successful restore SHALL replace the complete active ECS world, including previously live ephemeral entities and entities carrying `std.core.Persistent`, with the reconstructed snapshot. An empty compatible document SHALL produce an empty world. Reconstruction SHALL be staged: validation, allocation, and runtime resource preparation failures SHALL leave the previous world and its usable resources intact and SHALL release staged resources. No staged entity or resource SHALL be observable before publication. Ordinary spawn, destroy, load, and unload gameplay handlers SHALL NOT run as a consequence of replacement, while backend resource preparation and cleanup SHALL still occur. Completion SHALL be observable only after references, named bindings, and runtime resources are usable.

#### Scenario: Failed staging preserves the world
- **WHEN** resource preparation fails while constructing a restored world
- **THEN** the old world remains active, temporary resources are released, and restore reports failure with no gameplay lifecycle effects

#### Scenario: Restore does not repeat spawn rewards
- **WHEN** a saved entity's archetype has a spawn handler that creates loot
- **THEN** restoration does not execute that handler or duplicate loot

#### Scenario: Empty save replaces populated world
- **WHEN** an empty compatible document is restored over a populated world
- **THEN** no old ECS entity survives, including entities carrying `std.core.Persistent`

### Requirement: Old-world entity handles are unobservable after publication
Publication SHALL reset runtime state that holds entity handles from the replaced world, including pointer hover and capture state, pending-destruction sets, editor selection and camera state, and spatial or scheduler caches. No such handle SHALL be readable as a live entity of the restored world. This requirement SHALL be satisfied by resetting the holders; it SHALL NOT require restored entities to avoid reusing the numeric identifiers or versions of the replaced world.

#### Scenario: Stale hover target does not select a restored entity
- **WHEN** the pointer was hovering an entity in the replaced world
- **THEN** after publication no hovered entity is reported, rather than a restored entity that happens to reuse the identifier

#### Scenario: Ephemeral content is rebuilt by gameplay, not resurrected
- **WHEN** a restored world contains no camera because no camera archetype was eligible
- **THEN** gameplay can rebuild it in response to the restore outcome event, and nothing from the replaced world persists implicitly
