#include "backends/cpp-manual/runtime.h"

#include <raylib.h>

int main() {
    const auto config = cactus::runtime::manual_backend::generated_project_config();

    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    cactus::runtime::manual_backend::generated_init_project();
    cactus::runtime::manual_backend::generated_update_project(0.0F);
    cactus::runtime::manual_backend::generated_render_project();

    CloseWindow();
    return 0;
}