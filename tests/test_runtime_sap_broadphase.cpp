// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace cactus::runtime::entt_backend;
using cactus::runtime::Quat;
namespace quat = cactus::runtime::stdlib::math::quat;

namespace {

using EntityPair = std::pair<entt::entity, entt::entity>;

// Bounds carry a tiny rounding slack, so compare them with a margin.
Catch::Approx near(float value) {
    return Catch::Approx(value).margin(1e-3);
}

template <typename Proxy>
std::vector<EntityPair> candidate_entities(std::span<const Proxy> proxies, std::span<const SapCandidatePair> pairs) {
    std::vector<EntityPair> result;
    result.reserve(pairs.size());
    for (const auto& pair : pairs) {
        result.emplace_back(proxies[pair.left].entity, proxies[pair.right].entity);
    }
    return result;
}

ProxyAabb2D circle(std::uint32_t id, SapSide side, Vector2 center, float radius) {
    return circle_proxy(entt::entity{id}, id, side, center, radius);
}

ProxyAabb3D sphere(std::uint32_t id, SapSide side, Vector3 center, float radius) {
    return sphere_proxy(entt::entity{id}, id, side, center, radius);
}

ProxyAabb3D box(std::uint32_t id, SapSide side, Vector3 center, Vector3 size, Quat rotation) {
    return box_proxy(entt::entity{id}, id, side, center, size, rotation);
}

bool aabbs_overlap(const ProxyAabb2D& lhs, const ProxyAabb2D& rhs) {
    return lhs.max.x >= rhs.min.x && rhs.max.x >= lhs.min.x && lhs.max.y >= rhs.min.y && rhs.max.y >= lhs.min.y;
}

bool aabbs_overlap(const ProxyAabb3D& lhs, const ProxyAabb3D& rhs) {
    return lhs.max.x >= rhs.min.x && rhs.max.x >= lhs.min.x && lhs.max.y >= rhs.min.y && rhs.max.y >= lhs.min.y &&
           lhs.max.z >= rhs.min.z && rhs.max.z >= lhs.min.z;
}

// The exact AABB product the broad phase must reproduce, in (left ordinal, right ordinal) order.
template <typename Proxy>
std::vector<EntityPair> reference_aabb_product(std::span<const Proxy> proxies) {
    std::vector<const Proxy*> left;
    std::vector<const Proxy*> right;
    for (const auto& proxy : proxies) {
        (proxy.side == SapSide::Left ? left : right).push_back(&proxy);
    }
    std::ranges::sort(left, {}, &Proxy::ordinal);
    std::ranges::sort(right, {}, &Proxy::ordinal);
    std::vector<EntityPair> result;
    for (const auto* lhs : left) {
        for (const auto* rhs : right) {
            if (aabbs_overlap(*lhs, *rhs)) {
                result.emplace_back(lhs->entity, rhs->entity);
            }
        }
    }
    return result;
}

std::vector<ProxyAabb3D> random_spheres(std::uint32_t seed, std::size_t count, float extent, float max_radius) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> position(-extent, extent);
    std::uniform_real_distribution<float> radius(0.05F, max_radius);
    std::bernoulli_distribution left_side(0.5);
    std::vector<ProxyAabb3D> proxies;
    for (std::uint32_t id = 1; id <= count; ++id) {
        proxies.push_back(sphere(id,
                                 left_side(rng) ? SapSide::Left : SapSide::Right,
                                 Vector3{.x = position(rng), .y = position(rng), .z = position(rng)},
                                 radius(rng)));
    }
    return proxies;
}

// Mirrors std.collision.volume.sphere_box_overlap.
bool sphere_box_overlap(Vector3 center, float radius, Vector3 box_center, Vector3 size, Quat rotation) {
    const float safe_radius = std::max(radius, 0.0F);
    const Vector3 half{std::abs(size.x) / 2.0F, std::abs(size.y) / 2.0F, std::abs(size.z) / 2.0F};
    const Vector3 local = quat::rotate(quat::inverse(rotation), Vector3Subtract(center, box_center));
    const Vector3 clamped{std::clamp(local.x, -half.x, half.x),
                          std::clamp(local.y, -half.y, half.y),
                          std::clamp(local.z, -half.z, half.z)};
    const Vector3 delta    = Vector3Subtract(local, clamped);
    const float distance_squared = Vector3DotProduct(delta, delta);
    if (distance_squared > 0.0F) {
        return distance_squared < safe_radius * safe_radius;
    }
    const float inside = std::min({half.x - std::abs(local.x), half.y - std::abs(local.y), half.z - std::abs(local.z)});
    return safe_radius > 0.0F || inside > 0.0F;
}

}  // namespace

// ── Proxy bounds ─────────────────────────────────────────────────────────────

TEST_CASE("SAP proxies: a circle's bounds are its center plus and minus its radius", "[runtime][sap][bounds]") {
    const auto proxy = circle(7, SapSide::Right, Vector2{.x = 1.0F, .y = -2.0F}, 0.5F);
    CHECK(proxy.entity == entt::entity{7});
    CHECK(proxy.ordinal == 7);
    CHECK(proxy.side == SapSide::Right);
    CHECK(proxy.min.x == near(0.5F));
    CHECK(proxy.min.y == near(-2.5F));
    CHECK(proxy.max.x == near(1.5F));
    CHECK(proxy.max.y == near(-1.5F));
}

TEST_CASE("SAP proxies: a negative radius bounds by its magnitude", "[runtime][sap][bounds]") {
    // spheres_overlap squares the radius sum, so two negative radii still overlap.
    const auto proxy = sphere(1, SapSide::Left, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, -2.0F);
    CHECK(proxy.min.x == near(-2.0F));
    CHECK(proxy.max.z == near(2.0F));
}

TEST_CASE("SAP proxies: an unrotated box bounds by half its size", "[runtime][sap][bounds]") {
    const auto proxy = box(1, SapSide::Right, Vector3{.x = 1.0F, .y = 2.0F, .z = 3.0F},
                           Vector3{.x = 2.0F, .y = -4.0F, .z = 6.0F}, quat::identity());
    CHECK(proxy.min.x == near(0.0F));
    CHECK(proxy.min.y == near(0.0F));
    CHECK(proxy.min.z == near(0.0F));
    CHECK(proxy.max.x == near(2.0F));
    CHECK(proxy.max.y == near(4.0F));
    CHECK(proxy.max.z == near(6.0F));
}

TEST_CASE("SAP proxies: a rotated box bounds its rotated corners", "[runtime][sap][bounds]") {
    const auto rotation = quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, std::numbers::pi_v<float> / 4);
    const auto proxy    = box(1, SapSide::Right, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                              Vector3{.x = 2.0F, .y = 2.0F, .z = 2.0F}, rotation);
    CHECK(proxy.max.x >= std::numbers::sqrt2_v<float> - 1e-4F);
    CHECK(proxy.max.x == near(std::numbers::sqrt2_v<float>));
    CHECK(proxy.max.y == near(1.0F));
    CHECK(proxy.min.z <= -std::numbers::sqrt2_v<float> + 1e-4F);
}

TEST_CASE("SAP proxies: a zero rotation bounds like the identity", "[runtime][sap][bounds]") {
    const auto proxy = box(1, SapSide::Right, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                           Vector3{.x = 2.0F, .y = 4.0F, .z = 6.0F}, Quat{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F});
    CHECK(proxy.max.x == near(1.0F));
    CHECK(proxy.max.y == near(2.0F));
    CHECK(proxy.max.z == near(3.0F));
}

// ── Candidate generation ─────────────────────────────────────────────────────

TEST_CASE("SAP broad phase: an empty side produces no candidates", "[runtime][sap]") {
    const std::vector<ProxyAabb2D> proxies{
        circle(1, SapSide::Left, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        circle(2, SapSide::Left, Vector2{.x = 0.5F, .y = 0.0F}, 1.0F),
    };
    SapBroadPhase2D broad_phase;
    broad_phase.sync(proxies);
    CHECK(broad_phase.candidate_pairs().empty());
}

TEST_CASE("SAP broad phase: only left-right overlaps are candidates", "[runtime][sap]") {
    const std::vector<ProxyAabb2D> proxies{
        circle(1, SapSide::Left, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        circle(2, SapSide::Left, Vector2{.x = 0.5F, .y = 0.0F}, 1.0F),
        circle(3, SapSide::Right, Vector2{.x = 1.5F, .y = 0.0F}, 0.2F),
        circle(4, SapSide::Right, Vector2{.x = 1.6F, .y = 0.0F}, 0.2F),
        circle(5, SapSide::Right, Vector2{.x = 50.0F, .y = 0.0F}, 0.2F),
    };
    for (const std::size_t threshold : {std::size_t{0}, std::size_t{1'000'000}}) {
        SapBroadPhase2D broad_phase;
        broad_phase.set_small_domain_threshold_for_testing(threshold);
        broad_phase.sync(proxies);
        CHECK(candidate_entities<ProxyAabb2D>(proxies, broad_phase.candidate_pairs()) ==
              std::vector<EntityPair>{{entt::entity{2}, entt::entity{3}}, {entt::entity{2}, entt::entity{4}}});
    }
}

TEST_CASE("SAP broad phase: touching bounds produce a candidate", "[runtime][sap]") {
    const std::vector<ProxyAabb3D> proxies{
        sphere(1, SapSide::Left, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        sphere(2, SapSide::Right, Vector3{.x = 2.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
    };
    SapBroadPhase3D broad_phase;
    broad_phase.sync(proxies);
    CHECK(broad_phase.candidate_pairs().size() == 1);
}

TEST_CASE("SAP broad phase: an entity on both sides yields its self pair", "[runtime][sap]") {
    for (const std::size_t threshold : {std::size_t{0}, std::size_t{1'000'000}}) {
        const std::vector<ProxyAabb3D> proxies{
            sphere(1, SapSide::Left, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
            box(1, SapSide::Right, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                quat::identity()),
        };
        SapBroadPhase3D broad_phase;
        broad_phase.set_small_domain_threshold_for_testing(threshold);
        broad_phase.sync(proxies);
        CHECK(candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs()) ==
              std::vector<EntityPair>{{entt::entity{1}, entt::entity{1}}});
    }
}

TEST_CASE("SAP broad phase: candidates come in left-ordinal, right-ordinal order", "[runtime][sap]") {
    // Input order deliberately disagrees with ordinal order on both sides.
    const std::vector<ProxyAabb2D> proxies{
        circle(9, SapSide::Right, Vector2{.x = 0.0F, .y = 0.0F}, 1.0F),
        circle(5, SapSide::Left, Vector2{.x = 0.1F, .y = 0.0F}, 1.0F),
        circle(4, SapSide::Right, Vector2{.x = 0.2F, .y = 0.0F}, 1.0F),
        circle(2, SapSide::Left, Vector2{.x = 0.3F, .y = 0.0F}, 1.0F),
    };
    for (const std::size_t threshold : {std::size_t{0}, std::size_t{1'000'000}}) {
        SapBroadPhase2D broad_phase;
        broad_phase.set_small_domain_threshold_for_testing(threshold);
        broad_phase.sync(proxies);
        CHECK(candidate_entities<ProxyAabb2D>(proxies, broad_phase.candidate_pairs()) ==
              std::vector<EntityPair>{{entt::entity{2}, entt::entity{4}},
                                      {entt::entity{2}, entt::entity{9}},
                                      {entt::entity{5}, entt::entity{4}},
                                      {entt::entity{5}, entt::entity{9}}});
    }
}

TEST_CASE("SAP broad phase: a resync reflects moved and removed proxies", "[runtime][sap]") {
    SapBroadPhase3D broad_phase;
    std::vector<ProxyAabb3D> proxies{
        sphere(1, SapSide::Left, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        sphere(2, SapSide::Right, Vector3{.x = 10.0F, .y = 0.0F, .z = 0.0F}, 1.0F),
        sphere(3, SapSide::Right, Vector3{.x = 0.5F, .y = 0.0F, .z = 0.0F}, 1.0F),
    };
    broad_phase.sync(proxies);
    CHECK(candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs()) ==
          std::vector<EntityPair>{{entt::entity{1}, entt::entity{3}}});

    proxies[1] = sphere(2, SapSide::Right, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    proxies.pop_back();
    broad_phase.sync(proxies);
    CHECK(candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs()) ==
          std::vector<EntityPair>{{entt::entity{1}, entt::entity{2}}});
}

TEST_CASE("SAP broad phase: primary axis is the largest-spread axis", "[runtime][sap]") {
    const std::vector<ProxyAabb3D> proxies{
        sphere(1, SapSide::Left, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.1F),
        sphere(2, SapSide::Right, Vector3{.x = 1.0F, .y = 2.0F, .z = 9.0F}, 0.1F),
    };
    SapBroadPhase3D broad_phase;
    broad_phase.sync(proxies);
    CHECK(broad_phase.primary_axis_for_testing() == 2);

    const std::vector<ProxyAabb2D> tied{
        circle(1, SapSide::Left, Vector2{.x = 0.0F, .y = 0.0F}, 0.1F),
        circle(2, SapSide::Right, Vector2{.x = 3.0F, .y = 3.0F}, 0.1F),
    };
    SapBroadPhase2D tied_phase;
    tied_phase.sync(tied);
    CHECK(tied_phase.primary_axis_for_testing() == 0);
}

TEST_CASE("SAP broad phase: the global threshold override forces a strategy", "[runtime][sap]") {
    const auto proxies = random_spheres(42, 60, 3.0F, 0.8F);
    SapBroadPhase3D broad_phase;
    set_sap_small_domain_threshold_override_for_testing(0);
    CHECK(sap_small_domain_threshold_override_for_testing() == 0);
    broad_phase.sync(proxies);
    const auto swept = candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs());
    set_sap_small_domain_threshold_override_for_testing(std::nullopt);
    broad_phase.set_small_domain_threshold_for_testing(1'000'000);
    broad_phase.sync(proxies);
    CHECK(candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs()) == swept);
}

TEST_CASE("SAP broad phase: swept and brute-force candidates equal the exact AABB product (randomized)",
          "[runtime][sap][randomized]") {
    std::size_t total = 0;
    for (const std::uint32_t seed : {11U, 22U, 33U, 44U, 55U, 66U, 77U, 88U}) {
        for (const float extent : {3.0F, 40.0F}) {
            const auto proxies   = random_spheres(seed, 80, extent, 1.0F);
            const auto reference = reference_aabb_product<ProxyAabb3D>(proxies);
            total += reference.size();
            for (const std::size_t threshold : {std::size_t{0}, std::size_t{1'000'000}}) {
                SapBroadPhase3D broad_phase;
                broad_phase.set_small_domain_threshold_for_testing(threshold);
                broad_phase.sync(proxies);
                CHECK(candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs()) == reference);
            }
        }
    }
    CHECK(total > 0);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> position(-4.0F, 4.0F);
    std::vector<ProxyAabb2D> flat;
    for (std::uint32_t id = 1; id <= 60; ++id) {
        flat.push_back(circle(id, id % 3 == 0 ? SapSide::Left : SapSide::Right,
                              Vector2{.x = position(rng), .y = position(rng)}, 0.6F));
    }
    SapBroadPhase2D flat_phase;
    flat_phase.set_small_domain_threshold_for_testing(0);
    flat_phase.sync(flat);
    CHECK(candidate_entities<ProxyAabb2D>(flat, flat_phase.candidate_pairs()) == reference_aabb_product<ProxyAabb2D>(flat));
}

TEST_CASE("SAP broad phase: every sphere-box overlap is a candidate (randomized rotated boxes)",
          "[runtime][sap][randomized]") {
    std::mt19937 rng(20261003);
    std::uniform_real_distribution<float> position(-3.0F, 3.0F);
    std::uniform_real_distribution<float> extent(-1.5F, 1.5F);
    std::uniform_real_distribution<float> component(-1.0F, 1.0F);
    std::uniform_real_distribution<float> radius(-0.3F, 0.8F);
    std::size_t overlaps = 0;
    for (int round = 0; round < 20; ++round) {
        std::vector<ProxyAabb3D> proxies;
        std::vector<std::pair<Vector3, float>> spheres;
        std::vector<std::tuple<Vector3, Vector3, Quat>> boxes;
        for (std::uint32_t id = 0; id < 30; ++id) {
            const Vector3 center{position(rng), position(rng), position(rng)};
            const float r = radius(rng);
            spheres.emplace_back(center, r);
            proxies.push_back(sphere(id, SapSide::Left, center, r));
        }
        for (std::uint32_t id = 0; id < 30; ++id) {
            const Vector3 center{position(rng), position(rng), position(rng)};
            const Vector3 size{extent(rng), extent(rng), extent(rng)};
            const Quat rotation{component(rng), component(rng), component(rng), component(rng)};
            boxes.emplace_back(center, size, rotation);
            proxies.push_back(box(100 + id, SapSide::Right, center, size, rotation));
        }
        SapBroadPhase3D broad_phase;
        broad_phase.set_small_domain_threshold_for_testing(0);
        broad_phase.sync(proxies);
        const auto candidates = candidate_entities<ProxyAabb3D>(proxies, broad_phase.candidate_pairs());
        for (std::uint32_t s = 0; s < spheres.size(); ++s) {
            for (std::uint32_t b = 0; b < boxes.size(); ++b) {
                const auto& [box_center, size, rotation] = boxes[b];
                if (!sphere_box_overlap(spheres[s].first, spheres[s].second, box_center, size, rotation)) {
                    continue;
                }
                ++overlaps;
                CHECK(std::ranges::find(candidates, EntityPair{entt::entity{s}, entt::entity{100 + b}}) !=
                      candidates.end());
            }
        }
    }
    CHECK(overlaps > 0);
}

// ── Candidate-generation benchmark ───────────────────────────────────────────
// Hidden ([.]) so ctest's default run stays fast; run explicitly with
// `test_runtime_sap_broadphase.exe "[benchmark]"`. Each count is split evenly
// between the two sides, so the product size is (N/2)^2.

TEST_CASE("SAP broad phase 3D: candidate-generation benchmark, brute-force vs swept across representative counts",
          "[.][benchmark][runtime][sap][3d]") {
    for (const std::size_t count :
         {std::size_t{16}, std::size_t{64}, std::size_t{128}, std::size_t{256}, std::size_t{512}, std::size_t{1024}}) {
        auto proxies = random_spheres(12345, count, 3.0F, 0.42F);
        for (std::size_t i = 0; i < proxies.size(); ++i) {
            proxies[i].side = i % 2 == 0 ? SapSide::Left : SapSide::Right;
        }

        SapBroadPhase3D brute_force;
        brute_force.set_small_domain_threshold_for_testing(std::numeric_limits<std::size_t>::max());
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
