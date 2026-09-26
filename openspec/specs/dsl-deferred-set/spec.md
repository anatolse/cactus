# dsl-deferred-set Specification

## Purpose

Lets a rule handler change fields of a trait on any entity, including entities it did not select, through a deferred, deterministic field patch that applies at the activation commit.

## Requirements

### Requirement: `set` statement syntax
The language SHALL support a `set` statement inside rule event handler bodies with the form `set TraitName on target:` followed by an indented block of one or more `field = expression` assignments. `TraitName` MAY be module-qualified (`models.ModelAnimator`). The `on target` part is required. `set` SHALL be recognized contextually, only at statement start when followed by a trait reference and `on`, and SHALL NOT become a reserved keyword elsewhere.

#### Scenario: Set statement with a block parses
- **WHEN** a handler contains `set models.ModelAnimator on visual:` followed by indented `clip = 3` and `time = 0.0`
- **THEN** the parser produces a set statement naming trait `models.ModelAnimator`, target expression `visual`, and two field assignments in source order

#### Scenario: Missing target is rejected
- **WHEN** a handler contains `set Health:` followed by a field block
- **THEN** the compiler reports an error that `set` requires an `on <entity>` target

#### Scenario: Empty field block is rejected
- **WHEN** a `set` statement has no field assignments
- **THEN** the compiler reports an error that `set` needs at least one field assignment

#### Scenario: `set` remains usable as an identifier elsewhere
- **WHEN** a module declares a local or field named `set` outside the statement-start `set <Trait> on` form
- **THEN** the compiler accepts it as an ordinary identifier

### Requirement: `set` statement validation
The compiler SHALL reject a `set` statement when the trait is unknown, the trait declares no fields, a named field is not declared on the trait, a value's type does not match its field, the target expression is not of type `entity_id`, the statement appears outside a rule event handler body, or the program does not use the graph-driven phase scheduler.

#### Scenario: Unknown field is rejected
- **WHEN** `set Health on target:` assigns `armor = 5` and `Health` declares no `armor` field
- **THEN** the compiler reports an error naming the unknown field `armor` on trait `Health`

#### Scenario: Marker trait is rejected
- **WHEN** `set Frozen on target:` names a trait declared without fields
- **THEN** the compiler reports an error that `set` needs a trait with fields

#### Scenario: Non-entity target is rejected
- **WHEN** `set Health on 5:` is written
- **THEN** the compiler reports that the `set` target must be an `entity_id`

#### Scenario: Set inside a func is rejected
- **WHEN** `set` appears inside a `func` body
- **THEN** the compiler reports that `set` is only allowed inside rule event handlers

#### Scenario: Legacy frame path is rejected
- **WHEN** a program without any declared phase uses `set`
- **THEN** the compiler reports that `set` requires the graph-driven phase scheduler

### Requirement: `set` values are evaluated when the statement runs
Every field value expression and the target expression of a `set` statement SHALL be evaluated exactly once, in source order, when the statement executes. Later changes in the same activation to anything those expressions read SHALL NOT change the values the patch applies.

#### Scenario: Later write does not change the patch
- **WHEN** a handler runs `set Result on self:` with `x = transform.position.x` and `y = tv.world_position(self).x` while position x is 1.0, then assigns position x = 99.0 in the same handler
- **THEN** after the commit, both `Result.x` and `Result.y` equal 1.0

### Requirement: `set` applies at the activation commit
A `set` statement SHALL queue a patch in the activation's structural command list. The patch SHALL apply at the activation commit, in command-list order, and SHALL NOT be visible to any handler earlier in the same activation, including the handler that issued it.

#### Scenario: Patch is not visible within the activation
- **WHEN** handler A runs `set Health on target:` with `current = 1` and a later handler in the same activation reads `target`'s `Health.current`
- **THEN** the later handler reads the value from before the patch

#### Scenario: Patch is visible in the next activation
- **WHEN** a `set` is issued during one activation
- **THEN** handlers in the next activation read the patched field values

### Requirement: `set` is a pure patch
When a `set` patch applies, it SHALL overwrite only the named fields of the target's existing durable trait and SHALL leave every other field unchanged. It SHALL do nothing when the target is not live or does not carry the trait durably. It SHALL NOT attach the trait, SHALL NOT trigger spawn/destroy lifecycle notifications, and SHALL NOT change the entity's persistence construction baseline.

#### Scenario: Unnamed fields keep their values
- **WHEN** `set models.ModelAnimator on visual:` assigns only `clip` and `visual`'s animator has `speed = 2.0`
- **THEN** after the commit `speed` is still 2.0 and `clip` holds the new value

#### Scenario: Missing trait is a no-op
- **WHEN** `set Health on target:` applies and `target` does not carry `Health`
- **THEN** `target` still does not carry `Health` and no error occurs

#### Scenario: Stale target is a no-op
- **WHEN** `set Health on target:` applies after `target` was destroyed
- **THEN** nothing happens and no error occurs

#### Scenario: Unmarked field patch is not saved as baseline
- **WHEN** a save-eligible entity's non-`persist` field is patched with `set` and a save is then captured
- **THEN** the save records that field's construction value, the same as it would after a direct assignment

#### Scenario: Persist field patch is saved
- **WHEN** a save-eligible entity's `persist` field is patched with `set` and a save is captured after the commit
- **THEN** the save records the patched value

### Requirement: `set` ordering against other commands
Patches SHALL apply in the deterministic structural command order: handler execution-graph order, then entity iteration order within a handler, then statement order. When several patches name the same field of the same entity, the last applied patch SHALL win for that field. A patch SHALL act on the entity state at its position in the command list.

#### Scenario: Last writer wins
- **WHEN** two handlers in one activation each `set` the same field of the same entity, with the first handler ordered before the second
- **THEN** after the commit the field holds the second handler's value

#### Scenario: Different fields merge
- **WHEN** one `set` assigns `clip` and a later `set` in the same activation assigns `time` on the same animator
- **THEN** after the commit both fields hold their patched values

#### Scenario: Add then set patches the added trait
- **WHEN** a handler runs `add Health to e:` and then `set Health on e:` in the same activation
- **THEN** after the commit `e` carries `Health` with the patched values applied over the added values

#### Scenario: Remove then set is a no-op
- **WHEN** `remove Health from e` is queued before `set Health on e:` in the same activation
- **THEN** after the commit `e` does not carry `Health`

### Requirement: `set` targets the durable value under a projection
When the target's trait is currently overlaid by a frame-local projection on top of an existing durable value, a `set` patch SHALL update the durable value that the frame-end projection cleanup restores, and SHALL NOT change the visible projected value for the rest of that frame. When the trait exists on the target only as a projection, the patch SHALL do nothing.

#### Scenario: Patch survives projection cleanup
- **WHEN** `Tint.color` is durably red, a handler projects `Tint` with green, and a `set Tint on e:` assigns blue in the same frame
- **THEN** readers see green until the end-of-frame cleanup, and blue from the next frame on

#### Scenario: Projection-only trait is not patched
- **WHEN** `Highlight` exists on `e` only as a projection and `set Highlight on e:` applies
- **THEN** after the frame-end cleanup `e` does not carry `Highlight`

### Requirement: `set` is allowed in pair handlers
A pair handler SHALL be allowed to `set` a trait on either binding or on any other `entity_id`. This SHALL NOT count as a durable write through a pair binding, and it SHALL NOT change the membership or values seen by the pass in progress.

#### Scenario: Pair handler patches its binding
- **WHEN** a `pairs:` handler with bindings `a` and `b` runs `set Push on a:` with `amount = v`
- **THEN** the compiler accepts it, and every tuple in the pass reads the pre-pass `Push` values
