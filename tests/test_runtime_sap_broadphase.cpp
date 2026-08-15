// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

std::vector<std::pair<std::size_t, std::size_t>> sorted_pairs(std::span<const SapCandidatePair> pairs) {
    std::vector<std::pair<std::size_t, std::size_t>> result;
    result.reserve(pairs.size());
    for (const auto& pair : pairs) {
        result.emplace_back(pair.first, pair.second);
    }
    std::ranges::sort(result);
    return result;
}

// Candidate pairs are indices into whichever proxy span was most recently
// synced; across a spawn/destroy/move/resize, indices are meaningless on
// their own, so tests identify pairs by the stable entity behind each index.
template <typename Proxy>
std::vector<std::pair<entt::entity, entt::entity>> entity_pairs(std::span<const Proxy> proxies,
                                                                 std::span<const SapCandidatePair> candidates) {
    std::vector<std::pair<entt::entity, entt::entity>> result;
    result.reserve(candidates.size());
    for (const auto& pair : candidates) {
        const auto lhs = proxies[pair.first].entity;
        const auto rhs = proxies[pair.second].entity;
        result.emplace_back(std::min(lhs, rhs), std::max(lhs, rhs));
    }
    std::ranges::sort(result);
    return result;
}

// Reference ground truth (tasks.md 3.1): the exact, non-conservative overlap
// test used by std.collision.flat.circles_overlap / std.collision.volume.spheres_overlap
// (stdlib/std/collision/flat.cactus, stdlib/std/collision/volume.cactus) — squared
// distance strictly less than squared summed radii. Deliberately independent of
// the broad phase's own AABB overlap test, following the all-pairs scan pattern
// of cactus_dispatch_stdlib_flat_collisions (cpp_entt_codegen.cpp:2228-2247).

[[nodiscard]] bool circles_truly_overlap(const Proxy2D& a, const Proxy2D& b) noexcept {
    const float dx          = b.center.x - a.center.x;
    const float dy          = b.center.y - a.center.y;
    const float radius_sum  = a.radius + b.radius;
    return ((dx * dx) + (dy * dy)) < (radius_sum * radius_sum);
}

[[nodiscard]] bool spheres_truly_overlap(const Proxy3D& a, const Proxy3D& b) noexcept {
    const float dx         = b.center.x - a.center.x;
    const float dy         = b.center.y - a.center.y;
    const float dz         = b.center.z - a.center.z;
    const float radius_sum = a.radius + b.radius;
    return ((dx * dx) + (dy * dy) + (dz * dz)) < (radius_sum * radius_sum);
}

std::vector<std::pair<entt::entity, entt::entity>> brute_force_true_overlaps_2d(std::span<const Proxy2D> proxies) {
    std::vector<std::pair<entt::entity, entt::entity>> result;
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        for (std::size_t j = i + 1; j < proxies.size(); ++j) {
            if (circles_truly_overlap(proxies[i], proxies[j])) {
                result.emplace_back(std::min(proxies[i].entity, proxies[j].entity),
                                    std::max(proxies[i].entity, proxies[j].entity));
            }
        }
    }
    std::ranges::sort(result);
    return result;
}

std::vector<std::pair<entt::entity, entt::entity>> brute_force_true_overlaps_3d(std::span<const Proxy3D> proxies) {
    std::vector<std::pair<entt::entity, entt::entity>> result;
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        for (std::size_t j = i + 1; j < proxies.size(); ++j) {
            if (spheres_truly_overlap(proxies[i], proxies[j])) {
                result.emplace_back(std::min(proxies[i].entity, proxies[j].entity),
                                    std::max(proxies[i].entity, proxies[j].entity));
            }
        }
    }
    std::ranges::sort(result);
    return result;
}

Proxy2D make_proxy_2d(std::uint32_t id, Vector2 center, float radius) {
    return Proxy2D{.entity = entt::entity{id}, .ordinal = id, .center = center, .radius = radius};
}

Proxy3D make_proxy_3d(std::uint32_t id, Vector3 center, float radius) {
    return Proxy3D{.entity = entt::entity{id}, .ordinal = id, .center = center, .radius = radius};
}

// Random proxy configurations (tasks.md 3.2): `extent` controls how tightly
// packed proxies are relative to their radius range — a small extent with a
// large radius range yields a clustered, overlap-heavy configuration; a large
// extent with a small radius range yields a widely separated, mostly
// non-overlapping one. Fixed seed -> fixed configuration, every run.

std::vector<Proxy2D> random_proxies_2d(std::uint32_t seed, std::size_t count, float extent, float min_radius,
                                       float max_radius) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos_dist(-extent, extent);
    std::uniform_real_distribution<float> radius_dist(min_radius, max_radius);
    std::vector<Proxy2D> proxies;
    proxies.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        proxies.push_back(make_proxy_2d(static_cast<std::uint32_t>(i + 1),
                                        Vector2{.x = pos_dist(rng), .y = pos_dist(rng)}, radius_dist(rng)));
    }
    return proxies;
}

std::vector<Proxy3D> random_proxies_3d(std::uint32_t seed, std::size_t count, float extent, float min_radius,
                                       float max_radius) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos_dist(-extent, extent);
    std::uniform_real_distribution<float> radius_dist(min_radius, max_radius);
    std::vector<Proxy3D> proxies;
    proxies.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        proxies.push_back(make_proxy_3d(static_cast<std::uint32_t>(i + 1),
                                        Vector3{.x = pos_dist(rng), .y = pos_dist(rng), .z = pos_dist(rng)},
                                        radius_dist(rng)));
    }
    return proxies;
}

}  // namespace

TEST_CASE("SAP broad phase 2D: empty and single-proxy domains produce no candidates", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;

    broad_phase.sync({});
    CHECK(broad_phase.candidate_pairs().empty());

    const std::vector<Proxy2D> one_proxy{make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F)};
    broad_phase.sync(one_proxy);
    CHECK(broad_phase.candidate_pairs().empty());
}

TEST_CASE("SAP broad phase 2D: two overlapping proxies produce a candidate", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 1.0F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(sorted_pairs(broad_phase.candidate_pairs()) == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});
}

TEST_CASE("SAP broad phase 2D: two widely separated proxies produce no candidate", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 5.0F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(broad_phase.candidate_pairs().empty());
}

TEST_CASE("SAP broad phase 2D: touching bounds produce a candidate", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 2.0F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(sorted_pairs(broad_phase.candidate_pairs()) == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});
}

TEST_CASE("SAP broad phase 2D: mutually-overlapping cluster produces duplicate-free candidates",
         "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 0.1F, .y = 0.0F}, 1.0F),
        make_proxy_2d(3, Vector2{.x = 0.2F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(sorted_pairs(broad_phase.candidate_pairs()) ==
         std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {0, 2}, {1, 2}});
}

TEST_CASE("SAP broad phase 2D: no self-candidates regardless of radius", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 500.0F),
        make_proxy_2d(2, Vector2{.x = 100.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(3, Vector2{.x = 200.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(4, Vector2{.x = 300.0F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    const auto candidates = broad_phase.candidate_pairs();
    for (const auto& pair : candidates) {
        CHECK(pair.first != pair.second);
    }
    // The giant radius on proxy 0 overlaps every other proxy; 2/3 are too far apart to overlap each other.
    CHECK(candidates.size() == 3);
}

TEST_CASE("SAP broad phase 2D: repeated sync on the same input produces identical candidates",
         "[runtime][sap][2d]") {
    SapBroadPhase2D first;
    SapBroadPhase2D second;
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 0.5F, .y = 0.0F}, 1.0F),
        make_proxy_2d(3, Vector2{.x = 5.0F, .y = 5.0F}, 1.0F),
    };
    first.sync(proxies);
    second.sync(proxies);
    CHECK(sorted_pairs(first.candidate_pairs()) == sorted_pairs(second.candidate_pairs()));
}

TEST_CASE("SAP broad phase 3D: empty and single-proxy domains produce no candidates", "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;

    broad_phase.sync({});
    CHECK(broad_phase.candidate_pairs().empty());

    const std::vector<Proxy3D> one_proxy{make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F)};
    broad_phase.sync(one_proxy);
    CHECK(broad_phase.candidate_pairs().empty());
}

TEST_CASE("SAP broad phase 3D: two overlapping proxies produce a candidate", "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(2, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(sorted_pairs(broad_phase.candidate_pairs()) == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});
}

TEST_CASE("SAP broad phase 3D: two widely separated proxies produce no candidate", "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(2, Vector3{.x = 0.0F, .y = 0.0F, .z = 5.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(broad_phase.candidate_pairs().empty());
}

TEST_CASE("SAP broad phase 3D: touching bounds produce a candidate", "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(2, Vector3{.x = 0.0F, .y = 0.0F, .z = 2.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(sorted_pairs(broad_phase.candidate_pairs()) == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});
}

TEST_CASE("SAP broad phase 3D: mutually-overlapping cluster produces duplicate-free candidates",
         "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(2, Vector3{.x = 0.1F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(3, Vector3{.x = 0.2F, .y = 0.0F, .z = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(sorted_pairs(broad_phase.candidate_pairs()) ==
         std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {0, 2}, {1, 2}});
}

TEST_CASE("SAP broad phase 3D: no self-candidates regardless of radius", "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 500.0F),
        make_proxy_3d(2, Vector3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(3, Vector3{.x = 200.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(4, Vector3{.x = 300.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    const auto candidates = broad_phase.candidate_pairs();
    for (const auto& pair : candidates) {
        CHECK(pair.first != pair.second);
    }
    // The giant radius on proxy 0 overlaps every other proxy; 2/3 are too far apart to overlap each other.
    CHECK(candidates.size() == 3);
}

TEST_CASE("SAP broad phase 3D: repeated sync on the same input produces identical candidates",
         "[runtime][sap][3d]") {
    SapBroadPhase3D first;
    SapBroadPhase3D second;
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(2, Vector3{.x = 0.5F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(3, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, 1.0F),
    };
    first.sync(proxies);
    second.sync(proxies);
    CHECK(sorted_pairs(first.candidate_pairs()) == sorted_pairs(second.candidate_pairs()));
}

TEST_CASE("SAP broad phase 2D: primary axis is the largest-spread axis, ties resolve to a fixed axis",
         "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;

    const std::vector<Proxy2D> y_spread{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.1F),
        make_proxy_2d(2, Vector2{.x = 1.0F, .y = 5.0F}, 0.1F),
        make_proxy_2d(3, Vector2{.x = 2.0F, .y = 10.0F}, 0.1F),
    };
    broad_phase.sync(y_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 1);

    const std::vector<Proxy2D> x_spread{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.1F),
        make_proxy_2d(2, Vector2{.x = 5.0F, .y = 1.0F}, 0.1F),
        make_proxy_2d(3, Vector2{.x = 10.0F, .y = 2.0F}, 0.1F),
    };
    broad_phase.sync(x_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 0);

    const std::vector<Proxy2D> tied_spread{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.1F),
        make_proxy_2d(2, Vector2{.x = 5.0F, .y = 5.0F}, 0.1F),
    };
    broad_phase.sync(tied_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 0);
    broad_phase.sync(tied_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 0);
}

TEST_CASE("SAP broad phase 3D: primary axis is the largest-spread axis, ties resolve to a fixed axis",
         "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;

    const std::vector<Proxy3D> z_spread{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.1F),
        make_proxy_3d(2, Vector3{.x = 1.0F, .y = 2.0F, .z = 10.0F}, 0.1F),
    };
    broad_phase.sync(z_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 2);

    const std::vector<Proxy3D> tied_spread{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.1F),
        make_proxy_3d(2, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, 0.1F),
    };
    broad_phase.sync(tied_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 0);
    broad_phase.sync(tied_spread);
    CHECK(broad_phase.primary_axis_for_testing() == 0);
}

TEST_CASE("SAP broad phase 2D: small-domain brute-force fallback agrees with the swept path",
         "[runtime][sap][2d]") {
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 0.5F, .y = 0.3F}, 1.0F),
        make_proxy_2d(3, Vector2{.x = 3.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(4, Vector2{.x = 3.4F, .y = 0.1F}, 1.0F),
        make_proxy_2d(5, Vector2{.x = 10.0F, .y = 10.0F}, 1.0F),
        make_proxy_2d(6, Vector2{.x = -5.0F, .y = 2.0F}, 2.0F),
    };

    SapBroadPhase2D forced_swept;
    forced_swept.set_small_domain_threshold_for_testing(0);
    forced_swept.sync(proxies);

    SapBroadPhase2D forced_brute_force;
    forced_brute_force.set_small_domain_threshold_for_testing(1'000'000);
    forced_brute_force.sync(proxies);

    CHECK(sorted_pairs(forced_swept.candidate_pairs()) == sorted_pairs(forced_brute_force.candidate_pairs()));
}

TEST_CASE("SAP broad phase 3D: small-domain brute-force fallback agrees with the swept path",
         "[runtime][sap][3d]") {
    const std::vector<Proxy3D> proxies{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(2, Vector3{.x = 0.5F, .y = 0.3F, .z = 0.2F}, 1.0F),
        make_proxy_3d(3, Vector3{.x = 3.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        make_proxy_3d(4, Vector3{.x = 3.4F, .y = 0.1F, .z = 0.0F}, 1.0F),
        make_proxy_3d(5, Vector3{.x = 10.0F, .y = 10.0F, .z = 10.0F}, 1.0F),
        make_proxy_3d(6, Vector3{.x = -5.0F, .y = 2.0F, .z = 1.0F}, 2.0F),
    };

    SapBroadPhase3D forced_swept;
    forced_swept.set_small_domain_threshold_for_testing(0);
    forced_swept.sync(proxies);

    SapBroadPhase3D forced_brute_force;
    forced_brute_force.set_small_domain_threshold_for_testing(1'000'000);
    forced_brute_force.sync(proxies);

    CHECK(sorted_pairs(forced_swept.candidate_pairs()) == sorted_pairs(forced_brute_force.candidate_pairs()));
}

TEST_CASE("SAP broad phase 2D: a proxy inserted after spawn participates on its first sync", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> before{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.5F),
        make_proxy_2d(2, Vector2{.x = 5.0F, .y = 0.0F}, 0.5F),
    };
    broad_phase.sync(before);
    CHECK(broad_phase.candidate_pairs().empty());

    const std::vector<Proxy2D> after{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.5F),
        make_proxy_2d(2, Vector2{.x = 5.0F, .y = 0.0F}, 0.5F),
        make_proxy_2d(3, Vector2{.x = 0.3F, .y = 0.0F}, 0.5F),
    };
    broad_phase.sync(after);
    CHECK(entity_pairs<Proxy2D>(after, broad_phase.candidate_pairs()) ==
         std::vector<std::pair<entt::entity, entt::entity>>{{entt::entity{1}, entt::entity{3}}});
}

TEST_CASE("SAP broad phase 2D: a destroyed proxy stops appearing in any candidate once removal is synced",
         "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> before{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(2, Vector2{.x = 0.5F, .y = 0.0F}, 1.0F),
        make_proxy_2d(3, Vector2{.x = 20.0F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(before);
    CHECK(entity_pairs<Proxy2D>(before, broad_phase.candidate_pairs()) ==
         std::vector<std::pair<entt::entity, entt::entity>>{{entt::entity{1}, entt::entity{2}}});

    // Entity 2 destroyed; the next sync's span no longer includes it.
    const std::vector<Proxy2D> after{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(3, Vector2{.x = 20.0F, .y = 0.0F}, 1.0F),
    };
    broad_phase.sync(after);
    CHECK(broad_phase.candidate_pairs().empty());
}

TEST_CASE(
    "SAP broad phase 2D: a moved proxy's candidates reflect its new neighbors after a large primary-axis reorder",
    "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> before{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(2, Vector2{.x = 10.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(3, Vector2{.x = 20.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(4, Vector2{.x = 30.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(5, Vector2{.x = 40.0F, .y = 0.0F}, 0.4F),
    };
    broad_phase.sync(before);
    CHECK(broad_phase.candidate_pairs().empty());

    // Entity 5 moves from the far end of the sweep to right next to entity 2,
    // reordering most of the primary-axis (X) sort.
    const std::vector<Proxy2D> after{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(2, Vector2{.x = 10.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(3, Vector2{.x = 20.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(4, Vector2{.x = 30.0F, .y = 0.0F}, 0.4F),
        make_proxy_2d(5, Vector2{.x = 10.5F, .y = 0.0F}, 0.4F),
    };
    broad_phase.sync(after);
    CHECK(entity_pairs<Proxy2D>(after, broad_phase.candidate_pairs()) ==
         std::vector<std::pair<entt::entity, entt::entity>>{{entt::entity{2}, entt::entity{5}}});
}

TEST_CASE("SAP broad phase 2D: a resized proxy's candidates reflect its new radius", "[runtime][sap][2d]") {
    SapBroadPhase2D broad_phase;
    const std::vector<Proxy2D> before{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 0.5F),
        make_proxy_2d(2, Vector2{.x = 3.0F, .y = 0.0F}, 0.5F),
    };
    broad_phase.sync(before);
    CHECK(broad_phase.candidate_pairs().empty());

    const std::vector<Proxy2D> after{
        make_proxy_2d(1, Vector2{.x = 0.0F, .y = 0.0F}, 3.0F),
        make_proxy_2d(2, Vector2{.x = 3.0F, .y = 0.0F}, 0.5F),
    };
    broad_phase.sync(after);
    CHECK(entity_pairs<Proxy2D>(after, broad_phase.candidate_pairs()) ==
         std::vector<std::pair<entt::entity, entt::entity>>{{entt::entity{1}, entt::entity{2}}});
}

TEST_CASE("SAP broad phase 3D: proxy-set changes across syncs (spawn/destroy/move/resize) are all reflected",
         "[runtime][sap][3d]") {
    SapBroadPhase3D broad_phase;
    const entt::entity a{1};
    const entt::entity c{3};

    // Spawn: a and b start far apart with no candidates; c then appears next to a.
    broad_phase.sync(std::vector<Proxy3D>{
        make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.5F),
        make_proxy_3d(2, Vector3{.x = 5.0F, .y = 0.0F, .z = 0.0F}, 0.5F),
    });
    CHECK(broad_phase.candidate_pairs().empty());

    {
        const std::vector<Proxy3D> proxies{
            make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.5F),
            make_proxy_3d(2, Vector3{.x = 5.0F, .y = 0.0F, .z = 0.0F}, 0.5F),
            make_proxy_3d(3, Vector3{.x = 0.5F, .y = 0.0F, .z = 0.0F}, 0.5F),
        };
        broad_phase.sync(proxies);
        CHECK(entity_pairs<Proxy3D>(proxies, broad_phase.candidate_pairs()) ==
             std::vector<std::pair<entt::entity, entt::entity>>{{a, c}});
    }

    // Destroy: b leaves the synced set; a/c remain candidates.
    {
        const std::vector<Proxy3D> proxies{
            make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.5F),
            make_proxy_3d(3, Vector3{.x = 0.5F, .y = 0.0F, .z = 0.0F}, 0.5F),
        };
        broad_phase.sync(proxies);
        CHECK(entity_pairs<Proxy3D>(proxies, broad_phase.candidate_pairs()) ==
             std::vector<std::pair<entt::entity, entt::entity>>{{a, c}});
    }

    // Move: c moves far away from a; no candidates remain.
    {
        const std::vector<Proxy3D> proxies{
            make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.5F),
            make_proxy_3d(3, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, 0.5F),
        };
        broad_phase.sync(proxies);
        CHECK(broad_phase.candidate_pairs().empty());
    }

    // Resize: a grows large enough to reach c again.
    {
        const std::vector<Proxy3D> proxies{
            make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 6.0F),
            make_proxy_3d(3, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, 0.5F),
        };
        broad_phase.sync(proxies);
        CHECK(entity_pairs<Proxy3D>(proxies, broad_phase.candidate_pairs()) ==
             std::vector<std::pair<entt::entity, entt::entity>>{{a, c}});
    }
}

TEST_CASE("SAP broad phase 2D: every truly overlapping pair is present in the candidate set (randomized, clustered)",
         "[runtime][sap][2d][randomized]") {
    std::size_t reference_pairs_found = 0;
    for (const std::uint32_t seed : {101U, 202U, 303U, 404U, 505U, 606U, 707U, 808U}) {
        const auto proxies = random_proxies_2d(seed, 40, /*extent=*/3.0F, /*min_radius=*/0.1F, /*max_radius=*/1.2F);
        SapBroadPhase2D broad_phase;
        broad_phase.sync(proxies);
        const auto candidates = entity_pairs<Proxy2D>(proxies, broad_phase.candidate_pairs());
        const auto reference  = brute_force_true_overlaps_2d(proxies);
        reference_pairs_found += reference.size();
        for (const auto& pair : reference) {
            CHECK(std::ranges::binary_search(candidates, pair));
        }
    }
    CHECK(reference_pairs_found > 0);
}

TEST_CASE(
    "SAP broad phase 2D: every truly overlapping pair is present in the candidate set (randomized, widely separated)",
    "[runtime][sap][2d][randomized]") {
    for (const std::uint32_t seed : {101U, 202U, 303U, 404U, 505U, 606U, 707U, 808U}) {
        const auto proxies = random_proxies_2d(seed, 40, /*extent=*/60.0F, /*min_radius=*/0.05F, /*max_radius=*/0.3F);
        SapBroadPhase2D broad_phase;
        broad_phase.sync(proxies);
        const auto candidates = entity_pairs<Proxy2D>(proxies, broad_phase.candidate_pairs());
        for (const auto& pair : brute_force_true_overlaps_2d(proxies)) {
            CHECK(std::ranges::binary_search(candidates, pair));
        }
    }
}

TEST_CASE("SAP broad phase 3D: every truly overlapping pair is present in the candidate set (randomized, clustered)",
         "[runtime][sap][3d][randomized]") {
    std::size_t reference_pairs_found = 0;
    for (const std::uint32_t seed : {111U, 222U, 333U, 444U, 555U, 666U, 777U, 888U}) {
        const auto proxies = random_proxies_3d(seed, 40, /*extent=*/3.0F, /*min_radius=*/0.1F, /*max_radius=*/1.2F);
        SapBroadPhase3D broad_phase;
        broad_phase.sync(proxies);
        const auto candidates = entity_pairs<Proxy3D>(proxies, broad_phase.candidate_pairs());
        const auto reference  = brute_force_true_overlaps_3d(proxies);
        reference_pairs_found += reference.size();
        for (const auto& pair : reference) {
            CHECK(std::ranges::binary_search(candidates, pair));
        }
    }
    CHECK(reference_pairs_found > 0);
}

TEST_CASE(
    "SAP broad phase 3D: every truly overlapping pair is present in the candidate set (randomized, widely separated)",
    "[runtime][sap][3d][randomized]") {
    for (const std::uint32_t seed : {111U, 222U, 333U, 444U, 555U, 666U, 777U, 888U}) {
        const auto proxies = random_proxies_3d(seed, 40, /*extent=*/60.0F, /*min_radius=*/0.05F, /*max_radius=*/0.3F);
        SapBroadPhase3D broad_phase;
        broad_phase.sync(proxies);
        const auto candidates = entity_pairs<Proxy3D>(proxies, broad_phase.candidate_pairs());
        for (const auto& pair : brute_force_true_overlaps_3d(proxies)) {
            CHECK(std::ranges::binary_search(candidates, pair));
        }
    }
}

// ── sap_execute_pair_tuples: self-tuple merge, directed expansion, resort ───

TEST_CASE("sap_execute_pair_tuples 2D: self-tuples are always included, even with no candidates",
         "[runtime][sap][tuples]") {
    const std::vector<Proxy2D> proxies{
        make_proxy_2d(10, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        make_proxy_2d(20, Vector2{.x = 100.0F, .y = 0.0F}, 1.0F),
    };
    std::vector<std::pair<entt::entity, entt::entity>> observed;
    sap_execute_pair_tuples(
        proxies, {}, [&](entt::entity left, entt::entity right) { observed.emplace_back(left, right); });

    CHECK(observed == std::vector<std::pair<entt::entity, entt::entity>>{
                          {entt::entity{10}, entt::entity{10}},
                          {entt::entity{20}, entt::entity{20}},
                      });
}

TEST_CASE("sap_execute_pair_tuples 2D: a candidate pair expands into both directed tuples, sorted by ordinal",
         "[runtime][sap][tuples]") {
    // Index 0 deliberately carries the *higher* creation ordinal, so a test
    // failure here would reveal a sort keyed on array index instead of
    // ordinal.
    const auto high_ordinal = make_proxy_2d(5, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F);
    const auto low_ordinal  = make_proxy_2d(2, Vector2{.x = 1.0F, .y = 0.0F}, 1.0F);
    const std::vector<Proxy2D> proxies{high_ordinal, low_ordinal};
    const std::vector<SapCandidatePair> candidates{SapCandidatePair{.first = 0, .second = 1}};

    std::vector<std::pair<entt::entity, entt::entity>> observed;
    sap_execute_pair_tuples(
        proxies, candidates, [&](entt::entity left, entt::entity right) { observed.emplace_back(left, right); });

    CHECK(observed == std::vector<std::pair<entt::entity, entt::entity>>{
                          {low_ordinal.entity, low_ordinal.entity},
                          {low_ordinal.entity, high_ordinal.entity},
                          {high_ordinal.entity, low_ordinal.entity},
                          {high_ordinal.entity, high_ordinal.entity},
                      });
}

TEST_CASE("sap_execute_pair_tuples 3D: self-tuples and directed expansion both hold", "[runtime][sap][tuples]") {
    const auto p1 = make_proxy_3d(1, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    const auto p2 = make_proxy_3d(2, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    const auto p3 = make_proxy_3d(3, Vector3{.x = 50.0F, .y = 50.0F, .z = 50.0F}, 1.0F);
    const std::vector<Proxy3D> proxies{p1, p2, p3};
    const std::vector<SapCandidatePair> candidates{SapCandidatePair{.first = 0, .second = 1}};

    std::vector<std::pair<entt::entity, entt::entity>> observed;
    sap_execute_pair_tuples(
        proxies, candidates, [&](entt::entity left, entt::entity right) { observed.emplace_back(left, right); });

    CHECK(observed == std::vector<std::pair<entt::entity, entt::entity>>{
                          {p1.entity, p1.entity},
                          {p1.entity, p2.entity},
                          {p2.entity, p1.entity},
                          {p2.entity, p2.entity},
                          {p3.entity, p3.entity},
                      });
}

// ── Candidate-generation benchmark (tasks.md 6.4) ────────────────────────────
// Hidden ([.]) so ctest's default run stays fast; run explicitly with
// `test_runtime_sap_broadphase.exe "[benchmark]"`. Compares the brute-force
// fallback against the swept path across representative ball counts, using a
// bouncy-balls-3d-shaped proxy distribution (box extent, radius range), to
// pick 1.5's small-domain threshold from real crossover data rather than a
// guess. 2D shares the identical templated sweep/brute-force implementation
// (runtime.cpp), so the 3D crossover found here is representative for both.

TEST_CASE("SAP broad phase 3D: candidate-generation benchmark, brute-force vs swept across representative ball counts",
         "[.][benchmark][runtime][sap][3d]") {
    for (const std::size_t count :
        {std::size_t{8}, std::size_t{32}, std::size_t{64}, std::size_t{128}, std::size_t{256}, std::size_t{512},
         std::size_t{1024}}) {
        const auto proxies =
            random_proxies_3d(/*seed=*/12345, count, /*extent=*/3.0F, /*min_radius=*/0.15F, /*max_radius=*/0.42F);

        SapBroadPhase3D brute_force;
        brute_force.set_small_domain_threshold_for_testing(1'000'000);
        BENCHMARK("brute-force candidate generation, N=" + std::to_string(count)) {
            brute_force.sync(proxies);
            return brute_force.candidate_pairs().size();
        };

        SapBroadPhase3D swept;
        swept.set_small_domain_threshold_for_testing(0);
        BENCHMARK("swept candidate generation, N=" + std::to_string(count)) {
            swept.sync(proxies);
            return swept.candidate_pairs().size();
        };
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
