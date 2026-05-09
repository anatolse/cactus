// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/cactus_runtime.hpp"

#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <vector>

using namespace cactus::runtime;

namespace fs = std::filesystem;

namespace {

constexpr auto kConstexprScalarLerp = stdlib::math::lerp(0.0F, 10.0F, 0.5F);
constexpr auto kConstexprVec2Lerp =
    stdlib::math::vec2::lerp(Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 20.0F}, 0.25F);
constexpr auto kConstexprVec3Cross =
    stdlib::math::vec3::cross(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
constexpr auto kConstexprQuatIdentity = stdlib::math::quat::identity();
constexpr auto kConstexprQuatMultiply = stdlib::math::quat::multiply(kConstexprQuatIdentity, kConstexprQuatIdentity);

static_assert(kConstexprScalarLerp == 5.0F);
static_assert(kConstexprVec2Lerp.x == 2.5F);
static_assert(kConstexprVec2Lerp.y == 5.0F);
static_assert(kConstexprVec3Cross.z == 1.0F);
static_assert(kConstexprQuatMultiply.w == 1.0F);
static_assert(noexcept(stdlib::math::lerp(0.0F, 1.0F, 0.5F)));
static_assert(noexcept(stdlib::math::clamp(0.0F, 0.0F, 1.0F)));
static_assert(noexcept(stdlib::math::vec2::dot(Vector2{.x = 1.0F, .y = 0.0F}, Vector2{.x = 0.0F, .y = 1.0F})));
static_assert(noexcept(stdlib::math::vec3::cross(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                                 Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F})));
static_assert(noexcept(stdlib::math::vec3::reflect(Vector3{.x = 1.0F, .y = -1.0F, .z = 0.0F},
                                                   Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F})));
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
    const auto norm2 = stdlib::math::vec2::normalize(Vector2{.x = 3.0F, .y = 4.0F});
    CHECK(norm2.x == Catch::Approx(0.6F));
    CHECK(norm2.y == Catch::Approx(0.8F));
    CHECK(stdlib::math::vec2::normalize(Vector2{0.0F, 0.0F}).x == Catch::Approx(0.0F));
    CHECK(stdlib::math::vec2::dot(Vector2{1.0F, 0.0F}, Vector2{0.0F, 1.0F}) == Catch::Approx(0.0F));
    const auto lerp2 = stdlib::math::vec2::lerp(Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 10.0F}, 0.5F);
    CHECK(lerp2.x == Catch::Approx(5.0F));
    CHECK(lerp2.y == Catch::Approx(5.0F));
    CHECK(stdlib::math::vec2::distance(Vector2{1.0F, 2.0F}, Vector2{4.0F, 6.0F}) == Catch::Approx(5.0F));
    CHECK(stdlib::math::vec2::angle(Vector2{0.0F, 1.0F}) == Catch::Approx(1.5707963F));

    CHECK(stdlib::math::vec3::length(Vector3{1.0F, 2.0F, 2.0F}) == Catch::Approx(3.0F));
    const auto norm3 = stdlib::math::vec3::normalize(Vector3{.x = 0.0F, .y = 3.0F, .z = 4.0F});
    CHECK(norm3.y == Catch::Approx(0.6F));
    CHECK(norm3.z == Catch::Approx(0.8F));
    CHECK(stdlib::math::vec3::dot(Vector3{1.0F, 2.0F, 3.0F}, Vector3{4.0F, -5.0F, 6.0F}) == Catch::Approx(12.0F));
    CHECK(stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F}).z == Catch::Approx(1.0F));
    const auto lerp3 = stdlib::math::vec3::lerp(
        Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 2.0F, .y = 4.0F, .z = 6.0F}, 0.5F);
    CHECK(lerp3.x == Catch::Approx(1.0F));
    CHECK(lerp3.y == Catch::Approx(2.0F));
    CHECK(lerp3.z == Catch::Approx(3.0F));
    CHECK(stdlib::math::vec3::distance(Vector3{1.0F, 2.0F, 3.0F}, Vector3{4.0F, 6.0F, 3.0F}) == Catch::Approx(5.0F));
    const auto reflected = stdlib::math::vec3::reflect(Vector3{.x = 1.0F, .y = -1.0F, .z = 0.0F},
                                                       Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
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

    const auto axis_angle =
        stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, std::numbers::pi_v<float>);
    const auto rotated = stdlib::math::quat::rotate(identity, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
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

TEST_CASE("Runtime stdlib: allocation-free helper contracts stay constexpr and noexcept",
          "[runtime][stdlib][contract]") {
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

TEST_CASE("Runtime stdlib: backend hierarchy runtime sources enforce pmr allocator discipline",
          "[runtime][hierarchy][pmr][review]") {
    const auto entt_runtime = read_text_file(repo_root() / "src/backends/cpp-entt/runtime.cpp");

    REQUIRE_FALSE(entt_runtime.empty());

    CHECK(entt_runtime.find("std::pmr::monotonic_buffer_resource scratch_resource") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::vector<entt::entity> active_entities") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::unsynchronized_pool_resource destroying_resource") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::vector<entt::entity> child_entities") != std::string::npos);
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
        return AssetRecord{
            .handle = handle, .kind = AssetKind::Mesh, .asset_id = "enemy", .runtime_id = 202, .materialized = true};
    });
    const auto lazy = registry.resolve(AssetKind::Mesh, 2U);
    CHECK(lazy.ready());
    CHECK(lazy.runtime_id == 202);

    const auto missing = registry.resolve(AssetKind::Material, 99U);
    CHECK_FALSE(missing.valid());
    CHECK(registry.missing_count() >= 1);
}

TEST_CASE("Runtime stdlib: EnTT mesh submission respects visibility and missing assets", "[runtime][assets][entt]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_mesh(21U, "mesh", 7);
    registry.register_material(22U, "mat", 8);

    cactus::runtime::entt_backend::reset_render_debug_state();
    cactus::runtime::entt_backend::submit_mesh(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                               stdlib::math::quat::identity(),
                                               Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                                               21U,
                                               22U,
                                               true,
                                               true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_meshes == 1);

    cactus::runtime::entt_backend::submit_mesh(Vector3{.x = 1.0F, .y = 2.0F, .z = 3.0F},
                                               stdlib::math::quat::identity(),
                                               Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                                               21U,
                                               22U,
                                               false,
                                               true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_meshes == 1);

    cactus::runtime::entt_backend::submit_mesh(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                               stdlib::math::quat::identity(),
                                               Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                                               999U,
                                               22U,
                                               true,
                                               true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().missing_assets >= 1);
}

TEST_CASE("Runtime stdlib: EnTT sprite submissions preserve layer ordering and default 2D camera fallback",
          "[runtime][assets][entt]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_texture(31U, "back", 31);
    registry.register_texture(32U, "front", 32);

    cactus::runtime::entt_backend::reset_render_debug_state();
    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_sprite(
        Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 1.0F, .y = 1.0F}, WHITE, 32U, true, 5);
    cactus::runtime::entt_backend::submit_sprite(
        Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 1.0F, .y = 1.0F}, WHITE, 31U, true, 1);
    cactus::runtime::entt_backend::end_render_frame();

    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_sprites == 2);
    CHECK(cactus::runtime::entt_backend::render_debug_state().used_default_2d_camera);
    REQUIRE(cactus::runtime::entt_backend::render_debug_state().drawn_sprite_layers.size() == 2);
    CHECK(cactus::runtime::entt_backend::render_debug_state().drawn_sprite_layers[0] == 1);
    CHECK(cactus::runtime::entt_backend::render_debug_state().drawn_sprite_layers[1] == 5);
}

TEST_CASE("Runtime stdlib: EnTT render frame marks default 3D camera for queued meshes", "[runtime][assets][entt]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_mesh(41U, "mesh", 41);
    registry.register_material(42U, "mat", 42);

    cactus::runtime::entt_backend::reset_render_debug_state();
    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_mesh(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                               stdlib::math::quat::identity(),
                                               Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                                               41U,
                                               42U,
                                               true,
                                               true);
    cactus::runtime::entt_backend::end_render_frame();

    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_meshes == 1);
    CHECK(cactus::runtime::entt_backend::render_debug_state().used_default_3d_camera);
}

TEST_CASE("Runtime stdlib: EnTT point lights participate in lit mesh frame state", "[runtime][assets][entt]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_mesh(51U, "blue_cube", 51);
    registry.register_material(52U, "blue_material", 52);

    cactus::runtime::entt_backend::reset_render_debug_state();
    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_mesh(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                               stdlib::math::quat::identity(),
                                               Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                                               51U,
                                               52U,
                                               true,
                                               true);
    cactus::runtime::entt_backend::register_point_light(
        Vector3{.x = -2.0F, .y = 1.0F, .z = 2.0F}, ORANGE, 1.4F, 8.0F, true);
    cactus::runtime::entt_backend::register_point_light(
        Vector3{.x = 2.0F, .y = 1.0F, .z = 2.0F}, SKYBLUE, 1.2F, 8.0F, true);
    cactus::runtime::entt_backend::register_point_light(
        Vector3{.x = 0.0F, .y = 3.0F, .z = 0.0F}, WHITE, 0.5F, 8.0F, false);
    cactus::runtime::entt_backend::end_render_frame();

    CHECK(cactus::runtime::entt_backend::render_debug_state().registered_point_lights == 2);
    CHECK(cactus::runtime::entt_backend::render_debug_state().active_point_lights == 2);
    CHECK(cactus::runtime::entt_backend::render_debug_state().used_lit_mesh_shader);
    CHECK(cactus::runtime::entt_backend::render_debug_state().used_default_3d_camera);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
