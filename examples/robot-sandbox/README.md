# Robot Sandbox

A robot explores, digs and builds in a seeded block world. It is a bounded
creative sandbox: the world has a fixed size, every edit applies at once and
materials never run out.

Build and run it from the repository root:

```
cmake --build build --target example_robot_sandbox_generated
build/example_robot_sandbox_generated
```

## Controls

First person (the mouse is captured):

| Input | Action |
|---|---|
| Mouse | Look |
| W A S D | Walk |
| Space | Jump (one block high) |
| Left click | Dig the cell under the crosshair |
| Right click | Build the chosen material against the struck face |
| Tab | Switch to the strategy view |
| R | Return to the spawn pocket |
| Esc | Pause menu |

Strategy view (the pointer is free):

| Input | Action |
|---|---|
| W A S D | Pan |
| Mouse wheel | Zoom |
| C | Centre on the robot |
| Left drag | Select a region of the current layers; release to dig or build it |
| Esc | Cancel a drag, or open the pause menu |
| Tab | Back to first person |

The toolbar at the bottom chooses the material (wood, stone, iron), dig or
build, the top layer and the depth of a region edit, the cutaway and the view.
A cutaway hides everything above the current layer in the strategy view only;
it never changes collision or the saved world. The clock at the top right
shows the day and time and steps the calendar speed.

## World

- Size: 64 x 48 x 64 cells, y from -16 to 31 (`WorldConfig` in `main.cactus`).
  The headless tests use a 16 x 12 x 16 world from `small_world.cactus`.
- New Game uses seed 7. The same seed and generator version always make the same world, whatever
  the batch size. `world.cactus` documents recipe version 1.
- The bottom layer is bedrock, which no edit removes. Building skips cells the
  robot stands in.
- The robot starts in a 3 x 3 x 3 air pocket over a stone floor. Recovering
  reopens the pocket.

## Time

One game minute passes per real second at 1x. The speeds are 0x, 0.5x, 1x, 2x,
4x and 8x, and they change only the calendar, never movement or physics. 0x
stops the sky, and pausing stops everything. The sun rises in the east at
06:00 and sets at 18:00, and the moon takes the other half of the day. Each
drives a directional light that fades through the horizon. The robot carries
a lamp.

## Saving

There is one save slot, `robot-sandbox`. A save keeps the cells themselves
(so loading never regenerates terrain), along with:
- the robot's pose, velocity and look;
- the camera choice and strategy view;
- the calendar;
- the options;
- the selected material and edit settings.

Menus, cameras and the sky are rebuilt after a load. A failed save or load
shows its error and keeps the current game. Exit asks first when there are
unsaved changes.

## Modules

| Module | Role |
|---|---|
| `game` | The shared `Session` state and the request events every screen sends |
| `world` | World size, the generation recipe and terrain textures |
| `robot` | The robot, first-person and strategy cameras, movement and checkpoints |
| `editing` | Single-cell and region dig/build, selection preview |
| `sky` | Calendar, sun, moon and sky colour |
| `hud` | Menus, toolbar, clock and labels built from `std.ui` |
| `flow` | The screen state machine, generation progress, save, load and exit |
| `main` | Window and the default world size |
| `small_world` | Test entry: a small world and scripted test requests |

## Assets

| File | Source | License |
|---|---|---|
| `art/wood.png`, `art/stone.png`, `art/iron.png`, `art/soil.png` | Made by `art/generate_textures.py` for this project | Same as this repository |
| `../model-renderer/art/robot.glb` | [raylib examples](https://github.com/raysan5/raylib/tree/master/examples/models/resources/models/gltf), model by [@Quaternius](https://www.patreon.com/quaternius) | CC0 1.0 (Public Domain) |

## Out of scope

Survival rules, inventories and crafting, work orders, other robots, an
endless or streamed world, multiple save slots, and undo.
