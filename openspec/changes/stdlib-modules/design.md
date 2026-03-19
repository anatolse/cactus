## Context

The Cactus DSL currently has two stdlib modules: `std.core` (Persistent + SceneCleanup) and `std.input` (keyboard/gamepad/mouse). The language supports game development but lacks any standard vocabulary for math, 3D transforms, rendering, physics, audio, or camera. Game code must either reinvent these or call undeclared C++ stubs.

This change adds 13 new modules across 7 groups, all implemented as `.cactus` source files in `stdlib/std/`. The modules follow a sub-module per type pattern and a flat/volume naming convention for spatial sub-modules.

All new modules are data definitions and pure function declarations — no compiler source changes are needed to parse or analyze them. Backend changes (teaching the Raylib codegen to recognize and act on the new passive traits) are a separate change.

## Goals / Non-Goals

**Goals:**
- Create the `.cactus` source files for all 13 stdlib modules
- Define the agreed module surfaces: traits, events, and pure functions
- Follow the sub-module architecture decided during exploration
- Provide a complete, internally consistent stdlib that supports 2D platformers, 3D shooters, and mixed-space games

**Non-Goals:**
- Backend changes to make passive traits actually render/simulate (separate change)
- `std.debug` semantic analyzer enforcement (string literal exception, effectful restriction) — separate compiler change
- `std.collider_shape` built-in type — needed by `std.physics.volume.Collider` but is a type system addition (separate change); the volume Collider is stubbed until then
- `std.camera` active systems (FollowCamera, FirstPersonCamera) — require entity query capability not yet in the spec; these system declarations are stubbed

## Decisions

### D1: Sub-modules per type for math

**Decision:** `std.math` for scalars, `std.math.vec2`/`vec3`/`quat` for type-specific functions. Users import only what they need and use module aliases for clean call sites: `v3.length(...)`, `quat.forward(...)`.

**Rationale:** Without function overloading, `std.math.length` can't accept both `vec2` and `vec3`. Sub-modules solve this cleanly — each module contains `length` but for its own type. Module aliases make call sites readable.

**Alternative:** Naming suffixes (`length2`, `length3`). Rejected — ugly and hard to discover.

---

### D2: `flat` / `volume` naming for spatial sub-modules

**Decision:** Spatial sub-modules (transform, physics, camera) use `flat` (2D, positions are `vec2`) and `volume` (3D, positions are `vec3`). Render sub-modules use `sprites` / `meshes` (technology-based naming).

**Rationale:** "flat" and "volume" describe the spatial dimensionality without using digits (which are invalid identifier starts). "sprites" and "meshes" describe what you're drawing, which is more useful than knowing the spatial dimension. The naming schemes are intentionally different because they describe different things.

**Alternative:** Consistent `flat`/`volume` for render too. Rejected — "flat rendering" vs "volume rendering" doesn't convey what the traits contain as well as "sprites" vs "meshes."

---

### D3: Same trait name in both spatial sub-modules

**Decision:** `std.transform.flat.Transform` and `std.transform.volume.Transform` are both named `Transform`. Same for `CharacterBody`, `Camera`, etc. Module aliases disambiguate.

**Rationale:** Users import one spatial dimension per project. The module qualifier (`tr_flat.Transform`) or unqualified name (if unique) is the disambiguation. Users learn one vocabulary: "Transform" — not "Transform2D" vs "Transform3D". Switching a game from 2D to 3D means changing the import line, not renaming all trait references in code.

**Alternative:** Dimensional suffixes on trait names (`Transform2D`, `Transform3D`). Rejected — verbose, couples trait names to the dimension rather than the module.

---

### D4: Passive trait architecture for rendering and physics

**Decision:** Render, physics, and audio traits are "passive" — they contain data that the backend reads and acts on automatically. No user-written render/physics system is needed. An entity with `Transform + Renderer` is drawn automatically by the generated C++ code.

**Rationale:** This is the core ECS principle: data drives behavior. It eliminates boilerplate render systems, makes trait combinations compose naturally, and keeps game code focused on logic rather than plumbing.

**Alternative:** User writes explicit render systems calling backend draw funcs. Rejected — too verbose, defeats the purpose of a declarative ECS DSL.

---

### D5: Free functions, not methods

**Decision:** Math functions are pure funcs in named modules, not methods on built-in types. `v3.length(velocity)` not `velocity.length()`.

**Rationale:** Avoids adding method dispatch to built-in types, which would require compiler changes. Module aliases make call sites nearly as readable as methods. Consistent with how `std.input.pressed(Jump)` works.

**Alternative:** Built-in type methods. Rejected — requires significant compiler extension and breaks the "funcs are all you get for logic" simplicity.

---

### D6: `std.audio` uses an event for fire-and-forget sounds

**Decision:** One-shot sound effects use `emit PlaySound(ShotSound, 1.0, 1.0)`. Continuous/looping sounds use `AudioSource` trait. Background music uses `MusicTrack` trait.

**Rationale:** Fire-and-forget sounds have no persistent state — an event is the right model. `func` declarations can't be used (they're pure). Continuous sounds need persistent state on an entity → trait. Music needs persistence + runtime control → trait. Three patterns, three primitives.

---

### D7: `std.debug` as effectful free functions

**Decision:** Debug utilities are declared as `pub func` in `std.debug` but are designated effectful — the semantic analyzer (future change) will reject calls inside pure `func` declarations. All calls compile to nothing with `--release`. String literals are permitted as label arguments (a second exception to the const-string rule, alongside asset paths).

**Rationale:** Debug code must be easy to write (quick `watch_float("speed", speed)` calls without const boilerplate). Stripping in release is standard practice. Effectful designation prevents debug calls from "infecting" pure functions.

## Risks / Trade-offs

**[Risk] Camera FollowCamera/FPS/TPS systems require entity query — not yet in spec**
→ Mitigation: Camera system declarations are stubbed with `# TODO: requires entity query` comments. The trait definitions are complete and usable. Camera-following behavior can be implemented by user code until the query mechanism lands.

**[Risk] `std.physics.volume.Collider` requires `collider_shape` built-in type**
→ Mitigation: `Collider` in the volume module uses `let shape: collider_shape` with a comment noting it's a pending type system addition. In the interim, users can define their own collider without the shape field.

**[Risk] Passive traits require backend changes to have any effect**
→ Mitigation: The `.cactus` files define the vocabulary. The backend changes are scoped separately. Until backend support lands, users can still reference these traits in their code — they just won't auto-render/simulate. The proposal documents this explicitly.

**[Risk] Same trait name in both sub-modules — if user imports both, ambiguity**
→ Mitigation: The semantic analyzer already detects ambiguous symbol references and requires qualification. The error message "ambiguous reference 'Transform': defined in std.transform.flat and std.transform.volume; use qualified form" is helpful. Mixed imports are unusual and intentional.

**[Risk] `std.debug` string literal exception requires semantic analyzer change**
→ Mitigation: Without the semantic analyzer change, `watch_float("speed", val)` would be a const-string rule violation. Users can still use debug functions with `const:`-defined labels in the interim.

## Open Questions

- **`collider_shape` definition**: What shapes are needed? AABB-only for v0.1? Sphere? Capsule? This should be defined when the physics volume spec is detailed.
- **`std.camera` system body**: The `FollowCameraSystem` and `FirstPersonCameraSystem` systems need to read the Transform of `target: entity_id`. The query mechanism for this is not defined yet. Should these systems be removed from the scope of this change or left as stubs?
