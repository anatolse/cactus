# Editor 3D Example

Demonstrates the 3D editor path of `std.editor`: a fixed angled camera looks
down on the y=0 ground grid (drawn by `GizmoRenderer3D` whenever edit mode is
active), the template palette lists the two `pub template` characters, and
Place-mode clicks spawn animated characters where the cursor ray hits the
ground plane. Clicking a placed character raycasts against its model bounds
and outlines the selection with a wire box.

The editor starts in 2D interaction mode by default; this example flips
`EditorState.use_3d` to `true` in a load handler. The flag makes the 2D and 3D
selection/placement systems mutually exclusive — a 3D project that leaves it
`false` gets silently dead editor clicks, so set it whenever you combine
`std.editor` with `std.transform.volume`.

Spawned characters are height-normalized on tick: a marker trait
(`CharacterScale.normalized`) starts `false`, and a system scales the
transform so the model's bind-pose height equals `TARGET_HEIGHT`, then sets
the flag. This runs for palette spawns and scene-authored entities alike.

## Controls

| Input                  | Action                                          |
|------------------------|-------------------------------------------------|
| F1                     | Toggle edit mode (grid, palette, and overlay)   |
| Palette click / T      | Enter Place mode (palette click picks template) |
| Click (Place mode)     | Spawn the active template on the ground grid    |
| Click (Select mode)    | Select the model under the cursor (wire box)    |
| W / E / R              | Translate / Rotate / Scale gizmo mode           |

Known quirk: a Place-mode click on a palette button also projects onto the
ground plane, so it both switches templates and spawns under the button. The
palette sits on the left edge to keep this out of the way.

Build and run from the project root (asset paths resolve against the process
working directory):

```
cmake --build build --target example_editor_3d_generated
./build/example_editor_3d_generated.exe
```

## Asset attribution

| File | Source | License |
|------|--------|---------|
| `../model-renderer/art/robot.glb` (shared) | [raylib examples](https://github.com/raysan5/raylib/tree/master/examples/models/resources/models/gltf), model by [@Quaternius](https://www.patreon.com/quaternius) | CC0 1.0 (Public Domain) |
| `../model-renderer/art/Crispoly-Characters-Mini-Red-Knight.glb` (shared) | Crispoly Characters pack, shipped with the model-renderer example | see model-renderer example |
