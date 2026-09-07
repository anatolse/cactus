## Purpose

Define fixed-step lifetime and restartable one-shot timer behavior. This capability gives gameplay authors predictable behavior without backend plumbing.

## Requirements

### Requirement: Fixed-step lifetime

std.time SHALL provide Lifetime with remaining: float and paused: bool = false. Each eligible fixed step SHALL subtract fixed_tick.dt, clamp at zero, and queue destruction once when the lifetime expires. A new Lifetime SHALL first advance on the next eligible fixed-step invocation after its structural commit. Non-finite or negative remaining values SHALL be treated as zero. Paused lifetimes SHALL not advance or expire.

#### Scenario: Expiry
- **WHEN** an unpaused lifetime has 0.01 seconds remaining before a 0.02 second fixed step
- **THEN** it queues destruction and is removed at the ordinary activation commit

#### Scenario: Pause and catch-up
- **WHEN** a frame performs three fixed steps while the lifetime is paused
- **THEN** remaining is unchanged

#### Scenario: No simulation step
- **WHEN** a frame performs no fixed steps or drops excess catch-up time
- **THEN** the lifetime consumes none of that unsimulated time

---

### Requirement: Restartable one-shot timers

std.time SHALL provide Timer with remaining: float = 0.0, armed: bool = false and paused: bool = false; RestartTimer with duration: float; CancelTimer; and zero-payload TimerExpired. RestartTimer SHALL replace remaining and arm an existing Timer, preserving paused. CancelTimer SHALL disarm it. Requests SHALL apply in event delivery order. A non-finite or negative duration SHALL normalize to zero. Timers SHALL advance only during fixed steps and SHALL disarm before emitting one targeted TimerExpired.

#### Scenario: Restart wins
- **WHEN** an existing timer receives two restarts before its next step
- **THEN** the last duration replaces the first rather than adding to it

#### Scenario: Cancel wins
- **WHEN** RestartTimer is followed by CancelTimer before a step
- **THEN** no expiration is emitted

#### Scenario: Zero duration
- **WHEN** an unpaused zero-duration timer is armed
- **THEN** it expires once at its next eligible fixed-step invocation, not recursively at the restart site

#### Scenario: Expired receiver restarts
- **WHEN** TimerExpired restarts the same timer
- **THEN** it cannot expire again until a later fixed-step invocation

---

### Requirement: Timer ownership and total behavior

RestartTimer, CancelTimer and TimerExpired SHALL use normal targeted-event delivery and structural timing. Stale or nonmatching targets SHALL be no-matches. Destroying a timer owner SHALL cancel its pending countdown without an expiration. Multiple independent countdowns SHALL be represented by distinct timer entities. An entity's local paused fields SHALL be the only pause policy introduced by this capability.

#### Scenario: Destroyed owner
- **WHEN** an armed owner is destroyed before expiration
- **THEN** no TimerExpired is delivered to another entity reusing its storage

#### Scenario: Independent owners
- **WHEN** two timer entities share one intended gameplay recipient
- **THEN** each expires independently and can relay its own authored event

#### Scenario: Pause resume
- **WHEN** a timer is paused, restarted and later unpaused
- **THEN** the replacement duration starts advancing after unpause without counting paused time
