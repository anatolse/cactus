# Model Renderer Example

Demonstrates the `model` asset type and the `std.render.models.ModelRenderer`
trait: two GLB models rendered with their embedded materials under the stdlib
lighting pipeline.

- The **player** model renders with a red body, white eyes, and black pupils —
  three primitives, each bound to its own flat-color embedded material.
- The **robot** model renders statically with its three embedded materials
  (skeletal animation is a follow-up change, `dsl-model-animation`).

Build and run from the project root (asset paths resolve against the process
working directory):

```
cmake --build build --target example_model_renderer_generated
./build/example_model_renderer_generated.exe
```

## Asset attribution

| File             | Source                                                                                          | License |
|------------------|-------------------------------------------------------------------------------------------------|---------|
| `art/player.glb` | [GDQuest — Squash the Creeps 3D](https://github.com/godotengine/godot-demo-projects/tree/master/3d/squash_the_creeps) (Godot "Your First 3D Game" tutorial assets) | MIT (© 2020 GDQuest) |
| `art/robot.glb`  | [raylib examples](https://github.com/raysan5/raylib/tree/master/examples/models/resources/models/gltf), model by [@Quaternius](https://www.patreon.com/quaternius) | CC0 1.0 (Public Domain) |
