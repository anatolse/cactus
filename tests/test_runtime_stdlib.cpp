// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/cactus_runtime.hpp"

#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
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
    CHECK(entt_runtime.find("std::pmr::unordered_set<entt::entity> active_entities") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::unsynchronized_pool_resource destroying_resource") != std::string::npos);
    CHECK(entt_runtime.find("std::pmr::unordered_set<entt::entity> destroying_entities") != std::string::npos);
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

TEST_CASE("Runtime stdlib: shared asset registry resolves fake model records through the shared contract",
          "[runtime][assets][dsl-model-assets]") {
    auto& registry = shared_asset_registry();
    registry.clear();

    // Eager fake record (test seam): no filesystem access involved.
    registry.register_model(11U, "art/robot.glb", 501);
    const auto eager = registry.resolve(AssetKind::Model, 11U);
    CHECK(eager.ready());
    CHECK(eager.runtime_id == 501);
    CHECK(eager.asset_id == "art/robot.glb");

    // Registered-but-unmaterialized record mirrors lazy-load registration.
    registry.register_model(12U, "art/player.glb", 502, false);
    const auto registered = registry.resolve(AssetKind::Model, 12U);
    CHECK(registered.valid());
    CHECK_FALSE(registered.ready());
    CHECK(registered.runtime_id == 502);

    // Lazy resolver slot works for models like other kinds.
    registry.set_lazy_resolver(AssetKind::Model, [](AssetHandle handle) -> std::optional<AssetRecord> {
        if (handle != 13U) {
            return std::nullopt;
        }
        return AssetRecord{
            .handle = handle, .kind = AssetKind::Model, .asset_id = "lazy", .runtime_id = 503, .materialized = true};
    });
    const auto lazy = registry.resolve(AssetKind::Model, 13U);
    CHECK(lazy.ready());
    CHECK(lazy.runtime_id == 503);

    // Missing model handles follow the defined diagnostic path.
    const auto missing = registry.resolve(AssetKind::Model, 99U);
    CHECK_FALSE(missing.valid());
    CHECK(registry.missing_count() >= 1);
    registry.clear();
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

TEST_CASE("Runtime stdlib: EnTT model submissions count visible entities and respect visibility",
          "[runtime][assets][entt][dsl-model-assets]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_model(61U, "art/robot.glb", 61);

    cactus::runtime::entt_backend::reset_render_debug_state();
    const auto identity = stdlib::math::quat::identity();
    const auto origin   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const auto unit     = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F};

    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 61U, true, true);
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 61U, true, true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_models == 2);

    // Invisible models are not submitted.
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 61U, false, true);
    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_models == 2);
}

TEST_CASE("Runtime stdlib: EnTT missing model file skips draw without placeholder and reports one diagnostic",
          "[runtime][assets][entt][dsl-model-assets]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    registry.register_model(71U, "does/not/exist.glb", 71);

    cactus::runtime::entt_backend::reset_render_debug_state();
    const auto identity = stdlib::math::quat::identity();
    const auto origin   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const auto unit     = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F};

    // Repeated frames must not repeat the diagnostic or retry the load.
    for (int frame = 0; frame < 3; ++frame) {
        cactus::runtime::entt_backend::begin_render_frame();
        cactus::runtime::entt_backend::submit_model(origin, identity, unit, 71U, true, true);
        cactus::runtime::entt_backend::end_render_frame();
    }

    const auto& debug = cactus::runtime::entt_backend::render_debug_state();
    CHECK(debug.submitted_models == 3);
    CHECK(debug.drawn_models == 0);
    REQUIRE(debug.model_diagnostics.size() == 1);
    CHECK(debug.model_diagnostics[0].find("does/not/exist.glb") != std::string::npos);
}

TEST_CASE("Runtime stdlib: EnTT unregistered model handle reports one diagnostic across submissions",
          "[runtime][assets][entt][dsl-model-assets]") {
    auto& registry = shared_asset_registry();
    registry.clear();

    cactus::runtime::entt_backend::reset_render_debug_state();
    const auto identity = stdlib::math::quat::identity();
    const auto origin   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const auto unit     = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F};

    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 999U, true, true);
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 999U, true, true);

    const auto& debug = cactus::runtime::entt_backend::render_debug_state();
    CHECK(debug.submitted_models == 0);
    CHECK(debug.missing_assets >= 2);
    CHECK(debug.model_diagnostics.size() == 1);
}

TEST_CASE("Runtime stdlib: EnTT animation introspection stays total on fake records and missing handles",
          "[runtime][assets][entt][dsl-model-animation]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();

    // Unregistered handle: count 0, empty name, no crash.
    CHECK(cactus::runtime::entt_backend::model_animation_count(999U) == 0);
    CHECK(cactus::runtime::entt_backend::model_animation_name(999U, 0).empty());
    CHECK(cactus::runtime::entt_backend::model_animation_duration(999U, 0) == 0.0F);

    // Fake record (test seam) pointing at a missing file: same degradation.
    registry.register_model(81U, "does/not/exist.glb", 81);
    CHECK(cactus::runtime::entt_backend::model_animation_count(81U) == 0);
    CHECK(cactus::runtime::entt_backend::model_animation_name(81U, 0).empty());
    CHECK(cactus::runtime::entt_backend::model_animation_duration(81U, 0) == 0.0F);
    registry.clear();
}

TEST_CASE("Runtime stdlib: EnTT animation introspection reads real GLB clips before first draw",
          "[runtime][assets][entt][dsl-model-animation]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();
    const auto robot_path = (repo_root() / "examples/model-renderer/art/robot.glb").string();
    REQUIRE(fs::exists(robot_path));
    registry.register_model(82U, robot_path, 82);

    // No begin/end render frame has run: introspection triggers the lazy load
    // itself (animation data is CPU-side, so this works headless too).
    const int count = cactus::runtime::entt_backend::model_animation_count(82U);
    CHECK(count == 14);
    const auto first_clip = cactus::runtime::entt_backend::model_animation_name(82U, 0);
    CHECK_FALSE(first_clip.empty());
    CHECK(first_clip.rfind("Robot_", 0) == 0);
    CHECK(cactus::runtime::entt_backend::model_animation_duration(82U, 0) > 0.0F);

    // Out-of-range indices degrade to empty/zero, never crash.
    CHECK(cactus::runtime::entt_backend::model_animation_name(82U, count).empty());
    CHECK(cactus::runtime::entt_backend::model_animation_name(82U, -1).empty());
    CHECK(cactus::runtime::entt_backend::model_animation_duration(82U, count) == 0.0F);
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();
}

TEST_CASE("Runtime stdlib: EnTT invalid animation clip degrades to bind pose with one diagnostic per (asset, clip)",
          "[runtime][assets][entt][dsl-model-animation]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();
    const auto robot_path = (repo_root() / "examples/model-renderer/art/robot.glb").string();
    registry.register_model(83U, robot_path, 83);

    const auto identity = stdlib::math::quat::identity();
    const auto origin   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const auto unit     = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F};

    // Repeated frames with the same out-of-range clip: exactly one diagnostic.
    for (int frame = 0; frame < 3; ++frame) {
        cactus::runtime::entt_backend::begin_render_frame();
        cactus::runtime::entt_backend::submit_model(origin, identity, unit, 83U, true, true, 99, 0.0F);
        cactus::runtime::entt_backend::end_render_frame();
    }
    const auto& debug = cactus::runtime::entt_backend::render_debug_state();
    REQUIRE(debug.model_diagnostics.size() == 1);
    CHECK(debug.model_diagnostics[0].find("invalid animation clip 99") != std::string::npos);

    // A different invalid clip on the same asset gets its own diagnostic.
    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 83U, true, true, -1, 0.0F);
    cactus::runtime::entt_backend::end_render_frame();
    CHECK(debug.model_diagnostics.size() == 2);

    // A valid clip produces no diagnostic.
    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 83U, true, true, 0, 0.25F);
    cactus::runtime::entt_backend::end_render_frame();
    CHECK(debug.model_diagnostics.size() == 2);
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();
}

TEST_CASE("Runtime stdlib: EnTT animator on a clip-less model reports a single bind-pose diagnostic",
          "[runtime][assets][entt][dsl-model-animation]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();

    // A present file that carries no animation clips (junk .glb: the loader
    // yields zero clips, mirroring an animation-less model).
    const auto junk_path = fs::temp_directory_path() / "cactus_test_clipless.glb";
    {
        std::ofstream junk(junk_path);
        junk << "not a model";
    }
    registry.register_model(84U, junk_path.string(), 84);

    const auto identity = stdlib::math::quat::identity();
    const auto origin   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const auto unit     = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F};

    for (int frame = 0; frame < 3; ++frame) {
        cactus::runtime::entt_backend::begin_render_frame();
        cactus::runtime::entt_backend::submit_model(origin, identity, unit, 84U, true, true, 0, 0.0F);
        cactus::runtime::entt_backend::end_render_frame();
    }

    const auto& debug = cactus::runtime::entt_backend::render_debug_state();
    REQUIRE(debug.model_diagnostics.size() == 1);
    CHECK(debug.model_diagnostics[0].find("invalid animation clip 0 (model has 0)") != std::string::npos);
    fs::remove(junk_path);
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();
}

TEST_CASE("Runtime stdlib: EnTT entities sharing a model asset submit independent animator poses",
          "[runtime][assets][entt][dsl-model-animation]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();
    registry.register_model(85U, "art/robot.glb", 85);

    const auto identity = stdlib::math::quat::identity();
    const auto origin   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const auto unit     = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F};

    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 85U, true, true, 0, 0.0F);
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 85U, true, true, 3, 0.5F);
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 85U, true, true, 7, 1.25F);
    // A fourth entity without ModelAnimator shares the asset at bind pose.
    cactus::runtime::entt_backend::submit_model(origin, identity, unit, 85U, true, true);

    const auto& debug = cactus::runtime::entt_backend::render_debug_state();
    CHECK(debug.submitted_models == 4);
    REQUIRE(debug.animated_model_submissions.size() == 3);
    CHECK(debug.animated_model_submissions[0].clip == 0);
    CHECK(debug.animated_model_submissions[0].time == Catch::Approx(0.0F));
    CHECK(debug.animated_model_submissions[1].clip == 3);
    CHECK(debug.animated_model_submissions[1].time == Catch::Approx(0.5F));
    CHECK(debug.animated_model_submissions[2].clip == 7);
    CHECK(debug.animated_model_submissions[2].time == Catch::Approx(1.25F));
    cactus::runtime::entt_backend::end_render_frame();

    // The per-frame pose record clears with the other render queues.
    cactus::runtime::entt_backend::begin_render_frame();
    CHECK(debug.animated_model_submissions.empty());
    cactus::runtime::entt_backend::end_render_frame();
    registry.clear();
}

TEST_CASE("Runtime stdlib: EnTT screen label submissions count visible labels only",
          "[runtime][assets][entt][dsl-model-animation]") {
    cactus::runtime::entt_backend::reset_render_debug_state();

    cactus::runtime::entt_backend::begin_render_frame();
    cactus::runtime::entt_backend::submit_screen_label(
        Vector2{.x = 16.0F, .y = 16.0F}, 32, WHITE, "Robot 1 - Idle", true);
    cactus::runtime::entt_backend::submit_screen_label(
        Vector2{.x = 16.0F, .y = 48.0F}, 32, WHITE, "hidden", false);
    cactus::runtime::entt_backend::submit_screen_label(
        Vector2{.x = 16.0F, .y = 80.0F}, 24, WHITE, "second", true);
    cactus::runtime::entt_backend::end_render_frame();

    CHECK(cactus::runtime::entt_backend::render_debug_state().submitted_screen_labels == 2);
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

// ── std.random tests ──────────────────────────────────────────────────────────

TEST_CASE("Runtime stdlib: seeded is deterministic", "[runtime][stdlib][random]") {
    const auto a = stdlib::random::seeded(42);
    const auto b = stdlib::random::seeded(42);
    CHECK(a.state == b.state);

    const auto c = stdlib::random::seeded(99);
    const auto d = stdlib::random::seeded(99);
    CHECK(c.state == d.state);

    // Different seeds should (almost certainly) produce different states.
    CHECK(a.state != c.state);
}

TEST_CASE("Runtime stdlib: advance is deterministic", "[runtime][stdlib][random]") {
    const auto rng = stdlib::random::seeded(1);

    const auto a = stdlib::random::advance(rng);
    const auto b = stdlib::random::advance(rng);
    CHECK(a.state == b.state);

    // Advance must change the state.
    CHECK(a.state != rng.state);
}

TEST_CASE("Runtime stdlib: sample is in [lo, hi)", "[runtime][stdlib][random]") {
    const auto dist = stdlib::random::uniform(0.0F, 10.0F);
    auto rng        = stdlib::random::seeded(7);

    for (int i = 0; i < 200; ++i) {
        rng          = stdlib::random::advance(rng);
        const float v = stdlib::random::sample(rng, dist);
        CHECK(v >= 0.0F);
        CHECK(v < 10.0F);
    }
}

TEST_CASE("Runtime stdlib: sample_int is in [lo, hi] inclusive", "[runtime][stdlib][random]") {
    const auto dist = stdlib::random::uniform_int(1, 6);
    auto rng        = stdlib::random::seeded(13);

    std::array<bool, 7> seen{};
    for (int i = 0; i < 500; ++i) {
        rng         = stdlib::random::advance(rng);
        const int v = stdlib::random::sample_int(rng, dist);
        CHECK(v >= 1);
        CHECK(v <= 6);
        if (v >= 1 && v <= 6) {
            seen[static_cast<std::size_t>(v)] = true;
        }
    }
    for (int k = 1; k <= 6; ++k) {
        CHECK(seen[static_cast<std::size_t>(k)]);
    }
}

TEST_CASE("Runtime stdlib: chance boundaries", "[runtime][stdlib][random]") {
    const auto rng = stdlib::random::seeded(0);
    CHECK(stdlib::random::chance(rng, 0.0F) == false);
    CHECK(stdlib::random::chance(rng, 1.0F) == true);
}

TEST_CASE("Runtime stdlib: sequence reproducibility", "[runtime][stdlib][random]") {
    auto rng_a = stdlib::random::seeded(5);
    auto rng_b = stdlib::random::seeded(5);

    const auto dist = stdlib::random::uniform(0.0F, 1.0F);
    for (int i = 0; i < 20; ++i) {
        rng_a           = stdlib::random::advance(rng_a);
        rng_b           = stdlib::random::advance(rng_b);
        const float va  = stdlib::random::sample(rng_a, dist);
        const float vb  = stdlib::random::sample(rng_b, dist);
        CHECK(va == Catch::Approx(vb));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
