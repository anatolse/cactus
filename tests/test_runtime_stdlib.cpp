#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "backends/cpp-entt/runtime.h"
#include "backends/cpp-manual/runtime.h"
#include "common/cactus_runtime.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

using namespace cactus::runtime;

namespace fs = std::filesystem;

namespace {

constexpr auto kConstexprScalarLerp = stdlib::math::lerp(0.0F, 10.0F, 0.5F);
constexpr auto kConstexprVec2Lerp = stdlib::math::vec2::lerp(Vector2{0.0F, 0.0F}, Vector2{10.0F, 20.0F}, 0.25F);
constexpr auto kConstexprVec3Cross = stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F});
constexpr auto kConstexprQuatIdentity = stdlib::math::quat::identity();
constexpr auto kConstexprQuatMultiply = stdlib::math::quat::multiply(kConstexprQuatIdentity, kConstexprQuatIdentity);

static_assert(kConstexprScalarLerp == 5.0F);
static_assert(kConstexprVec2Lerp.x == 2.5F);
static_assert(kConstexprVec2Lerp.y == 5.0F);
static_assert(kConstexprVec3Cross.z == 1.0F);
static_assert(kConstexprQuatMultiply.w == 1.0F);
static_assert(noexcept(stdlib::math::lerp(0.0F, 1.0F, 0.5F)));
static_assert(noexcept(stdlib::math::clamp(0.0F, 0.0F, 1.0F)));
static_assert(noexcept(stdlib::math::vec2::dot(Vector2{1.0F, 0.0F}, Vector2{0.0F, 1.0F})));
static_assert(noexcept(stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F})));
static_assert(noexcept(stdlib::math::vec3::reflect(Vector3{1.0F, -1.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F})));
static_assert(noexcept(stdlib::math::quat::identity()));
static_assert(noexcept(stdlib::math::quat::multiply(kConstexprQuatIdentity, kConstexprQuatIdentity)));

fs::path repo_root() {
    return {CACTUS_TEST_SOURCE_DIR};
}

std::string read_text_file(const fs::path& path) {
    std::ifstream ifs(path);
    std::ostringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

}  // namespace

namespace cactus::runtime::manual_backend {

int cactus_input_button_key(std::uint8_t /*button*/) noexcept {
    return 0;
}

float cactus_input_axis_value(std::uint8_t /*action*/) noexcept {
    return 0.0F;
}

}  // namespace cactus::runtime::manual_backend

TEST_CASE("Runtime stdlib: scalar and vector math helpers behave correctly", "[runtime][stdlib][math]") {
    CHECK(stdlib::math::lerp(0.0F, 10.0F, 0.5F) == Catch::Approx(5.0F));
    CHECK(stdlib::math::clamp(15.0F, 0.0F, 10.0F) == Catch::Approx(10.0F));
    CHECK(stdlib::math::abs(-3.5F) == Catch::Approx(3.5F));
    CHECK(stdlib::math::min(2.0F, 5.0F) == Catch::Approx(2.0F));
    CHECK(stdlib::math::max(2.0F, 5.0F) == Catch::Approx(5.0F));
    CHECK(stdlib::math::sqrt(25.0F) == Catch::Approx(5.0F));
    CHECK(stdlib::math::sin(0.0F) == Catch::Approx(0.0F));
    CHECK(stdlib::math::cos(0.0F) == Catch::Approx(1.0F));
    CHECK(stdlib::math::atan2(1.0F, 0.0F) == Catch::Approx(1.5707963F));
    CHECK(stdlib::math::floor(3.7F) == 3);
    CHECK(stdlib::math::ceil(3.2F) == 4);
    CHECK(stdlib::math::round(3.6F) == 4);
    CHECK(stdlib::math::pow(2.0F, 3.0F) == Catch::Approx(8.0F));

    CHECK(stdlib::math::vec2::length(Vector2{3.0F, 4.0F}) == Catch::Approx(5.0F));
    const auto norm2 = stdlib::math::vec2::normalize(Vector2{3.0F, 4.0F});
    CHECK(norm2.x == Catch::Approx(0.6F));
    CHECK(norm2.y == Catch::Approx(0.8F));
    CHECK(stdlib::math::vec2::normalize(Vector2{0.0F, 0.0F}).x == Catch::Approx(0.0F));
    CHECK(stdlib::math::vec2::dot(Vector2{1.0F, 0.0F}, Vector2{0.0F, 1.0F}) == Catch::Approx(0.0F));
    const auto lerp2 = stdlib::math::vec2::lerp(Vector2{0.0F, 0.0F}, Vector2{10.0F, 10.0F}, 0.5F);
    CHECK(lerp2.x == Catch::Approx(5.0F));
    CHECK(lerp2.y == Catch::Approx(5.0F));
    CHECK(stdlib::math::vec2::distance(Vector2{1.0F, 2.0F}, Vector2{4.0F, 6.0F}) == Catch::Approx(5.0F));
    CHECK(stdlib::math::vec2::angle(Vector2{0.0F, 1.0F}) == Catch::Approx(1.5707963F));

    CHECK(stdlib::math::vec3::length(Vector3{1.0F, 2.0F, 2.0F}) == Catch::Approx(3.0F));
    const auto norm3 = stdlib::math::vec3::normalize(Vector3{0.0F, 3.0F, 4.0F});
    CHECK(norm3.y == Catch::Approx(0.6F));
    CHECK(norm3.z == Catch::Approx(0.8F));
    CHECK(stdlib::math::vec3::dot(Vector3{1.0F, 2.0F, 3.0F}, Vector3{4.0F, -5.0F, 6.0F}) == Catch::Approx(12.0F));
    CHECK(stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F}).z == Catch::Approx(1.0F));
    const auto lerp3 = stdlib::math::vec3::lerp(Vector3{0.0F, 0.0F, 0.0F}, Vector3{2.0F, 4.0F, 6.0F}, 0.5F);
    CHECK(lerp3.x == Catch::Approx(1.0F));
    CHECK(lerp3.y == Catch::Approx(2.0F));
    CHECK(lerp3.z == Catch::Approx(3.0F));
    CHECK(stdlib::math::vec3::distance(Vector3{1.0F, 2.0F, 3.0F}, Vector3{4.0F, 6.0F, 3.0F}) == Catch::Approx(5.0F));
    const auto reflected = stdlib::math::vec3::reflect(Vector3{1.0F, -1.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F});
    CHECK(reflected.x == Catch::Approx(1.0F));
    CHECK(reflected.y == Catch::Approx(1.0F));
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

    const auto right = stdlib::math::quat::right(identity);
    CHECK(right.x == Catch::Approx(1.0F));
    CHECK(right.y == Catch::Approx(0.0F));
    CHECK(right.z == Catch::Approx(0.0F));

    const auto up = stdlib::math::quat::up(identity);
    CHECK(up.x == Catch::Approx(0.0F));
    CHECK(up.y == Catch::Approx(1.0F));
    CHECK(up.z == Catch::Approx(0.0F));

    const auto axis_angle = stdlib::math::quat::from_axis_angle(Vector3{0.0F, 1.0F, 0.0F}, 3.14159265F);
    const auto rotated = stdlib::math::quat::rotate(identity, Vector3{1.0F, 0.0F, 0.0F});
    CHECK(rotated.x == Catch::Approx(1.0F));
    CHECK(rotated.y == Catch::Approx(0.0F));
    CHECK(rotated.z == Catch::Approx(0.0F));

    const auto halfway = stdlib::math::quat::slerp(identity, axis_angle, 0.5F);
    CHECK(halfway.w != Catch::Approx(identity.w));

    const auto combined = stdlib::math::quat::multiply(identity, axis_angle);
    CHECK(combined.w == Catch::Approx(axis_angle.w));

    const auto inv = stdlib::math::quat::inverse(identity);
    CHECK(inv.x == Catch::Approx(0.0F));
    CHECK(inv.y == Catch::Approx(0.0F));
    CHECK(inv.z == Catch::Approx(0.0F));
    CHECK(inv.w == Catch::Approx(1.0F));
}

TEST_CASE("Runtime stdlib: allocation-free helper contracts stay constexpr and noexcept", "[runtime][stdlib][contract]") {
    CHECK(kConstexprScalarLerp == Catch::Approx(5.0F));
    CHECK(kConstexprVec2Lerp.x == Catch::Approx(2.5F));
    CHECK(kConstexprVec2Lerp.y == Catch::Approx(5.0F));
    CHECK(kConstexprVec3Cross.z == Catch::Approx(1.0F));
    CHECK(kConstexprQuatMultiply.w == Catch::Approx(1.0F));
    CHECK(noexcept(stdlib::math::lerp(0.0F, 1.0F, 0.5F)));
    CHECK(noexcept(stdlib::math::vec2::lerp(Vector2{0.0F, 0.0F}, Vector2{1.0F, 1.0F}, 0.5F)));
    CHECK(noexcept(stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F})));
    CHECK(noexcept(stdlib::math::quat::identity()));
    CHECK(noexcept(stdlib::math::quat::multiply(stdlib::math::quat::identity(), stdlib::math::quat::identity())));
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

TEST_CASE("Runtime stdlib: backend hierarchy runtime sources enforce pmr allocator discipline", "[runtime][hierarchy][pmr][review]") {
    const auto entt_runtime = read_text_file(repo_root() / "src/backends/cpp-entt/runtime.cpp");
    const auto manual_runtime = read_text_file(repo_root() / "src/backends/cpp-manual/runtime.cpp");

    REQUIRE_FALSE(entt_runtime.empty());
    REQUIRE_FALSE(manual_runtime.empty());

    CHECK(entt_runtime.find("std::pmr::monotonic_buffer_resource scratch_resource") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::vector<entt::entity> active_entities") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::unsynchronized_pool_resource destroying_resource") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::vector<entt::entity> child_entities") != std::string::npos);

    CHECK(manual_runtime.find("std::pmr::monotonic_buffer_resource scratch_resource") != std::string::npos);
    CHECK(manual_runtime.find("std::pmr::vector<std::uint8_t> active") != std::string::npos);
    CHECK(manual_runtime.find("std::pmr::unsynchronized_pool_resource destroying_resource") != std::string::npos);
    CHECK(manual_runtime.find("std::pmr::vector<std::size_t> destroying_entities") != std::string::npos);
}

TEST_CASE("Runtime stdlib: shared asset registry supports eager and lazy resolution", "[runtime][assets]") {
    auto& registry = shared_asset_registry();
    registry.clear();

    registry.register_texture(1U, "hero", 101);
    const auto eager = registry.resolve(AssetKind::Texture, 1U);
    CHECK(eager.ready());
    CHECK(eager.runtime_id == 101);

    registry.set_lazy_resolver(AssetKind::Mesh, [](AssetHandle handle) -> std::optional<AssetRecord> {
        if (handle != 2U) {
            return std::nullopt;
        }
        return AssetRecord{.handle = handle, .kind = AssetKind::Mesh, .asset_id = "enemy", .runtime_id = 202, .materialized = true};
    });
    const auto lazy = registry.resolve(AssetKind::Mesh, 2U);
    CHECK(lazy.ready());
    CHECK(lazy.runtime_id == 202);

    const auto missing = registry.resolve(AssetKind::Material, 99U);
    CHECK_FALSE(missing.valid());
    CHECK(registry.missing_count() >= 1);
}

TEST_CASE("Runtime stdlib: manual backend asset adapters record submissions and misses", "[runtime][assets][manual]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_texture(11U, "sprite", 1);
    registry.register_mesh(12U, "mesh", 2);
    registry.register_material(13U, "mat", 3);

    cactus::runtime::manual_backend::reset_render_debug_state();
    cactus::runtime::manual_backend::submit_sprite(Vector2{0.0F, 0.0F}, Vector2{1.0F, 1.0F}, WHITE, 11U, true);
    cactus::runtime::manual_backend::submit_mesh(Vector3{0.0F, 0.0F, 0.0F}, stdlib::math::quat::identity(), Vector3{1.0F, 1.0F, 1.0F}, 12U, 13U, true, true);
    cactus::runtime::manual_backend::register_point_light(Vector3{0.0F, 1.0F, 0.0F}, WHITE, 1.0F, 10.0F, true);
    CHECK(cactus::runtime::manual_backend::render_debug_state().submitted_sprites == 1);
    CHECK(cactus::runtime::manual_backend::render_debug_state().submitted_meshes == 1);
    CHECK(cactus::runtime::manual_backend::render_debug_state().registered_point_lights == 1);

    cactus::runtime::manual_backend::submit_sprite(Vector2{0.0F, 0.0F}, Vector2{1.0F, 1.0F}, WHITE, 999U, true);
    CHECK(cactus::runtime::manual_backend::render_debug_state().missing_assets >= 1);
}

TEST_CASE("Runtime stdlib: EnTT mesh submission respects visibility and missing assets", "[runtime][assets][entt]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_mesh(21U, "mesh", 7);
    registry.register_material(22U, "mat", 8);

    cactus::runtime::entt_backend::reset_render_debug_state();
    cactus::runtime::entt_backend::submit_mesh(
        Vector3{0.0F, 0.0F, 0.0F},
        stdlib::math::quat::identity(),
        Vector3{1.0F, 1.0F, 1.0F},
        21U,
        22U,
        true,
        true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_meshes == 1);

    cactus::runtime::entt_backend::submit_mesh(
        Vector3{1.0F, 2.0F, 3.0F},
        stdlib::math::quat::identity(),
        Vector3{1.0F, 1.0F, 1.0F},
        21U,
        22U,
        false,
        true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_meshes == 1);

    cactus::runtime::entt_backend::submit_mesh(
        Vector3{0.0F, 0.0F, 0.0F},
        stdlib::math::quat::identity(),
        Vector3{1.0F, 1.0F, 1.0F},
        999U,
        22U,
        true,
        true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().missing_assets >= 1);
}