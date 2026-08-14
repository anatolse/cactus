// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/cactus_runtime.hpp"

#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
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
constexpr auto kConstexprVec2Clamp =
    stdlib::math::vec2::clamp(Vector2{.x = 15.0F, .y = -5.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 10.0F});
constexpr auto kConstexprVec3Clamp = stdlib::math::vec3::clamp(Vector3{.x = 15.0F, .y = -5.0F, .z = 2.0F},
                                                                Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                                Vector3{.x = 10.0F, .y = 10.0F, .z = 10.0F});
constexpr auto kConstexprQuatIdentity = stdlib::math::quat::identity();
constexpr auto kConstexprQuatMultiply = stdlib::math::quat::multiply(kConstexprQuatIdentity, kConstexprQuatIdentity);

static_assert(kConstexprScalarLerp == 5.0F);
static_assert(kConstexprVec2Lerp.x == 2.5F);
static_assert(kConstexprVec2Lerp.y == 5.0F);
static_assert(kConstexprVec3Cross.z == 1.0F);
static_assert(kConstexprVec2Clamp.x == 10.0F);
static_assert(kConstexprVec2Clamp.y == 0.0F);
static_assert(kConstexprVec3Clamp.z == 2.0F);
static_assert(kConstexprQuatMultiply.w == 1.0F);
static_assert(noexcept(stdlib::math::lerp(0.0F, 1.0F, 0.5F)));
static_assert(noexcept(stdlib::math::clamp(0.0F, 0.0F, 1.0F)));
static_assert(noexcept(stdlib::math::vec2::dot(Vector2{.x = 1.0F, .y = 0.0F}, Vector2{.x = 0.0F, .y = 1.0F})));
static_assert(noexcept(stdlib::math::vec2::clamp(Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 0.0F, .y = 0.0F},
                                                  Vector2{.x = 1.0F, .y = 1.0F})));
static_assert(noexcept(stdlib::math::vec3::cross(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                                 Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F})));
static_assert(noexcept(stdlib::math::vec3::reflect(Vector3{.x = 1.0F, .y = -1.0F, .z = 0.0F},
                                                   Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F})));
static_assert(noexcept(stdlib::math::vec3::clamp(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F})));
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
    const auto clamped2 =
        stdlib::math::vec2::clamp(Vector2{.x = 15.0F, .y = -5.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 10.0F});
    CHECK(clamped2.x == Catch::Approx(10.0F));
    CHECK(clamped2.y == Catch::Approx(0.0F));
    const auto rotated2 = stdlib::math::vec2::rotate(Vector2{.x = 1.0F, .y = 0.0F}, std::numbers::pi_v<float> / 2.0F);
    CHECK(rotated2.x == Catch::Approx(0.0F).margin(1e-5));
    CHECK(rotated2.y == Catch::Approx(1.0F));

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
    const auto clamped3 = stdlib::math::vec3::clamp(
        Vector3{.x = 15.0F, .y = -5.0F, .z = 2.0F}, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 10.0F, .y = 10.0F, .z = 10.0F});
    CHECK(clamped3.x == Catch::Approx(10.0F));
    CHECK(clamped3.y == Catch::Approx(0.0F));
    CHECK(clamped3.z == Catch::Approx(2.0F));
    const auto rotated3 = stdlib::math::vec3::rotate(
        Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.0F, .y = 0.0F, .z = 1.0F}, std::numbers::pi_v<float> / 2.0F);
    CHECK(rotated3.x == Catch::Approx(0.0F).margin(1e-5));
    CHECK(rotated3.y == Catch::Approx(1.0F));
    CHECK(rotated3.z == Catch::Approx(0.0F).margin(1e-5));
    const auto rotated3_nonunit_axis = stdlib::math::vec3::rotate(
        Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.0F, .y = 0.0F, .z = 5.0F}, std::numbers::pi_v<float> / 2.0F);
    CHECK(rotated3_nonunit_axis.x == Catch::Approx(rotated3.x).margin(1e-5));
    CHECK(rotated3_nonunit_axis.y == Catch::Approx(rotated3.y));
    CHECK(rotated3_nonunit_axis.z == Catch::Approx(rotated3.z).margin(1e-5));
}

TEST_CASE("Runtime stdlib: global-namespace vec2/vec3 splat constructors match component form",
          "[runtime][stdlib][vector-expressions]") {
    const auto splat2 = vec2(2.5F);
    CHECK(splat2.x == Catch::Approx(2.5F));
    CHECK(splat2.y == Catch::Approx(2.5F));
    CHECK((splat2.x == vec2(2.5F, 2.5F).x && splat2.y == vec2(2.5F, 2.5F).y));

    const auto splat3 = vec3(4.0F);
    CHECK(splat3.x == Catch::Approx(4.0F));
    CHECK(splat3.y == Catch::Approx(4.0F));
    CHECK(splat3.z == Catch::Approx(4.0F));
    CHECK((splat3.x == vec3(4.0F, 4.0F, 4.0F).x && splat3.z == vec3(4.0F, 4.0F, 4.0F).z));
}

TEST_CASE("Runtime stdlib: global-namespace vec2/vec3 component constructors", "[runtime][stdlib][vector-expressions]") {
    const auto v2 = vec2(3.0F, 4.0F);
    CHECK(v2.x == Catch::Approx(3.0F));
    CHECK(v2.y == Catch::Approx(4.0F));

    const auto v3 = vec3(1.0F, 2.0F, 3.0F);
    CHECK(v3.x == Catch::Approx(1.0F));
    CHECK(v3.y == Catch::Approx(2.0F));
    CHECK(v3.z == Catch::Approx(3.0F));
}

TEST_CASE("Runtime stdlib: vec2/vec3 scalar multiply is commutative via reachable raymath + global operators",
          "[runtime][stdlib][vector-expressions]") {
    const auto a2 = vec2(2.0F, 3.0F) * 2.0F;  // raymath.h's vector-first operator*
    const auto b2 = 2.0F * vec2(2.0F, 3.0F);  // this header's scalar-first operator*
    CHECK(a2.x == Catch::Approx(b2.x));
    CHECK(a2.y == Catch::Approx(b2.y));
    CHECK(a2.x == Catch::Approx(4.0F));
    CHECK(a2.y == Catch::Approx(6.0F));

    const auto a3 = vec3(1.0F, 2.0F, 3.0F) * 3.0F;
    const auto b3 = 3.0F * vec3(1.0F, 2.0F, 3.0F);
    CHECK(a3.x == Catch::Approx(b3.x));
    CHECK(a3.y == Catch::Approx(b3.y));
    CHECK(a3.z == Catch::Approx(b3.z));
}

TEST_CASE("Runtime stdlib: vec2/vec3 component-wise multiply via raymath is not a dot product",
          "[runtime][stdlib][vector-expressions]") {
    const auto product2 = vec2(2.0F, 3.0F) * vec2(4.0F, 5.0F);  // raymath.h's component-wise operator*
    CHECK(product2.x == Catch::Approx(8.0F));
    CHECK(product2.y == Catch::Approx(15.0F));

    const auto product3 = vec3(1.0F, 2.0F, 3.0F) * vec3(4.0F, 5.0F, 6.0F);
    CHECK(product3.x == Catch::Approx(4.0F));
    CHECK(product3.y == Catch::Approx(10.0F));
    CHECK(product3.z == Catch::Approx(18.0F));
}

TEST_CASE("Runtime stdlib: vec2/vec3 add/subtract/divide and compound-assignment forms are reachable",
          "[runtime][stdlib][vector-expressions]") {
    auto sum2 = vec2(1.0F, 2.0F) + vec2(3.0F, 4.0F);
    CHECK(sum2.x == Catch::Approx(4.0F));
    CHECK(sum2.y == Catch::Approx(6.0F));
    sum2 -= vec2(1.0F, 1.0F);
    CHECK(sum2.x == Catch::Approx(3.0F));
    CHECK(sum2.y == Catch::Approx(5.0F));
    sum2 *= 2.0F;
    CHECK(sum2.x == Catch::Approx(6.0F));
    CHECK(sum2.y == Catch::Approx(10.0F));
    sum2 /= 2.0F;
    CHECK(sum2.x == Catch::Approx(3.0F));
    CHECK(sum2.y == Catch::Approx(5.0F));

    auto sum3 = vec3(1.0F, 2.0F, 3.0F) + vec3(1.0F, 1.0F, 1.0F);
    sum3 -= vec3(0.0F, 1.0F, 2.0F);
    sum3 *= 2.0F;
    sum3 /= 2.0F;
    CHECK(sum3.x == Catch::Approx(2.0F));
    CHECK(sum3.y == Catch::Approx(2.0F));
    CHECK(sum3.z == Catch::Approx(2.0F));
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

TEST_CASE("Runtime stdlib: quaternion total-behavior wrappers normalize and handle degenerate inputs",
          "[runtime][stdlib][quat]") {
    constexpr Quat kZeroQuat{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    const auto identity = stdlib::math::quat::identity();

    SECTION("inverse of the zero quaternion returns identity") {
        const auto inv = stdlib::math::quat::inverse(kZeroQuat);
        CHECK(inv.x == Catch::Approx(identity.x));
        CHECK(inv.y == Catch::Approx(identity.y));
        CHECK(inv.z == Catch::Approx(identity.z));
        CHECK(inv.w == Catch::Approx(identity.w));
    }

    SECTION("slerp normalizes non-unit inputs and takes the shortest path") {
        const Quat non_unit{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 2.0F};
        const auto result = stdlib::math::quat::slerp(non_unit, non_unit, 0.5F);
        const float len_sq = (result.x * result.x) + (result.y * result.y) + (result.z * result.z) + (result.w * result.w);
        CHECK(len_sq == Catch::Approx(1.0F));

        const Quat q{.x = 0.0F, .y = 0.0F, .z = 0.7071068F, .w = 0.7071068F};
        const Quat negated_q{.x = -q.x, .y = -q.y, .z = -q.z, .w = -q.w};
        const auto shortest = stdlib::math::quat::slerp(identity, negated_q, 0.5F);
        const auto direct    = stdlib::math::quat::slerp(identity, q, 0.5F);
        CHECK(shortest.x == Catch::Approx(direct.x));
        CHECK(shortest.y == Catch::Approx(direct.y));
        CHECK(shortest.z == Catch::Approx(direct.z));
        CHECK(shortest.w == Catch::Approx(direct.w));
    }

    SECTION("slerp of two zero quaternions returns identity") {
        const auto result = stdlib::math::quat::slerp(kZeroQuat, kZeroQuat, 0.5F);
        CHECK(result.x == Catch::Approx(identity.x));
        CHECK(result.y == Catch::Approx(identity.y));
        CHECK(result.z == Catch::Approx(identity.z));
        CHECK(result.w == Catch::Approx(identity.w));
    }

    SECTION("rotate/forward/right/up normalize a non-unit quaternion input") {
        const Quat non_unit{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 2.0F};

        const auto rotated = stdlib::math::quat::rotate(non_unit, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
        CHECK(rotated.x == Catch::Approx(1.0F));
        CHECK(rotated.y == Catch::Approx(0.0F));
        CHECK(rotated.z == Catch::Approx(0.0F));

        const auto fwd = stdlib::math::quat::forward(non_unit);
        CHECK(fwd.x == Catch::Approx(0.0F));
        CHECK(fwd.y == Catch::Approx(0.0F));
        CHECK(fwd.z == Catch::Approx(-1.0F));

        const auto rt = stdlib::math::quat::right(non_unit);
        CHECK(rt.x == Catch::Approx(1.0F));
        CHECK(rt.y == Catch::Approx(0.0F));
        CHECK(rt.z == Catch::Approx(0.0F));

        const auto up = stdlib::math::quat::up(non_unit);
        CHECK(up.x == Catch::Approx(0.0F));
        CHECK(up.y == Catch::Approx(1.0F));
        CHECK(up.z == Catch::Approx(0.0F));
    }

    SECTION("from_axis_angle with a zero-length axis returns identity") {
        const auto result = stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.5F);
        CHECK(result.x == Catch::Approx(identity.x));
        CHECK(result.y == Catch::Approx(identity.y));
        CHECK(result.z == Catch::Approx(identity.z));
        CHECK(result.w == Catch::Approx(identity.w));
    }
}

TEST_CASE("Runtime stdlib: quaternion new composition/comparison functions", "[runtime][stdlib][quat]") {
    constexpr Quat kZeroQuat{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    const auto identity = stdlib::math::quat::identity();

    SECTION("normalize scales a non-unit input to unit length and maps zero to identity") {
        const Quat non_unit{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 2.0F};
        const auto normalized = stdlib::math::quat::normalize(non_unit);
        CHECK(normalized.x == Catch::Approx(0.0F));
        CHECK(normalized.y == Catch::Approx(0.0F));
        CHECK(normalized.z == Catch::Approx(0.0F));
        CHECK(normalized.w == Catch::Approx(1.0F));

        const auto zero_normalized = stdlib::math::quat::normalize(kZeroQuat);
        CHECK(zero_normalized.x == Catch::Approx(identity.x));
        CHECK(zero_normalized.y == Catch::Approx(identity.y));
        CHECK(zero_normalized.z == Catch::Approx(identity.z));
        CHECK(zero_normalized.w == Catch::Approx(identity.w));
    }

    SECTION("dot returns the raw unnormalized component dot product") {
        const Quat a{.x = 1.0F, .y = 2.0F, .z = 3.0F, .w = 4.0F};
        const Quat b{.x = 5.0F, .y = 6.0F, .z = 7.0F, .w = 8.0F};
        CHECK(stdlib::math::quat::dot(a, b) == Catch::Approx(70.0F));
    }

    SECTION("compose applies inner first, outer second, and normalizes the result") {
        const auto outer =
            stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, std::numbers::pi_v<float> / 2.0F);
        const auto inner =
            stdlib::math::quat::from_axis_angle(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, std::numbers::pi_v<float> / 4.0F);
        const auto composed = stdlib::math::quat::compose(outer, inner);

        const float len_sq =
            (composed.x * composed.x) + (composed.y * composed.y) + (composed.z * composed.z) + (composed.w * composed.w);
        CHECK(len_sq == Catch::Approx(1.0F));

        const Vector3 v{.x = 1.0F, .y = 0.5F, .z = -0.25F};
        const auto via_compose = stdlib::math::quat::rotate(composed, v);
        const auto via_nested  = stdlib::math::quat::rotate(outer, stdlib::math::quat::rotate(inner, v));
        CHECK(via_compose.x == Catch::Approx(via_nested.x));
        CHECK(via_compose.y == Catch::Approx(via_nested.y));
        CHECK(via_compose.z == Catch::Approx(via_nested.z));
    }

    SECTION("rotate_local and rotate_world delegate to compose with swapped argument order") {
        const auto current =
            stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, std::numbers::pi_v<float> / 3.0F);
        const auto delta =
            stdlib::math::quat::from_axis_angle(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, std::numbers::pi_v<float> / 6.0F);

        const auto local  = stdlib::math::quat::rotate_local(current, delta);
        const auto expected_local = stdlib::math::quat::compose(current, delta);
        CHECK(local.x == Catch::Approx(expected_local.x));
        CHECK(local.y == Catch::Approx(expected_local.y));
        CHECK(local.z == Catch::Approx(expected_local.z));
        CHECK(local.w == Catch::Approx(expected_local.w));

        const auto world = stdlib::math::quat::rotate_world(current, delta);
        const auto expected_world = stdlib::math::quat::compose(delta, current);
        CHECK(world.x == Catch::Approx(expected_world.x));
        CHECK(world.y == Catch::Approx(expected_world.y));
        CHECK(world.z == Catch::Approx(expected_world.z));
        CHECK(world.w == Catch::Approx(expected_world.w));

        const bool observably_different =
            (local.x != Catch::Approx(world.x)) || (local.y != Catch::Approx(world.y)) ||
            (local.z != Catch::Approx(world.z)) || (local.w != Catch::Approx(world.w));
        CHECK(observably_different);
    }

    SECTION("same_rotation treats q and -q as equal, and rejects a negative tolerance") {
        const auto q = stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, 1.2F);
        const Quat negated_q{.x = -q.x, .y = -q.y, .z = -q.z, .w = -q.w};
        CHECK(stdlib::math::quat::same_rotation(q, negated_q, 0.0001F));
        CHECK_FALSE(stdlib::math::quat::same_rotation(q, negated_q, -0.0001F));

        const auto other = stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, 0.5F);
        CHECK_FALSE(stdlib::math::quat::same_rotation(q, other, 0.0001F));
    }
}

TEST_CASE("Runtime stdlib: Standard UI image fit geometry computes Stretch/Contain/Cover source/dest rects",
          "[runtime][stdlib][ui][render]") {
    constexpr int kStretch = 0;
    constexpr int kContain = 1;
    constexpr int kCover   = 2;

    // Stretch: whole frame maps onto the whole destination, no cropping.
    const auto stretched = entt_backend::compute_image_draw_rects(
        kStretch, Vector2{.x = 10.0F, .y = 20.0F}, Vector2{.x = 50.0F, .y = 25.0F}, Vector2{.x = 100.0F, .y = 50.0F},
        /*frame_index=*/0, /*frame_count=*/1);
    CHECK(stretched.source.x == Catch::Approx(0.0F));
    CHECK(stretched.source.width == Catch::Approx(100.0F));
    CHECK(stretched.dest.x == Catch::Approx(10.0F));
    CHECK(stretched.dest.width == Catch::Approx(50.0F));
    CHECK(stretched.dest.height == Catch::Approx(25.0F));

    // Contain: a 100x50 (2:1) image inside a 40x40 (1:1) box is width-limited
    // (scale 0.4), letterboxed and centered on the taller axis.
    const auto contained = entt_backend::compute_image_draw_rects(
        kContain, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 40.0F, .y = 40.0F}, Vector2{.x = 100.0F, .y = 50.0F}, 0, 1);
    CHECK(contained.source.width == Catch::Approx(100.0F));
    CHECK(contained.source.height == Catch::Approx(50.0F));
    CHECK(contained.dest.width == Catch::Approx(40.0F));
    CHECK(contained.dest.height == Catch::Approx(20.0F));
    CHECK(contained.dest.x == Catch::Approx(0.0F));
    CHECK(contained.dest.y == Catch::Approx(10.0F));  // (40 - 20) / 2

    // Cover: the same 2:1 image filling a 40x40 box crops the source's width
    // down to match the box's aspect ratio (height-limited, scale 0.8),
    // centered horizontally, while the destination fills the whole box.
    const auto covered = entt_backend::compute_image_draw_rects(
        kCover, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 40.0F, .y = 40.0F}, Vector2{.x = 100.0F, .y = 50.0F}, 0, 1);
    CHECK(covered.dest.width == Catch::Approx(40.0F));
    CHECK(covered.dest.height == Catch::Approx(40.0F));
    CHECK(covered.source.width == Catch::Approx(50.0F));
    CHECK(covered.source.height == Catch::Approx(50.0F));
    CHECK(covered.source.x == Catch::Approx(25.0F));  // (100 - 50) / 2

    // Horizontal-strip frame slicing: a 400x100 4-frame strip's frame 2 (0-based)
    // occupies x in [200, 300), independent of fit mode.
    const auto frame2 = entt_backend::compute_image_draw_rects(
        kStretch, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 100.0F, .y = 100.0F}, Vector2{.x = 400.0F, .y = 100.0F},
        /*frame_index=*/2, /*frame_count=*/4);
    CHECK(frame2.source.x == Catch::Approx(200.0F));
    CHECK(frame2.source.width == Catch::Approx(100.0F));

    // A negative/out-of-range frame index wraps modulo frame_count rather than
    // producing a negative or out-of-bounds source rect.
    const auto wrapped = entt_backend::compute_image_draw_rects(
        kStretch, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 100.0F, .y = 100.0F}, Vector2{.x = 400.0F, .y = 100.0F},
        /*frame_index=*/-1, /*frame_count=*/4);
    CHECK(wrapped.source.x == Catch::Approx(300.0F));  // frame 3 of 4
}

TEST_CASE("Runtime stdlib: pointer candidate sorting orders each domain deterministically",
          "[runtime][stdlib][pointer]") {
    using entt_backend::PointerCandidate;

    // Window: descending draw_order ("greater computed draw order is
    // considered first").
    std::vector<PointerCandidate> window{
        PointerCandidate{.entity = entt::entity{1}, .draw_order = 2},
        PointerCandidate{.entity = entt::entity{2}, .draw_order = 5},
        PointerCandidate{.entity = entt::entity{3}, .draw_order = 0},
    };
    entt_backend::sort_window_pointer_candidates(window);
    CHECK(window[0].entity == entt::entity{2});
    CHECK(window[1].entity == entt::entity{1});
    CHECK(window[2].entity == entt::entity{3});

    // Flat world: priority descending, then stable creation ordinal ascending.
    std::vector<PointerCandidate> flat{
        PointerCandidate{.entity = entt::entity{1}, .priority = 0, .creation_ordinal = 5},
        PointerCandidate{.entity = entt::entity{2}, .priority = 1, .creation_ordinal = 9},
        PointerCandidate{.entity = entt::entity{3}, .priority = 0, .creation_ordinal = 1},
    };
    entt_backend::sort_flat_world_pointer_candidates(flat);
    CHECK(flat[0].entity == entt::entity{2});  // highest priority
    CHECK(flat[1].entity == entt::entity{3});  // tie on priority, lower creation ordinal first
    CHECK(flat[2].entity == entt::entity{1});

    // Volume world: nearest positive distance first.
    std::vector<PointerCandidate> volume{
        PointerCandidate{.entity = entt::entity{1}, .distance = 10.0F},
        PointerCandidate{.entity = entt::entity{2}, .distance = 2.5F},
        PointerCandidate{.entity = entt::entity{3}, .distance = 6.0F},
    };
    entt_backend::sort_volume_world_pointer_candidates(volume);
    CHECK(volume[0].entity == entt::entity{2});
    CHECK(volume[1].entity == entt::entity{3});
    CHECK(volume[2].entity == entt::entity{1});
}

TEST_CASE("Runtime stdlib: pointer target resolution respects front-to-back blocking", "[runtime][stdlib][pointer]") {
    using entt_backend::PointerCandidate;
    using entt_backend::resolve_pointer_target;

    // Window overlay wins over world entity: window candidates are simply
    // ordered first in the concatenated list.
    CHECK(resolve_pointer_target({PointerCandidate{.entity = entt::entity{1}, .enabled = true},
                                  PointerCandidate{.entity = entt::entity{2}, .enabled = true}}) == entt::entity{1});

    // Disabled control prevents click-through: a disabled blocking candidate
    // stops evaluation, so neither it nor anything behind it is selected.
    CHECK(resolve_pointer_target({PointerCandidate{.entity = entt::entity{1}, .enabled = false, .blocks_lower = true},
                                  PointerCandidate{.entity = entt::entity{2}, .enabled = true}}) == entt::entity{entt::null});

    // Nonblocking overlay permits the lower target.
    CHECK(
        resolve_pointer_target({PointerCandidate{.entity = entt::entity{1}, .enabled = false, .blocks_lower = false},
                                PointerCandidate{.entity = entt::entity{2}, .enabled = true}}) == entt::entity{2});

    // No candidates, or every candidate declines and none block: no target.
    CHECK(resolve_pointer_target({}) == entt::entity{entt::null});
    CHECK(resolve_pointer_target(
              {PointerCandidate{.entity = entt::entity{1}, .enabled = false, .blocks_lower = false}}) == entt::entity{entt::null});
}

TEST_CASE("Runtime stdlib: pointer hit-test geometry covers rect/box/circle point containment",
          "[runtime][stdlib][pointer]") {
    using entt_backend::point_in_flat_box;
    using entt_backend::point_in_flat_circle;
    using entt_backend::point_in_rect;

    CHECK(point_in_rect(Vector2{.x = 5.0F, .y = 5.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 10.0F}));
    CHECK_FALSE(
        point_in_rect(Vector2{.x = 15.0F, .y = 5.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 10.0F}));
    // Boundary is inclusive.
    CHECK(
        point_in_rect(Vector2{.x = 10.0F, .y = 10.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 10.0F, .y = 10.0F}));

    CHECK(point_in_flat_box(Vector2{.x = 1.0F, .y = 1.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 4.0F, .y = 4.0F}));
    CHECK_FALSE(
        point_in_flat_box(Vector2{.x = 3.0F, .y = 0.0F}, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 4.0F, .y = 4.0F}));

    CHECK(point_in_flat_circle(Vector2{.x = 1.0F, .y = 0.0F}, Vector2{.x = 0.0F, .y = 0.0F}, 2.0F));
    CHECK_FALSE(point_in_flat_circle(Vector2{.x = 3.0F, .y = 0.0F}, Vector2{.x = 0.0F, .y = 0.0F}, 2.0F));
}

TEST_CASE("Runtime stdlib: allocation-free helper contracts stay constexpr and noexcept",
          "[runtime][stdlib][contract]") {
    CHECK(kConstexprScalarLerp == Catch::Approx(5.0F));
    CHECK(kConstexprVec2Lerp.x == Catch::Approx(2.5F));
    CHECK(kConstexprVec2Lerp.y == Catch::Approx(5.0F));
    CHECK(kConstexprVec3Cross.z == Catch::Approx(1.0F));
    CHECK(kConstexprVec2Clamp.x == Catch::Approx(10.0F));
    CHECK(kConstexprVec3Clamp.z == Catch::Approx(2.0F));
    CHECK(kConstexprQuatMultiply.w == Catch::Approx(1.0F));
    CHECK(noexcept(stdlib::math::lerp(0.0F, 1.0F, 0.5F)));
    CHECK(noexcept(stdlib::math::vec2::lerp(Vector2{0.0F, 0.0F}, Vector2{1.0F, 1.0F}, 0.5F)));
    CHECK(noexcept(stdlib::math::vec2::clamp(Vector2{0.0F, 0.0F}, Vector2{0.0F, 0.0F}, Vector2{1.0F, 1.0F})));
    CHECK(noexcept(stdlib::math::vec3::cross(Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F})));
    CHECK(noexcept(
        stdlib::math::vec3::clamp(Vector3{0.0F, 0.0F, 0.0F}, Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F})));
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

TEST_CASE("Runtime stdlib: EnTT model bounds introspection stays total without a loadable model",
          "[runtime][assets][entt][dynamic-model-spawning]") {
    auto& registry = shared_asset_registry();
    registry.clear();
    cactus::runtime::entt_backend::reset_render_debug_state();

    // Unregistered handle: zero extents, no crash.
    const auto missing = cactus::runtime::entt_backend::model_bounds_size(999U);
    CHECK(missing.x == 0.0F);
    CHECK(missing.y == 0.0F);
    CHECK(missing.z == 0.0F);

    // Fake record (test seam) pointing at a missing file: same degradation.
    registry.register_model(84U, "does/not/exist.glb", 84);
    const auto failed = cactus::runtime::entt_backend::model_bounds_size(84U);
    CHECK(failed.x == 0.0F);
    CHECK(failed.y == 0.0F);
    CHECK(failed.z == 0.0F);

    // Real GLB but no rendering window (headless test run): the lazy load
    // cannot complete, so bounds_size reports zero extents instead of crashing.
    const auto robot_path = (repo_root() / "examples/model-renderer/art/robot.glb").string();
    REQUIRE(fs::exists(robot_path));
    registry.register_model(85U, robot_path, 85);
    const auto headless = cactus::runtime::entt_backend::model_bounds_size(85U);
    CHECK(headless.x == 0.0F);
    CHECK(headless.y == 0.0F);
    CHECK(headless.z == 0.0F);

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
    cactus::runtime::entt_backend::submit_screen_label(Vector2{.x = 16.0F, .y = 48.0F}, 32, WHITE, "hidden", false);
    cactus::runtime::entt_backend::submit_screen_label(Vector2{.x = 16.0F, .y = 80.0F}, 24, WHITE, "second", true);
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
        rng           = stdlib::random::advance(rng);
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

TEST_CASE("Runtime stdlib: editor ray/plane intersection hits, rejects parallel and behind-origin rays",
          "[runtime][editor][entt]") {
    constexpr Vector3 kGroundOrigin{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    constexpr Vector3 kGroundNormal{.x = 0.0F, .y = 1.0F, .z = 0.0F};

    SECTION("Downward center ray from an overhead camera hits the plane origin") {
        const Ray ray{.position = {.x = 0.0F, .y = 10.0F, .z = 0.0F}, .direction = {.x = 0.0F, .y = -1.0F, .z = 0.0F}};
        const auto hit = entt_backend::editor_ray_plane_intersect(ray, kGroundOrigin, kGroundNormal);
        REQUIRE(hit.has_value());
        CHECK(hit->x == Catch::Approx(0.0F).margin(1e-5));
        CHECK(hit->y == Catch::Approx(0.0F).margin(1e-5));
        CHECK(hit->z == Catch::Approx(0.0F).margin(1e-5));
    }

    SECTION("Angled ray lands at the projected ground point") {
        const float inv_sqrt2 = 1.0F / std::numbers::sqrt2_v<float>;
        const Ray ray{.position  = {.x = 0.0F, .y = 10.0F, .z = 0.0F},
                      .direction = {.x = inv_sqrt2, .y = -inv_sqrt2, .z = 0.0F}};
        const auto hit = entt_backend::editor_ray_plane_intersect(ray, kGroundOrigin, kGroundNormal);
        REQUIRE(hit.has_value());
        CHECK(hit->x == Catch::Approx(10.0F));
        CHECK(hit->y == Catch::Approx(0.0F).margin(1e-5));
        CHECK(hit->z == Catch::Approx(0.0F).margin(1e-5));
    }

    SECTION("Ray parallel to the plane misses") {
        const Ray ray{.position = {.x = 0.0F, .y = 10.0F, .z = 0.0F}, .direction = {.x = 1.0F, .y = 0.0F, .z = 0.0F}};
        CHECK_FALSE(entt_backend::editor_ray_plane_intersect(ray, kGroundOrigin, kGroundNormal).has_value());
    }

    SECTION("Intersection behind the ray origin misses") {
        const Ray ray{.position = {.x = 0.0F, .y = 10.0F, .z = 0.0F}, .direction = {.x = 0.0F, .y = 1.0F, .z = 0.0F}};
        CHECK_FALSE(entt_backend::editor_ray_plane_intersect(ray, kGroundOrigin, kGroundNormal).has_value());
    }

    SECTION("Two projected cursor rays yield a ground-plane delta") {
        // Project current and previous cursor rays onto y=0 and difference them
        // — the underlying logic behind screen_delta_on_plane_3d's 3D delta.
        const Ray current{.position  = {.x = 0.0F, .y = 10.0F, .z = 0.0F},
                          .direction = {.x = 0.1F, .y = -1.0F, .z = 0.05F}};
        const Ray previous{.position  = {.x = 0.0F, .y = 10.0F, .z = 0.0F},
                           .direction = {.x = 0.0F, .y = -1.0F, .z = 0.0F}};
        const auto current_hit  = entt_backend::editor_ray_plane_intersect(current, kGroundOrigin, kGroundNormal);
        const auto previous_hit = entt_backend::editor_ray_plane_intersect(previous, kGroundOrigin, kGroundNormal);
        REQUIRE(current_hit.has_value());
        REQUIRE(previous_hit.has_value());
        const Vector3 delta{.x = current_hit->x - previous_hit->x,
                            .y = current_hit->y - previous_hit->y,
                            .z = current_hit->z - previous_hit->z};
        CHECK(delta.y == Catch::Approx(0.0F).margin(1e-5));
        CHECK(std::abs(delta.x) + std::abs(delta.z) > 0.0F);
    }
}

TEST_CASE("Runtime stdlib: editor_raycast_3d without a registered impl returns null", "[runtime][editor][entt]") {
    entt::registry registry;
    entt_backend::register_editor_raycast_impl({});
    CHECK(entt_backend::editor_raycast_3d(registry, Vector2{.x = 100.0F, .y = 100.0F}, 1) == entt::entity{entt::null});
}

TEST_CASE("Runtime stdlib: editor_hit_test_2d without a registered impl returns null", "[runtime][editor][entt]") {
    entt::registry registry;
    entt_backend::register_editor_hit_test_impl({});
    CHECK(entt_backend::editor_hit_test_2d(registry, Vector2{.x = 50.0F, .y = 50.0F}, 1) == entt::entity{entt::null});
}

TEST_CASE("Runtime stdlib: editor_spawn_template without a registered impl returns null", "[runtime][editor][entt]") {
    entt::registry registry;
    entt_backend::register_editor_spawn_impl({});
    CHECK(entt_backend::editor_spawn_template(
              registry, "Enemy", Vector2{.x = 0.0F, .y = 0.0F}, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}) ==
          entt::entity{entt::null});
}

TEST_CASE("Runtime stdlib: consumed input state reset clears all consumed keys and buttons",
          "[runtime][editor][entt]") {
    entt_backend::reset_consumed_input();

    CHECK_FALSE(entt_backend::is_input_key_consumed(87));   // KEY_W — starts clear
    CHECK_FALSE(entt_backend::is_input_mouse_consumed(1));  // MOUSE_BUTTON_RIGHT — starts clear

    entt_backend::mark_input_key_consumed(87);
    entt_backend::mark_input_mouse_consumed(1);
    CHECK(entt_backend::is_input_key_consumed(87));
    CHECK(entt_backend::is_input_mouse_consumed(1));

    entt_backend::reset_consumed_input();
    CHECK_FALSE(entt_backend::is_input_key_consumed(87));
    CHECK_FALSE(entt_backend::is_input_mouse_consumed(1));
}

TEST_CASE("Runtime stdlib: mark_input_key_consumed is code-specific and does not affect other codes",
          "[runtime][editor][entt]") {
    entt_backend::reset_consumed_input();
    entt_backend::mark_input_key_consumed(65);  // KEY_A
    CHECK(entt_backend::is_input_key_consumed(65));
    CHECK_FALSE(entt_backend::is_input_key_consumed(66));   // KEY_B — not consumed
    CHECK_FALSE(entt_backend::is_input_mouse_consumed(0));  // mouse codes are separate
}

TEST_CASE("Runtime stdlib: editor camera rig lifecycle functions are safe without registered impls",
          "[runtime][editor][entt]") {
    entt::registry registry;
    entt_backend::register_editor_camera_enter_impl({});
    entt_backend::register_editor_camera_exit_impl({});
    entt_backend::register_editor_apply_camera_2d_impl({});
    entt_backend::register_editor_apply_camera_3d_impl({});
    entt_backend::register_editor_entity_position_2d_impl({});
    entt_backend::register_editor_entity_position_3d_impl({});

    // enter with no impl returns null and records null as rig
    const auto rig = entt_backend::editor_camera_enter(registry, false);
    CHECK(rig == entt::entity{entt::null});

    // apply and query with no impls don't crash and return zero values
    entt_backend::editor_apply_camera_2d(registry, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F);
    entt_backend::editor_apply_camera_3d(
        registry, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F});
    const auto pos2 = entt_backend::editor_entity_position_2d(registry, entt::entity{entt::null});
    CHECK(pos2.x == Catch::Approx(0.0F));
    CHECK(pos2.y == Catch::Approx(0.0F));
    const auto pos3 = entt_backend::editor_entity_position_3d(registry, entt::entity{entt::null});
    CHECK(pos3.x == Catch::Approx(0.0F));
    CHECK(pos3.z == Catch::Approx(0.0F));

    // exit with no impl doesn't crash
    entt_backend::editor_camera_exit(registry);
}

// ── Spatial query helpers (extract-spatial-query-helpers) ──────────────────────
// Behavioral coverage for the runtime search algorithms behind
// std.physics.flat.query / std.physics.volume.query, exercised directly
// against a real registry so a math regression fails here rather than only
// showing up as a generated-code text mismatch.

namespace {
struct SpatialPos2 {
    Vector2 position;
};
struct SpatialPos3 {
    Vector3 position;
};
}  // namespace

TEST_CASE("Runtime stdlib: query_nearest (2D) returns the closest entity and null on an empty view",
          "[runtime][stdlib][query][spatial]") {
    entt::registry registry;
    const auto near_e = registry.create();
    const auto mid_e  = registry.create();
    const auto far_e  = registry.create();
    registry.emplace<SpatialPos2>(near_e, Vector2{.x = 1.0F, .y = 0.0F});
    registry.emplace<SpatialPos2>(mid_e, Vector2{.x = 5.0F, .y = 0.0F});
    registry.emplace<SpatialPos2>(far_e, Vector2{.x = 10.0F, .y = 0.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos2>(e).position; };
    auto view        = registry.view<SpatialPos2>();
    CHECK(entt_backend::query_nearest(view, Vector2{.x = 0.0F, .y = 0.0F}, position_of) == near_e);

    entt::registry empty_registry;
    auto empty_view = empty_registry.view<SpatialPos2>();
    CHECK(entt_backend::query_nearest(empty_view, Vector2{.x = 0.0F, .y = 0.0F}, position_of) ==
          entt::entity{entt::null});
}

TEST_CASE("Runtime stdlib: query_nearest (3D) accounts for the z axis", "[runtime][stdlib][query][spatial][3d]") {
    entt::registry registry;
    const auto in_plane = registry.create();
    const auto off_axis = registry.create();
    registry.emplace<SpatialPos3>(in_plane, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
    registry.emplace<SpatialPos3>(off_axis, Vector3{.x = 0.0F, .y = 0.0F, .z = 5.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos3>(e).position; };
    auto view        = registry.view<SpatialPos3>();
    CHECK(entt_backend::query_nearest(view, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, position_of) == in_plane);
}

TEST_CASE("Runtime stdlib: query_overlap_box (2D/3D) matches only entities within the half-extents",
          "[runtime][stdlib][query][spatial]") {
    entt::registry registry;
    const auto inside  = registry.create();
    const auto outside = registry.create();
    registry.emplace<SpatialPos2>(inside, Vector2{.x = 1.0F, .y = 1.0F});
    registry.emplace<SpatialPos2>(outside, Vector2{.x = 10.0F, .y = 10.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos2>(e).position; };
    auto view        = registry.view<SpatialPos2>();
    const auto hits  = entt_backend::query_overlap_box(
        view, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 4.0F, .y = 4.0F}, position_of);
    CHECK(hits.size() == 1);
    CHECK(std::ranges::find(hits, inside) != hits.end());

    entt::registry registry3d;
    const auto inside3d  = registry3d.create();
    const auto outside3d = registry3d.create();
    registry3d.emplace<SpatialPos3>(inside3d, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    registry3d.emplace<SpatialPos3>(outside3d, Vector3{.x = 1.0F, .y = 1.0F, .z = 10.0F});

    auto position_of_3d = [&](entt::entity e) { return registry3d.get<SpatialPos3>(e).position; };
    auto view3d         = registry3d.view<SpatialPos3>();
    const auto hits3d   = entt_backend::query_overlap_box(
        view3d, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 4.0F, .y = 4.0F, .z = 4.0F}, position_of_3d);
    CHECK(hits3d.size() == 1);
    CHECK(std::ranges::find(hits3d, inside3d) != hits3d.end());
}

TEST_CASE("Runtime stdlib: query_overlap_circle matches only entities within the radius",
          "[runtime][stdlib][query][spatial]") {
    entt::registry registry;
    const auto inside  = registry.create();
    const auto outside = registry.create();
    registry.emplace<SpatialPos2>(inside, Vector2{.x = 1.0F, .y = 0.0F});
    registry.emplace<SpatialPos2>(outside, Vector2{.x = 10.0F, .y = 0.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos2>(e).position; };
    auto view        = registry.view<SpatialPos2>();
    const auto hits  = entt_backend::query_overlap_circle(view, Vector2{.x = 0.0F, .y = 0.0F}, 2.0F, position_of);
    CHECK(hits.size() == 1);
    CHECK(std::ranges::find(hits, inside) != hits.end());
}

TEST_CASE("Runtime stdlib: query_overlap_sphere matches only entities within the radius",
          "[runtime][stdlib][query][spatial][3d]") {
    entt::registry registry;
    const auto inside  = registry.create();
    const auto outside = registry.create();
    registry.emplace<SpatialPos3>(inside, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
    registry.emplace<SpatialPos3>(outside, Vector3{.x = 10.0F, .y = 0.0F, .z = 0.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos3>(e).position; };
    auto view        = registry.view<SpatialPos3>();
    const auto hits =
        entt_backend::query_overlap_sphere(view, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 2.0F, position_of);
    CHECK(hits.size() == 1);
    CHECK(std::ranges::find(hits, inside) != hits.end());
}

TEST_CASE("Runtime stdlib: query_raycast (2D) hits an entity within the perpendicular threshold and max_dist",
          "[runtime][stdlib][query][spatial]") {
    entt::registry registry;
    const auto on_ray     = registry.create();
    const auto off_ray    = registry.create();
    const auto behind_ray = registry.create();
    const auto too_far    = registry.create();
    registry.emplace<SpatialPos2>(on_ray, Vector2{.x = 5.0F, .y = 0.0F});
    registry.emplace<SpatialPos2>(off_ray, Vector2{.x = 5.0F, .y = 5.0F});
    registry.emplace<SpatialPos2>(behind_ray, Vector2{.x = -5.0F, .y = 0.0F});
    registry.emplace<SpatialPos2>(too_far, Vector2{.x = 50.0F, .y = 0.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos2>(e).position; };
    auto view        = registry.view<SpatialPos2>();
    const auto hit   = entt_backend::query_raycast(
        view, Vector2{.x = 0.0F, .y = 0.0F}, Vector2{.x = 1.0F, .y = 0.0F}, 20.0F, position_of);
    CHECK(hit == on_ray);
}

TEST_CASE("Runtime stdlib: query_raycast (3D) hits an entity within the perpendicular threshold and max_dist",
          "[runtime][stdlib][query][spatial][3d]") {
    entt::registry registry;
    const auto on_ray  = registry.create();
    const auto off_ray = registry.create();
    registry.emplace<SpatialPos3>(on_ray, Vector3{.x = 5.0F, .y = 0.0F, .z = 0.0F});
    registry.emplace<SpatialPos3>(off_ray, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F});

    auto position_of = [&](entt::entity e) { return registry.get<SpatialPos3>(e).position; };
    auto view        = registry.view<SpatialPos3>();
    const auto hit   = entt_backend::query_raycast(
        view, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 20.0F, position_of);
    CHECK(hit == on_ray);
}

// ── Hierarchy propagation (quaternion-rotation-semantics) ──────────────────────
// The generated ACCUMULATE_FROM_PARENT lambda for std.transform.volume calls
// quat::compose directly (system_emitter.cpp); mirror that exact lambda body
// here against the real propagate_hierarchy runtime helper and a real
// registry, so a drift/normalization regression fails here rather than only
// showing up as a generated-code text mismatch.

namespace {
struct HierarchyParent {
    entt::entity parent{entt::null};
};
struct HierarchyLocalTransform {
    Vector3 position{};
    Quat rotation{};
    Vector3 scale{};
};
struct HierarchyWorldTransform {
    Vector3 position{};
    Quat rotation{};
    Vector3 scale{};
};
}  // namespace

TEST_CASE(
    "Runtime stdlib: propagate_hierarchy composes quaternion rotation through a multi-level volume parent chain",
    "[runtime][backend][hierarchy][quat]") {
    entt::registry registry;

    const auto root       = registry.create();
    const auto child       = registry.create();
    const auto grandchild = registry.create();

    registry.emplace<HierarchyLocalTransform>(
        root,
        HierarchyLocalTransform{
            .position = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            .rotation = stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, 0.7F),
            .scale    = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
        });
    registry.emplace<HierarchyWorldTransform>(root, HierarchyWorldTransform{});

    registry.emplace<HierarchyParent>(child, HierarchyParent{.parent = root});
    registry.emplace<HierarchyLocalTransform>(
        child,
        HierarchyLocalTransform{
            .position = Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
            .rotation = stdlib::math::quat::from_axis_angle(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 0.9F),
            .scale    = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
        });
    registry.emplace<HierarchyWorldTransform>(child, HierarchyWorldTransform{});

    registry.emplace<HierarchyParent>(grandchild, HierarchyParent{.parent = child});
    registry.emplace<HierarchyLocalTransform>(
        grandchild,
        HierarchyLocalTransform{
            .position = Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            .rotation = stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 0.0F, .z = 1.0F}, 1.1F),
            .scale    = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
        });
    registry.emplace<HierarchyWorldTransform>(grandchild, HierarchyWorldTransform{});

    const std::function<bool(entt::entity)> has_local_world = [&](entt::entity e) {
        return registry.all_of<HierarchyLocalTransform, HierarchyWorldTransform>(e);
    };
    const std::function<entt::entity(entt::entity)> get_parent = [&](entt::entity e) {
        if (const auto* parent = registry.try_get<HierarchyParent>(e); parent != nullptr) {
            return parent->parent;
        }
        return entt::entity{entt::null};
    };
    const std::function<void(entt::entity)> copy_local = [&](entt::entity e) {
        const auto& local = registry.get<HierarchyLocalTransform>(e);
        auto& world        = registry.get<HierarchyWorldTransform>(e);
        world.position      = local.position;
        world.rotation      = local.rotation;
        world.scale         = local.scale;
    };
    // Mirrors system_emitter.cpp's volume ACCUMULATE_FROM_PARENT lambda exactly, including the
    // quat::compose call this change migrated off raw quat::multiply.
    const std::function<void(entt::entity, entt::entity)> accumulate_from_parent =
        [&](entt::entity parent_entity, entt::entity entity) {
            const auto& local        = registry.get<HierarchyLocalTransform>(entity);
            auto& world               = registry.get<HierarchyWorldTransform>(entity);
            const auto& parent_world = registry.get<HierarchyWorldTransform>(parent_entity);
            world.position = Vector3{
                .x = parent_world.position.x + local.position.x,
                .y = parent_world.position.y + local.position.y,
                .z = parent_world.position.z + local.position.z,
            };
            world.rotation = stdlib::math::quat::compose(parent_world.rotation, local.rotation);
            world.scale    = Vector3{
                   .x = parent_world.scale.x * local.scale.x,
                   .y = parent_world.scale.y * local.scale.y,
                   .z = parent_world.scale.z * local.scale.z,
            };
        };

    entt_backend::propagate_hierarchy(registry, has_local_world, get_parent, copy_local, accumulate_from_parent);

    auto length_squared_of = [](Quat q) { return (q.x * q.x) + (q.y * q.y) + (q.z * q.z) + (q.w * q.w); };
    CHECK(length_squared_of(registry.get<HierarchyWorldTransform>(root).rotation) == Catch::Approx(1.0F));
    CHECK(length_squared_of(registry.get<HierarchyWorldTransform>(child).rotation) == Catch::Approx(1.0F));
    CHECK(length_squared_of(registry.get<HierarchyWorldTransform>(grandchild).rotation) == Catch::Approx(1.0F));

    const auto expected_child_world = stdlib::math::quat::compose(
        registry.get<HierarchyLocalTransform>(root).rotation, registry.get<HierarchyLocalTransform>(child).rotation);
    const auto expected_grandchild_world = stdlib::math::quat::compose(
        expected_child_world, registry.get<HierarchyLocalTransform>(grandchild).rotation);
    const auto actual_grandchild_world = registry.get<HierarchyWorldTransform>(grandchild).rotation;
    CHECK(actual_grandchild_world.x == Catch::Approx(expected_grandchild_world.x));
    CHECK(actual_grandchild_world.y == Catch::Approx(expected_grandchild_world.y));
    CHECK(actual_grandchild_world.z == Catch::Approx(expected_grandchild_world.z));
    CHECK(actual_grandchild_world.w == Catch::Approx(expected_grandchild_world.w));
}

TEST_CASE("Runtime stdlib: sequence reproducibility", "[runtime][stdlib][random]") {
    auto rng_a = stdlib::random::seeded(5);
    auto rng_b = stdlib::random::seeded(5);

    const auto dist = stdlib::random::uniform(0.0F, 1.0F);
    for (int i = 0; i < 20; ++i) {
        rng_a          = stdlib::random::advance(rng_a);
        rng_b          = stdlib::random::advance(rng_b);
        const float va = stdlib::random::sample(rng_a, dist);
        const float vb = stdlib::random::sample(rng_b, dist);
        CHECK(va == Catch::Approx(vb));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
