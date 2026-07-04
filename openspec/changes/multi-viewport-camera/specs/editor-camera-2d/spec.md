## MODIFIED Requirements

## REMOVED Requirements

### Requirement: Codegen emits camera-sync block at top of generated_update_project
**Reason**: The camera-sync block in `generated_update_project` is removed entirely. Active-camera setting now happens per-viewport-iteration inside the viewport render loop in `generated_render_project`. There is no conditional or legacy path.
**Migration**: Import `std.camera.viewport` and add a `Viewport` trait to camera entities. The viewport render loop calls `set_active_camera_2d` / `set_active_camera_3d` automatically before each viewport's render systems.
