## 1. Math Modules

- [x] 1.1 Create `stdlib/std/math.cactus` — module std.math with PI constant and scalar pure functions: lerp, clamp, abs, min, max, sqrt, sin, cos, atan2, floor, ceil, round, pow
- [x] 1.2 Create `stdlib/std/math/vec2.cactus` — module std.math.vec2 with 2D vector pure functions: length, normalize, dot, lerp, distance, angle
- [x] 1.3 Create `stdlib/std/math/vec3.cactus` — module std.math.vec3 with 3D vector pure functions: length, normalize, dot, cross, lerp, distance, reflect
- [x] 1.4 Create `stdlib/std/math/quat.cactus` — module std.math.quat with quaternion pure functions: identity, from_euler, from_axis_angle, forward, right, up, rotate, slerp, multiply, inverse

## 2. Transform Modules

- [x] 2.1 Create `stdlib/std/transform/flat.cactus` — module std.transform.flat with `pub trait Transform: var position: vec2, var rotation: float, var scale: vec2` with defaults
- [x] 2.2 Create `stdlib/std/transform/volume.cactus` — module std.transform.volume with `pub trait Transform: var position: vec3, var rotation: quat, var scale: vec3` with defaults (identity quat)

## 3. Render Modules

- [x] 3.1 Create `stdlib/std/render/sprites.cactus` — module std.render.sprites with pub traits: Renderer (texture: texture_id let, size: vec2, color: color, visible: bool, layer: int), AnimatedSprite (texture, frame, frame_count, fps, playing), Canvas2D (color, visible)
- [x] 3.2 Create `stdlib/std/render/meshes.cactus` — module std.render.meshes with pub traits: Renderer (mesh: mesh_id let, material: material_id let, visible, cast_shadow), BillboardRenderer (texture, size, color, visible), PointLight (color, intensity, range, enabled), DirectionalLight (direction, color, intensity, enabled)

## 4. Physics Modules

- [x] 4.1 Create `stdlib/std/physics/flat.cactus` — module std.physics.flat with: pub trait CharacterBody (velocity: vec2, grounded: bool, gravity: float), pub trait Collider (width, height, layer, mask: int), pub event CollisionEnter (other: entity_id, overlap: vec2)
- [x] 4.2 Create `stdlib/std/physics/volume.cactus` — module std.physics.volume with: pub trait CharacterBody (velocity: vec3, grounded: bool, ground_normal: vec3, step_height: float), pub trait Collider (layer, mask: int; note: shape field stubbed pending collider_shape type), pub event CollisionEnter (other: entity_id, point: vec3, normal: vec3)

## 5. Camera Modules

- [x] 5.1 Create `stdlib/std/camera/flat.cactus` — module std.camera.flat with: pub trait Camera (zoom: float, offset: vec2, rotation: float, active: bool), pub trait FollowCamera (target: entity_id, offset: vec2, smoothing: float), pub system FollowCameraSystem (stubbed with TODO comment pending entity query)
- [x] 5.2 Create `stdlib/std/camera/volume.cactus` — module std.camera.volume with: pub trait Camera (fov_y, near, far: float, active: bool), pub trait FollowCamera (target: entity_id, offset: vec3, smoothing: float), pub trait FirstPersonCamera (pitch, yaw, sensitivity: float), pub trait ThirdPersonCamera (target: entity_id, distance, height, smoothing: float), all camera systems stubbed with TODO pending entity query

## 6. Audio Module

- [x] 6.1 Create `stdlib/std/audio.cactus` — module std.audio with: pub event PlaySound (sound: sound_id, volume: float, pitch: float), pub trait AudioSource (sound, playing, looping, volume, pitch, radius), pub trait MusicTrack (music, playing, looping, volume), pub trait AudioSettings (master_volume, music_volume, sfx_volume)
