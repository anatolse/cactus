# Cactus Platformer — 2.5D Rayman-Inspired Example

A side-scrolling platformer built with the Cactus DSL, demonstrating ECS-based game mechanics: player movement with double-jump, platform collision, enemy patrol AI, collectible gems, parallax camera, and a HUD overlay.

## Prerequisites

- **CMake** 3.20+
- **C++20** compatible compiler (MSVC 2022, GCC 12+, Clang 15+)
- **Git** (for FetchContent to download Raylib and EnTT)
- **Cactus compiler** built from the root project

## Building

### Step 1: Build the Cactus Compiler

From the repository root:

```bash
# Configure
cmake --preset default

# Build
cmake --build build

# The compiler binary will be at:
#   build/cactus.exe  (Windows)
#   build/cactus      (Linux/macOS)
```

### Step 2: Build the Platformer Game

From the `examples/platformer/game/` directory:

```bash
# Create build directory
mkdir build
cd build

# Configure — point to the cactus compiler
cmake -DCACTUS_COMPILER=../../../../build/cactus.exe ..

# On Linux/macOS:
# cmake -DCACTUS_COMPILER=../../../../build/cactus ..

# Build
cmake --build . --config Release
```

### Step 3: Run

```bash
# Windows
.\Release\platformer.exe

# Linux/macOS
./platformer
```

## How It Works

1. **CMake** invokes the `cactus` compiler on `platformer.cactus` via `add_custom_command`
2. The compiler generates `platformer_generated.cpp` using the `cpp-entt` backend
3. The generated file contains everything: components, systems, entity creation, and `main()`
4. Dependencies (Raylib 5.0, EnTT 3.13.1) are fetched automatically via CMake FetchContent

## Controls

| Key | Action |
|-----|--------|
| `A` / `←` | Move left |
| `D` / `→` | Move right |
| `Space` / `W` / `↑` | Jump (press again in air for double-jump) |
| `Esc` | Quit |

## Project Structure

```
examples/platformer/
├── platformer.cactus    # Single-file: all traits, systems, units, events
├── main.cactus          # (multi-file variant) Window config, module imports
├── player.cactus        # (multi-file variant) Player traits, physics, movement
├── level.cactus         # (multi-file variant) Platforms, collision detection
├── enemies.cactus       # (multi-file variant) Enemy AI, patrol system
├── collectibles.cactus  # (multi-file variant) Gems, scoring system
├── camera.cactus        # (multi-file variant) Side-scroll camera, parallax
├── ui.cactus            # (multi-file variant) HUD overlay
├── assets/              # Placeholder art assets
│   ├── sprites/         # Player, enemy, gem sprites (PNG placeholders)
│   ├── tiles/           # Platform and ground tiles (PNG placeholders)
│   ├── backgrounds/     # Parallax background layers (PNG placeholders)
│   └── gen_assets.py    # Script to regenerate placeholder PNGs
└── game/                # CMake sub-project
    ├── CMakeLists.txt   # Build config with cactus compiler integration
    └── README.md        # This file
```

## Notes

- Assets are placeholder colored rectangles — see `assets/README.md` for replacement guidance
- The single-file `platformer.cactus` is the canonical input; multi-file `.cactus` files show how the same game could be split across modules
- The generated code uses `Vector2` (raylib type) for `vec2` fields and `enum class` with `::` scope resolution
