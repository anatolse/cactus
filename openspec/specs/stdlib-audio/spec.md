## Requirements

### Requirement: std.audio provides a PlaySound event for fire-and-forget sounds
The `std.audio` module SHALL provide a `PlaySound` event for playing short, one-shot sound effects. Any system may `emit PlaySound(...)` and the backend SHALL play the sound immediately, non-positionally. The sound plays once and requires no cleanup.

#### Scenario: PlaySound event fields
- **WHEN** `use std.audio` is imported
- **THEN** `PlaySound` event has fields: `sound: sound_id`, `volume: float`, `pitch: float`

#### Scenario: Emitting PlaySound plays the sound
- **WHEN** a system handler executes `emit PlaySound(GemSound, 0.8, 1.0)`
- **THEN** the backend plays the `GemSound` sound effect once at 80% volume

#### Scenario: PlaySound is non-positional
- **WHEN** `emit PlaySound(...)` is used without a `to entity_id` target
- **THEN** the backend plays the sound globally (same volume regardless of camera distance)

---

### Requirement: std.audio provides an AudioSource trait for persistent entity-attached audio
The `AudioSource` trait SHALL define a continuous sound source attached to an entity. When `playing = true`, the backend plays the sound. When `playing = false`, the backend stops it. If the entity also has a `std.transform.volume.Transform`, the sound is rendered spatially (volume attenuates with distance from the active camera).

#### Scenario: AudioSource starts playing
- **WHEN** an entity has `AudioSource` with `playing = true` and a valid `sound: sound_id`
- **THEN** the backend plays the sound (looping if `looping = true`) from the entity's position

#### Scenario: AudioSource stops when playing set to false
- **WHEN** a system sets `AudioSource.playing = false`
- **THEN** the backend stops the sound on that entity

#### Scenario: Spatial audio with volume Transform
- **WHEN** an entity has both a `std.transform.volume.Transform` and `AudioSource`
- **THEN** the backend attenuates the volume based on distance from the active camera using `radius` as the effective range

#### Scenario: Non-spatial audio without Transform
- **WHEN** an entity has `AudioSource` but no Transform
- **THEN** the backend plays the sound non-spatially at full volume (same as PlaySound)

---

### Requirement: std.audio provides a MusicTrack trait for streaming background music
The `MusicTrack` trait SHALL define a streaming music track. When `playing = true`, the backend streams and loops the music. When `playing = false`, it stops. Setting `music` to a different `music_id` while playing transitions to the new track. The backend calls `UpdateMusicStream` automatically each frame.

#### Scenario: MusicTrack starts playing
- **WHEN** an entity has `MusicTrack` with `playing = true` and a valid `music: music_id`
- **THEN** the backend starts streaming the music track

#### Scenario: Music track transition by changing music field
- **WHEN** a system sets `MusicTrack.music = BossTheme` while the track is playing
- **THEN** the backend transitions to the new music track

#### Scenario: Music volume respects AudioSettings
- **WHEN** an entity has `AudioSettings` with `music_volume = 0.5` and a `MusicTrack` entity has `volume = 1.0`
- **THEN** the effective music volume is `1.0 × 0.5 × master_volume`

---

### Requirement: std.audio provides an AudioSettings trait for global volume control
The `AudioSettings` trait SHALL define global audio volume multipliers. Applied to a singleton entity. The backend multiplies individual track/sound volumes by the appropriate category volume and master volume.

#### Scenario: AudioSettings controls volume categories
- **WHEN** a singleton entity has `AudioSettings` with `sfx_volume = 0.7, music_volume = 0.8, master_volume = 1.0`
- **THEN** all SFX play at 70% and music at 80% of their individual volumes

#### Scenario: Pausing music via volume
- **WHEN** a system sets `AudioSettings.music_volume = 0.0`
- **THEN** all music tracks play silently (effectively paused without stopping the stream)
