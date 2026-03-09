## Why

The Cactus DSL currently has only one example (`cactus_shop`) which demonstrates a 3D shop/inventory scenario. To showcase the language's versatility for real game development — especially 2.5D platformer mechanics à la Rayman — we need a second, richer example. A platformer example exercises movement physics, collision, parallax scrolling, enemy AI, collectibles, and level structure, proving the DSL handles action-game patterns. It also serves as an end-to-end integration test: `.cactus` files → cactus compiler → generated C++ → CMake build → runnable game.

## What Changes

- **New example `examples/platformer/`**: A set of `.cactus` files describing a 2.5D platformer inspired by Rayman, with:
  - Player character with run, jump, double-jump, and wall-slide mechanics
  - Side-scrolling camera with parallax background layers
  - Platform tiles and collision traits
  - Collectible items (gems/lums) and score tracking
  - Enemy entities with simple patrol AI
  - Level definition composing all units
  - HUD overlay showing health, score, and lives
- **New subfolder `examples/platformer/game/`**: A CMake project that invokes the cactus compiler to generate C++ source from the `.cactus` files, then builds a runnable executable linked against Raylib.
- **Draft assets in `examples/platformer/assets/`**: Placeholder sprite sheets, tilesets, and background layers (simple SVG/PNG placeholders) so the example is self-contained.

## Capabilities

### New Capabilities
- `platformer-cactus-files`: The `.cactus` source files defining the platformer game's ECS data model, systems, events, and units.
- `platformer-cmake-project`: The CMake integration project that uses the cactus compiler as a build step to generate and compile the game.
- `platformer-draft-assets`: Placeholder art assets (sprites, tiles, backgrounds) for the example.

### Modified Capabilities

_(none — this change adds a new example without modifying existing compiler specs or behavior)_

## Impact

- **New files only** — no changes to the compiler source, existing examples, or build system.
- Depends on the `cactus` compiler CLI being buildable (the root `CMakeLists.txt` already produces it).
- The sub-project CMake will use `find_program` or a relative path to locate the built `cactus` executable and invoke it at configure/build time.
- Raylib is used as the rendering backend (already a FetchContent dependency in the root project; the sub-project will fetch it independently to stay self-contained).
