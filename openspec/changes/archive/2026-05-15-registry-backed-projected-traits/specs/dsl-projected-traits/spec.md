## MODIFIED Requirements

### Requirement: Project statement creates frame-local projected trait facts
The DSL SHALL support a `project` statement that creates or patches a projected trait value for a target entity as frame-local trait state.

```cactus
project TraitName:
    field = value

project TraitName to target_entity:
    field = value
```

If no target expression is supplied, the target SHALL be the current `self` entity. Projected trait values SHALL remain visible through the current rendered frame and SHALL be cleared at the frame boundary after render processing completes. The language semantics SHALL NOT require projected traits to be stored separately from durable backend component storage; a backend MAY materialize projected traits in its normal component registry if it restores/removes them at the frame boundary.

#### Scenario: Project marker trait to self
- **WHEN** a handler executes `project Grounded`
- **THEN** the current entity has a projected `Grounded` trait visible to later systems in the current frame

#### Scenario: Project trait payload to another entity
- **WHEN** a handler executes `project InExplosion to hit.entity: damage = 10`
- **THEN** `hit.entity` has a projected `InExplosion` value with `damage = 10` for the current frame

#### Scenario: Projected traits clear at frame boundary
- **WHEN** a trait is projected during a frame
- **THEN** it is no longer visible after that frame's render/frame boundary cleanup completes unless projected again

### Requirement: Projected trait values are coalesced, not accumulated
Projected trait state SHALL contain at most one projected value per `(entity, trait)` at a time. Repeated `project` statements for the same `(entity, trait)` in the same frame SHALL patch or replace the existing projected value according to the same field-initialization semantics selected for `add`-like trait initialization.

Projected traits SHALL NOT create multiple per-entity facts. Multiple occurrences SHALL be modeled with events or explicit bounded foreach logic.

#### Scenario: Repeated projection coalesces by entity and trait
- **WHEN** a handler projects `DamageFlash` to the same entity twice in one frame
- **THEN** the entity has one projected `DamageFlash` value rather than two accumulated records

#### Scenario: Events remain the multiple occurrence model
- **WHEN** two independent damage occurrences must both be processed
- **THEN** authored code uses `emit Damage` for each occurrence rather than relying on accumulated projected traits

### Requirement: Projected traits participate in system filtering
System `filter:` and `exclude:` matching SHALL consider both durable trait state and projected trait state visible at the time the system handler is scheduled.

`after:` ordering constraints SHALL be the author-facing mechanism for ensuring a consumer system runs after a producer system that projects traits.

#### Scenario: Later system filters projected trait
- **WHEN** `DetectGround` projects `GroundContact` and `ApplyGroundState` has `filter: GroundContact as ground` with `after: DetectGround`
- **THEN** `ApplyGroundState` matches the entity during the same frame and may access `ground` fields

#### Scenario: Exclude sees projected traits
- **WHEN** an entity has a projected `Suppressed` trait and a later system has `exclude: Suppressed`
- **THEN** that entity is skipped by the later system for the current frame
