// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/collider_physics.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <optional>

using namespace cactus::runtime::physics;

namespace {

constexpr Quaternion kIdentity{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};

Catch::Approx near(float value) {
    return Catch::Approx(value).margin(1e-3);
}

Quaternion yaw(float radians) {
    return Quaternion{.x = 0.0F, .y = std::sin(radians / 2.0F), .z = 0.0F, .w = std::cos(radians / 2.0F)};
}

ColliderShape point_at(Vector3 center) {
    return sphere_shape(center, 0.0F, 1, 1);
}

ColliderShape segment_at(Vector3 center, float half_segment) {
    return capsule_shape(center, 0.0F, half_segment * 2.0F, 1, 1);
}

ColliderShape box_at(Vector3 center, Vector3 size, Quaternion rotation = kIdentity) {
    return box_shape(center, rotation, size, 1, 1);
}

ColliderShape sphere_at(Vector3 center, float radius) {
    return sphere_shape(center, radius, 1, 1);
}

ColliderShape capsule_at(Vector3 center, float radius, float height) {
    return capsule_shape(center, radius, height, 1, 1);
}

float length(Vector3 v) {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

void check_vec(Vector3 actual, Vector3 expected) {
    CHECK(actual.x == near(expected.x));
    CHECK(actual.y == near(expected.y));
    CHECK(actual.z == near(expected.z));
}

}  // namespace

// ── Shapes ──────────────────────────────────────────────────────────────────

TEST_CASE("collider shapes derive their cores from authored sizes", "[runtime][physics]") {
    const auto box = box_shape({1.0F, 2.0F, 3.0F}, kIdentity, {2.0F, 4.0F, -6.0F}, 4, 3);
    CHECK(box.kind == ShapeKind::Box);
    check_vec(box.half_extents, {1.0F, 2.0F, 3.0F});
    CHECK(box.radius == 0.0F);
    CHECK(box.layer == 4);
    CHECK(box.mask == 3);

    const auto capsule = capsule_shape({}, 0.5F, 3.0F, 1, 1);
    CHECK(capsule.kind == ShapeKind::Capsule);
    CHECK(capsule.radius == 0.5F);
    CHECK(capsule.half_segment == near(1.0F));

    const auto squat = capsule_shape({}, 1.0F, 1.0F, 1, 1);
    CHECK(squat.half_segment == 0.0F);

    const auto sphere = sphere_shape({}, -0.25F, 1, 1);
    CHECK(sphere.kind == ShapeKind::Sphere);
    CHECK(sphere.radius == 0.25F);
}

// ── GJK on cores ────────────────────────────────────────────────────────────

TEST_CASE("core distance between points", "[runtime][physics][gjk]") {
    const auto result = core_distance(point_at({0.0F, 0.0F, 0.0F}), point_at({3.0F, 4.0F, 0.0F}));
    CHECK_FALSE(result.overlap);
    CHECK(result.distance == near(5.0F));
    check_vec(result.on_a, {0.0F, 0.0F, 0.0F});
    check_vec(result.on_b, {3.0F, 4.0F, 0.0F});

    CHECK(core_distance(point_at({1.0F, 1.0F, 1.0F}), point_at({1.0F, 1.0F, 1.0F})).overlap);
}

TEST_CASE("core distance between a point and a segment", "[runtime][physics][gjk]") {
    const auto side = core_distance(point_at({2.0F, 0.5F, 0.0F}), segment_at({0.0F, 0.0F, 0.0F}, 1.0F));
    CHECK(side.distance == near(2.0F));
    check_vec(side.on_b, {0.0F, 0.5F, 0.0F});

    const auto above = core_distance(point_at({0.0F, 4.0F, 0.0F}), segment_at({0.0F, 0.0F, 0.0F}, 1.0F));
    CHECK(above.distance == near(3.0F));
    check_vec(above.on_b, {0.0F, 1.0F, 0.0F});

    CHECK(core_distance(point_at({0.0F, 0.25F, 0.0F}), segment_at({0.0F, 0.0F, 0.0F}, 1.0F)).overlap);
}

TEST_CASE("core distance between a point and an oriented box", "[runtime][physics][gjk]") {
    const auto face = core_distance(point_at({3.0F, 0.0F, 0.0F}), box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}));
    CHECK(face.distance == near(2.0F));
    check_vec(face.on_b, {1.0F, 0.0F, 0.0F});

    const auto corner = core_distance(point_at({2.0F, 2.0F, 2.0F}), box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}));
    CHECK(corner.distance == near(std::sqrt(3.0F)));

    // Rotated 45 degrees, the box's corner points along +X at distance sqrt(2).
    const auto rotated = core_distance(point_at({3.0F, 0.0F, 0.0F}),
                                       box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, yaw(std::numbers::pi_v<float> / 4.0F)));
    CHECK(rotated.distance == near(3.0F - std::numbers::sqrt2_v<float>));

    CHECK(core_distance(point_at({0.5F, 0.0F, 0.0F}), box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F})).overlap);
}

TEST_CASE("core distance between segments", "[runtime][physics][gjk]") {
    const auto parallel = core_distance(segment_at({0.0F, 0.0F, 0.0F}, 1.0F), segment_at({3.0F, 0.5F, 0.0F}, 1.0F));
    CHECK(parallel.distance == near(3.0F));

    const auto stacked = core_distance(segment_at({0.0F, 0.0F, 0.0F}, 1.0F), segment_at({0.0F, 5.0F, 0.0F}, 1.0F));
    CHECK(stacked.distance == near(3.0F));
    check_vec(stacked.on_a, {0.0F, 1.0F, 0.0F});
    check_vec(stacked.on_b, {0.0F, 4.0F, 0.0F});

    CHECK(core_distance(segment_at({0.0F, 0.0F, 0.0F}, 1.0F), segment_at({0.0F, 1.5F, 0.0F}, 1.0F)).overlap);
}

TEST_CASE("core distance between a segment and a box", "[runtime][physics][gjk]") {
    const auto side = core_distance(segment_at({4.0F, 0.0F, 0.0F}, 1.0F), box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}));
    CHECK(side.distance == near(3.0F));

    const auto above = core_distance(segment_at({0.0F, 4.0F, 0.0F}, 1.0F), box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}));
    CHECK(above.distance == near(2.0F));
    check_vec(above.on_a, {0.0F, 3.0F, 0.0F});

    CHECK(core_distance(segment_at({0.0F, 1.5F, 0.0F}, 1.0F), box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F})).overlap);
}

TEST_CASE("core distance between oriented boxes", "[runtime][physics][gjk]") {
    const auto apart = core_distance(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}), box_at({5.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}));
    CHECK(apart.distance == near(3.0F));

    const auto rotated = core_distance(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}),
                                       box_at({5.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, yaw(std::numbers::pi_v<float> / 4.0F)));
    CHECK(rotated.distance == near(4.0F - std::numbers::sqrt2_v<float>));

    CHECK(core_distance(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}), box_at({1.5F, 0.5F, 0.0F}, {2.0F, 2.0F, 2.0F})).overlap);
}

TEST_CASE("core distance handles degenerate cores", "[runtime][physics][gjk]") {
    SECTION("zero-size box behaves like a point") {
        const auto result = core_distance(box_at({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}), point_at({0.0F, 2.0F, 0.0F}));
        CHECK(result.distance == near(2.0F));
    }
    SECTION("zero-length segment behaves like a point") {
        const auto result = core_distance(segment_at({0.0F, 0.0F, 0.0F}, 0.0F), box_at({3.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}));
        CHECK(result.distance == near(2.0F));
    }
    SECTION("flat box behaves like a rectangle") {
        const auto result = core_distance(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 2.0F}), point_at({0.5F, 1.0F, 0.5F}));
        CHECK(result.distance == near(1.0F));
    }
    SECTION("face-parallel boxes report the gap between faces") {
        const auto result =
            core_distance(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}), box_at({0.3F, 2.5F, -0.2F}, {2.0F, 2.0F, 2.0F}));
        CHECK_FALSE(result.overlap);
        CHECK(result.distance == near(0.5F));
        CHECK(result.on_a.y == near(1.0F));
        CHECK(result.on_b.y == near(1.5F));
    }
    SECTION("face-parallel boxes in contact") {
        const auto result =
            core_distance(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}), box_at({0.3F, 2.0F, -0.2F}, {2.0F, 2.0F, 2.0F}));
        CHECK(result.distance == near(0.0F));
    }
}

// ── touching ────────────────────────────────────────────────────────────────

TEST_CASE("touching works across all shape pairs", "[runtime][physics][touching]") {
    const auto box     = box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F});
    const auto sphere  = sphere_at({0.0F, 0.0F, 0.0F}, 1.0F);
    const auto capsule = capsule_at({0.0F, 0.0F, 0.0F}, 1.0F, 3.0F);

    const auto moved = [](ColliderShape shape, Vector3 offset) {
        shape.center = Vector3{shape.center.x + offset.x, shape.center.y + offset.y, shape.center.z + offset.z};
        return shape;
    };
    for (const auto& a : {box, sphere, capsule}) {
        for (const auto& b : {box, sphere, capsule}) {
            CHECK(touching(moved(a, {1.4F, 0.0F, 0.0F}), b, false));
            CHECK_FALSE(touching(moved(a, {2.1F, 0.0F, 0.0F}), b, false));
        }
    }
}

TEST_CASE("touching uses oriented boxes, not their bounds", "[runtime][physics][touching]") {
    const auto wall  = box_at({0.0F, 0.0F, 0.0F}, {4.0F, 1.0F, 0.2F}, yaw(std::numbers::pi_v<float> / 4.0F));
    const auto probe = sphere_at({1.2F, 0.0F, 1.2F}, 0.2F);
    CHECK_FALSE(touching(probe, wall, false));
    CHECK(touching(sphere_at({0.7F, 0.0F, -0.7F}, 0.2F), wall, false));
}

TEST_CASE("touching uses capsule caps, not their bounds", "[runtime][physics][touching]") {
    const auto capsule = capsule_at({0.0F, 0.0F, 0.0F}, 0.5F, 3.0F);
    CHECK_FALSE(touching(sphere_at({0.5F, 1.5F, 0.5F}, 0.1F), capsule, false));
    CHECK(touching(sphere_at({0.0F, 1.55F, 0.0F}, 0.1F), capsule, false));
}

TEST_CASE("touching agrees for rotated box against capsule", "[runtime][physics][touching]") {
    const auto wall = box_at({0.0F, 1.0F, 0.0F}, {4.0F, 2.0F, 0.2F}, yaw(std::numbers::pi_v<float> / 4.0F));
    CHECK(touching(capsule_at({0.5F, 1.0F, -0.4F}, 0.3F, 2.0F), wall, false));
    CHECK_FALSE(touching(capsule_at({1.5F, 1.0F, 1.5F}, 0.3F, 2.0F), wall, false));
}

// ── sweep ───────────────────────────────────────────────────────────────────

TEST_CASE("sweep matches the centered-box example", "[runtime][physics][sweep]") {
    const auto hit = sweep(sphere_at({-5.0F, 0.0F, 0.0F}, 0.5F), {10.0F, 0.0F, 0.0F}, box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}), false);
    REQUIRE(hit.hit);
    CHECK(hit.t == near(0.35F));
    check_vec(hit.point, {-1.0F, 0.0F, 0.0F});
    check_vec(hit.normal, {-1.0F, 0.0F, 0.0F});
}

TEST_CASE("sweep hits and misses across all shape pairs", "[runtime][physics][sweep]") {
    const auto box     = box_at({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const auto sphere  = sphere_at({0.0F, 0.0F, 0.0F}, 0.5F);
    const auto capsule = capsule_at({0.0F, 0.0F, 0.0F}, 0.5F, 2.0F);

    const auto at = [](ColliderShape shape, Vector3 center) {
        shape.center = center;
        return shape;
    };
    for (const auto& subject : {box, sphere, capsule}) {
        for (const auto& target : {box, sphere, capsule}) {
            const auto hit = sweep(at(subject, {-5.0F, 0.0F, 0.0F}), {10.0F, 0.0F, 0.0F}, target, false);
            REQUIRE(hit.hit);
            CHECK(hit.t == near(0.4F));
            check_vec(hit.normal, {-1.0F, 0.0F, 0.0F});
            CHECK(hit.point.x == near(-0.5F));

            const auto miss = sweep(at(subject, {-5.0F, 3.0F, 0.0F}), {10.0F, 0.0F, 0.0F}, target, false);
            CHECK_FALSE(miss.hit);
        }
    }
}

TEST_CASE("sweep does not tunnel through a thin box at high speed", "[runtime][physics][sweep]") {
    const auto wall = box_at({0.0F, 0.0F, 0.0F}, {0.02F, 4.0F, 4.0F});
    const auto hit  = sweep(box_at({-0.5F, 0.0F, 0.0F}, {0.1F, 0.1F, 0.1F}), {50.0F, 0.0F, 0.0F}, wall, false);
    REQUIRE(hit.hit);
    CHECK(hit.t == near(0.44F / 50.0F));
    check_vec(hit.normal, {-1.0F, 0.0F, 0.0F});
}

TEST_CASE("sweep exits quickly when grazing parallel to a face", "[runtime][physics][sweep]") {
    const auto floor = box_at({0.0F, -0.5F, 0.0F}, {20.0F, 1.0F, 20.0F});
    const auto grazing = sweep(capsule_at({0.0F, 1.0F + 1e-3F, 0.0F}, 0.5F, 2.0F), {3.0F, 0.0F, 1.0F}, floor, false);
    CHECK_FALSE(grazing.hit);
    CHECK(grazing.steps <= 2);

    const auto sliding = sweep(sphere_at({0.0F, 0.5F + 5e-4F, 0.0F}, 0.5F), {3.0F, -0.1F, 0.0F}, floor, false);
    REQUIRE(sliding.hit);
    CHECK(sliding.steps <= 3);
    check_vec(sliding.normal, {0.0F, 1.0F, 0.0F});
}

TEST_CASE("sweep tests the oriented box, not its bounds", "[runtime][physics][sweep]") {
    const auto wall = box_at({0.0F, 0.0F, 0.0F}, {4.0F, 1.0F, 0.2F}, yaw(std::numbers::pi_v<float> / 4.0F));
    // Passes through the empty corner of the wall's axis-aligned bounds.
    const auto miss = sweep(sphere_at({1.2F, 0.0F, 3.0F}, 0.1F), {0.0F, 0.0F, -1.6F}, wall, false);
    CHECK_FALSE(miss.hit);
    const auto hit = sweep(sphere_at({1.2F, 0.0F, 3.0F}, 0.1F), {0.0F, 0.0F, -6.0F}, wall, false);
    CHECK(hit.hit);
}

TEST_CASE("sweep tests the capsule cap, not its bounds", "[runtime][physics][sweep]") {
    const auto capsule = capsule_at({0.0F, 0.0F, 0.0F}, 0.5F, 3.0F);
    // Moves through the cap's bounding corner, outside the rounded cap.
    const auto miss = sweep(sphere_at({0.45F, 1.45F, -3.0F}, 0.05F), {0.0F, 0.0F, 6.0F}, capsule, false);
    CHECK_FALSE(miss.hit);
    const auto hit = sweep(sphere_at({0.0F, 1.45F, -3.0F}, 0.05F), {0.0F, 0.0F, 6.0F}, capsule, false);
    REQUIRE(hit.hit);
    CHECK(hit.normal.z < 0.0F);
}

TEST_CASE("sweep with zero delta tests overlap only", "[runtime][physics][sweep]") {
    const auto box = box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F});
    const auto overlapping = sweep(sphere_at({1.2F, 0.0F, 0.0F}, 0.5F), {0.0F, 0.0F, 0.0F}, box, false);
    REQUIRE(overlapping.hit);
    CHECK(overlapping.t == 0.0F);
    const auto apart = sweep(sphere_at({3.0F, 0.0F, 0.0F}, 0.5F), {0.0F, 0.0F, 0.0F}, box, false);
    CHECK_FALSE(apart.hit);
}

TEST_CASE("a sweep miss has the documented fields", "[runtime][physics][sweep]") {
    const auto miss = sweep(sphere_at({0.0F, 5.0F, 0.0F}, 0.5F), {1.0F, 0.0F, 0.0F}, box_at({}, {1.0F, 1.0F, 1.0F}), false);
    CHECK_FALSE(miss.hit);
    CHECK(miss.t == 1.0F);
    check_vec(miss.point, {0.0F, 0.0F, 0.0F});
    check_vec(miss.normal, {0.0F, 0.0F, 0.0F});

    const auto none = sweep_miss();
    CHECK_FALSE(none.hit);
    CHECK(none.t == 1.0F);
}

TEST_CASE("a sweep that starts far and ends short misses", "[runtime][physics][sweep]") {
    const auto box  = box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F});
    const auto miss = sweep(sphere_at({-5.0F, 0.0F, 0.0F}, 0.5F), {3.0F, 0.0F, 0.0F}, box, false);
    CHECK_FALSE(miss.hit);
    const auto away = sweep(sphere_at({-5.0F, 0.0F, 0.0F}, 0.5F), {-3.0F, 0.0F, 0.0F}, box, false);
    CHECK_FALSE(away.hit);
}

TEST_CASE("sweep hit normals are unit length", "[runtime][physics][sweep]") {
    const auto capsule = capsule_at({0.0F, 0.0F, 0.0F}, 0.5F, 2.0F);
    const auto hit     = sweep(sphere_at({-3.0F, 1.5F, 0.2F}, 0.3F), {6.0F, -1.0F, 0.0F}, capsule, false);
    REQUIRE(hit.hit);
    CHECK(length(hit.normal) == near(1.0F));
    CHECK(hit.t >= 0.0F);
    CHECK(hit.t <= 1.0F);
}

// ── penetration ─────────────────────────────────────────────────────────────

TEST_CASE("a sweep starting in contact hits at t = 0 and pushes out", "[runtime][physics][penetration]") {
    const auto box = box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F});

    SECTION("sphere overlapping a face, cores apart") {
        const auto hit = sweep(sphere_at({1.3F, 0.0F, 0.0F}, 0.5F), {1.0F, 0.0F, 0.0F}, box, false);
        REQUIRE(hit.hit);
        CHECK(hit.t == 0.0F);
        check_vec(hit.normal, {1.0F, 0.0F, 0.0F});
        CHECK(hit.point.x == near(1.0F));
    }
    SECTION("box overlapping a box picks the least-depth axis") {
        const auto hit = sweep(box_at({0.2F, 1.8F, 0.0F}, {2.0F, 2.0F, 2.0F}), {0.0F, -1.0F, 0.0F}, box, false);
        REQUIRE(hit.hit);
        CHECK(hit.t == 0.0F);
        check_vec(hit.normal, {0.0F, 1.0F, 0.0F});
    }
    SECTION("box target overlapped by a box subject from below") {
        const auto hit = sweep(box_at({0.0F, -1.7F, 0.1F}, {2.0F, 2.0F, 2.0F}), {0.0F, 1.0F, 0.0F}, box, false);
        REQUIRE(hit.hit);
        check_vec(hit.normal, {0.0F, -1.0F, 0.0F});
    }
    SECTION("sphere core inside a box exits through the nearest face") {
        const auto hit = sweep(sphere_at({0.0F, 0.0F, 0.8F}, 0.1F), {0.0F, 0.0F, 0.0F}, box, false);
        REQUIRE(hit.hit);
        check_vec(hit.normal, {0.0F, 0.0F, 1.0F});
        CHECK(hit.point.z == near(1.0F));
    }
    SECTION("capsule core inside a box uses the segment point nearest the center") {
        const auto hit = sweep(capsule_at({-0.7F, 1.5F, 0.0F}, 0.2F, 3.0F), {0.0F, 0.0F, 0.0F}, box, false);
        REQUIRE(hit.hit);
        check_vec(hit.normal, {-1.0F, 0.0F, 0.0F});
    }
    SECTION("box subject around a sphere target pushes the box away") {
        const auto hit = sweep(box, {0.0F, 0.0F, 0.0F}, sphere_at({0.0F, 0.9F, 0.0F}, 0.2F), false);
        REQUIRE(hit.hit);
        check_vec(hit.normal, {0.0F, -1.0F, 0.0F});
    }
    SECTION("coincident round cores push along +Y") {
        const auto hit = sweep(sphere_at({0.0F, 0.0F, 0.0F}, 0.5F), {1.0F, 0.0F, 0.0F}, sphere_at({0.0F, 0.0F, 0.0F}, 0.5F), false);
        REQUIRE(hit.hit);
        check_vec(hit.normal, {0.0F, 1.0F, 0.0F});
    }
    SECTION("offset round cores push apart") {
        const auto hit =
            sweep(capsule_at({0.0F, 0.5F, 0.0F}, 0.5F, 2.0F), {0.0F, 0.0F, 0.0F}, capsule_at({0.0F, 0.0F, 0.0F}, 0.5F, 2.0F), false);
        REQUIRE(hit.hit);
        check_vec(hit.normal, {0.0F, 1.0F, 0.0F});
    }
}

// ── filtering ───────────────────────────────────────────────────────────────

TEST_CASE("collider queries honor mask, identity and missing data", "[runtime][physics][filter]") {
    auto subject = sphere_shape({-2.0F, 0.0F, 0.0F}, 0.5F, 1, 3);
    auto target  = box_shape({0.0F, 0.0F, 0.0F}, kIdentity, {2.0F, 2.0F, 2.0F}, 4, 1);
    const Vector3 delta{4.0F, 0.0F, 0.0F};

    SECTION("mask excludes the target's layer") {
        CHECK_FALSE(sweep(subject, delta, target, false).hit);
        subject.center = {0.0F, 0.0F, 0.0F};
        CHECK_FALSE(touching(subject, target, false));
    }
    SECTION("mask sharing a bit with the layer collides") {
        target.layer = 2;
        CHECK(sweep(subject, delta, target, false).hit);
    }
    SECTION("an entity never hits itself") {
        target.layer = 1;
        CHECK_FALSE(sweep(subject, delta, target, true).hit);
        CHECK_FALSE(touching(subject, subject, true));
    }
    SECTION("a missing descriptor is a miss") {
        target.layer = 1;
        CHECK_FALSE(sweep(std::nullopt, delta, target, false).hit);
        CHECK_FALSE(sweep(subject, delta, std::nullopt, false).hit);
        CHECK_FALSE(touching(std::nullopt, target, false));
        CHECK_FALSE(touching(subject, std::nullopt, false));
    }
}

// ── bounds ──────────────────────────────────────────────────────────────────

TEST_CASE("collider bounds enclose the shape, swept bounds enclose the motion", "[runtime][physics][bounds]") {
    const auto capsule = bounds(capsule_at({1.0F, 2.0F, 3.0F}, 0.5F, 3.0F));
    check_vec(capsule.min, {0.5F, 0.5F, 2.5F});
    check_vec(capsule.max, {1.5F, 3.5F, 3.5F});

    const auto rotated = bounds(box_at({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, yaw(std::numbers::pi_v<float> / 4.0F)));
    CHECK(rotated.max.x == near(std::numbers::sqrt2_v<float>));
    CHECK(rotated.max.y == near(1.0F));

    const auto swept = swept_bounds(sphere_at({0.0F, 0.0F, 0.0F}, 0.5F), {3.0F, -1.0F, 0.0F});
    check_vec(swept.min, {-0.5F, -1.5F, -0.5F});
    check_vec(swept.max, {3.5F, 0.5F, 0.5F});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
