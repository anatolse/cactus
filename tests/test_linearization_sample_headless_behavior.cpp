// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "linearization_check.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Spin = cactus_gen::linearization_sample_runtime::Spin;

}  // namespace

TEST_CASE("every linear extension of the sample gives the stable order's world",
          "[runtime][codegen-entt][handler-execution-graph]") {
    cactus::test::require_linearizations_match(8, cactus::test::no_setup());
}

TEST_CASE("every unordered pair of the sample gives the same world in both orders",
          "[runtime][codegen-entt][handler-execution-graph]") {
    namespace backend = cactus::runtime::entt_backend;
    CHECK(cactus::test::require_pair_swaps_match(8, cactus::test::no_setup()) > 0);
    const auto tick_handler = [](std::string_view rule) {
        return "linearization_sample_runtime." + std::string(rule) + "/on std.core.tick";
    };
    const auto listed = [&](std::string_view first, std::string_view second) {
        return std::ranges::contains(
            backend::linearization_pairs(),
            backend::LinearizationPair{.first = tick_handler(first), .second = tick_handler(second)});
    };
    CHECK(listed("Rotate", "Move"));
    // Warm writes the level Shine reads, so the schedule orders them.
    CHECK_FALSE(listed("Warm", "Shine"));
    CHECK_FALSE(listed("Shine", "Warm"));
}

TEST_CASE("the sample's world changes across frames", "[runtime][codegen-entt][handler-execution-graph]") {
    const auto digests = cactus::test::linearization_digests(std::nullopt, 3, cactus::test::no_setup());
    REQUIRE(digests.size() == 3);
    CHECK(digests[0] != digests[1]);
    CHECK(digests[1] != digests[2]);
}

TEST_CASE("equal worlds digest equal and a one-bit float change does not",
          "[runtime][codegen-entt][handler-execution-graph]") {
    namespace backend = cactus::runtime::entt_backend;
    const auto make_world = [](entt::registry& registry) {
        backend::generated_reset_scheduler_state();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
    };
    entt::registry left;
    entt::registry right;
    make_world(left);
    make_world(right);
    CHECK(backend::generated_world_digest(left) == backend::generated_world_digest(right));

    auto& spin  = right.get<Spin>(*right.view<Spin>().begin());
    spin.angle  = std::bit_cast<float>(std::bit_cast<std::uint32_t>(spin.angle) ^ 1U);
    CHECK(backend::generated_world_digest(left) != backend::generated_world_digest(right));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
