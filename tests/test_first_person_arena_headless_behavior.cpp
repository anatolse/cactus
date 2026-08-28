// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kFrameDt = 1.0F / 60.0F;

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

template <typename Trait>
std::size_t count(entt::registry& registry) {
    return registry.view<Trait>().size();
}

void drive_frames(entt::registry& registry, const int count) {
    for (int frame = 0; frame < count; ++frame) {
        cactus_headless_test::drive_frame(registry, kFrameDt);
    }
}

float horizontal_distance(const Vector3 left, const Vector3 right) {
    const float dx = left.x - right.x;
    const float dz = left.z - right.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool same_color(const Color left, const Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

// Enemies are grounded at torso height on their own entity (collision
// capsule center); the rendered model (ModelRenderer/ModelAnimator) lives on
// a child EnemyVisual entity offset down by half_height instead, so the
// visible mesh's feet-anchored origin lands on the floor. Tests that assert
// on rendering state look it up through the Parent relation.
entt::entity enemy_visual_of(entt::registry& registry, const entt::entity enemy) {
    for (const auto visual : registry.view<main__EnemyVisual, std_core__Parent>()) {
        if (registry.get<std_core__Parent>(visual).parent == enemy) {
            return visual;
        }
    }
    return entt::null;
}

void dispatch_unload(entt::registry& registry) {
    auto& activation  = cactus::runtime::entt_backend::generated_scheduler_state().activation;
    activation.active = true;
    cactus::runtime::entt_backend::generated_dispatch_event(registry, std_core__unloadEvent{});
    cactus::runtime::entt_backend::generated_drain_event_cascade(registry);
    cactus::runtime::entt_backend::generated_commit_activation(registry);
    activation.active = false;
}

}  // namespace

TEST_CASE("first-person arena headless: authored arena, player camera, HUD, and initial wave load",
          "[runtime][codegen-entt][first-person-arena]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    CHECK(cactus_raylib_fake::cursor_captured());
    CHECK(count<main__Floor>(registry) == 1);
    CHECK(count<main__PerimeterWall>(registry) == 4);
    CHECK(count<main__BuildingWall>(registry) == 6);
    CHECK(count<main__Stair>(registry) == 8);
    CHECK(count<main__BuildingRoof>(registry) == 1);
    CHECK(count<main__RobotSpawnPoint>(registry) == 2);
    CHECK(count<main__KnightSpawnPoint>(registry) == 2);
    CHECK(count<main__RobotEnemy>(registry) == 2);
    CHECK(count<main__KnightEnemy>(registry) == 2);
    CHECK(count<main__Threat>(registry) == 4);

    const auto player = only_entity<main__Player>(registry);
    CHECK(registry.all_of<std_physics_volume__Collider, std_physics_volume__CapsuleCollider>(player));
    const auto camera = only_entity<main__CameraRig>(registry);
    REQUIRE(registry.all_of<std_core__Parent>(camera));
    CHECK(registry.get<std_core__Parent>(camera).parent == player);
    CHECK(registry.all_of<std_camera_volume__Camera, std_camera_viewport__Viewport>(camera));
    CHECK(same_color(registry.get<std_camera_viewport__Viewport>(camera).clear_color,
                     Color{.r = 72, .g = 144, .b = 224, .a = 255}));

    const auto floor = only_entity<main__Floor>(registry);
    CHECK(
        same_color(registry.get<std_render_meshes__Renderer>(floor).color, Color{.r = 64, .g = 64, .b = 64, .a = 255}));
    const auto perimeter = registry.view<main__PerimeterWall, std_render_meshes__Renderer>();
    REQUIRE(perimeter.begin() != perimeter.end());
    CHECK(same_color(perimeter.template get<std_render_meshes__Renderer>(*perimeter.begin()).color,
                     Color{.r = 192, .g = 192, .b = 192, .a = 255}));
    const auto building = registry.view<main__BuildingWall, std_render_meshes__Renderer>();
    REQUIRE(building.begin() != building.end());
    CHECK(same_color(building.template get<std_render_meshes__Renderer>(*building.begin()).color,
                     Color{.r = 128, .g = 80, .b = 32, .a = 255}));

    const auto crosshair = only_entity<main__Crosshair>(registry);
    const auto game_over = only_entity<main__GameOverLabel>(registry);
    CHECK(registry.get<std_render_text__ScreenLabel>(crosshair).visible);
    CHECK_FALSE(registry.get<std_render_text__ScreenLabel>(game_over).visible);
}

TEST_CASE("first-person arena headless: enemy visuals are grounded at floor level, not the collision capsule center",
          "[runtime][codegen-entt][first-person-arena][grounding]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto enemies = registry.view<main__Enemy, std_transform_volume__WorldTransform>();
    REQUIRE(count<main__Enemy>(registry) == 4);
    for (const auto enemy : enemies) {
        const auto& root_transform = registry.get<std_transform_volume__WorldTransform>(enemy);
        // The collision/grounding root stays at torso height (floor + half
        // height) so wall separation, player-contact, and bullet-hit checks
        // keep working against a stable reference.
        CHECK(root_transform.position.y == Catch::Approx(0.9F).margin(0.01F));

        const auto visual = enemy_visual_of(registry, enemy);
        REQUIRE(registry.valid(visual));
        REQUIRE(registry.all_of<std_render_models__ModelRenderer, std_render_models__ModelAnimator>(visual));
        const auto& visual_transform = registry.get<std_transform_volume__WorldTransform>(visual);
        // The rendered (feet-anchored) model is offset down by half_height,
        // so its composed world position lands on the floor, not floating at
        // the capsule's center.
        CHECK(visual_transform.position.x == Catch::Approx(root_transform.position.x).margin(0.001F));
        CHECK(visual_transform.position.y == Catch::Approx(0.0F).margin(0.01F));
        CHECK(visual_transform.position.z == Catch::Approx(root_transform.position.z).margin(0.001F));

        const auto& animator = registry.get<std_render_models__ModelAnimator>(visual);
        CHECK(animator.playing);
        if (registry.all_of<main__RobotEnemy>(enemy)) {
            CHECK(animator.clip == 6);
        } else {
            REQUIRE(registry.all_of<main__KnightEnemy>(enemy));
            CHECK(animator.clip == 4);
        }
    }
}

TEST_CASE("first-person arena headless: each corner spawner produces an independent ten-second wave",
          "[runtime][codegen-entt][first-person-arena][spawning]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);
    REQUIRE(count<main__Enemy>(registry) == 4);

    drive_frames(registry, 610);
    CHECK(count<main__RobotEnemy>(registry) == 4);
    CHECK(count<main__KnightEnemy>(registry) == 4);
}

TEST_CASE("first-person arena headless: facing-relative movement, seeking, wall separation, and stairs work",
          "[runtime][codegen-entt][first-person-arena][movement]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto player      = only_entity<main__Player>(registry);
    auto& player_transform = registry.get<std_transform_volume__WorldTransform>(player);
    const auto enemies     = registry.view<main__Enemy, std_transform_volume__WorldTransform>();
    REQUIRE(enemies.begin() != enemies.end());
    const auto seeker = *enemies.begin();
    for (const auto enemy : enemies) {
        if (enemy != seeker) {
            registry.destroy(enemy);
        }
    }
    player_transform.position   = Vector3{.x = 0.0F, .y = 0.9F, .z = 8.0F};
    auto& seeker_transform      = registry.get<std_transform_volume__WorldTransform>(seeker);
    seeker_transform.position   = Vector3{.x = 0.0F, .y = 0.9F, .z = 12.0F};
    const float distance_before = horizontal_distance(player_transform.position, seeker_transform.position);
    drive_frames(registry, 30);
    CHECK(horizontal_distance(player_transform.position, seeker_transform.position) < distance_before);

    registry.destroy(seeker);
    player_transform.position = Vector3{.x = 0.0F, .y = 0.9F, .z = -14.35F};
    player_transform.rotation = QuaternionIdentity();
    cactus_raylib_fake::set_key_down(KEY_W, true);
    drive_frames(registry, 60);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    CHECK(player_transform.position.z >= -14.41F);

    player_transform.position = Vector3{.x = 8.4F, .y = 0.9F, .z = 0.0F};
    player_transform.rotation = QuaternionFromEuler(0.0F, 1.57079633F, 0.0F);
    auto& camera_state        = registry.get<std_camera_volume__FirstPersonCamera>(player);
    camera_state.yaw          = 1.57079633F;
    cactus_raylib_fake::set_key_down(KEY_W, true);
    drive_frames(registry, 100);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    CHECK(player_transform.position.x < 4.0F);
    CHECK(player_transform.position.y == Catch::Approx(4.9F).margin(0.05F));
    CHECK(registry.get<main__KinematicActor>(player).ground_surface == Catch::Approx(4.0F).margin(0.01F));

    player_transform.position = Vector3{.x = 0.0F, .y = 0.9F, .z = 8.0F};
    camera_state.yaw          = 0.0F;
    player_transform.rotation = QuaternionIdentity();
    cactus_raylib_fake::set_mouse_delta(Vector2{.x = -785.3982F, .y = -2000.0F});
    cactus_raylib_fake::set_key_down(KEY_W, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    cactus_raylib_fake::set_mouse_delta(Vector2{});
    CHECK(player_transform.position.x < 0.0F);
    CHECK(std::abs(player_transform.position.x) > std::abs(player_transform.position.z - 8.0F));
    CHECK(camera_state.pitch <= 1.45F);
    CHECK(camera_state.pitch >= -1.45F);
}

TEST_CASE("first-person arena headless: bullets follow aim, serialize contacts, and drive the death fade",
          "[runtime][codegen-entt][first-person-arena][projectile]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto enemies = registry.view<main__Enemy, std_transform_volume__WorldTransform>();
    REQUIRE(count<main__Enemy>(registry) >= 2);
    auto iterator          = enemies.begin();
    const auto first_enemy = *iterator;
    ++iterator;
    const auto second_enemy = *iterator;
    for (const auto enemy : enemies) {
        registry.get<std_transform_volume__WorldTransform>(enemy).position = Vector3{.x = 12.0F, .y = 0.9F, .z = 12.0F};
    }
    registry.get<std_transform_volume__WorldTransform>(first_enemy).position = Vector3{.x = 0.0F, .y = 0.9F, .z = 8.5F};
    registry.get<std_transform_volume__WorldTransform>(second_enemy).position =
        Vector3{.x = 0.0F, .y = 0.9F, .z = 8.5F};

    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, false);
    const auto bullets = registry.view<main__Bullet>();
    REQUIRE(bullets.size() == 1);
    const auto velocity = bullets.template get<main__Bullet>(*bullets.begin()).velocity;
    CHECK(velocity.x == Catch::Approx(0.0F).margin(0.001F));
    CHECK(velocity.z < 0.0F);

    const auto bullet_position  = registry.get<std_transform_volume__WorldTransform>(*bullets.begin()).position;
    const auto contact_position = Vector3{.x = bullet_position.x + velocity.x * kFrameDt,
                                          .y = bullet_position.y + velocity.y * kFrameDt,
                                          .z = bullet_position.z + velocity.z * kFrameDt};
    registry.get<std_transform_volume__WorldTransform>(first_enemy).position  = contact_position;
    registry.get<std_transform_volume__WorldTransform>(second_enemy).position = contact_position;
    cactus_headless_test::drive_frame(registry, kFrameDt);
    CHECK(count<main__Bullet>(registry) == 0);
    const auto dying = registry.view<main__Enemy>();
    const auto dying_count =
        std::ranges::count_if(dying, [&](const auto enemy) { return registry.get<main__Enemy>(enemy).dying; });
    CHECK(dying_count == 1);

    entt::entity dying_enemy = entt::null;
    for (const auto enemy : dying) {
        auto& state = registry.get<main__Enemy>(enemy);
        if (state.dying) {
            dying_enemy = enemy;
        } else {
            registry.get<std_transform_volume__WorldTransform>(enemy).position =
                Vector3{.x = 12.0F, .y = 0.9F, .z = 12.0F};
        }
    }
    REQUIRE(registry.valid(dying_enemy));
    const auto dying_visual = enemy_visual_of(registry, dying_enemy);
    REQUIRE(registry.valid(dying_visual));
    // The death animation clip actually plays (not frozen): a type-specific
    // death clip starts from time 0 and keeps advancing while dying.
    const auto& dying_animator = registry.get<std_render_models__ModelAnimator>(dying_visual);
    CHECK(dying_animator.playing);
    if (registry.all_of<main__RobotEnemy>(dying_enemy)) {
        CHECK(dying_animator.clip == 1);
    } else {
        REQUIRE(registry.all_of<main__KnightEnemy>(dying_enemy));
        CHECK(dying_animator.clip == 6);
    }
    const auto death_start_rotation = registry.get<std_transform_volume__WorldTransform>(dying_enemy).rotation;
    drive_frames(registry, 28);
    REQUIRE(registry.valid(dying_enemy));
    const auto& halfway = registry.get<main__Enemy>(dying_enemy);
    CHECK(halfway.death_elapsed == Catch::Approx(0.5F).margin(0.08F));
    const auto alpha = registry.get<std_render_models__ModelRenderer>(dying_visual).color.a;
    CHECK(alpha >= 100);
    CHECK(alpha <= 155);
    const auto halfway_rotation = registry.get<std_transform_volume__WorldTransform>(dying_enemy).rotation;
    const float rotation_change =
        std::abs(halfway_rotation.x - death_start_rotation.x) + std::abs(halfway_rotation.y - death_start_rotation.y) +
        std::abs(halfway_rotation.z - death_start_rotation.z) + std::abs(halfway_rotation.w - death_start_rotation.w);
    CHECK(rotation_change > 0.1F);

    drive_frames(registry, 35);
    CHECK_FALSE(registry.valid(dying_enemy));
}

TEST_CASE("first-person arena headless: game over is exactly once, terminal, and releases cursor",
          "[runtime][codegen-entt][first-person-arena][game-over]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto player        = only_entity<main__Player>(registry);
    auto& player_transform   = registry.get<std_transform_volume__WorldTransform>(player);
    const auto enemy         = *registry.view<main__Enemy, std_transform_volume__WorldTransform>().begin();
    auto& enemy_transform    = registry.get<std_transform_volume__WorldTransform>(enemy);
    enemy_transform.position = player_transform.position;
    cactus_headless_test::drive_frame(registry, kFrameDt);

    const auto& state = registry.get<main__Player>(player);
    CHECK(state.game_over);
    CHECK(state.game_over_count == 1);
    CHECK_FALSE(cactus_raylib_fake::cursor_captured());
    const auto crosshair = only_entity<main__Crosshair>(registry);
    const auto label     = only_entity<main__GameOverLabel>(registry);
    CHECK_FALSE(registry.get<std_render_text__ScreenLabel>(crosshair).visible);
    CHECK(registry.get<std_render_text__ScreenLabel>(label).visible);
    CHECK(registry.get<std_render_text__ScreenLabel>(label).text == "GAME OVER");

    const Vector3 frozen_player   = player_transform.position;
    const Vector3 frozen_enemy    = enemy_transform.position;
    const auto frozen_enemy_count = count<main__Enemy>(registry);
    cactus_raylib_fake::set_key_down(KEY_W, true);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
    drive_frames(registry, 610);
    CHECK(horizontal_distance(player_transform.position, frozen_player) == Catch::Approx(0.0F));
    CHECK(horizontal_distance(enemy_transform.position, frozen_enemy) == Catch::Approx(0.0F));
    CHECK(count<main__Bullet>(registry) == 0);
    CHECK(count<main__Enemy>(registry) == frozen_enemy_count);
    CHECK(registry.get<main__Player>(player).game_over_count == 1);
}

TEST_CASE("first-person arena headless: camera world pose follows the player's position, yaw, and pitch",
          "[runtime][codegen-entt][first-person-arena][camera]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto player      = only_entity<main__Player>(registry);
    const auto camera      = only_entity<main__CameraRig>(registry);
    auto& player_transform = registry.get<std_transform_volume__WorldTransform>(player);
    auto& camera_state     = registry.get<std_camera_volume__FirstPersonCamera>(player);

    constexpr float kYaw      = 0.8F;
    constexpr float kPitch    = -0.3F;
    player_transform.position = Vector3{.x = 3.0F, .y = 0.9F, .z = -5.0F};
    player_transform.rotation = QuaternionFromEuler(0.0F, kYaw, 0.0F);
    camera_state.yaw          = kYaw;
    camera_state.pitch        = kPitch;
    cactus_headless_test::drive_frame(registry, kFrameDt);

    const auto& camera_transform     = registry.get<std_transform_volume__WorldTransform>(camera);
    constexpr float kCameraEyeHeight = 0.65F;
    CHECK(camera_transform.position.x == Catch::Approx(player_transform.position.x).margin(0.001F));
    CHECK(camera_transform.position.y == Catch::Approx(player_transform.position.y + kCameraEyeHeight).margin(0.001F));
    CHECK(camera_transform.position.z == Catch::Approx(player_transform.position.z).margin(0.001F));

    const auto expected_rotation =
        QuaternionMultiply(QuaternionFromEuler(0.0F, kYaw, 0.0F), QuaternionFromEuler(kPitch, 0.0F, 0.0F));
    const auto expected_forward =
        Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, QuaternionNormalize(expected_rotation));
    const auto actual_forward = Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 0.0F, .z = -1.0F},
                                                          QuaternionNormalize(camera_transform.rotation));
    CHECK(actual_forward.x == Catch::Approx(expected_forward.x).margin(0.01F));
    CHECK(actual_forward.y == Catch::Approx(expected_forward.y).margin(0.01F));
    CHECK(actual_forward.z == Catch::Approx(expected_forward.z).margin(0.01F));
}

TEST_CASE("first-person arena headless: unloading also releases cursor",
          "[runtime][codegen-entt][first-person-arena][cursor]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);
    REQUIRE(cactus_raylib_fake::cursor_captured());
    dispatch_unload(registry);
    CHECK_FALSE(cactus_raylib_fake::cursor_captured());
}

TEST_CASE("first-person arena headless: Escape releases cursor capture without ending play",
          "[runtime][codegen-entt][first-person-arena][cursor]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);
    REQUIRE(cactus_raylib_fake::cursor_captured());
    const auto player = only_entity<main__Player>(registry);
    REQUIRE(registry.get<main__Player>(player).cursor_captured);

    cactus_raylib_fake::set_key_pressed_this_frame(KEY_ESCAPE, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_key_pressed_this_frame(KEY_ESCAPE, false);

    CHECK_FALSE(cactus_raylib_fake::cursor_captured());
    CHECK_FALSE(registry.get<main__Player>(player).cursor_captured);
    CHECK_FALSE(registry.get<main__Player>(player).game_over);
}

TEST_CASE("first-person arena headless: a primary click while released recaptures the cursor without also firing",
          "[runtime][codegen-entt][first-person-arena][cursor]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);
    const auto player = only_entity<main__Player>(registry);

    cactus_raylib_fake::set_key_pressed_this_frame(KEY_ESCAPE, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_key_pressed_this_frame(KEY_ESCAPE, false);
    REQUIRE_FALSE(cactus_raylib_fake::cursor_captured());

    const auto bullets_before = count<main__Bullet>(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, false);

    CHECK(cactus_raylib_fake::cursor_captured());
    CHECK(registry.get<main__Player>(player).cursor_captured);
    CHECK(count<main__Bullet>(registry) == bullets_before);
}

TEST_CASE("first-person arena headless: the arena is lit by exactly one enabled directional sun light",
          "[runtime][codegen-entt][first-person-arena][lighting]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto lights = registry.view<std_render_meshes__DirectionalLight>();
    REQUIRE(lights.size() == 1);
    CHECK(registry.get<std_render_meshes__DirectionalLight>(*lights.begin()).enabled);
}

TEST_CASE("first-person arena headless: cursor_captured stays consistent with actual capture state across game over",
          "[runtime][codegen-entt][first-person-arena][cursor][game-over]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto player        = only_entity<main__Player>(registry);
    auto& player_transform   = registry.get<std_transform_volume__WorldTransform>(player);
    const auto enemy         = *registry.view<main__Enemy, std_transform_volume__WorldTransform>().begin();
    auto& enemy_transform    = registry.get<std_transform_volume__WorldTransform>(enemy);
    enemy_transform.position = player_transform.position;
    cactus_headless_test::drive_frame(registry, kFrameDt);

    REQUIRE(registry.get<main__Player>(player).game_over);
    CHECK_FALSE(cactus_raylib_fake::cursor_captured());
    CHECK_FALSE(registry.get<main__Player>(player).cursor_captured);
}

TEST_CASE("first-person arena headless: pressing R after game over fully restarts the arena",
          "[runtime][codegen-entt][first-person-arena][restart]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto player            = only_entity<main__Player>(registry);
    auto& player_transform       = registry.get<std_transform_volume__WorldTransform>(player);
    auto& camera_state           = registry.get<std_camera_volume__FirstPersonCamera>(player);
    const Vector3 spawn_position = player_transform.position;
    const Quat spawn_rotation    = player_transform.rotation;

    // Fire a bullet, then walk an enemy into the player to trigger game
    // over, so both a live Bullet and live Enemy entity exist at restart
    // time (bullets are never respawned, unlike enemies).
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, false);
    REQUIRE(count<main__Bullet>(registry) == 1);

    const auto pre_restart_enemy = *registry.view<main__Enemy, std_transform_volume__WorldTransform>().begin();
    registry.get<std_transform_volume__WorldTransform>(pre_restart_enemy).position = player_transform.position;
    cactus_headless_test::drive_frame(registry, kFrameDt);
    REQUIRE(registry.get<main__Player>(player).game_over);
    REQUIRE(registry.get<main__Player>(player).game_over_count == 1);

    // Disturb the player's transform/camera as if they'd wandered before
    // dying, proving restart actually resets it instead of it already
    // happening to sit at spawn.
    player_transform.position = Vector3{.x = 5.0F, .y = 2.0F, .z = -3.0F};
    player_transform.rotation = QuaternionFromEuler(0.0F, 1.0F, 0.0F);
    camera_state.yaw          = 1.0F;
    camera_state.pitch        = 0.4F;

    cactus_raylib_fake::set_key_pressed_this_frame(KEY_R, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_key_pressed_this_frame(KEY_R, false);

    const auto& state = registry.get<main__Player>(player);
    CHECK_FALSE(state.game_over);
    CHECK(state.game_over_count == 1);
    CHECK(state.cursor_captured);
    CHECK(cactus_raylib_fake::cursor_captured());
    CHECK(player_transform.position.x == Catch::Approx(spawn_position.x));
    CHECK(player_transform.position.y == Catch::Approx(spawn_position.y));
    CHECK(player_transform.position.z == Catch::Approx(spawn_position.z));
    CHECK(player_transform.rotation.x == Catch::Approx(spawn_rotation.x));
    CHECK(player_transform.rotation.y == Catch::Approx(spawn_rotation.y));
    CHECK(player_transform.rotation.z == Catch::Approx(spawn_rotation.z));
    CHECK(player_transform.rotation.w == Catch::Approx(spawn_rotation.w));
    CHECK(camera_state.yaw == Catch::Approx(0.0F));
    CHECK(camera_state.pitch == Catch::Approx(0.0F));

    // The specific pre-restart enemy is gone and no bullets remain. Enemy
    // spawn points also resume on the same immediate-first-wave schedule as
    // a fresh game start (their countdown resets to 0.0, same as game load),
    // so a full fresh wave already exists after this same restart frame.
    CHECK_FALSE(registry.valid(pre_restart_enemy));
    CHECK(count<main__Bullet>(registry) == 0);
    CHECK(count<main__Enemy>(registry) == 4);

    const auto crosshair = only_entity<main__Crosshair>(registry);
    const auto label     = only_entity<main__GameOverLabel>(registry);
    CHECK(registry.get<std_render_text__ScreenLabel>(crosshair).visible);
    CHECK_FALSE(registry.get<std_render_text__ScreenLabel>(label).visible);
}

TEST_CASE("first-person arena headless: R has no effect while the game is not over",
          "[runtime][codegen-entt][first-person-arena][restart]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kFrameDt);

    const auto player             = only_entity<main__Player>(registry);
    auto& player_transform        = registry.get<std_transform_volume__WorldTransform>(player);
    player_transform.position     = Vector3{.x = 5.0F, .y = 0.9F, .z = -3.0F};
    const auto enemy_count_before = count<main__Enemy>(registry);

    cactus_raylib_fake::set_key_pressed_this_frame(KEY_R, true);
    cactus_headless_test::drive_frame(registry, kFrameDt);
    cactus_raylib_fake::set_key_pressed_this_frame(KEY_R, false);

    CHECK_FALSE(registry.get<main__Player>(player).game_over);
    CHECK(player_transform.position.x == Catch::Approx(5.0F));
    CHECK(player_transform.position.z == Catch::Approx(-3.0F));
    CHECK(count<main__Enemy>(registry) == enemy_count_before);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
