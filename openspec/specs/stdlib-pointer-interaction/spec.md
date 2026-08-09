# stdlib-pointer-interaction Specification

## Purpose

Define a generic mouse-to-entity interaction model that routes the same targeted pointer lifecycle to window UI, flat-world entities, volume-world entities, editor handles, and other collider-backed gameplay objects.

## Requirements

### Requirement: `std.pointer` exposes reusable target and state traits
The `std.pointer` module SHALL expose `PointerTarget` and `PointerState`. `PointerTarget` SHALL contain `enabled`, `blocks_lower`, and integer `priority` fields. `PointerState` SHALL contain `hovered` and `pressed` fields updated by the standard pointer router.

Collider or hit-region membership SHALL define where an entity can be intersected; `PointerTarget` SHALL independently define whether it accepts pointer interaction. Decorative colliders without PointerTarget SHALL not become pointer recipients.

#### Scenario: Decorative UI does not intercept input
- **WHEN** a Text node has a computed hit region but no PointerTarget
- **THEN** the pointer router does not target that Text entity

#### Scenario: Regular game unit is a pointer target
- **WHEN** a flat- or volume-world entity carries a supported collider and PointerTarget
- **THEN** it can participate in the same pointer event lifecycle as a UI button

### Requirement: `std.pointer` exposes targeted pointer lifecycle events
The module SHALL expose targeted `PointerEnter`, `PointerLeave`, `PointerPress`, `PointerRelease`, and `Click` events carrying the current screen-space pointer position. These events SHALL use ordinary targeted-event delivery, so a consumer runs only when the recipient satisfies that consumer's domain.

#### Scenario: UI and game actions consume the same Click event
- **WHEN** one Button entity and one selectable world unit each have PointerTarget and separate marker traits
- **THEN** each can use an ordinary unary `on pointer.Click` handler filtered by its own marker without a UI-specific click mechanism

#### Scenario: Targeted click does not broadcast
- **WHEN** Click is emitted to one live PointerTarget among several matching entities
- **THEN** only that recipient's matching handlers execute

### Requirement: pointer picking merges coordinate-space-specific hit providers
The standard pointer router SHALL form candidates from supported window-space hit regions, flat-world colliders under the active 2D camera, and volume-world colliders intersected by the active 3D camera ray. Coordinate conversion and intersection geometry MAY be backend-provided, but all selected entities SHALL enter one common target, capture, and event-routing lifecycle.

Window-space candidates SHALL be considered in reverse painter order before world candidates because they render over world content. Volume-world candidates SHALL use nearest positive ray distance. Flat-world candidates SHALL use PointerTarget priority followed by stable creation ordinal unless a more specific standard painter depth is available. PointerTarget priority SHALL order candidates within the same coordinate-space class before its geometric tie-breaker.

#### Scenario: Window overlay wins over world entity
- **WHEN** a visible window-space PointerTarget and a world PointerTarget both lie under the cursor
- **THEN** the window-space target is selected first

#### Scenario: Nearest volume entity wins
- **WHEN** the pointer ray intersects two enabled volume-world PointerTargets at different positive distances
- **THEN** the nearer entity is selected

#### Scenario: Sibling UI hit follows painter order
- **WHEN** two window-space targets overlap
- **THEN** the candidate with the greater computed draw order is considered first

### Requirement: top-hit routing respects blocking and effective state
The pointer router SHALL inspect candidates front-to-back and select the first enabled target that accepts interaction. A candidate with `blocks_lower = true` SHALL prevent lower candidates from receiving the pointer action even when it is disabled for activation. A candidate with `blocks_lower = false` SHALL permit evaluation of lower candidates when it does not accept the action. Invisible, stale, or clipped-out window candidates SHALL not participate.

#### Scenario: Disabled control prevents click-through
- **WHEN** a disabled blocking button overlaps an enabled button behind it
- **THEN** neither button receives Click

#### Scenario: Nonblocking overlay permits lower target
- **WHEN** a nonblocking target declines interaction and a lower enabled target contains the pointer
- **THEN** the lower target can be selected

### Requirement: pointer hover transitions are targeted and deterministic
The router SHALL maintain at most one hovered target. When the top accepted hover target changes, it SHALL clear the former target's PointerState.hovered and emit PointerLeave to it before setting the new target's hovered state and emitting PointerEnter to it.

#### Scenario: Pointer moves between targets
- **WHEN** the pointer moves from one live target directly onto another
- **THEN** the former target receives PointerLeave and the new target receives PointerEnter in deterministic order

#### Scenario: Hovered target is destroyed
- **WHEN** the hovered entity becomes stale before the next pointer routing activation
- **THEN** routing safely clears its stored hover handle and does not deliver an event to the stale entity

### Requirement: pointer press uses entity capture
On primary mouse press, the router SHALL capture the top accepted target, set its PointerState.pressed, emit PointerPress to it, and retain that capture until primary release or target invalidation. While captured, another entity SHALL NOT receive the corresponding press/release sequence.

On release, the router SHALL clear pressed state and emit PointerRelease to the live captured target. It SHALL emit Click to that same target only when it remains enabled and the pointer still intersects it at release. A stale, disabled, hidden, or no-longer-intersected capture SHALL not receive Click.

#### Scenario: Press and release on same entity clicks
- **WHEN** the primary pointer is pressed on an enabled target and released while still over that live target
- **THEN** the target receives PointerPress, PointerRelease, and Click and its pressed state returns to false

#### Scenario: Release outside cancels click
- **WHEN** a target is captured on press and the pointer is released outside its hit region
- **THEN** it receives PointerRelease but not Click

#### Scenario: Captured entity is destroyed
- **WHEN** a captured target becomes stale before release
- **THEN** the capture is cleared safely and no release or click event is delivered to that stale entity

### Requirement: accepted pointer actions consume their logical input
When the standard pointer router accepts a primary press, owns an active primary capture, or handles the corresponding release, it SHALL consume the configured logical mouse action so later gameplay input queries in that frame do not independently act on the same physical action. Pointer handlers and lower gameplay input handlers SHALL be graph-ordered so this consumption is observable deterministically.

#### Scenario: UI press does not also select the world
- **WHEN** a blocking UI target accepts the primary press above a selectable world entity
- **THEN** the pointer event is routed to the UI entity and later world-selection input logic observes the logical action as consumed

#### Scenario: Miss leaves gameplay input available
- **WHEN** no pointer target accepts or blocks the primary action
- **THEN** the pointer router does not consume that logical input action

### Requirement: Button presentation derives from generic pointer state
Standard Button appearance SHALL use effective enabled state and generic PointerState rather than a separate UI-only routing state. Disabled, pressed, hovered, and normal colors SHALL be selected in that precedence order.

#### Scenario: Generic hover changes Button appearance
- **WHEN** a Button entity's PointerState.hovered becomes true while it is effectively enabled and not pressed
- **THEN** Standard UI renders its hover color without requiring a Button-specific hover event system
