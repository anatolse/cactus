## Context

The Cactus DSL project has a working compiler (lexer → parser → semantic analysis → C++ code generation) and one example (`cactus_shop`). The shop example is a static 3D scene with inventory/purchase mechanics. We need a second example that demonstrates action-game patterns: physics, collision, scrolling, enemy AI, and collectibles — a 2.5D platformer inspired by Rayman.

The example must be self-contained: `.cactus` source files, a CMake sub-project that invokes the cactus compiler, and placeholder assets. It should work as a reference for users and as an integration test for the compiler pipeline.

## Goals / Non-Goals

**Goals:**
- Demonstrate Cactus DSL's ability to express platformer game mechanics (movement, physics, collision, AI)
- Provide a complete end-to-end example: `.cactus` → cactus compiler → generated C++ → CMake build → executable
- Show idiomatic Cactus patterns: traits for data, systems for logic, events for decoupling, units for composition
- Include placeholder assets so the example is visually runnable out of the box
- Keep the CMake sub-project independent (fetches its own dependencies, only needs the `cactus` compiler binary)

**Non-Goals:**
- Production-quality art or animations — assets are intentionally draft/placeholder
- Modifying the Cactus compiler itself — this is purely an example consumer
- Full game with menus, save/load, audio — just enough to demonstrate the platformer pattern
- Multi-file compilation (the compiler currently processes one `.cactus` file at a time; the CMake project will invoke it per-file)

## Decisions

### 1. File Organization: Modular `.cactus` files mirroring `cactus_shop`

Each concern gets its own `.cactus` file: `main.cactus` (entry/config), `player.cactus` (character + physics), `level.cactus` (platforms + world), `enemies.cactus` (enemy AI), `collectibles.cactus` (gems/score), `camera.cactus` (side-scroll camera), `ui.cactus` (HUD).

**Rationale:** Matches the existing `cactus_shop` convention. Each file maps to a module, making the example easy to navigate and demonstrating the `module`/`use` system.

**Alternative considered:** Single monolithic `.cactus` file — rejected because it doesn't showcase modularity.

### 2. CMake Sub-Project with Custom Command for Code Generation

The `examples/platformer/game/CMakeLists.txt` will:
1. Accept `CACTUS_COMPILER` as a CMake variable (path to the `cactus` executable)
2. Use `add_custom_command` to invoke `cactus <file>.cactus --backend cpp-entt -o <file>.generated.h` for each `.cactus` file
3. Compile a hand-written `main.cpp` that `#include`s the generated headers and wires up the Raylib game loop
4. FetchContent Raylib and EnTT independently

**Rationale:** `add_custom_command` integrates naturally with CMake's dependency tracking. Using `cpp-entt` backend because EnTT is the more practical backend for a real game. The hand-written `main.cpp` is necessary because the generated code provides ECS components/systems but not the game loop boilerplate.

**Alternative considered:** Using `execute_process` at configure time — rejected because it doesn't re-run when `.cactus` files change.

### 3. 2.5D Approach: 2D Gameplay with Parallax Depth Layers

The game plays on a 2D plane (X horizontal, Y vertical) but uses multiple parallax background layers to create depth. All physics and collision are 2D (using `vec2` for positions). The camera tracks the player horizontally with smooth follow.

**Rationale:** True 2.5D (3D models on a 2D plane) would require 3D asset pipelines beyond the scope of a draft example. Parallax 2D achieves the Rayman aesthetic with simple sprite assets.

### 4. Draft Assets as Simple Geometric PNGs

Placeholder assets will be minimal PNG files generated or hand-drawn: colored rectangles for platforms, a simple character sprite sheet, circle gems for collectibles, and gradient backgrounds for parallax layers. An `assets/README.md` will describe what each asset represents and its intended replacement.

**Rationale:** Keeps the example self-contained without requiring external asset downloads. PNGs are universally supported by Raylib.

## Risks / Trade-offs

- **[Risk] Compiler may not support all DSL features used** → Mitigation: Only use language features already tested in `cactus_shop` and the test fixtures. Avoid untested edge cases.
- **[Risk] Generated code may not compile with EnTT backend** → Mitigation: The example's `main.cpp` will include the generated headers behind `#ifdef` guards so the project can still build even if generation has issues, with stub implementations.
- **[Risk] CMake sub-project complexity** → Mitigation: Keep it minimal — just FetchContent + custom commands + one executable target. Include clear comments.
- **[Trade-off] Placeholder assets look rough** → Acceptable for a compiler example. The `assets/README.md` documents intended replacements.
- **[Trade-off] No actual physics engine** → Simple gravity + AABB collision in Cactus systems is sufficient to demonstrate the pattern. Real games would use a physics library.
