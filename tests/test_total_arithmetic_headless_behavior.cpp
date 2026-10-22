// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

template <typename Trait>
Trait after_one_frame() {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return registry.get<Trait>(*view.begin());
}

}  // namespace

TEST_CASE("integer arithmetic wraps modulo 2^32", "[runtime][codegen-entt][numeric-semantics]") {
    const auto wrap = after_one_frame<cactus_gen::total_arithmetic::Wrap>();
    CHECK(wrap.max_plus_one == -2147483647 - 1);
    CHECK(wrap.square == 0);
    CHECK(wrap.negated_min == -2147483647 - 1);
    CHECK(wrap.counter == -2147483647);
}

TEST_CASE("integer division is floor division and total", "[runtime][codegen-entt][numeric-semantics]") {
    const auto division = after_one_frame<cactus_gen::total_arithmetic::Division>();
    CHECK(division.quotient == -4);
    CHECK(division.remainder == 1);
    CHECK(division.quotient_by_negative == -4);
    CHECK(division.remainder_by_negative == -1);
    CHECK(division.grid_cell == 7);
    CHECK(division.by_zero == 0);
    CHECK(division.remainder_by_zero == 5);
    CHECK(division.min_by_minus_one == -2147483647 - 1);
}

TEST_CASE("an integer constant uses floor division", "[runtime][codegen-entt][const-expressions]") {
    CHECK(after_one_frame<cactus_gen::total_arithmetic::Division>().const_cell == -4);
}

TEST_CASE("float-to-int conversion is total", "[runtime][codegen-entt][numeric-semantics][stdlib-math]") {
    const auto conversion = after_one_frame<cactus_gen::total_arithmetic::Conversion>();
    CHECK(conversion.mixed == 16777216.0F);
    CHECK(conversion.floor_nan == 0);
    CHECK(conversion.ceil_nan == 0);
    CHECK(conversion.round_nan == 0);
    CHECK(conversion.floor_huge == 2147483647);
    CHECK(conversion.ceil_huge == 2147483647);
    CHECK(conversion.round_huge == -2147483647 - 1);
}

TEST_CASE("float division and comparison follow IEEE-754", "[runtime][codegen-entt][numeric-semantics]") {
    const auto conversion = after_one_frame<cactus_gen::total_arithmetic::Conversion>();
    CHECK(conversion.inf_quotient == std::numeric_limits<float>::infinity());
    CHECK_FALSE(conversion.nan_equal);
    CHECK(conversion.zeros_equal);
}

TEST_CASE("a float field holds the nearest binary32 value", "[runtime][codegen-entt][type-system]") {
    CHECK(std::bit_cast<std::uint32_t>(after_one_frame<cactus_gen::total_arithmetic::Conversion>().tenth) ==
          0x3DCCCCCDU);
}

TEST_CASE("math.min returns NaN when an argument is NaN", "[runtime][codegen-entt][stdlib-math]") {
    CHECK(std::isnan(after_one_frame<cactus_gen::total_arithmetic::Conversion>().min_nan));
}

TEST_CASE("a float constant matches the runtime call bit for bit", "[runtime][codegen-entt][const-expressions]") {
    CHECK(after_one_frame<cactus_gen::total_arithmetic::Conversion>().sine_matches);
    CHECK(std::bit_cast<std::uint32_t>(cactus_gen::total_arithmetic::SINE) ==
          std::bit_cast<std::uint32_t>(cactus::runtime::stdlib::math::sin(0.5F)));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
