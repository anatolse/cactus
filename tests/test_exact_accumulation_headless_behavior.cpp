// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Built twice: from examples/exact_accumulation.cactus and from a copy that
// declares each writer pair in the other order. Both must give these bits.

namespace {

using Tally = exact_accumulation__Tally;
using Pool  = exact_accumulation__Pool;
using Seen  = exact_accumulation__Seen;
using Drift = exact_accumulation__Drift;
using Body  = exact_accumulation__Body;

template <typename Trait>
const Trait& only(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return registry.get<Trait>(*view.begin());
}

struct OneFrame {
    entt::registry registry;

    OneFrame() {
        cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    }
};

}  // namespace

TEST_CASE("two unordered handlers add to one float exactly", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    CHECK(only<Tally>(world.registry).total == 1.0F);
}

TEST_CASE("several rows add to one float exactly", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    cactus::runtime::reduce::Sum<float> exact;
    for (int row = 0; row < 10; ++row) {
        exact.add(0.1F);
    }
    CHECK(only<Tally>(world.registry).rows == exact.value());
}

TEST_CASE("a later reader sees the merged value", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    CHECK(only<Pool>(world.registry).level == 1.0F);
    CHECK(only<Seen>(world.registry).level == 1.0F);
}

TEST_CASE("a vector component accumulates exactly and leaves the others", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    const auto& offset = only<Drift>(world.registry).offset;
    CHECK(offset.x == 1.0F);
    CHECK(offset.y == 0.0F);
    CHECK(std::signbit(offset.y));
}

TEST_CASE("a lone accumulation equals a plain add", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    CHECK(only<Body>(world.registry).x == 0.0F + 0.1F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
