## Why

After `dsl-spec-fixes` resolves internal inconsistencies and semantic gaps, the Cactus DSL spec still cannot describe a real game. Three categories of game-facing functionality are entirely absent:

1. **No update phase model** — `on tick(dt: float)` is the only per-frame lifecycle. Games require input sampling, deterministic physics (fixed timestep), general update, and camera/dependent-transform update to happen in a specific order. Without these phases, gameplay logic has no guaranteed ordering guarantees and physics cannot be decoupled from frame rate.

2. **No asset declarations** — There is no way in the DSL to reference external files (meshes, textures, sounds, fonts). Without this, traits like `MeshRenderer` cannot hold typed asset references, and the "strings only in const" rule blocks file paths entirely.

3. **No input declarations** — The `on input()` lifecycle phase (added in `dsl-spec-fixes`) has no content without a way to query input state. The DSL needs declarative input mapping so gameplay systems can read from named, remappable actions rather than raw key codes, and so the mapping layer is expressed in the language itself rather than buried in C++ backend code.

These three additions are what make the spec sufficient for expressing a simple 3D platformer or shooter in Cactus.

## What Changes

**`spec/cactus_dsl_spec.md`** is extended with:

### 1. Update phases — four lifecycle event names added

The following built-in lifecycle event names are added alongside `tick`, `spawn`, `destroy`, `load`, `unload`:

| Handler | Phase | `dt` param | Description |
|---------|-------|-----------|-------------|
| `on input()` | Input | none | Input snapshot is fresh. Read device state, write intent traits. |
| `on fixed_tick(dt: float)` | Physics | fixed step | Deterministic simulation. Runs 0–N times per rendered frame (accumulator model). |
| `on late_tick(dt: float)` | Post | variable | Runs after `tick`. Camera follow, dependent transforms, trail updates. |

`on tick(dt: float)` remains unchanged as the general per-frame handler.

**Frame execution order:**
```
on input()              [once per frame]
  → event phase         [cascade, depth ≤ max_cascade_depth]
on fixed_tick(dt)       [0..N times, accumulator-based]
  → event phase         [cascade, depth ≤ max_cascade_depth, per fixed step]
on tick(dt)             [once per frame]
  → event phase         [cascade, depth ≤ max_cascade_depth]
on late_tick(dt)        [once per frame]
  → event phase         [depth > max_cascade_depth → deferred to next frame]
RENDER                  [backend, not user code]
```

A system participates in a phase by declaring the corresponding handler. A system may declare handlers for multiple phases. The filter clause applies to all handlers within the system.

The keyword list gains `fixed_tick` and `late_tick`. (`input` is already a keyword per the input declaration addition below.)

### 2. Asset declarations — new top-level declaration form

A new declaration form allows modules to register external resource files under named compile-time handles:

```ebnf
asset_decl = [ "pub" ] "asset" IDENTIFIER ":" asset_type "=" STRING_LITERAL NEWLINE ;
asset_type = "mesh" | "texture" | "sound" | "music" | "font" | "material" ;
```

- The declared name resolves to a typed asset ID value at compile time.
- Asset path string literals are the **only** exception to the "strings only in const" rule; they are valid exclusively inside `asset` declarations.
- Asset types map to new built-in opaque ID types: `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`.
- Assets are loaded by the runtime at startup (not lazily). An `asset` declaration is a guarantee that the resource is available.
- `pub asset` makes the handle visible to importing modules.

**Usage:**

```cactus
asset PlayerMesh:  mesh    = "models/player.glb"
asset ShotSound:   sound   = "audio/shot.wav"
asset MainTheme:   music   = "audio/theme.ogg"
asset HudFont:     font    = "fonts/hud.ttf"

trait MeshRenderer:
    let mesh: mesh_id
    var visible: bool = true

trait AudioSource:
    let sound: sound_id
    var volume: float = 1.0

unit Player:
    apply:
        Transform
        MeshRenderer
        AudioSource
    config:
        mesh  = PlayerMesh
        sound = ShotSound
```

**New built-in types added to Section 5.1:**

| Type | Description |
|------|-------------|
| `mesh_id` | Opaque handle to a loaded mesh resource |
| `texture_id` | Opaque handle to a loaded texture resource |
| `sound_id` | Opaque handle to a loaded sound (short, one-shot) resource |
| `music_id` | Opaque handle to a loaded music (streaming) resource |
| `font_id` | Opaque handle to a loaded font resource |
| `material_id` | Opaque handle to a loaded material resource |

### 3. Input declarations — new top-level declaration form

A new declaration form maps named logical actions to physical device inputs:

```ebnf
input_decl = [ "pub" ] "input" IDENTIFIER ":" ( "button" | "axis" ) NEWLINE INDENT
             { input_prop }
             DEDENT ;
input_prop = IDENTIFIER "=" expression NEWLINE ;
```

- A `button` input resolves to type `InputButton` — a named action with boolean pressed/down/released state.
- An `axis` input resolves to type `InputAxis` — a named action with a float value from -1.0 to 1.0.
- `pub input` makes the name visible to importing modules.

**Valid `input_prop` keys:**

For `button`: `key`, `mouse`, `gamepad`
For `axis`: `negative`, `positive`, `gamepad`, `mouse_delta_x`, `mouse_delta_y`, `invert`

The property values reference enum constants defined in `std.input`:
- `Key.A`, `Key.D`, `Key.S`, `Key.W`, `Key.Space`, `Key.Escape`, `Key.Left`, `Key.Right`, `Key.Up`, `Key.Down`, `Key.F1`..`Key.F12`, etc.
- `MouseButton.Left`, `MouseButton.Right`, `MouseButton.Middle`
- `GamepadButton.South`, `GamepadButton.North`, `GamepadButton.East`, `GamepadButton.West`, `GamepadButton.L1`, `GamepadButton.R1`, `GamepadButton.Start`, `GamepadButton.Select`
- `GamepadAxis.LeftX`, `GamepadAxis.LeftY`, `GamepadAxis.RightX`, `GamepadAxis.RightY`, `GamepadAxis.TriggerL`, `GamepadAxis.TriggerR`

**Query functions in `std.input`** (callable from `on input()` handlers and anywhere):

```cactus
pub func pressed(b: InputButton) -> bool   # true on first frame of press
pub func down(b: InputButton) -> bool      # true while held
pub func released(b: InputButton) -> bool  # true on first frame of release
pub func axis(a: InputAxis) -> float       # -1.0 to 1.0
pub func axis2(x: InputAxis, y: InputAxis) -> vec2
```

**New built-in types added to Section 5.1:**

| Type | Description |
|------|-------------|
| `InputButton` | Named button action handle (from `input ... : button` declaration) |
| `InputAxis` | Named axis action handle (from `input ... : axis` declaration) |

**Complete example:**

```cactus
use std.input

input MoveX: axis
    negative = Key.A
    positive = Key.D
    gamepad  = GamepadAxis.LeftX

input MoveY: axis
    negative = Key.S
    positive = Key.W
    invert   = true
    gamepad  = GamepadAxis.LeftY

input Jump: button
    key     = Key.Space
    gamepad = GamepadButton.South

input Fire: button
    mouse   = MouseButton.Left
    gamepad = GamepadButton.R1

trait MoveIntent:
    var axis: vec2 = vec2(0.0, 0.0)
    var jump_pressed: bool = false

trait CombatIntent:
    var fire_pressed: bool = false

system ReadPlayerInput:
    filter:
        MoveIntent as move
        CombatIntent as combat
        PlayerTag

    on input():
        move.axis        = input.axis2(MoveX, MoveY)
        move.jump_pressed = input.pressed(Jump)
        combat.fire_pressed = input.pressed(Fire)

system PlayerMovement:
    filter:
        Transform as t
        CharacterBody as body
        MoveIntent as move
        PlayerTag

    on fixed_tick(dt: float):
        body.velocity.x = move.axis.x * ctrl.move_speed
        body.velocity.z = move.axis.y * ctrl.move_speed

        if body.grounded and move.jump_pressed:
            body.velocity.y = phys.jump_force * -1.0

system CameraFollow:
    filter:
        Transform as cam_t
        FollowCamera as cam

    on late_tick(dt: float):
        let target_pos = ...
        cam_t.position = lerp3(cam_t.position, target_pos + cam.offset, cam.smooth * dt)
```

### 4. Grammar and keyword additions

- `asset` added to keyword list and `declaration` production
- `input` added to keyword list and `declaration` production
- `fixed_tick`, `late_tick` added to `event_name` production (alongside `tick`, `spawn`, `destroy`, `load`, `unload`, `input`)
- `program declaration` updated:
  ```ebnf
  declaration = module_decl | use_decl | const_block | struct_decl
              | enum_decl | trait_decl | unit_decl | template_decl | system_decl
              | event_decl | func_decl | asset_decl | input_decl ;
  ```

### 5. String literal rule update

Section 6.1 (Const-String Rule) updated to add:

> **Exception:** string literals are permitted in `asset` declarations as the resource path value. They are not permitted in any other position outside `const` blocks.

## Capabilities

### New Capabilities

- `dsl-asset-declarations`: Compile-time resource handle declarations with typed asset IDs
- `dsl-input-declarations`: Declarative input action mapping with `InputButton`/`InputAxis` types
- `dsl-update-phases`: Four-phase per-frame execution model (`input`, `fixed_tick`, `tick`, `late_tick`)

### Modified Capabilities

- `dsl-type-system`: New built-in types `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`, `InputButton`, `InputAxis`
- `dsl-parser`: New `asset_decl` and `input_decl` grammar productions; `fixed_tick` and `late_tick` added to `event_name`

## Impact

- **`spec/cactus_dsl_spec.md`**: additive changes — new sections for asset, input, and update phases; type table extended; string rule amended
- **`stdlib/std/input.cactus`**: new stdlib file defining `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis` enums and the query func signatures
- **`examples/platformer/*.cactus`**: update to use `on input()` + `on fixed_tick()`, replace stub input calls with `input.axis()`/`input.pressed()`, add `asset` declarations for sprites
- Compiler backend changes required to implement asset loading, input snapshot, and phase dispatch (separate compiler changes will follow)
- Depends on: `dsl-spec-fixes` (field access model and event semantics must be in place first)
