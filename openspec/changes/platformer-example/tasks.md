## 1. Cactus DSL Source Files

- [x] 1.1 Create `examples/platformer/main.cactus` with window config constants and all module imports
- [x] 1.2 Create `examples/platformer/player.cactus` with Position, PlayerPhysics, PlayerController, Health traits, Player unit, and MovementSystem/GravitySystem/JumpSystem
- [x] 1.3 Create `examples/platformer/level.cactus` with Platform trait, PlatformType enum, platform units, ground unit, and CollisionSystem
- [x] 1.4 Create `examples/platformer/enemies.cactus` with EnemyAI trait, enemy units, PatrolSystem, and PlayerDamaged event
- [x] 1.5 Create `examples/platformer/collectibles.cactus` with Collectible trait, Score trait, gem units, CollectionSystem, and GemCollected event
- [x] 1.6 Create `examples/platformer/camera.cactus` with SideScrollCamera trait, CameraUnit, and CameraFollowSystem
- [x] 1.7 Create `examples/platformer/ui.cactus` with HUD view, HUD constants, and HUDSystem

## 2. CMake Sub-Project

- [x] 2.1 Create `examples/platformer/game/CMakeLists.txt` with FetchContent for Raylib/EnTT, CACTUS_COMPILER variable, and add_custom_command per .cactus file
- [x] 2.2 Create `examples/platformer/game/main.cpp` with Raylib game loop, EnTT registry setup, and generated header includes
- [x] 2.3 Create `examples/platformer/game/README.md` with build instructions and example cmake commands

## 3. Draft Assets

- [x] 3.1 Create `examples/platformer/assets/sprites/` with placeholder player.png, enemy.png, and gem.png
- [x] 3.2 Create `examples/platformer/assets/tiles/` with placeholder platform.png and ground.png
- [x] 3.3 Create `examples/platformer/assets/backgrounds/` with placeholder sky.png and mountains.png
- [x] 3.4 Create `examples/platformer/assets/README.md` documenting all assets with purpose and replacement guidance
