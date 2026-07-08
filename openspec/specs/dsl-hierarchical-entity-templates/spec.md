## Purpose
Define hierarchical entity/template syntax: a `children:` block inside archetype bodies that lets templates and entities declare a tree of child archetypes, with deterministic parent-first creation, automatic `Parent` relation assignment, scoped role names, nested overrides, and cycle detection.

## Requirements

### Requirement: Archetype bodies support recursive child declarations

The language SHALL support a contextual `children:` block inside entity/template archetype bodies. Each entry in a `children:` block SHALL declare a child archetype with an entity-like role name, optional `from TemplateName` template reference, ordinary trait/template-use body entries, and an optional nested `children:` block. Nested child declarations SHALL be allowed to arbitrary finite depth.

#### Scenario: Template declares a direct child
- **WHEN** a template contains `children:` with `entity Crown from CrownTemplate:`
- **THEN** the parser records `Crown` as a child archetype of the template root using `CrownTemplate` as its base

#### Scenario: Template declares a grandchild
- **WHEN** a child declaration contains its own `children:` block with `entity BladeGlow from GlowTemplate:`
- **THEN** the parser records `BladeGlow` as a child of that child, not as a sibling of the root

#### Scenario: Existing flat archetype body remains valid
- **WHEN** a template or entity contains only trait entries and body-level `use TemplateName` entries
- **THEN** the parser accepts the body with no child declarations

### Requirement: Hierarchical archetype creation returns the root entity

Creating a hierarchical template or template-backed entity SHALL create one entity for the root and one entity for each declared descendant. The creation expression or generated setup function SHALL return or expose the root entity as the created entity. Descendants SHALL be created as implementation-owned children and SHALL NOT introduce author-visible top-level entity identifiers.

#### Scenario: Spawn of hierarchical template returns root
- **WHEN** a handler executes `let root = spawn PlayerRig:` and `PlayerRig` has descendants
- **THEN** the expression result is the root entity id of the `PlayerRig` tree

#### Scenario: Load-time hierarchical entity creates descendants
- **WHEN** a module declares `entity Tree1 from TreeTemplate:` and `TreeTemplate` declares a `Crown` child
- **THEN** module/scene load creates both the root `Tree1` entity and the `Crown` descendant

### Requirement: Non-root descendants receive generated Parent relations

For every non-root entity created from a hierarchical archetype, the implementation SHALL assign the standard `Parent` relationship so that `Parent.parent` references the entity created for the immediate containing archetype node. Grandchildren SHALL reference their direct parent rather than the root.

#### Scenario: Direct child parent points to root
- **WHEN** `TreeTemplate` declares child `Crown` and `Tree1` is created from `TreeTemplate`
- **THEN** the created `Crown` entity has `Parent.parent` equal to the created `Tree1` root entity

#### Scenario: Grandchild parent points to child
- **WHEN** `PlayerRig` declares `WeaponSocket` and `WeaponSocket` declares child `Sword`
- **THEN** the created `Sword` entity has `Parent.parent` equal to the created `WeaponSocket` entity

#### Scenario: Root has no generated Parent relation
- **WHEN** a hierarchical template root is created
- **THEN** the root entity does not receive a generated `Parent` relation solely because it has children

### Requirement: Child role names are scoped to siblings

Child role names SHALL be unique within a single `children:` block and SHALL be used for diagnostics and nested overrides. The same role name MAY appear under different parent roles. Child role names SHALL NOT introduce global entity declarations or author-visible `entity_id` constants.

#### Scenario: Duplicate sibling role rejected
- **WHEN** one `children:` block declares two children both named `Wheel`
- **THEN** semantic analysis reports a duplicate child role error for that parent archetype

#### Scenario: Same role name under different parents accepted
- **WHEN** `LeftDoor` and `RightDoor` each declare a child named `Handle`
- **THEN** semantic analysis accepts both handles because their role paths differ

#### Scenario: Child role is not an entity_id expression
- **WHEN** a template declares child role `Crown` and handler code references `Crown` as an expression
- **THEN** semantic analysis does not resolve `Crown` as an `entity_id` solely because of the child declaration

### Requirement: Template-backed entities and spawn sites support nested child overrides

Template-backed entity declarations and block-structured spawn sites SHALL support a `children:` override block. Each override entry SHALL name an existing child role at that level and MAY override that child's trait fields and recursively override descendants. Overrides SHALL merge field-by-field with the referenced child archetype exactly as root trait overrides merge with the root archetype.

#### Scenario: Override direct child trait field
- **WHEN** `TreeTemplate` declares child `Crown` with `TreeGrowth.target_scale = 1.0` and `entity Tree1 from TreeTemplate:` overrides `children: Crown: TreeGrowth.target_scale = 1.25`
- **THEN** the created `Crown` child for `Tree1` uses `target_scale = 1.25`

#### Scenario: Override grandchild trait field
- **WHEN** `PlayerRig` declares child `WeaponSocket` with grandchild `Sword`, and a spawn site overrides `children: WeaponSocket: children: Sword: Renderer.material`
- **THEN** only the created `Sword` grandchild receives the overridden material

#### Scenario: Override child role inherited from the child's template
- **WHEN** `SwordTemplate` declares child `GlowFx`, `PlayerRig` declares `entity Sword from SwordTemplate:`, and a spawn site overrides `children: Sword: children: GlowFx:` with a trait field
- **THEN** the created `GlowFx` descendant under the sword receives the overridden value, because a child `from` template splices its flattened subtree into the parent tree

#### Scenario: Unknown child override rejected
- **WHEN** a template-backed entity override references child role `MissingChild` that does not exist at that level
- **THEN** semantic analysis reports an unknown child role error

### Requirement: Hierarchical templates reject recursive child-template cycles

Semantic analysis SHALL include child template references when detecting template dependency cycles. A template SHALL NOT directly or indirectly include itself through child `from TemplateName` references or through composition that reaches such a child reference.

#### Scenario: Direct child-template cycle rejected
- **WHEN** `template A:` declares `children: entity Again from A:`
- **THEN** semantic analysis reports a cyclic hierarchical template dependency

#### Scenario: Indirect child-template cycle rejected
- **WHEN** `template A` declares a child from `B` and `template B` declares a child from `A`
- **THEN** semantic analysis reports the cycle path involving `A` and `B`

### Requirement: Hierarchical creation order is deterministic parent-first preorder

Hierarchical archetype creation SHALL create and initialize the parent before its children, then create descendants in source order recursively. Lifecycle visibility for a created hierarchy SHALL be deterministic and SHALL NOT depend on backend container iteration order.

#### Scenario: Children created in source order
- **WHEN** a template declares children `A`, `B`, and `C` in that order
- **THEN** creation initializes the root, then `A`, then `B`, then `C`, including each child's descendants before moving to the next sibling

#### Scenario: Parent exists before child Parent assignment
- **WHEN** a child entity is initialized
- **THEN** its immediate parent entity has already been created so the generated `Parent.parent` relation can reference a live entity

### Requirement: Hierarchical syntax relies on existing hierarchy runtime behavior

The hierarchical entity/template syntax SHALL create parent-child relationships but SHALL NOT by itself imply transform propagation, rendering, physics, or child lookup behavior. Transform following SHALL remain controlled by existing `LocalTransform`, `WorldTransform`, `Parent`, and transform propagation systems. Destroying a parent SHALL use the existing recursive descendant deletion behavior where supported.

#### Scenario: Parentage without transform traits does not imply transform following
- **WHEN** a child is created with `Parent` but without the transform traits required by the active transform propagation system
- **THEN** the hierarchy syntax alone does not derive or update that child's transform

#### Scenario: Destroy root destroys descendants through existing hierarchy behavior
- **WHEN** a root entity created from a hierarchical template is destroyed on a backend with recursive descendant deletion support
- **THEN** its generated descendants are destroyed by the existing `Parent`-based cascade behavior
