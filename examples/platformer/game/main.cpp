// Cactus Platformer — 2.5D Rayman-inspired example
// This file provides the Raylib game loop and EnTT registry setup.
// Generated headers from .cactus files are included below.

#include "raylib.h"
#include <entt/entt.hpp>

// ── Generated headers from Cactus compiler ─────────────────────────────────────
// These are produced by: cactus <module>.cactus --backend cpp-entt -o <module>.generated.h
// If generation fails, the project still compiles with stub behavior.

#if __has_include("main.generated.h")
#include "main.generated.h"
#endif

#if __has_include("player.generated.h")
#include "player.generated.h"
#endif

#if __has_include("level.generated.h")
#include "level.generated.h"
#endif

#if __has_include("enemies.generated.h")
#include "enemies.generated.h"
#endif

#if __has_include("collectibles.generated.h")
#include "collectibles.generated.h"
#endif

#if __has_include("camera.generated.h")
#include "camera.generated.h"
#endif

#if __has_include("ui.generated.h")
#include "ui.generated.h"
#endif

// ── Placeholder constants (used if generated headers are not available) ─────────
#ifndef WINDOW_TITLE
#define WINDOW_TITLE "Cactus Platformer"
#endif

#ifndef WINDOW_WIDTH
#define WINDOW_WIDTH 1280
#endif

#ifndef WINDOW_HEIGHT
#define WINDOW_HEIGHT 720
#endif

#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

// ── Stub component types (used if generated code doesn't provide them) ──────────
namespace stub {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

struct PlayerTag {};

struct Platform {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    Color color = BROWN;
};

struct Enemy {
    float patrol_min = 0.0f;
    float patrol_max = 0.0f;
    float speed = 2.0f;
    float direction = 1.0f;
    Color color = RED;
    float w = 32.0f;
    float h = 32.0f;
};

struct Collectible {
    int points = 10;
    bool collected = false;
    Color color = GOLD;
    float size = 16.0f;
};

struct Health {
    int hp = 3;
    int lives = 3;
    int max_hp = 3;
};

struct Score {
    int score = 0;
    int gems = 0;
};

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float smoothing = 5.0f;
};

} // namespace stub

// ── Helper: Create stub entities for demo ───────────────────────────────────────
static void create_demo_entities(entt::registry& registry) {
    // Player
    auto player = registry.create();
    registry.emplace<stub::Position>(player, 100.0f, 300.0f, 0.0f, 0.0f);
    registry.emplace<stub::PlayerTag>(player);
    registry.emplace<stub::Health>(player, 3, 3, 3);
    registry.emplace<stub::Score>(player, 0, 0);

    // Ground
    auto ground = registry.create();
    registry.emplace<stub::Platform>(ground, 0.0f, 600.0f, 5000.0f, 40.0f, DARKGREEN);

    // Floating platforms
    struct PlatDef { float x, y, w, h; };
    PlatDef plats[] = {
        {300.0f, 450.0f, 200.0f, 20.0f},
        {600.0f, 350.0f, 150.0f, 20.0f},
        {950.0f, 280.0f, 180.0f, 20.0f},
        {1300.0f, 400.0f, 250.0f, 20.0f},
        {1700.0f, 300.0f, 120.0f, 20.0f},
    };
    for (auto& p : plats) {
        auto e = registry.create();
        registry.emplace<stub::Platform>(e, p.x, p.y, p.w, p.h, BROWN);
    }

    // Enemies
    struct EnemyDef { float x, y, pmin, pmax; };
    EnemyDef enemies[] = {
        {400.0f, 568.0f, 350.0f, 550.0f},
        {800.0f, 568.0f, 700.0f, 1000.0f},
        {1100.0f, 250.0f, 1000.0f, 1250.0f},
    };
    for (auto& ed : enemies) {
        auto e = registry.create();
        registry.emplace<stub::Position>(e, ed.x, ed.y, 0.0f, 0.0f);
        registry.emplace<stub::Enemy>(e, ed.pmin, ed.pmax, 2.0f, 1.0f, RED, 32.0f, 32.0f);
    }

    // Collectibles
    struct GemDef { float x, y; int pts; Color col; };
    GemDef gems[] = {
        {250.0f, 560.0f, 10, BLUE},
        {350.0f, 410.0f, 10, BLUE},
        {650.0f, 310.0f, 10, BLUE},
        {1000.0f, 240.0f, 25, RED},
        {1750.0f, 260.0f, 100, GOLD},
    };
    for (auto& g : gems) {
        auto e = registry.create();
        registry.emplace<stub::Position>(e, g.x, g.y, 0.0f, 0.0f);
        registry.emplace<stub::Collectible>(e, g.pts, false, g.col, 16.0f);
    }

    // Camera
    auto cam = registry.create();
    registry.emplace<stub::Camera>(cam, 0.0f, 0.0f, 5.0f);
}

// ── Stub systems (fallback rendering when generated code is not available) ──────
static void update_stub_systems(entt::registry& registry, float dt) {
    // Get camera offset
    float cam_x = 0.0f;
    float cam_y = 0.0f;
    registry.view<stub::Camera>().each([&](auto& cam) {
        cam_x = cam.x;
        cam_y = cam.y;
    });

    // Player movement
    auto player_view = registry.view<stub::Position, stub::PlayerTag>();
    player_view.each([&](auto& pos, auto&) {
        float speed = 6.0f;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) pos.vx = speed;
        else if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) pos.vx = -speed;
        else pos.vx = 0.0f;

        // Gravity
        pos.vy += 30.0f * dt;

        // Jump
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) && pos.y >= 550.0f) {
            pos.vy = -12.0f;
        }

        pos.x += pos.vx * dt * 60.0f;
        pos.y += pos.vy * dt * 60.0f;

        // Simple ground collision
        if (pos.y > 552.0f) {
            pos.y = 552.0f;
            pos.vy = 0.0f;
        }

        // Update camera
        registry.view<stub::Camera>().each([&](auto& cam) {
            float target_x = pos.x - 640.0f;
            float t = cam.smoothing * dt;
            if (t > 1.0f) t = 1.0f;
            cam.x += (target_x - cam.x) * t;
            if (cam.x < 0.0f) cam.x = 0.0f;
            if (cam.x > 3720.0f) cam.x = 3720.0f;
        });
    });

    // Enemy patrol
    registry.view<stub::Position, stub::Enemy>().each([&](auto& pos, auto& enemy) {
        pos.x += enemy.speed * enemy.direction * dt * 60.0f;
        if (pos.x >= enemy.patrol_max) { enemy.direction = -1.0f; pos.x = enemy.patrol_max; }
        if (pos.x <= enemy.patrol_min) { enemy.direction = 1.0f; pos.x = enemy.patrol_min; }
    });

    // Draw background
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, {135, 206, 235, 255}); // sky
    DrawRectangleGradientV(0, WINDOW_HEIGHT / 2, WINDOW_WIDTH, WINDOW_HEIGHT / 2,
                           {135, 206, 235, 255}, {200, 230, 200, 255});

    // Draw platforms
    registry.view<stub::Platform>().each([&](auto& plat) {
        DrawRectangle(
            static_cast<int>(plat.x - cam_x),
            static_cast<int>(plat.y - cam_y),
            static_cast<int>(plat.w),
            static_cast<int>(plat.h),
            plat.color
        );
    });

    // Draw collectibles
    registry.view<stub::Position, stub::Collectible>().each([&](auto& pos, auto& col) {
        if (!col.collected) {
            DrawRectangle(
                static_cast<int>(pos.x - cam_x),
                static_cast<int>(pos.y - cam_y),
                static_cast<int>(col.size),
                static_cast<int>(col.size),
                col.color
            );
        }
    });

    // Draw enemies
    registry.view<stub::Position, stub::Enemy>().each([&](auto& pos, auto& enemy) {
        DrawRectangle(
            static_cast<int>(pos.x - cam_x),
            static_cast<int>(pos.y - cam_y),
            static_cast<int>(enemy.w),
            static_cast<int>(enemy.h),
            enemy.color
        );
    });

    // Draw player
    player_view.each([&](auto& pos, auto&) {
        DrawRectangle(
            static_cast<int>(pos.x - cam_x),
            static_cast<int>(pos.y - cam_y),
            32, 48,
            {100, 149, 237, 255} // cornflower blue player
        );
    });

    // Draw HUD
    auto health_view = registry.view<stub::Health, stub::Score>();
    health_view.each([&](auto& health, auto& score) {
        // Health bar background
        DrawRectangle(10, 10, 150, 16, DARKGRAY);
        // Health bar
        int bar_w = 150 * health.hp / health.max_hp;
        DrawRectangle(10, 10, bar_w, 16, GREEN);
        // Lives
        DrawText(TextFormat("Lives: %d", health.lives), 10, 35, 20, {255, 100, 100, 255});
        // Score
        DrawText(TextFormat("Score: %d", score.score), 100, 35, 20, GOLD);
    });
}

// ── Main ────────────────────────────────────────────────────────────────────────
int main() {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);

    entt::registry registry;
    create_demo_entities(registry);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Use generated systems if available, otherwise fall back to stubs
        update_stub_systems(registry, dt);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
