# Model Animation Example

Demonstrates skeletal animation via `std.render.models.ModelAnimator` and
window-space HUD text via `std.render.text.ScreenLabel`: three robots share
one GLB model asset yet each plays its own animation clip — the runtime
re-poses the shared model per draw with GPU skinning, so no model cloning.

The HUD label at the top-left names the selected robot and its current clip
(looked up at runtime with `models.animation_name`), and works in this
`volume`-flavor program where `TextLabel`'s 2D path would be a no-op.

## Controls

| Input                  | Action                                          |
|------------------------|-------------------------------------------------|
| TAB                    | Select the next robot                           |
| Arrow keys / WASD      | Rotate the selected robot                       |
| Space                  | Advance the selected robot's animation clip     |

Selection is lock-step distributed state: every robot sees the same TAB press
and advances its own `selected` counter, so "am I selected" is a local
`index == selected` check with no cross-entity reads.

Build and run from the project root (asset paths resolve against the process
working directory):

```
cmake --build build --target example_model_animation_generated
./build/example_model_animation_generated.exe
```

## Asset attribution

| File | Source | License |
|------|--------|---------|
| `../model-renderer/art/robot.glb` (shared) | [raylib examples](https://github.com/raysan5/raylib/tree/master/examples/models/resources/models/gltf), model by [@Quaternius](https://www.patreon.com/quaternius) | CC0 1.0 (Public Domain) |
