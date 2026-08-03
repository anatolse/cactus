# example-model-animation Specification

## Purpose

Define the behavior of the model-animation example app: dynamically spawned, height-normalized animated characters with gradual selection scaling and a per-entity clip-cycling HUD.

## Requirements

### Requirement: Characters are spawned dynamically at load
The model-animation example SHALL declare no per-character `entity` blocks. Instead it SHALL declare a `CharacterTemplate` and a bootstrap entity carrying a single-instance marker trait; a rule's `on load()` handler filtered on that marker SHALL spawn three characters — a robot at x = −2.5, a knight at x = 0, and a robot at x = +2.5 — assigning each a sequential `index` starting at 0.

#### Scenario: Three characters exist after startup
- **WHEN** the example starts
- **THEN** exactly three model-rendered characters exist — robot, knight, robot from left to right — none of which were declared as top-level entities

#### Scenario: Spawner runs exactly once
- **WHEN** the startup load phase fires
- **THEN** the spawning handler body executes once (one bootstrap marker entity), producing exactly three characters

### Requirement: Characters are normalized to a shared height
The example SHALL compute each character's base scale as a shared target height divided by the model's `bounds_size(...).y`, guarding against a zero height (fallback base scale 1.0). Both robot and knight SHALL render at the same visual height when unselected.

#### Scenario: Mixed models render the same height
- **WHEN** the robot and knight GLBs have different native heights
- **THEN** each spawned character's transform scale compensates so both render at the shared target height

#### Scenario: Zero-height bounds falls back safely
- **WHEN** `bounds_size` returns zero extents for a model
- **THEN** the character spawns with base scale 1.0 instead of dividing by zero

### Requirement: Selection scales characters gradually
The example SHALL cycle the selected character with TAB. The selected character's scale multiplier SHALL ease toward an emphasis factor greater than 1.0, and deselected characters' multipliers SHALL ease back toward 1.0, over multiple frames (rate governed by a speed constant and `tick.dt`) rather than snapping.

#### Scenario: Newly selected character grows over time
- **WHEN** TAB moves selection to a character
- **THEN** that character's rendered scale increases over successive frames until it reaches the emphasis factor times its base scale

#### Scenario: Deselected character shrinks back
- **WHEN** selection moves away from a character
- **THEN** its rendered scale decreases over successive frames back to its base scale

### Requirement: Clip cycling and HUD use each entity's own model
Clip cycling (SPACE) and the HUD label SHALL query animation metadata through the selected entity's own `ModelRenderer.model` field — `models.animation_count(model)` and `models.animation_name(model, clip)` — never a hardcoded asset constant.

#### Scenario: Knight cycles within knight clips
- **WHEN** the knight is selected and SPACE is pressed repeatedly
- **THEN** the knight's `clip` wraps modulo the knight model's own clip count

#### Scenario: HUD shows the selected character's clip name
- **WHEN** any character is selected
- **THEN** the HUD label shows that character's number and the clip name read from that character's model
