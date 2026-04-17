#include "backends/cpp-entt/runtime.h"

#include <raylib.h>

namespace cactus::runtime::entt_backend {

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

}  // namespace cactus::runtime::entt_backend

int main() noexcept { // NOLINT(readability-function-cognitive-complexity,bugprone-exception-escape)
    using namespace cactus::runtime::entt_backend;

    const ProjectConfig config = generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    entt::dispatcher dispatcher;

    generated_setup_dispatcher(dispatcher);
    generated_init_project(registry);

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        generated_update_project(registry, dispatcher, dt);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        generated_render_project(registry, dispatcher);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}