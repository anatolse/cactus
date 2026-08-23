## Purpose

Define how the language specs themselves are curated and organized, including scoping the normative surface to active gameplay features, separating deferred features, and keeping core language guidance distinct from stdlib and backend detail.

## Requirements

### Requirement: Normative DSL surface is curated to active gameplay features
The project SHALL maintain a single normative gameplay-core language surface for Cactus. A feature SHALL appear in the main language grammar, main language guide, and maintained teaching examples only if it is part of the active supported surface.

The active supported surface SHALL be determined by an accepted capability story and at least one of the following:
- implemented compiler support,
- maintained example usage,
- an accepted stdlib/backend contract that is explicitly treated as current rather than deferred.

#### Scenario: Active feature appears in the main language guide
- **WHEN** a feature is documented as part of the normative DSL grammar
- **THEN** it belongs to the active gameplay-core surface and is not merely a deferred idea

#### Scenario: Deferred feature is not presented as active grammar
- **WHEN** a feature lacks an active support story
- **THEN** it is moved out of normative grammar and examples into deferred or future-work documentation

### Requirement: Deferred features are clearly separated from current language commitments
Features that are deferred, experimental, or unsupported SHALL NOT appear as normative grammar productions in the human-facing DSL spec. They SHALL be documented separately with an explicit reason for deferral and a migration or revisit path when relevant.

#### Scenario: Unsupported UI construct is deferred
- **WHEN** a retained-UI concept has no current implementation or stdlib/backend contract
- **THEN** it is documented as deferred rather than included in the active language surface

#### Scenario: Legacy syntax receives migration guidance
- **WHEN** an older syntax form is removed from the active surface
- **THEN** the documentation includes guidance for rewriting it into the canonical current form

### Requirement: Maintained language examples cover both platformer and shooter gameplay loops
The maintained example set and normative documentation SHALL demonstrate that the gameplay-core profile can express both platformer-style and shooter-style gameplay using the same small set of core constructs.

The demonstrated core constructs SHALL include input handling, spawned entities, events, runtime trait changes or entity cleanup, and ordinary gameplay state updates.

#### Scenario: Platformer loop is covered
- **WHEN** the maintained examples are reviewed
- **THEN** at least one example demonstrates movement, jumping or gravity, collision-style reactions, and game-state updates using the gameplay-core surface

#### Scenario: Shooter loop is covered
- **WHEN** the maintained examples are reviewed
- **THEN** at least one example or focused slice demonstrates firing intent, projectile or spawned-entity gameplay, hit/damage flow, and cleanup using the gameplay-core surface

### Requirement: Core language guidance is separated from stdlib and backend detail
The main language guide SHALL prioritize core authoring constructs first and SHALL treat stdlib and backend-specific material as secondary layers rather than part of the minimal language identity.

#### Scenario: Reader encounters the core language first
- **WHEN** a new reader starts with the main DSL language guide
- **THEN** they encounter the gameplay-core surface before engine-facing stdlib catalogs or deferred concepts

#### Scenario: Backend responsibility is not mistaken for language complexity
- **WHEN** rendering, physics, audio, or UI behavior depends on stdlib/backend support
- **THEN** the documentation distinguishes that concern from the minimal core language surface