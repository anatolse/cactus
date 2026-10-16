// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace backend = cactus::runtime::entt_backend;

// A 10x10 box with its corner at (5000, 5000), far from the example's own entities.
entt::entity make_box(entt::registry& registry) {
    const auto entity = registry.create();
    registry.emplace<backend::CreationOrdinal>(entity,
                                               backend::CreationOrdinal{.value = backend::generated_next_creation_ordinal()});
    registry.emplace<std_transform_flat__WorldTransform>(
        entity, std_transform_flat__WorldTransform{.position = {.x = 5000.0F, .y = 5000.0F}});
    registry.emplace<std_physics_flat__BoxCollider>(entity, std_physics_flat__BoxCollider{.size = {.x = 10.0F, .y = 10.0F}});
    return entity;
}

}  // namespace

TEST_CASE("editor hit_test_2d gives overlapping boxes to the oldest", "[runtime][editor][native-result-ordering]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    backend::set_active_camera_2d(
        Camera2D{.offset = {.x = 0.0F, .y = 0.0F}, .target = {.x = 5000.0F, .y = 5000.0F}, .rotation = 0.0F, .zoom = 1.0F});

    const auto older = make_box(registry);
    make_box(registry);

    CHECK(backend::editor_hit_test_2d(registry, Vector2{.x = 5.0F, .y = 5.0F}, 1) == older);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
