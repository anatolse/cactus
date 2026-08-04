# example-behavior-tests

## Purpose

Headless functional testing of generated Cactus examples via compile-time-dispatched `cactus::runtime::raylib` wrappers: scripted input, direct `generated_*_project` invocation without a window, and call-log assertions (approximate value + call order) against recorded draw/input-adjacent calls. This complements `example-cpp-compilation-tests` (which only proves curated examples compile/link/lint) by proving generated rules actually produce the expected rendered output and input-driven behavior.
## Requirements
### Requirement: Compile-time wrapper functions dispatch between real raylib and fake recording
The repository SHALL provide `cactus::runtime::raylib` wrapper functions matching raylib's real C function signatures (same names, in a dedicated namespace) for keyboard/mouse input polling, window/screen queries, and drawing (`Draw*`, `Begin*`/`End*` mode/scissor/texture calls, `ClearBackground`). Each wrapper SHALL dispatch at compile time: when a `CACTUS_RAYLIB_FAKE` compile definition is not set, the wrapper SHALL call directly through to raylib's real implementation with no added indirection or runtime cost; when set, the wrapper SHALL call the fake's recording/scripted-state implementation instead. Real raylib SHALL be linked in both configurations. The repository SHALL NOT reimplement or wrap pure math functions (`raymath.h`'s `Vector*`/`Matrix*`/`Quaternion*` family), argument-only geometry helpers that read no hidden global or GPU state (`CheckCollisionPointRec`, `GetRayCollisionBox`, `GetScreenToWorldRay`, `GetModelBoundingBox`), or asset materialization functions (`Load*`/`Unload*`/`Gen*`, shader setup) — these SHALL continue to be called directly, unwrapped, in both configurations.

#### Scenario: Wrapper is a zero-overhead passthrough in real builds
- **WHEN** a real (non-headless) example executable calls a wrapped function, e.g. `cactus::runtime::raylib::DrawRectangleV(position, size, color)`
- **THEN** the call resolves directly to raylib's real `DrawRectangleV` with behavior identical to calling it directly, and no fake/test code is linked into that executable

#### Scenario: Wrapper routes to the fake when CACTUS_RAYLIB_FAKE is defined
- **WHEN** a headless behavioral test executable, built with `CACTUS_RAYLIB_FAKE` defined, calls a wrapped function
- **THEN** the call is routed to the fake's implementation instead of raylib's real implementation, without requiring any change to the calling code's source

#### Scenario: Pure functions and asset materialization are never wrapped
- **WHEN** generated code or the runtime calls a `raymath.h` function, one of the four argument-only geometry helpers, or an asset materialization function
- **THEN** the call resolves directly to raylib's real implementation in both configurations, is not recorded in the fake's call log, and requires no wrapper

### Requirement: Fake records I/O calls routed to it into an ordered, typed call log
When `CACTUS_RAYLIB_FAKE` is defined, the fake SHALL record every drawing call routed to it through a `cactus::runtime::raylib` wrapper into a single, flat, chronologically ordered call log. Each log entry SHALL be a strongly-typed struct specific to the function that produced it, capturing that function's arguments. `Begin*`/`End*`/`ClearBackground` calls SHALL be recorded as ordinary entries in the same log as `Draw*` calls, in the order they occurred, with no separate context/attribution structure attached to other entries.

#### Scenario: Draw call is recorded with typed arguments
- **WHEN** generated code invokes `cactus::runtime::raylib::DrawRectangleV(position, size, color)` while `CACTUS_RAYLIB_FAKE` is defined
- **THEN** the call log gains an entry whose type exposes `position`, `size`, and `color` as typed fields, not as an untyped/generic argument list

#### Scenario: Begin/End/ClearBackground calls are not filtered from the log
- **WHEN** generated code invokes `cactus::runtime::raylib::BeginMode2D(camera)` followed by one or more `Draw*` calls and then `EndMode2D()`, all through the wrapper namespace
- **THEN** the call log contains an entry for `BeginMode2D`, followed by the entries for the intervening `Draw*` calls, followed by an entry for `EndMode2D`, in that order

#### Scenario: Input functions are not part of the recorded log
- **WHEN** generated code invokes the wrapped `IsKeyDown`, `IsKeyPressed`, `IsKeyReleased`, `IsMouseButtonDown`, `IsMouseButtonPressed`, `IsMouseButtonReleased`, `GetMousePosition`, `GetMouseDelta`, or `GetMouseWheelMove` while `CACTUS_RAYLIB_FAKE` is defined
- **THEN** the call does not add an entry to the call log; input functions return the currently scripted state without being recorded as assertable calls

### Requirement: Fake exposes a scripting API for input state
The fake SHALL expose an API for a test to set keyboard key state and mouse state before driving a frame, and to reset all input state and the call log between scenarios.

#### Scenario: Scripted key state is observed by generated code
- **WHEN** a test scripts a key as down and then drives a frame that calls the wrapped `IsKeyDown` for that key
- **THEN** the fake reports the key as down for the remainder of that scripted state, matching what the test configured

#### Scenario: Reset clears both input state and call log
- **WHEN** a test invokes the fake's reset operation
- **THEN** all previously scripted key/mouse state reverts to not-pressed/not-down, and the call log becomes empty

### Requirement: Fake reports window as not ready by default
The wrapped `IsWindowReady` SHALL return `false` in the fake configuration unless a test explicitly overrides it, matching the assumption every existing headless runtime test already makes.

#### Scenario: Window-gated runtime paths stay unreached by default
- **WHEN** a headless test executable runs a curated example without overriding window-ready state
- **THEN** runtime code paths gated behind `if (IsWindowReady())` (sprite/mesh/model draw submission, asset materialization) do not execute, consistent with existing headless unit tests in `test_runtime_stdlib.cpp`

### Requirement: Headless test executables drive generated examples without a window
For a curated example under behavioral test, the build SHALL produce a dedicated test executable that compiles that example's generated C++ with `CACTUS_GENERATED_NO_MAIN` and `CACTUS_RAYLIB_FAKE` both defined, supplies its own `main()`/test entry point that calls `generated_init_project`, `generated_update_project`, and `generated_render_project` directly, and links against a build of the C++ EnTT runtime also compiled with `CACTUS_RAYLIB_FAKE` (not the shared, unmocked `cactus_runtime_cpp_entt` target used by real example executables), plus real raylib and the fake support library.

#### Scenario: Headless executable never calls InitWindow
- **WHEN** a headless behavioral test executable for a curated example runs
- **THEN** it does not call `InitWindow`, does not enter a `WindowShouldClose` loop, and drives exactly the frames the test scenario requests

#### Scenario: Each curated example under behavioral test has its own executable
- **WHEN** two curated examples are both covered by headless behavioral tests
- **THEN** each has its own distinct test executable and `ctest` entry, rather than sharing a single binary

### Requirement: Behavioral test assertions support approximate value and call-order matching
A headless behavioral test SHALL be able to assert that a specific call occurred in the fake's call log with approximately-equal floating-point argument values (not exact equality), and SHALL be able to assert that calls occurred in a given relative order, including full ordered subsequence matches.

#### Scenario: Approximate float matching passes for near-equal values
- **WHEN** a test asserts a recorded `DrawRectangleV` call's `position` against an expected `Vector2` using approximate matching
- **THEN** the assertion passes for floating-point differences within the matcher's tolerance and fails outside it

#### Scenario: Ordered subsequence assertion validates relative call order
- **WHEN** a test asserts that a list of expected call entries occurs as an ordered subsequence of the recorded call log
- **THEN** the assertion passes only if those entries appear in the log in that relative order, regardless of other interleaved entries

### Requirement: Blue-square example has headless behavioral coverage
The `blue-square` curated example SHALL have a headless behavioral test that scripts movement input, drives one or more frames through `generated_update_project`/`generated_render_project` without a window, and asserts against the fake raylib call log that the expected shape draw call occurred with the expected position, size, and color. Coverage SHALL include both a single-frame scenario and a multi-frame scenario, so that repeated/queued frame-event delivery bugs (not just single-tick behavior) are caught.

#### Scenario: Scripted input moves the square and is observed in the draw call
- **WHEN** the blue-square headless test scripts the input bound to positive `MoveX` as held and drives one frame with a fixed `dt`
- **THEN** the recorded `DrawRectangleV` call's position reflects the entity's starting position advanced by the expected input-driven movement for that `dt`, within approximate-matching tolerance

#### Scenario: No scripted input leaves the square at its starting position
- **WHEN** the blue-square headless test drives one frame with no input scripted
- **THEN** the recorded `DrawRectangleV` call's position equals the entity's starting position, unchanged

#### Scenario: Repeated scripted input across multiple frames accumulates movement
- **WHEN** the blue-square headless test scripts the input bound to positive `MoveX` as held and drives several frames in sequence, each with a fixed `dt`, injecting and draining a frame event before each frame's `DrawRectangleV` is recorded
- **THEN** each frame's recorded `DrawRectangleV` position reflects the entity's position after that many frames of accumulated input-driven movement, confirming the frame-event delivery pipeline runs correctly on every tick rather than only the first

### Requirement: Mesh-renderer example has headless behavioral coverage of queued draw flush
The `mesh-renderer` curated example SHALL have a headless behavioral test that asserts every mesh entity's queued `submit_mesh` draw is present in the fake raylib call log after the frame's flush — not silently dropped — and that the recorded `DrawMesh` calls are bracketed by the active viewport's `BeginMode3D`/`EndMode3D` calls in the call log.

#### Scenario: Queued mesh draws for all entities appear in the call log after flush
- **WHEN** the mesh-renderer headless test drives one frame with both `BlueCube` and `BlueCube2` present
- **THEN** the call log contains a `DrawMesh` entry attributable to each entity's mesh/material/transform, confirming neither entity's queued draw was dropped by the flush

#### Scenario: Flushed mesh draws are bracketed by the viewport's 3D mode calls
- **WHEN** the mesh-renderer headless test drives one frame
- **THEN** the call log contains `BeginMode3D`, followed by the frame's `DrawMesh` entries, followed by `EndMode3D`, as an ordered subsequence — guarding against the per-viewport queue/flush ordering regressions previously fixed for queued sprite/mesh/text submission

### Requirement: Model-animation example has headless behavioral coverage of animated mesh flush across frames
The `model-animation` curated example SHALL have a headless behavioral test that, reusing the mesh-renderer flush-assertion pattern, asserts queued draws for dynamically spawned, animated character entities appear in the call log on every one of several driven frames (not just the first), and that window-space HUD content is recorded after per-viewport 3D content within the same frame.

#### Scenario: Dynamically spawned character draws appear across multiple frames
- **WHEN** the model-animation headless test drives the load handler that spawns the three `CharacterTemplate` characters, then drives several further frames
- **THEN** each frame's call log contains a `DrawMesh` entry attributable to each spawned character, confirming neither the spawn-time creation-ordinal assignment nor the per-frame flush drops a dynamically created entity's draw

#### Scenario: Window-space HUD content is recorded after per-viewport 3D content
- **WHEN** the model-animation headless test drives one frame with the `Hud` screen label present
- **THEN** the call log records the frame's `DrawMesh`/`BeginMode3D`/`EndMode3D` entries before the HUD's window-space text draw entry, as an ordered subsequence

