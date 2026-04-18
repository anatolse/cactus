#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "backends/cpp-manual/runtime.h"
#include "common/cactus_runtime.h"

#include <algorithm>
#include <optional>
#include <vector>

using namespace cactus::runtime;

TEST_CASE("Runtime stdlib: scalar and vector math helpers behave correctly", "[runtime][stdlib][math]") {
    CHECK(stdlib::math::lerp(0.0F, 10.0F, 0.5F) == Catch::Approx(5.0F));
    CHECK(stdlib::math::clamp(15.0F, 0.0F, 10.0F) == Catch::Approx(10.0F));
    CHECK(stdlib::math::vec2::length(Vector2{3.0F, 4.0F}) == Catch::Approx(5.0F));
    CHECK(stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F}).z == Catch::Approx(1.0F));
}

TEST_CASE("Runtime stdlib: quaternion helpers behave correctly", "[runtime][stdlib][quat]") {
    const auto identity = stdlib::math::quat::identity();
    CHECK(identity.x == Catch::Approx(0.0F));
    CHECK(identity.y == Catch::Approx(0.0F));
    CHECK(identity.z == Catch::Approx(0.0F));
    CHECK(identity.w == Catch::Approx(1.0F));

    const auto forward = stdlib::math::quat::forward(identity);
    CHECK(forward.x == Catch::Approx(0.0F));
    CHECK(forward.y == Catch::Approx(0.0F));
    CHECK(forward.z == Catch::Approx(-1.0F));
}

TEST_CASE("Runtime stdlib: manual hierarchy propagation and recursive destroy work", "[runtime][hierarchy][manual]") {
    struct Transform2D {
        Vector2 position{};
        float rotation{};
        Vector2 scale{1.0F, 1.0F};
    };

    std::vector<Transform2D> local(2);
    std::vector<Transform2D> world(2);
    local[0].position = {1.0F, 2.0F};
    local[1].position = {3.0F, 4.0F};

    cactus::runtime::manual_backend::propagate_hierarchy(
        2,
        [&](std::size_t entity) -> std::optional<std::size_t> {
            if (entity == 1) {
                return 0;
            }
            return std::nullopt;
        },
        [&](std::size_t entity) { world[entity] = local[entity]; },
        [&](std::size_t parent, std::size_t child) {
            world[child] = local[child];
            world[child].position.x += world[parent].position.x;
            world[child].position.y += world[parent].position.y;
        });

    CHECK(world[0].position.x == Catch::Approx(1.0F));
    CHECK(world[0].position.y == Catch::Approx(2.0F));
    CHECK(world[1].position.x == Catch::Approx(4.0F));
    CHECK(world[1].position.y == Catch::Approx(6.0F));

    std::vector<std::size_t> removed;
    std::vector<bool> alive = {true, true, true};
    cactus::runtime::manual_backend::destroy_entity_recursive(
        0,
        [&](std::size_t entity) { return entity < alive.size() && alive[entity]; },
        [&](std::size_t parent, const auto& visitor) {
            if (parent == 0) {
                visitor(1);
                visitor(2);
            }
        },
        [&](std::size_t entity) {
            alive[entity] = false;
            removed.push_back(entity);
        });

    CHECK(removed.size() == 3);
    CHECK(std::find(removed.begin(), removed.end(), 0) != removed.end());
    CHECK(std::find(removed.begin(), removed.end(), 1) != removed.end());
    CHECK(std::find(removed.begin(), removed.end(), 2) != removed.end());
}