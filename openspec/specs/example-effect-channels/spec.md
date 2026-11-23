# example-effect-channels Specification

## Purpose

Define the `examples/effect-channels` example: common visual effects built from stdlib effect templates, next to one of the same effects built by hand in plain Cactus, so the mechanism behind the stdlib module stays visible and testable.

## Requirements

### Requirement: Effect channels example with stdlib effects
`examples/effect-channels/main.cactus` SHALL show a jolt, a hit flash, a pop-in, a fade-out and a permanent highlight on drawn entities, each made from `std.render.effects` channels as a template with no rule of its own. A key press SHALL start each timed effect. One rule SHALL start all timed effects, so the example needs no `after:` between effect starts.

#### Scenario: Jolt and flash together
- **WHEN** the jolt and the flash are started on one entity in the same frame
- **THEN** the entity is drawn shifted and brightened, and its `WorldTransform` and color are unchanged

#### Scenario: Effects end by themselves
- **WHEN** a timed effect's span ends
- **THEN** its entity is gone and the target is drawn as before

### Requirement: The same effect built by hand
`examples/effect-channels/by_hand.cactus` SHALL build the jolt without `std.render.effects`: its own effect trait with a clock, its own relation to the target, and one `pairs:` rule with `reduce per` that projects `DrawnPose`. It SHALL use no extern code.

#### Scenario: Same picture
- **WHEN** the hand-made jolt and the stdlib jolt start on two entities in the same frame with the same push and length
- **THEN** at every step both entities have the same `DrawnPose` offset from their `WorldTransform`
