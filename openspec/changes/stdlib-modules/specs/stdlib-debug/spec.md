## ADDED Requirements

### Requirement: std.debug provides value inspection functions
The `std.debug` module SHALL provide `watch_*` functions that display named values on screen each frame in a debug overlay panel. Functions SHALL be named by value type (no overloading). These are effectful utilities — they MAY NOT be called from pure `func` declarations, only from system event handler bodies.

#### Scenario: Watch integer value on screen
- **WHEN** `use std.debug as dbg` is imported and `dbg.watch_int("health", h.health)` is called in a system handler
- **THEN** the backend displays "health: [value]" in the debug overlay panel for that frame

#### Scenario: Watch vec3 value
- **WHEN** `dbg.watch_vec3("position", t.position)` is called
- **THEN** the backend displays "position: (x, y, z)" in the debug overlay

#### Scenario: Calling watch from pure func is an error
- **WHEN** `dbg.watch_float("speed", v)` appears inside a `func` declaration body
- **THEN** the compiler reports an error: "std.debug functions may not be called from pure func declarations"

---

### Requirement: std.debug provides a runtime assertion function
The `assert` function SHALL check a condition and terminate execution with an error message if the condition is false. In `--release` builds, the call SHALL compile to nothing.

#### Scenario: Assert passes when condition is true
- **WHEN** `dbg.assert(h.health > 0, "health must be positive")` is called and `health > 0`
- **THEN** execution continues normally

#### Scenario: Assert fails when condition is false
- **WHEN** `dbg.assert(false, "unreachable")` is called in a debug build
- **THEN** the program terminates with the message "unreachable" and the source location

---

### Requirement: std.debug provides visual overlay drawing functions
The `std.debug` module SHALL provide functions that draw wireframe shapes for the current frame only. These are useful for visualizing colliders, velocities, raycasts, and triggers during development.

#### Scenario: Draw 3D line
- **WHEN** `dbg.draw_line3(start, end, #FF0000)` is called
- **THEN** a red line is drawn from `start` to `end` in the 3D world for that frame

#### Scenario: Draw 2D rectangle
- **WHEN** `dbg.draw_rect2(center, size, #00FF00)` is called
- **THEN** a green wireframe rectangle is drawn at `center` with `size` dimensions for that frame

#### Scenario: Debug shapes are per-frame only
- **WHEN** debug draw functions are called inside `on tick(dt: float)`
- **THEN** the shapes are visible for that frame only; they do not persist

---

### Requirement: All std.debug calls compile to nothing in release builds
When the compiler is invoked with the `--release` flag, all calls to `std.debug` functions SHALL be omitted from the generated C++ output entirely. No runtime overhead is incurred in release builds.

#### Scenario: Release build strips all debug calls
- **WHEN** code using `dbg.watch_float` and `dbg.draw_line3` is compiled with `--release`
- **THEN** the generated C++ contains no calls to any debug output functions

---

### Requirement: String literals are permitted as labels in std.debug calls
As an exception to the const-string rule (§6.1), string literal arguments in the `label` position of `std.debug` functions SHALL be permitted without requiring a `const` block declaration. This allows quick throwaway labels in debug code.

#### Scenario: Inline string literal as debug label
- **WHEN** `dbg.watch_int("lives", h.lives)` is written with an inline string literal
- **THEN** the compiler accepts it without requiring a const block entry for the label string

#### Scenario: const-defined label also accepted
- **WHEN** `dbg.watch_int(LIVES_LABEL, h.lives)` is written using a const-defined string id
- **THEN** the compiler also accepts this form
