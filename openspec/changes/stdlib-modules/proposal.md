## Why

The Cactus DSL has two stdlib modules today — `std.core` (Persistent trait + SceneCleanup system) and `std.input` (keyboard/gamepad/mouse enums + query functions). These cover scene management and input reading, but nothing else. A game written in Cactus currently has no standard vocabulary for math, spatial transforms, rendering, physics, audio, or camera — forcing game code to define all of it from scratch or call undeclared C++ functions.

Without a standard library, every game reimplements the same patterns:
- Custom `lerp` and `clamp` functions instead of `std.math.lerp`
- Locally defined `trait Position` instead of `std.transform.flat.Transform`
- Stubbed backend calls `get_player_position()` instead of proper system-driven simulation
- No standard way to express audio, physics, or camera behavior

The stdlib is the "standard game vocabulary" that makes Cactus a usable game DSL rather than an abstract ECS specification.

## What Changes

A set of new `.cactus` files under `stdlib/std/` implementing the agreed stdlib architecture. Each module is independently importable — no forced dependencies between them.

### Architecture principles

1. **Free functions, not methods** — math and utility functions are pure funcs in named modules (`v3.length(v)`, not `v.length()`), avoiding the need for method dispatch on built-in types.

2. **Sub-modules per type** — `std.math.vec2`, `std.math.vec3`, `std.math.quat` each contain functions for their specific type. Module aliases (`use std.math.vec3 as v3`) make usage clean.

3. **`flat` / `volume` naming for spatial sub-modules** — The `flat` sub-module covers 2D (positions are `vec2`, rotation is `float`); the `volume` sub-module covers 3D (positions are `vec3`, rotation is `quat`). Same trait name in both (`Transform`, `CharacterBody`, `Camera`) — the module qualifier disambiguates.

4. **`sprites` / `meshes` naming for render sub-modules** — Named for the drawing technology: `sprites` for 2D sprite/texture rendering, `meshes` for 3D geometry. More descriptive of what you're drawing than `flat`/`volume`.

5. **Passive traits + backend rendering** — Rendering, physics, and audio traits are "passive" — the backend (Raylib code generation) reads them and acts. No user system is needed for standard rendering behavior. Entities with `Transform + Renderer` are drawn automatically.

6. **`std.audio` uses an event for fire-and-forget sounds** — `emit PlaySound(...)` is the idiomatic way to play one-shot sounds, consistent with the event-driven model.

7. **`std.debug` is effectful and build-stripped** — Functions in `std.debug` may only be called from system handler bodies (not from pure `func` declarations). All calls are compiled to nothing in `--release` builds.

### New stdlib files

```
stdlib/std/
  math.cactus              → std.math      (scalar functions, PI constant)
  math/
    vec2.cactus            → std.math.vec2 (2D vector functions)
    vec3.cactus            → std.math.vec3 (3D vector functions)
    quat.cactus            → std.math.quat (quaternion functions)
  transform/
    flat.cactus            → std.transform.flat   (Transform: position:vec2)
    volume.cactus          → std.transform.volume (Transform: position:vec3)
  render/
    sprites.cactus         → std.render.sprites   (2D: Renderer, Canvas2D)
    meshes.cactus          → std.render.meshes    (3D: Renderer, BillboardRenderer, lights)
  physics/
    flat.cactus            → std.physics.flat     (CharacterBody, Collider, CollisionEnter for 2D)
    volume.cactus          → std.physics.volume   (CharacterBody, Collider, CollisionEnter for 3D)
  camera/
    flat.cactus            → std.camera.flat      (Camera, FollowCamera + systems for 2D)
    volume.cactus          → std.camera.volume    (Camera, FollowCamera, FirstPersonCamera + systems for 3D)
  audio.cactus             → std.audio    (PlaySound event, AudioSource, MusicTrack, AudioSettings)
  debug.cactus             → std.debug    (watch_*, draw_*, assert — effectful, debug-only)
```

(Existing `std.core` and `std.input` are unchanged.)

### Module surfaces

**std.math** — scalar pure functions:
```cactus
pub const PI = 3.14159265
pub func lerp(a, b, t: float) -> float
pub func clamp(x, lo, hi: float) -> float
pub func abs(v: float) -> float
pub func min(a, b: float) -> float
pub func max(a, b: float) -> float
pub func sqrt(v: float) -> float
pub func sin(a: float) -> float
pub func cos(a: float) -> float
pub func atan2(y, x: float) -> float
pub func floor(v: float) -> int
pub func ceil(v: float) -> int
pub func round(v: float) -> int
pub func pow(base, exp: float) -> float
```

**std.math.vec2** — 2D vector pure functions:
```cactus
pub func length(v: vec2) -> float
pub func normalize(v: vec2) -> vec2
pub func dot(a, b: vec2) -> float
pub func lerp(a, b: vec2, t: float) -> vec2
pub func distance(a, b: vec2) -> float
pub func angle(v: vec2) -> float
```

**std.math.vec3** — 3D vector pure functions:
```cactus
pub func length(v: vec3) -> float
pub func normalize(v: vec3) -> vec3
pub func dot(a, b: vec3) -> float
pub func cross(a, b: vec3) -> vec3
pub func lerp(a, b: vec3, t: float) -> vec3
pub func distance(a, b: vec3) -> float
pub func reflect(v, normal: vec3) -> vec3
```

**std.math.quat** — quaternion pure functions:
```cactus
pub func identity() -> quat
pub func from_euler(pitch, yaw, roll: float) -> quat
pub func from_axis_angle(axis: vec3, angle: float) -> quat
pub func forward(q: quat) -> vec3
pub func right(q: quat) -> vec3
pub func up(q: quat) -> vec3
pub func rotate(q: quat, v: vec3) -> vec3
pub func slerp(a, b: quat, t: float) -> quat
pub func multiply(a, b: quat) -> quat
pub func inverse(q: quat) -> quat
```

**std.transform.flat** — 2D spatial transform:
```cactus
pub trait Transform:
    var position: vec2 = vec2(0.0, 0.0)
    var rotation: float = 0.0
    var scale: vec2 = vec2(1.0, 1.0)
```

**std.transform.volume** — 3D spatial transform:
```cactus
pub trait Transform:
    var position: vec3 = vec3(0.0, 0.0, 0.0)
    var rotation: quat = quat(0.0, 0.0, 0.0, 1.0)
    var scale: vec3 = vec3(1.0, 1.0, 1.0)
```

**std.render.sprites** — 2D rendering vocabulary:
```cactus
pub trait Renderer:
    let texture: texture_id
    var size: vec2 = vec2(1.0, 1.0)
    var color: color = #FFFFFFFF
    var visible: bool = true
    var layer: int = 0

pub trait AnimatedSprite:
    let texture: texture_id
    var frame: int = 0
    var frame_count: int = 1
    var fps: float = 12.0
    var playing: bool = true

pub trait Canvas2D:
    var color: color = #FFFFFFFF
    var visible: bool = true
```

**std.render.meshes** — 3D rendering vocabulary:
```cactus
pub trait Renderer:
    let mesh: mesh_id
    let material: material_id
    var visible: bool = true
    var cast_shadow: bool = true

pub trait BillboardRenderer:
    let texture: texture_id
    var size: vec2 = vec2(1.0, 1.0)
    var color: color = #FFFFFFFF
    var visible: bool = true

pub trait PointLight:
    var color: color = #FFFFFFFF
    var intensity: float = 1.0
    var range: float = 10.0
    var enabled: bool = true

pub trait DirectionalLight:
    var direction: vec3 = vec3(0.0, -1.0, 0.0)
    var color: color = #FFFFFFFF
    var intensity: float = 1.0
    var enabled: bool = true
```

**std.physics.flat** — 2D kinematic physics:
```cactus
pub trait CharacterBody:
    var velocity: vec2 = vec2(0.0, 0.0)
    var grounded: bool = false
    var gravity: float = 30.0

pub trait Collider:
    var width: float = 1.0
    var height: float = 1.0
    var layer: int = 1
    var mask: int = 1

pub event CollisionEnter:
    var other: entity_id
    var overlap: vec2
```

**std.physics.volume** — 3D kinematic physics:
```cactus
pub trait CharacterBody:
    var velocity: vec3 = vec3(0.0, 0.0, 0.0)
    var grounded: bool = false
    var ground_normal: vec3 = vec3(0.0, 1.0, 0.0)
    var step_height: float = 0.3

pub trait Collider:
    let shape: collider_shape
    var layer: int = 1
    var mask: int = 1

pub event CollisionEnter:
    var other: entity_id
    var point: vec3
    var normal: vec3
```

**std.camera.flat** — 2D camera traits + systems:
```cactus
pub trait Camera:
    var zoom: float = 1.0
    var offset: vec2 = vec2(0.0, 0.0)
    var rotation: float = 0.0
    var active: bool = true

pub trait FollowCamera:
    var target: entity_id
    var offset: vec2 = vec2(0.0, 0.0)
    var smoothing: float = 8.0

# System: updates Camera.offset to follow the target entity on late_tick
pub system FollowCameraSystem:
    filter:
        std.camera.flat.Camera as cam
        FollowCamera as follow
    on late_tick(dt: float):
        # ... updates cam.offset toward target entity's position
```

**std.camera.volume** — 3D camera traits + systems:
```cactus
pub trait Camera:
    var fov_y: float = 60.0
    var near: float = 0.1
    var far: float = 1000.0
    var active: bool = true

pub trait FollowCamera:
    var target: entity_id
    var offset: vec3 = vec3(0.0, 2.0, -6.0)
    var smoothing: float = 8.0

pub trait FirstPersonCamera:
    var pitch: float = 0.0
    var yaw: float = 0.0
    var sensitivity: float = 0.002

pub trait ThirdPersonCamera:
    var target: entity_id
    var distance: float = 6.0
    var height: float = 2.0
    var smoothing: float = 8.0

# Systems update Transform + Camera on late_tick
pub system FirstPersonCameraSystem: ...
pub system ThirdPersonCameraSystem: ...
pub system FollowCameraSystem: ...
```

**std.audio** — sound and music:
```cactus
pub event PlaySound:
    var sound: sound_id
    var volume: float
    var pitch: float

pub trait AudioSource:
    var sound: sound_id
    var playing: bool = false
    var looping: bool = true
    var volume: float = 1.0
    var pitch: float = 1.0
    var radius: float = 10.0

pub trait MusicTrack:
    var music: music_id
    var playing: bool = false
    var looping: bool = true
    var volume: float = 1.0

pub trait AudioSettings:
    var master_volume: float = 1.0
    var music_volume: float = 0.8
    var sfx_volume: float = 1.0
```

**std.debug** — effectful debug utilities (stripped in --release):
```cactus
pub func watch_int(label: string_id, value: int)
pub func watch_float(label: string_id, value: float)
pub func watch_bool(label: string_id, value: bool)
pub func watch_vec2(label: string_id, value: vec2)
pub func watch_vec3(label: string_id, value: vec3)
pub func assert(condition: bool, label: string_id)
pub func draw_line3(start, end: vec3, color: color)
pub func draw_box(center, size: vec3, color: color)
pub func draw_sphere(center: vec3, radius: float, color: color)
pub func draw_line2(start, end: vec2, color: color)
pub func draw_rect2(center, size: vec2, color: color)
```

## Capabilities

### New Capabilities

- `stdlib-math`: `std.math`, `std.math.vec2`, `std.math.vec3`, `std.math.quat`
- `stdlib-transform`: `std.transform.flat`, `std.transform.volume`
- `stdlib-render`: `std.render.sprites`, `std.render.meshes`
- `stdlib-physics`: `std.physics.flat`, `std.physics.volume`
- `stdlib-camera`: `std.camera.flat`, `std.camera.volume`
- `stdlib-audio`: `std.audio`
- `stdlib-debug`: `std.debug`

### Modified Capabilities

_(none — this change only adds new stdlib files)_

## Impact

- **New files only**: all changes are new `.cactus` files under `stdlib/std/`
- **No compiler source changes required** for the DSL part — these are just `.cactus` source files that the compiler processes with existing rules
- **Backend changes required** for passive traits: the Raylib backend code generator must be updated to read and act on `std.render`, `std.physics`, `std.audio`, and `std.camera` traits (separate backend change, not in scope here)
- **`std.debug`** requires a small semantic analyzer change to allow string literals in its function call arguments (debug label exception), and to enforce that its functions cannot be called from pure `func` declarations
- **`collider_shape`** is a new built-in type needed by `std.physics.volume.Collider` — requires a type system addition
- The `std.camera.flat` and `std.camera.volume` systems that update cameras need access to entity_id lookup (following a `target: entity_id` field) — the spec currently has no way to read another entity's trait fields from a system. This is a known gap that may require a query mechanism. The camera systems in these modules may need to be simplified or stubbed pending that feature.
